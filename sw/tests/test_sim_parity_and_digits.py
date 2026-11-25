"""Tests for identical sim/serial API, bit-exact seed parity, and 1,797-image load_digits timing."""

import importlib.util
from pathlib import Path

import numpy as np
import pytest

from fecim.crossbar import Crossbar
from fecim.exceptions import REG_NOISE_EN, REG_NOISE_SEED, REG_READ_SIGMA


def _get_run_digits_pass():
    bench_path = Path(__file__).resolve().parents[1] / "examples" / "bench_digits_throughput.py"
    if not bench_path.exists():
        pytest.skip(f"Missing {bench_path}")
    spec = importlib.util.spec_from_file_location("bench_digits_throughput", bench_path)
    assert spec is not None and spec.loader is not None
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod.run_digits_pass


def test_identical_api_methods_between_sim_and_serial(monkeypatch):
    class DummySerial:
        def __init__(self, port, baudrate, **kwargs):
            self.port = port
            self.baudrate = baudrate
            self.is_open = True
            self._buf = bytearray()

        def write(self, data):
            self._buf.extend(data)
            return len(data)

        def flush(self):
            pass

        def read(self, size):
            return b""

        def reset_input_buffer(self):
            self._buf.clear()

        def close(self):
            self.is_open = False

    monkeypatch.setattr("fecim.transport.serial.Serial", DummySerial)

    sim_cb = Crossbar(backend="sim", seed=0)
    ser_cb = Crossbar(backend="serial", port="COM6", baudrate=115200, retries=1)

    public_methods = [
        "identify",
        "set_config",
        "get_config",
        "set_quantization",
        "set_baud",
        "write_weights",
        "write_act",
        "write_atten",
        "compute",
        "read_result",
        "read_argmax",
        "reset_seed",
        "close",
    ]
    for name in public_methods:
        assert callable(getattr(sim_cb, name))
        assert callable(getattr(ser_cb, name))

    ser_cb.close()


def test_bit_exact_with_model_path_for_same_seed():
    weights = np.arange(-64, 64, dtype=np.int8).reshape(16, 8)
    acts = np.arange(0, 160, 10, dtype=np.uint8)

    for seed in (0, 12345):
        cb_a = Crossbar(backend="sim", seed=seed)
        cb_b = Crossbar(backend="sim", seed=seed)

        for cb in (cb_a, cb_b):
            cb.set_config(0x01, (8 << 16) | 16)
            cb.set_config(REG_NOISE_SEED, seed)
            cb.set_config(REG_READ_SIGMA, 32)
            cb.set_config(REG_NOISE_EN, (1 << 2))
            cb.write_weights(weights, addr=0)
            cb.write_act(acts, addr=0)
            cb.compute()

        assert cb_a.read_result(0, 8) == cb_b.read_result(0, 8)
        assert cb_a.read_argmax() == cb_b.read_argmax()


def test_full_1797_load_digits_throughput_timed():
    run_digits_pass = _get_run_digits_pass()
    n_imgs, elapsed_s, throughput = run_digits_pass(seed=7)
    assert n_imgs == 1797
    assert elapsed_s > 0.0
    assert throughput > 0.0