"""Timed full 1,797-image sklearn load_digits pass on Crossbar(backend='sim')."""

# run <PYTHONPATH=sw python3 sw/examples/bench_digits_throughput.py> to test

import time
import numpy as np
from sklearn.datasets import load_digits

from fecim.crossbar import Crossbar


def run_digits_pass(seed: int = 42) -> tuple[int, float, float]:
    digits = load_digits()
    images = digits.images.reshape(len(digits.images), -1)  # (1797, 64)
    targets = digits.target  # (1797,)

    # Quantize 64 features -> uint8 activations; build 64x10 int8 weight matrix
    x_u8 = np.clip(np.round(images * (255.0 / 16.0)), 0, 255).astype(np.uint8)
    rng = np.random.default_rng(seed)
    w_i8 = rng.integers(-64, 65, size=(64, 10), dtype=np.int8)

    cb = Crossbar(backend="sim", seed=seed)
    cb.set_config(0x01, (10 << 16) | 64)  # active_cols=10, active_rows=64
    cb.write_weights(w_i8, addr=0)

    correct = 0
    t0 = time.perf_counter()
    for idx in range(x_u8.shape[0]):
        cb.write_act(x_u8[idx], addr=0)
        cb.compute()
        pred = cb.read_argmax().max_idx
        if pred == int(targets[idx]):
            correct += 1
    elapsed_s = time.perf_counter() - t0
    images_per_sec = x_u8.shape[0] / elapsed_s if elapsed_s > 0 else 0.0
    return x_u8.shape[0], elapsed_s, images_per_sec


if __name__ == "__main__":
    n_imgs, elapsed, throughput = run_digits_pass()
    print(f"Processed {n_imgs} load_digits images in {elapsed:.4f}s ({throughput:.1f} img/s)")