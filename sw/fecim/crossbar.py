"""
FeCIM Crossbar Emulator Module (sw/fecim/crossbar.py)
Provides the Crossbar simulation class for matrix-vector / tile accumulation,
ADC quantization effects, and deterministic seed control.
"""

from typing import Optional
import numpy as np


class Crossbar:
    """
    Software crossbar array model supporting exact integer MAC accumulation,
    optional coarse ADC quantization, and backend/seed tracking.
    """

    def __init__(
        self,
        rows: int = 128,
        cols: int = 128,
        adc_bits: int = 8,
        backend: str = "sim",
        seed: Optional[int] = None,
    ) -> None:
        if rows <= 0 or cols <= 0:
            raise ValueError("Crossbar rows and cols must be positive integers.")
        if adc_bits <= 0 or adc_bits > 16:
            raise ValueError("adc_bits must be between 1 and 16.")

        self.rows = rows
        self.cols = cols
        self.adc_bits = adc_bits
        self.backend = backend
        self.seed = seed
        self._rng = np.random.default_rng(seed) if seed is not None else np.random.default_rng()

    def reset_seed(self, seed: Optional[int]) -> None:
        """Resets the internal RNG state deterministically."""
        self.seed = seed
        self._rng = np.random.default_rng(seed) if seed is not None else np.random.default_rng()

    def compute(self, q_w: np.ndarray, x_q: np.ndarray) -> np.ndarray:
        """
        Executes crossbar multiply-accumulate:
          - 1D x_q of shape (K,) and 2D q_w of shape (K, M) -> returns (M,)
          - 2D x_q of shape (B, K) and 2D q_w of shape (K, M) -> returns (B, M)
        """
        x_arr = np.asarray(x_q, dtype=np.int64)
        w_arr = np.asarray(q_w, dtype=np.int64)

        if x_arr.ndim not in (1, 2):
            raise ValueError(f"Expected 1D or 2D activations, got shape {x_arr.shape}.")
        if w_arr.ndim != 2:
            raise ValueError(f"Expected 2D weight matrix, got shape {w_arr.shape}.")
        if x_arr.shape[-1] != w_arr.shape[0]:
            raise ValueError(
                f"Inner dimension mismatch: activations {x_arr.shape} vs weights {w_arr.shape}."
            )

        acc = np.matmul(x_arr, w_arr).astype(np.float64)

        if self.adc_bits < 8:
            levels = float(2 ** self.adc_bits)
            half_levels = levels / 2.0
            max_abs = float(np.max(np.abs(acc))) if acc.size > 0 else 0.0
            if max_abs > 0.0:
                step = max_abs / half_levels
                # Round to nearest ADC code with deterministic tie-breaking toward lower magnitude
                scaled = acc / step
                floor_val = np.floor(scaled)
                frac = scaled - floor_val
                rounded = np.where(frac > 0.5, floor_val + 1.0, floor_val)
                acc = rounded * step

        return acc