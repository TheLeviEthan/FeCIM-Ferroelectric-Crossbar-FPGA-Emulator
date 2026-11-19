"""Tests for opcodes, non-OK status exceptions, chunking, baud switch, and 128x128 timing."""

import struct
import time
from typing import Callable, Dict, Optional

import pytest

"""
Tests:
- pytest -v sw/tests/test_driver_tasks.py -k "test_every_non_ok_status_raises_own_exception"

- pytest -v sw/tests/test_driver_tasks.py -k "opcodes or status"

- pytest -v sw/tests/test_driver_tasks.py -k "payload_limit"

- pytest -v sw/tests/test_driver_tasks.py -k "128x128_weight_load_timing"
- python -c "import time; from fecim.transport import SerialTransport; from fecim.driver import FeCIMDriver; t=SerialTransport('COM6', 921600); d=FeCIMDriver(t); t0=time.perf_counter(); d.write_weights(bytes(128*128), chunk_size=256); print(f'{(time.perf_counter()-t0)*1000:.1f} ms')"

- PS: python -c "from fecim.transport import SerialTransport; from fecim.driver import FeCIMDriver; t=SerialTransport('COM6', 115200); d=FeCIMDriver(t); print(d.identify())"
- WSL: python -c "from fecim.transport import SerialTransport; from fecim.driver import FeCIMDriver; t=SerialTransport('/dev/ttyUSB0', 115200); d=FeCIMDriver(t); print(d.identify())"
"""

from fecim.driver import (
    ArgmaxResult,
    DeviceIdentity,
    DEV_SYNC,
    FeCIMDriver,
    HOST_SYNC,
    MAX_PAYLOAD,
    build_request_frame,
    crc8,
)
from fecim.exceptions import (
    AddrRangeError,
    BadLenError,
    BusyError,
    CMD_COMPUTE,
    CMD_GET_CONFIG,
    CMD_IDENTIFY,
    CMD_READ_ARGMAX,
    CMD_READ_RESULT,
    CMD_SET_CONFIG,
    CMD_WRITE_ACT,
    CMD_WRITE_ATTEN,
    CMD_WRITE_WEIGHTS,
    CrcError,
    IDENTIFY_MAGIC,
    PROTO_VER_EXPECTED,
    ST_ADDR_RANGE,
    ST_BAD_LEN,
    ST_BUSY,
    ST_CRC_ERR,
    ST_OK,
    ST_UNKNOWN_CMD,
    UnknownCmdError,
)


def make_response(status: int, payload: bytes = b"") -> bytes:
    body = struct.pack("<BH", status, len(payload)) + payload
    return bytes([DEV_SYNC]) + body + bytes([crc8(body)])


class LoopbackOrCoSimTransport:
    """Loopback fake / virtual-UART co-sim (C5) transport."""

    def __init__(
        self,
        baudrate: int = 921600,
        responder: Optional[Callable[[int, bytes], tuple[int, bytes]]] = None,
        per_byte_delay_s: float = 0.0,
    ) -> None:
        self.baudrate = baudrate
        self.per_byte_delay_s = per_byte_delay_s
        self.tx_history: list[bytes] = []
        self._rx_queue = bytearray()
        self._responder = responder or self._default_responder

    def set_baudrate(self, baudrate: int) -> None:
        self.baudrate = baudrate

    def reset_input_buffer(self) -> None:
        self._rx_queue.clear()

    def close(self) -> None:
        pass

    def _default_responder(self, cmd: int, payload: bytes) -> tuple[int, bytes]:
        if cmd == CMD_IDENTIFY:
            resp = struct.pack(
                "<HBBHHHHHH",
                IDENTIFY_MAGIC,
                PROTO_VER_EXPECTED,
                0xA2,
                64,
                128,
                128,
                0,
                0,
                0,
            )
            return ST_OK, resp
        if cmd == CMD_SET_CONFIG:
            return ST_OK, b""
        if cmd == CMD_GET_CONFIG:
            return ST_OK, struct.pack("<I", 0x00000000)
        if cmd in (CMD_WRITE_WEIGHTS, CMD_WRITE_ACT, CMD_WRITE_ATTEN, CMD_COMPUTE):
            return ST_OK, b""
        if cmd == CMD_READ_RESULT:
            _, count = struct.unpack("<HH", payload)
            return ST_OK, struct.pack(f"<{count}i", *([0] * count))
        if cmd == CMD_READ_ARGMAX:
            return ST_OK, struct.pack("<Hii", 0, 0, 0)
        return ST_UNKNOWN_CMD, b""

    def write(self, data: bytes) -> int:
        self.tx_history.append(data)
        if self.per_byte_delay_s > 0:
            time.sleep(len(data) * self.per_byte_delay_s)
        if len(data) >= 4 and data[0] == HOST_SYNC:
            cmd, length = struct.unpack_from("<BH", data, 1)
            if length > MAX_PAYLOAD:
                self._rx_queue.extend(make_response(ST_BAD_LEN))
                return len(data)
            req_payload = data[4 : 4 + length]
            status, resp_payload = self._responder(cmd, req_payload)
            self._rx_queue.extend(make_response(status, resp_payload))
        return len(data)

    def read(self, size: int) -> bytes:
        if not self._rx_queue:
            return b""
        take = min(size, len(self._rx_queue))
        out = bytes(self._rx_queue[:take])
        del self._rx_queue[:take]
        return out


def test_all_opcodes_and_payload_limit():
    regs: Dict[int, int] = {}

    def cosim(cmd: int, payload: bytes):
        if cmd == CMD_IDENTIFY:
            return ST_OK, struct.pack("<HBBHHHHHH", IDENTIFY_MAGIC, PROTO_VER_EXPECTED, 0xA2, 64, 128, 128, 0, 1, 0)
        if cmd == CMD_SET_CONFIG:
            reg, val = struct.unpack("<BI", payload)
            regs[reg] = val
            return ST_OK, b""
        if cmd == CMD_GET_CONFIG:
            (reg,) = struct.unpack("<B", payload)
            return ST_OK, struct.pack("<I", regs.get(reg, 0))
        if cmd in (CMD_WRITE_WEIGHTS, CMD_WRITE_ACT, CMD_WRITE_ATTEN):
            assert len(payload) <= MAX_PAYLOAD
            return ST_OK, b""
        if cmd == CMD_COMPUTE:
            return ST_OK, b""
        if cmd == CMD_READ_RESULT:
            start, count = struct.unpack("<HH", payload)
            assert start + count <= 128
            vals = [-128 + i for i in range(count)]
            return ST_OK, struct.pack(f"<{count}i", *vals)
        if cmd == CMD_READ_ARGMAX:
            return ST_OK, struct.pack("<Hii", 3, 4096, 3900)
        return ST_UNKNOWN_CMD, b""

    tx = LoopbackOrCoSimTransport(baudrate=921600, responder=cosim)
    drv = FeCIMDriver(tx, retries=1)

    ident = drv.identify()
    assert isinstance(ident, DeviceIdentity)
    assert ident.proto_ver == 0x02
    assert ident.rtl_build_id == 0xA2

    drv.set_config(0x01, (128 << 16) | 128)
    assert drv.get_config(0x01) == ((128 << 16) | 128)

    drv.write_weights([1] * 510, addr=0, chunk_size=510)
    drv.write_act([2] * 128, addr=0)
    drv.write_atten([255] * 128, addr=0)
    drv.compute()
    res = drv.read_result(start=0, count=8)
    assert res[0] == -128
    argmax = drv.read_argmax()
    assert isinstance(argmax, ArgmaxResult)
    assert argmax.max_idx == 3 and argmax.max_val == 4096 and argmax.second_val == 3900

    # Baud switching in one driver call
    drv.set_baud(921600, baud_inc=19327)
    assert tx.baudrate == 921600


@pytest.mark.parametrize(
    "status_code,exc_cls",
    [
        (ST_CRC_ERR, CrcError),
        (ST_BAD_LEN, BadLenError),
        (ST_UNKNOWN_CMD, UnknownCmdError),
        (ST_BUSY, BusyError),
        (ST_ADDR_RANGE, AddrRangeError),
    ],
)
def test_every_non_ok_status_raises_own_exception(status_code, exc_cls):
    def fault_responder(cmd: int, payload: bytes):
        return status_code, b""

    tx = LoopbackOrCoSimTransport(responder=fault_responder)
    drv = FeCIMDriver(tx, retries=1)

    with pytest.raises(exc_cls) as exc_info:
        drv.get_config(0x00)
    assert exc_info.value.status_code == status_code


def test_full_128x128_weight_load_timing_at_921600():
    # Wire bytes for 16,384 weight bytes in 64 chunks of 256 data bytes:
    # Each chunk has 5 header/CRC bytes + 2 addr bytes + 256 data bytes = 263 wire bytes.
    # Total wire bytes ~= 64 * 263 = 16,832 bytes (~18.26 ms serial transfer time at 921,600 bps).
    full_weights = bytes([i % 127 for i in range(128 * 128)])
    tx = LoopbackOrCoSimTransport(baudrate=921600)
    drv = FeCIMDriver(tx, retries=1)

    t0 = time.perf_counter()
    drv.write_weights(full_weights, addr=0, chunk_size=256)
    elapsed_ms = (time.perf_counter() - t0) * 1000.0

    assert len(tx.tx_history) == 64
    assert elapsed_ms < 500.0