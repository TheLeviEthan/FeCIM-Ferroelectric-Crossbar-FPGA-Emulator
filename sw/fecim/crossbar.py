"""Unified FeCIM Crossbar interface for sim and serial backends."""

from typing import List, Optional, Sequence, Union

import numpy as np

from .driver import ArgmaxResult, DeviceIdentity, FeCIMDriver
from .exceptions import REG_BAUD_INC, REG_QUANT_LEVELS, REG_QUANT_MULT
from .sim_backend import ExactSimEngine
from .transport import SerialTransport


class Crossbar:
    """
    Unified crossbar interface supporting:
      - backend="sim": runs against model_exact (pybind11 or fecim_cli subprocess)
      - backend="serial": runs against physical/FPGA hardware over COM* or /dev/ttyUSB*
    """

    def __init__(
        self,
        backend: str = "sim",
        port: Optional[str] = None,
        baudrate: int = 115200,
        seed: Optional[int] = 0,
        tile_rows: int = 128,
        tile_cols: int = 128,
        num_lanes: int = 64,
        retries: int = 3,
    ) -> None:
        if backend not in ("sim", "serial"):
            raise ValueError(f"Unsupported backend {backend!r}; expected 'sim' or 'serial'")

        self.backend = backend
        self.seed = 0 if seed is None else int(seed)
        self._transport: Optional[SerialTransport] = None
        self._driver: Optional[FeCIMDriver] = None
        self._sim: Optional[ExactSimEngine] = None

        if backend == "sim":
            self._sim = ExactSimEngine(
                tile_rows=tile_rows,
                tile_cols=tile_cols,
                num_lanes=num_lanes,
                seed=self.seed,
            )
        else:
            if not port:
                raise ValueError("port (e.g. 'COM6' or '/dev/ttyUSB0') is required for backend='serial'")
            self._transport = SerialTransport(port=port, baudrate=baudrate)
            self._driver = FeCIMDriver(self._transport, retries=retries)

    def reset_seed(self, seed: Optional[int]) -> None:
        """Sets deterministic seed without remapping seed=0."""
        self.seed = 0 if seed is None else int(seed)
        if self._sim is not None:
            self._sim.set_seed(self.seed)

    def identify(self) -> DeviceIdentity:
        if self._sim is not None:
            return self._sim.identify()
        assert self._driver is not None
        return self._driver.identify()

    def set_config(self, reg: int, value: int) -> None:
        if self._sim is not None:
            self._sim.set_config(reg, value)
            return
        assert self._driver is not None
        self._driver.set_config(reg, value)

    def get_config(self, reg: int) -> int:
        if self._sim is not None:
            return self._sim.get_config(reg)
        assert self._driver is not None
        return self._driver.get_config(reg)

    def set_quantization(self, n_levels: int) -> None:
        if not (2 <= n_levels <= 255):
            raise ValueError(f"QUANT_LEVELS must be in 2..255, got {n_levels}")
        quant_mult = round(255 * 256 / (n_levels - 1))
        self.set_config(REG_QUANT_LEVELS, n_levels)
        self.set_config(REG_QUANT_MULT, quant_mult)

    def set_baud(self, baudrate: int, baud_inc: Optional[int] = None, clk_hz: int = 50_000_000) -> None:
        if baud_inc is None:
            baud_inc = max(1, round((baudrate * (2 ** 20)) / clk_hz))
        self.set_config(REG_BAUD_INC, baud_inc)
        if self._transport is not None:
            self._transport.set_baudrate(baudrate)

    def write_weights(
        self,
        weights_int8: Union[bytes, bytearray, Sequence[int], np.ndarray],
        addr: int = 0,
        chunk_size: int = 256,
    ) -> None:
        if isinstance(weights_int8, np.ndarray):
            raw = weights_int8.astype(np.int8, order="F").tobytes(order="F")
        elif isinstance(weights_int8, (bytes, bytearray)):
            raw = bytes(weights_int8)
        else:
            raw = bytes(np.asarray(weights_int8, dtype=np.int8).tobytes())

        if self._sim is not None:
            self._sim.write_weights(raw, addr=addr)
            return
        assert self._driver is not None
        self._driver.write_weights(raw, addr=addr, chunk_size=chunk_size)

    def write_act(
        self,
        activations_uint8: Union[bytes, bytearray, Sequence[int], np.ndarray],
        addr: int = 0,
        chunk_size: int = 510,
    ) -> None:
        if isinstance(activations_uint8, np.ndarray):
            raw = activations_uint8.astype(np.uint8).tobytes()
        elif isinstance(activations_uint8, (bytes, bytearray)):
            raw = bytes(activations_uint8)
        else:
            raw = bytes(np.asarray(activations_uint8, dtype=np.uint8).tobytes())

        if self._sim is not None:
            self._sim.write_act(raw, addr=addr)
            return
        assert self._driver is not None
        self._driver.write_act(raw, addr=addr, chunk_size=chunk_size)

    def write_atten(
        self,
        atten_uint8: Union[bytes, bytearray, Sequence[int], np.ndarray],
        addr: int = 0,
        chunk_size: int = 510,
    ) -> None:
        if isinstance(atten_uint8, np.ndarray):
            raw = atten_uint8.astype(np.uint8).tobytes()
        elif isinstance(atten_uint8, (bytes, bytearray)):
            raw = bytes(atten_uint8)
        else:
            raw = bytes(np.asarray(atten_uint8, dtype=np.uint8).tobytes())

        if self._sim is not None:
            self._sim.write_atten(raw, addr=addr)
            return
        assert self._driver is not None
        self._driver.write_atten(raw, addr=addr, chunk_size=chunk_size)

    def compute(self) -> None:
        if self._sim is not None:
            self._sim.compute()
            return
        assert self._driver is not None
        self._driver.compute()

    def read_result(self, start: int = 0, count: int = 128) -> List[int]:
        if self._sim is not None:
            return self._sim.read_result(start=start, count=count)
        assert self._driver is not None
        return self._driver.read_result(start=start, count=count)

    def read_argmax(self) -> ArgmaxResult:
        if self._sim is not None:
            return self._sim.read_argmax()
        assert self._driver is not None
        return self._driver.read_argmax()

    def close(self) -> None:
        if self._transport is not None:
            self._transport.close()