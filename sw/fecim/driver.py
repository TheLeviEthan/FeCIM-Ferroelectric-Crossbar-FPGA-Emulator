"""FeCIM Host Driver (protocol.md v0x02)."""

from dataclasses import dataclass
import math
import struct
import time
from typing import Iterable, List, Optional, Protocol, Sequence, Tuple, Union

from .exceptions import (
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
    FeCIMProtocolError,
    FeCIMStatusError,
    FeCIMTimeoutError,
    IDENTIFY_MAGIC,
    INTER_BYTE_TIMEOUT_S,
    MAX_PAYLOAD,
    PROTO_VER_EXPECTED,
    REG_BAUD_INC,
    REG_QUANT_LEVELS,
    REG_QUANT_MULT,
    ST_OK,
    UnknownCmdError,
    raise_for_status,
)

HOST_SYNC = 0xA5
DEV_SYNC = 0x5A
MAX_CHUNK_DATA = MAX_PAYLOAD - 2  # 2-byte little-endian address + data <= 512 bytes


class ByteTransport(Protocol):
    """Transport interface for serial (COM* / /dev/ttyUSB*), loopback fake, or virtual-UART co-sim."""
    baudrate: int

    def write(self, data: bytes) -> int: ...
    def read(self, size: int) -> bytes: ...
    def set_baudrate(self, baudrate: int) -> None: ...
    def reset_input_buffer(self) -> None: ...
    def close(self) -> None: ...


def crc8(data: bytes) -> int:
    """CRC-8/ATM: poly 0x07, init 0x00, no reflection, no final XOR (protocol.md §3.1)."""
    crc = 0x00
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ 0x07) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc


def build_request_frame(cmd: int, payload: bytes = b"") -> bytes:
    """Builds [0xA5][CMD:1][LEN:2][PAYLOAD:LEN][CRC:1] (protocol.md §3)."""
    length = len(payload)
    if length > MAX_PAYLOAD:
        raise ValueError(f"Payload length {length} exceeds MAX_PAYLOAD ({MAX_PAYLOAD})")
    body = struct.pack("<BH", cmd & 0xFF, length) + payload
    return bytes([HOST_SYNC]) + body + bytes([crc8(body)])


def parse_response_frame(frame: bytes) -> Tuple[int, bytes]:
    """Parses [0x5A][STATUS:1][LEN:2][PAYLOAD:LEN][CRC:1] and verifies CRC-8."""
    if len(frame) < 5:
        raise FeCIMProtocolError(f"Response frame too short: {len(frame)} bytes")
    if frame[0] != DEV_SYNC:
        raise FeCIMProtocolError(f"Invalid device sync byte: 0x{frame[0]:02X}")
    status, length = struct.unpack_from("<BH", frame, 1)
    if length > MAX_PAYLOAD:
        raise FeCIMProtocolError(f"Response length {length} exceeds MAX_PAYLOAD ({MAX_PAYLOAD})")
    expected_len = 1 + 1 + 2 + length + 1
    if len(frame) != expected_len:
        raise FeCIMProtocolError(f"Expected frame length {expected_len}, got {len(frame)}")
    body = frame[1 : 1 + 1 + 2 + length]
    wire_crc = frame[-1]
    calc_crc = crc8(body)
    if wire_crc != calc_crc:
        raise FeCIMProtocolError(f"CRC mismatch: wire=0x{wire_crc:02X}, calc=0x{calc_crc:02X}")
    payload = frame[4 : 4 + length]
    return status, payload


@dataclass(frozen=True)
class DeviceIdentity:
    """Parsed 16-byte IDENTIFY response (protocol.md §4.5)."""
    magic: int
    proto_ver: int
    rtl_build_id: int
    num_lanes: int
    tile_rows: int
    tile_cols: int
    framing_err_count: int
    crc_err_count: int
    timeout_count: int

    @property
    def acc_used_w(self) -> int:
        """ACC_USED_W = ceil(log2(254 * 255 * TILE_ROWS)) + 1 (protocol.md §6.5)."""
        return math.ceil(math.log2(254 * 255 * self.tile_rows)) + 1


@dataclass(frozen=True)
class ArgmaxResult:
    """Parsed 10-byte READ_ARGMAX response (protocol.md §4.6)."""
    max_idx: int
    max_val: int
    second_val: int


class FeCIMDriver:
    """Host driver for FeCIM FPGA emulator over serial or co-sim transport."""

    def __init__(
        self,
        transport: ByteTransport,
        retries: int = 3,
        check_version: bool = True,
    ) -> None:
        self.transport = transport
        self.retries = retries
        self.check_version = check_version
        self.last_identity: Optional[DeviceIdentity] = None

    def _read_exact(self, n: int) -> bytes:
        buf = bytearray()
        while len(buf) < n:
            chunk = self.transport.read(n - len(buf))
            if not chunk:
                raise FeCIMTimeoutError(f"Timed out waiting for {n - len(buf)} byte(s)")
            buf.extend(chunk)
        return bytes(buf)

    def _transact_once(self, cmd: int, payload: bytes = b"") -> bytes:
        req = build_request_frame(cmd, payload)
        self.transport.write(req)

        # Hunt for 0x5A sync byte
        while True:
            b = self._read_exact(1)
            if b[0] == DEV_SYNC:
                break

        hdr = self._read_exact(3)  # [STATUS:1][LEN:2]
        status, length = struct.unpack("<BH", hdr)
        if length > MAX_PAYLOAD:
            raise FeCIMProtocolError(f"Device declared LEN={length} > MAX_PAYLOAD ({MAX_PAYLOAD})")
        body_tail = self._read_exact(length + 1)  # [PAYLOAD:LEN][CRC:1]
        full_frame = bytes([DEV_SYNC]) + hdr + body_tail
        parsed_status, resp_payload = parse_response_frame(full_frame)
        raise_for_status(parsed_status, context=f"cmd=0x{cmd:02X}")
        return resp_payload

    def _send_command(self, cmd: int, payload: bytes = b"") -> bytes:
        """Sends a command with retry and 100 ms post-error/timeout quiet wait (protocol.md §7)."""
        last_exc: Optional[Exception] = None
        for attempt in range(self.retries):
            try:
                return self._transact_once(cmd, payload)
            except (FeCIMTimeoutError, FeCIMProtocolError, CrcError, BusyError) as exc:
                last_exc = exc
                if attempt < self.retries - 1:
                    time.sleep(INTER_BYTE_TIMEOUT_S)
                    continue
                raise
            except FeCIMStatusError:
                # Non-retryable protocol/address/length/opcode status errors
                raise
        raise last_exc if last_exc is not None else FeCIMTimeoutError("Command failed")

    def _write_chunked(
        self,
        cmd: int,
        start_addr: int,
        data: bytes,
        chunk_size: int = 256,
    ) -> None:
        if chunk_size <= 0 or chunk_size > MAX_CHUNK_DATA:
            raise ValueError(f"chunk_size must be in 1..{MAX_CHUNK_DATA}, got {chunk_size}")
        if start_addr < 0 or start_addr > 0xFFFF:
            raise ValueError(f"start_addr out of uint16 range: {start_addr}")

        offset = 0
        total = len(data)
        while offset < total:
            n = min(chunk_size, total - offset)
            addr = start_addr + offset
            if addr > 0xFFFF:
                raise AddrRangeError(f"Chunk address {addr} exceeds uint16")
            chunk = data[offset : offset + n]
            pkt_payload = struct.pack("<H", addr) + chunk
            self._send_command(cmd, pkt_payload)
            offset += n

    # --- Protocol §4 Command Methods ---

    def identify(self) -> DeviceIdentity:
        """CMD 0x06: IDENTIFY -> 16-byte DeviceIdentity (protocol.md §4.5)."""
        raw = self._send_command(CMD_IDENTIFY, b"")
        if len(raw) != 16:
            raise FeCIMProtocolError(f"IDENTIFY expected 16 bytes, got {len(raw)}")
        magic, proto_ver, build_id, num_lanes, tile_rows, tile_cols, ferr, cerr, terr = (
            struct.unpack("<HBBHHHHHH", raw)
        )
        ident = DeviceIdentity(
            magic=magic,
            proto_ver=proto_ver,
            rtl_build_id=build_id,
            num_lanes=num_lanes,
            tile_rows=tile_rows,
            tile_cols=tile_cols,
            framing_err_count=ferr,
            crc_err_count=cerr,
            timeout_count=terr,
        )
        if self.check_version:
            if ident.magic != IDENTIFY_MAGIC:
                raise FeCIMProtocolError(
                    f"IDENTIFY magic mismatch: expected 0x{IDENTIFY_MAGIC:04X}, got 0x{ident.magic:04X}"
                )
            if ident.proto_ver != PROTO_VER_EXPECTED:
                raise FeCIMProtocolError(
                    f"IDENTIFY protocol version mismatch: expected 0x{PROTO_VER_EXPECTED:02X}, "
                    f"got 0x{ident.proto_ver:02X} (stale bitstream)"
                )
        self.last_identity = ident
        return ident

    def set_config(self, reg: int, value: int) -> None:
        """CMD 0x03: SET_CONFIG [reg:1][value:4] (protocol.md §4 & §6)."""
        if not (0 <= reg <= 0xFF):
            raise ValueError(f"Register address out of byte range: {reg}")
        payload = struct.pack("<BI", reg, value & 0xFFFFFFFF)
        self._send_command(CMD_SET_CONFIG, payload)

    def get_config(self, reg: int) -> int:
        """CMD 0x07: GET_CONFIG [reg:1] -> uint32 value (protocol.md §4 & §6)."""
        if not (0 <= reg <= 0xFF):
            raise ValueError(f"Register address out of byte range: {reg}")
        raw = self._send_command(CMD_GET_CONFIG, struct.pack("<B", reg))
        if len(raw) != 4:
            raise FeCIMProtocolError(f"GET_CONFIG expected 4 bytes, got {len(raw)}")
        (val,) = struct.unpack("<I", raw)
        return val

    def write_weights(
        self,
        weights_int8: Union[bytes, bytearray, Sequence[int]],
        addr: int = 0,
        chunk_size: int = 256,
    ) -> None:
        """CMD 0x01: WRITE_WEIGHTS [addr:2][data:N], signed int8 column-major (protocol.md §4.1)."""
        if isinstance(weights_int8, (bytes, bytearray)):
            raw_bytes = bytes(weights_int8)
        else:
            raw_bytes = struct.pack(f"<{len(weights_int8)}b", *weights_int8)
        self._write_chunked(CMD_WRITE_WEIGHTS, addr, raw_bytes, chunk_size=chunk_size)

    def write_act(
        self,
        activations_uint8: Union[bytes, bytearray, Sequence[int]],
        addr: int = 0,
        chunk_size: int = MAX_CHUNK_DATA,
    ) -> None:
        """CMD 0x02: WRITE_ACT [addr:2][data:N], unsigned uint8 per row (protocol.md §4.2)."""
        if isinstance(activations_uint8, (bytes, bytearray)):
            raw_bytes = bytes(activations_uint8)
        else:
            raw_bytes = struct.pack(f"<{len(activations_uint8)}B", *activations_uint8)
        self._write_chunked(CMD_WRITE_ACT, addr, raw_bytes, chunk_size=chunk_size)

    def write_atten(
        self,
        atten_uint8: Union[bytes, bytearray, Sequence[int]],
        addr: int = 0,
        chunk_size: int = MAX_CHUNK_DATA,
    ) -> None:
        """CMD 0x09: WRITE_ATTEN [addr:2][data:N], Q0.8 uint8 (0..255) per row (protocol.md §4.2b)."""
        if isinstance(atten_uint8, (bytes, bytearray)):
            raw_bytes = bytes(atten_uint8)
        else:
            raw_bytes = struct.pack(f"<{len(atten_uint8)}B", *atten_uint8)
        self._write_chunked(CMD_WRITE_ATTEN, addr, raw_bytes, chunk_size=chunk_size)

    def compute(self) -> None:
        """CMD 0x04: COMPUTE (blocking until completion) (protocol.md §4.3)."""
        self._send_command(CMD_COMPUTE, b"")

    def read_result(self, start: int = 0, count: int = 128) -> List[int]:
        """CMD 0x05: READ_RESULT [start:2][count:2] -> list of int32 words (protocol.md §4.4)."""
        if count < 0 or count > 128:
            raise ValueError(f"count must be in 0..128 (MAX_PAYLOAD=512), got {count}")
        payload = struct.pack("<HH", start, count)
        raw = self._send_command(CMD_READ_RESULT, payload)
        expected_bytes = count * 4
        if len(raw) != expected_bytes:
            raise FeCIMProtocolError(f"READ_RESULT expected {expected_bytes} bytes, got {len(raw)}")
        return list(struct.unpack(f"<{count}i", raw))

    def read_argmax(self) -> ArgmaxResult:
        """CMD 0x08: READ_ARGMAX -> 10-byte ArgmaxResult (protocol.md §4.6)."""
        raw = self._send_command(CMD_READ_ARGMAX, b"")
        if len(raw) != 10:
            raise FeCIMProtocolError(f"READ_ARGMAX expected 10 bytes, got {len(raw)}")
        max_idx, max_val, second_val = struct.unpack("<Hii", raw)
        return ArgmaxResult(max_idx=max_idx, max_val=max_val, second_val=second_val)

    # --- Convenience & Baud Switching Helpers ---

    def set_quantization(self, n_levels: int) -> None:
        """Writes paired QUANT_LEVELS (0x02) and QUANT_MULT (0x0C) together (protocol.md §6.2)."""
        if not (2 <= n_levels <= 255):
            raise ValueError(f"QUANT_LEVELS must be in 2..255, got {n_levels}")
        quant_mult = round(255 * 256 / (n_levels - 1))
        self.set_config(REG_QUANT_LEVELS, n_levels)
        self.set_config(REG_QUANT_MULT, quant_mult)

    def set_baud(self, baudrate: int, baud_inc: Optional[int] = None, clk_hz: int = 50_000_000) -> None:
        """Switches baud rate in one driver call: updates REG_BAUD_INC if given and updates transport."""
        if baud_inc is None:
            # Default reset value is 2416 at 115200 baud on 50 MHz clock
            baud_inc = max(1, round((baudrate * (2 ** 20)) / clk_hz))
        self.set_config(REG_BAUD_INC, baud_inc)
        self.transport.set_baudrate(baudrate)