"""Exact crossbar model backend (pybind11 primary, fecim_cli subprocess fallback)."""

from dataclasses import dataclass, field
import json
import math
from pathlib import Path
import shutil
import subprocess
from typing import Dict, List, Optional

import numpy as np

from .driver import ArgmaxResult, DeviceIdentity
from .exceptions import (
    IDENTIFY_MAGIC,
    PROTO_VER_EXPECTED,
    REG_ADC_BITS,
    REG_BAUD_INC,
    REG_CTRL,
    REG_CYCLE_CNT,
    REG_D2D_SIGMA,
    REG_NOISE_EN,
    REG_NOISE_SEED,
    REG_QUANT_LEVELS,
    REG_QUANT_MULT,
    REG_READ_SIGMA,
    REG_STATUS,
    REG_STUCK_RATE,
    REG_TILE_CFG,
)

LANE_SALT_C = 0x9E37


def compute_acc_used_w(tile_rows: int) -> int:
    """ACC_USED_W = ceil(log2(254 * 255 * TILE_ROWS)) + 1."""
    if tile_rows <= 0:
        raise ValueError("tile_rows must be positive")
    return math.ceil(math.log2(254 * 255 * tile_rows)) + 1


def _find_fecim_cli() -> Optional[Path]:
    """Locates fecim_cli next to fecim_demo or on PATH."""
    on_path = shutil.which("fecim_cli")
    if on_path:
        return Path(on_path)
    repo_root = Path(__file__).resolve().parents[2]
    candidates = [
        repo_root / "build" / "fecim_cli",
        repo_root / "build" / "fecim_cli.exe",
        repo_root / "sw" / "fecim_demo" / "fecim_cli",
        repo_root / "sw" / "fecim_demo" / "fecim_cli.exe",
    ]
    for path in candidates:
        if path.exists() and path.is_file():
            return path
    return None


@dataclass
class ExactSimEngine:
    """Runs model_exact via pybind11 (fecim_exact_cpp) or fecim_cli subprocess."""

    tile_rows: int = 128
    tile_cols: int = 128
    num_lanes: int = 64
    rtl_build_id: int = 0xC8
    seed: int = 0
    cli_path: Optional[Path] = field(default_factory=_find_fecim_cli)

    def __post_init__(self) -> None:
        self._pybind_mod = None
        try:
            import fecim_exact_cpp  # type: ignore[import-not-found]
            self._pybind_mod = fecim_exact_cpp
        except ImportError:
            self._pybind_mod = None

        self.regs: Dict[int, int] = {
            REG_CTRL: 0,
            REG_TILE_CFG: ((self.tile_cols & 0xFFFF) << 16) | (self.tile_rows & 0xFFFF),
            REG_QUANT_LEVELS: 255,
            REG_D2D_SIGMA: 0,
            REG_READ_SIGMA: 0,
            REG_NOISE_SEED: self.seed & 0xFFFFFFFF,
            REG_NOISE_EN: 0,
            REG_ADC_BITS: compute_acc_used_w(self.tile_rows),
            REG_STUCK_RATE: 0,
            REG_BAUD_INC: 2416,
            REG_STATUS: 0x0040,
            REG_CYCLE_CNT: 128,
            REG_QUANT_MULT: 257,
        }
        self.weights = np.zeros((self.tile_rows, self.tile_cols), dtype=np.int8)
        self.activations = np.full((self.tile_rows,), 255, dtype=np.uint8)
        self.atten = np.full((self.tile_rows,), 255, dtype=np.uint8)
        self.results = np.zeros((self.tile_cols,), dtype=np.int32)
        self.framing_err_count = 0
        self.crc_err_count = 0
        self.timeout_count = 0
        self._result_seq = 0

    @property
    def mode(self) -> str:
        if self._pybind_mod is not None:
            return "pybind11"
        if self.cli_path is not None:
            return "subprocess_cli"
        return "python_exact_ref"

    def set_seed(self, seed: Optional[int]) -> None:
        val = 0 if seed is None else int(seed) & 0xFFFFFFFF
        self.seed = val
        self.regs[REG_NOISE_SEED] = val

    def identify(self) -> DeviceIdentity:
        return DeviceIdentity(
            magic=IDENTIFY_MAGIC,
            proto_ver=PROTO_VER_EXPECTED,
            rtl_build_id=self.rtl_build_id,
            num_lanes=self.num_lanes,
            tile_rows=self.tile_rows,
            tile_cols=self.tile_cols,
            framing_err_count=self.framing_err_count,
            crc_err_count=self.crc_err_count,
            timeout_count=self.timeout_count,
        )

    def set_config(self, reg: int, value: int) -> None:
        self.regs[reg & 0xFF] = int(value) & 0xFFFFFFFF
        if (reg & 0xFF) == REG_NOISE_SEED:
            self.seed = int(value) & 0xFFFFFFFF

    def get_config(self, reg: int) -> int:
        return int(self.regs.get(reg & 0xFF, 0)) & 0xFFFFFFFF

    def write_weights(self, weights_int8: bytes, addr: int = 0) -> None:
        arr = np.frombuffer(weights_int8, dtype=np.int8)
        flat = self.weights.reshape(-1, order="F")
        end = addr + arr.size
        if end > flat.size:
            raise ValueError(f"Weight write out of bounds: {end} > {flat.size}")
        flat[addr:end] = arr

    def write_act(self, activations_uint8: bytes, addr: int = 0) -> None:
        arr = np.frombuffer(activations_uint8, dtype=np.uint8)
        end = addr + arr.size
        if end > self.activations.size:
            raise ValueError(f"Activation write out of bounds: {end} > {self.activations.size}")
        self.activations[addr:end] = arr

    def write_atten(self, atten_uint8: bytes, addr: int = 0) -> None:
        arr = np.frombuffer(atten_uint8, dtype=np.uint8)
        end = addr + arr.size
        if end > self.atten.size:
            raise ValueError(f"Attenuation write out of bounds: {end} > {self.atten.size}")
        self.atten[addr:end] = arr

    def compute(self) -> None:
        noise_seed = int(self.regs.get(REG_NOISE_SEED, self.seed)) & 0xFFFFFFFF
        noise_en = int(self.regs.get(REG_NOISE_EN, 0)) & 0xFFFFFFFF
        d2d_reg = int(self.regs.get(REG_D2D_SIGMA, 0)) & 0xFF
        read_reg = int(self.regs.get(REG_READ_SIGMA, 0)) & 0xFF
        adc_bits = int(self.regs.get(REG_ADC_BITS, compute_acc_used_w(self.tile_rows)))
        tile_cfg = int(self.regs.get(REG_TILE_CFG, ((self.tile_cols << 16) | self.tile_rows)))
        active_rows = min(self.tile_rows, tile_cfg & 0xFFFF)
        active_cols = min(self.tile_cols, (tile_cfg >> 16) & 0xFFFF)

        if self._pybind_mod is not None:
            out = self._pybind_mod.model_exact(
                self.weights[:active_rows, :active_cols],
                self.activations[:active_rows],
                self.atten[:active_rows],
                noise_seed,
                noise_en,
                d2d_reg,
                read_reg,
                adc_bits,
                self.tile_rows,
            )
            self.results[:] = 0
            self.results[:active_cols] = np.asarray(out, dtype=np.int32)
        elif self.cli_path is not None:
            self.results[:] = self._run_cli_model_exact(
                active_rows=active_rows,
                active_cols=active_cols,
                noise_seed=noise_seed,
                noise_en=noise_en,
                d2d_reg=d2d_reg,
                read_reg=read_reg,
                adc_bits=adc_bits,
            )
        else:
            self.results[:] = self._python_model_exact(
                active_rows=active_rows,
                active_cols=active_cols,
                noise_seed=noise_seed,
                noise_en=noise_en,
                d2d_reg=d2d_reg,
                read_reg=read_reg,
                adc_bits=adc_bits,
            )

        self._result_seq = (self._result_seq + 1) & 0xFF
        self.regs[REG_STATUS] = 0x0040 | (self._result_seq << 8)
        self.regs[REG_CYCLE_CNT] = max(1, active_rows)

    def _run_cli_model_exact(
        self,
        active_rows: int,
        active_cols: int,
        noise_seed: int,
        noise_en: int,
        d2d_reg: int,
        read_reg: int,
        adc_bits: int,
    ) -> np.ndarray:
        payload = {
            "weights": self.weights[:active_rows, :active_cols].tolist(),
            "activations": self.activations[:active_rows].tolist(),
            "atten": self.atten[:active_rows].tolist(),
            "noise_seed": noise_seed,
            "noise_en": noise_en,
            "d2d_sigma": d2d_reg,
            "read_sigma": read_reg,
            "adc_bits": adc_bits,
            "tile_rows": self.tile_rows,
        }
        proc = subprocess.run(
            [str(self.cli_path), "model-exact"],
            input=json.dumps(payload),
            text=True,
            capture_output=True,
            check=True,
        )
        data = json.loads(proc.stdout)
        out = np.zeros((self.tile_cols,), dtype=np.int32)
        out[:active_cols] = np.asarray(data["results"], dtype=np.int32)
        return out

    def _python_model_exact(
        self,
        active_rows: int,
        active_cols: int,
        noise_seed: int,
        noise_en: int,
        d2d_reg: int,
        read_reg: int,
        adc_bits: int,
    ) -> np.ndarray:
        w = self.weights[:active_rows, :active_cols].astype(np.int32)
        x = self.activations[:active_rows].astype(np.int32)
        atten = self.atten[:active_rows].astype(np.float64) / 255.0

        eff_w = w.astype(np.float64)
        if noise_en & (1 << 4):
            eff_w = eff_w * atten[:, None]

        lane_seeds = [
            (noise_seed ^ (LANE_SALT_C * (col % max(1, self.num_lanes)))) | 1
            for col in range(active_cols)
        ]
        if noise_en & (1 << 1):
            d2d_scale = d2d_reg / 256.0
            for col in range(active_cols):
                rng = np.random.default_rng(lane_seeds[col])
                eff_w[:, col] += rng.normal(0.0, d2d_scale * 104.0, size=active_rows)

        mac = np.matmul(x.astype(np.float64), eff_w)

        if noise_en & (1 << 2):
            read_scale = read_reg / 512.0
            for col in range(active_cols):
                rng = np.random.default_rng(lane_seeds[col] ^ 0x5A5A)
                mac[col] += rng.normal(0.0, read_scale * 36.8)

        acc_used_w = compute_acc_used_w(self.tile_rows)
        if (noise_en & (1 << 5)) and adc_bits < acc_used_w:
            shift = max(0, acc_used_w - adc_bits)
            q_step = float(1 << shift)
            mac = np.round(mac / q_step) * q_step

        out = np.zeros((self.tile_cols,), dtype=np.int32)
        out[:active_cols] = np.clip(np.rint(mac), -2**31, 2**31 - 1).astype(np.int32)
        return out

    def read_result(self, start: int = 0, count: int = 128) -> List[int]:
        if start < 0 or count < 0 or (start + count) > self.tile_cols:
            raise IndexError(f"Result range [{start}, {start + count}) out of tile_cols={self.tile_cols}")
        return [int(v) for v in self.results[start : start + count]]

    def read_argmax(self) -> ArgmaxResult:
        tile_cfg = int(self.regs.get(REG_TILE_CFG, ((self.tile_cols << 16) | self.tile_rows)))
        active_cols = max(1, min(self.tile_cols, (tile_cfg >> 16) & 0xFFFF))
        vals = self.results[:active_cols]
        max_idx = int(np.argmax(vals))
        max_val = int(vals[max_idx])
        if active_cols == 1:
            second_val = 0
        else:
            sorted_desc = np.sort(vals)[::-1]
            second_val = int(sorted_desc[1])
        return ArgmaxResult(max_idx=max_idx, max_val=max_val, second_val=second_val)