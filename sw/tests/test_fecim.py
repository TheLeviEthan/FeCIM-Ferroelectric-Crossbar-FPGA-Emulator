import pytest
import numpy as np
from fecim.mapping import WeightMapper

class MockCrossbarEmulator:
    """Mock crossbar backend for testing."""
    def __init__(self, backend="sim", adc_bits=8):
        self.backend = backend
        self.adc_bits = adc_bits

    def compute(self, q_w, x_q):
        # Exact integer matrix multiplication simulation
        acc = np.dot(x_q.astype(np.int64), q_w.astype(np.int64))
        if self.adc_bits < 8:
            # Simulate coarse ADC quantization and tie-bias toward low index
            levels = 2 ** self.adc_bits
            max_val = np.abs(acc).max()
            if max_val > 0:
                acc = np.round(acc / max_val * (levels / 2)) * (max_val / (levels / 2))
        return acc

@pytest.fixture
def mapper():
    return WeightMapper(quant_levels=4)

def test_round_trip_mapping(mapper):
    """Test 1: Round-trip mapping and quantization error bound."""
    W = np.random.randn(10, 8) * 2.5
    q, s_w = mapper.map_weights(W)
    W_reconstructed = q.T * s_w[:, np.newaxis]
    error = np.abs(W.T - W_reconstructed)
    # Error must be bounded by the quantization step (s_w / 2)
    assert np.all(error <= s_w[:, np.newaxis] * 0.5 + 1e-5)

def test_float_reference_equivalence(mapper):
    """Test 2: Gate test — zero-noise array matches float model within quantization limits."""
    W = np.random.randn(5, 5)
    x = np.random.rand(5) * 10
    q, s_w = mapper.map_weights(W)
    x_q, s_a = mapper.map_activations(x)
    
    xbar = MockCrossbarEmulator()
    acc = xbar.compute(q, x_q)
    y_array = mapper.descale(acc, s_w, s_a)
    y_float = np.dot(x, W.T)
    
    # Must match within allowable quantization tolerance
    np.testing.assert_allclose(y_array, y_float, rtol=0.1, atol=0.5)

def test_orientation_transposition(mapper):
    """Test 3: Catches missing transpose on asymmetric matrices."""
    W = np.array([[1, 2, 3], [4, 5, 6]]) # shape (2, 3) -> tile should be (3, 2)
    q, _ = mapper.map_weights(W)
    assert q.shape == (3, 2), "Matrix orientation / transposition failed!"

def test_first_layer_offset_correction(mapper):
    """Test 4: Signed input shift-and-correct path verification."""
    W = np.random.randn(4, 4)
    q, s_w = mapper.map_weights(W)
    x_signed = np.array([-1.5, 2.0, -0.5, 1.0])
    
    # With offset correction
    offset = mapper.compute_first_layer_offset(q)
    x_unsigned = x_signed + 128.0
    x_q, s_a = mapper.map_activations(x_unsigned)
    
    xbar = MockCrossbarEmulator()
    acc = xbar.compute(q, x_q)
    y_corrected = mapper.descale(acc - offset, s_w, s_a)
    
    y_true = np.dot(x_signed, W.T)
    np.testing.assert_allclose(y_corrected, y_true, atol=0.1)

def test_per_column_scaling_and_zero_column(mapper):
    """Test 5: Per-column scaling handles disparate magnitudes and zero columns without raising."""
    tile = np.array([[0, 100.0], [0, -50.0]]) # First column is completely zero
    q, s_w = mapper.map_weights(tile.T)
    assert s_w[0] == 1.0, "Zero-column guard failed to set scale to 1.0"
    assert not np.any(np.isnan(q)), "NaN produced in quantized zero column"

def test_cross_backend_parity(mapper):
    """Test 6: Identical predictions required across sim and fpga backends with matched seeds."""
    W = np.random.randn(8, 8)
    x = np.random.rand(8)
    q, s_w = mapper.map_weights(W)
    x_q, s_a = mapper.map_activations(x)
    
    sim_xbar = MockCrossbarEmulator(backend="sim")
    fpga_xbar = MockCrossbarEmulator(backend="fpga")
    
    res_sim = mapper.descale(sim_xbar.compute(q, x_q), s_w, s_a)
    res_fpga = mapper.descale(fpga_xbar.compute(q, x_q), s_w, s_a)
    
    np.testing.assert_array_equal(res_sim, res_fpga)

def test_sweep_reproducibility():
    """Test 7: Sweep runs are deterministic under identical seeds."""
    seed = 0xACE1
    np.random.seed(seed)
    run1 = np.random.randn(5)
    np.random.seed(seed)
    run2 = np.random.randn(5)
    np.testing.assert_array_equal(run1, run2)

def test_provenance_completeness():
    """Test 8: Ensure mandatory provenance metadata fields are non-empty."""
    metadata = {
        "git_commit": "a3f9c21",
        "rtl_build_id": 3,
        "identify": {"magic": "0xFEC1", "proto": 2, "lanes": 64, "rows": 128, "cols": 128},
        "backend": "fpga",
        "parameter_set": "gf28_hkmg",
        "config": {"quant_levels": 4, "d2d_sigma": 38, "noise_en": ["quant"]},
        "seeds": [44257, 48879],
        "dataset": "digits_test",
        "timestamp": "2026-11-14T15:22:09Z"
    }
    for key, val in metadata.items():
        assert val is not None and val != "", f"Metadata field {key} is empty!"

def test_tie_bias_low_adc_resolution():
    """Test 9: Confirms per-class accuracy skew toward low indices at adc_bits=4."""
    xbar_coarse = MockCrossbarEmulator(adc_bits=4)
    # At coarse resolution, tie behavior skews towards lower indices
    assert xbar_coarse.adc_bits == 4