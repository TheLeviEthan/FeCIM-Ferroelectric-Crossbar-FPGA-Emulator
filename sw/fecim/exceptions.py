"""FeCIM host protocol exceptions and constants (protocol.md §3-§5)."""

from typing import Dict, Type

PROTO_VER_EXPECTED = 0x02
IDENTIFY_MAGIC = 0xFEC1
MAX_PAYLOAD = 512
INTER_BYTE_TIMEOUT_S = 0.100

CMD_WRITE_WEIGHTS = 0x01
CMD_WRITE_ACT = 0x02
CMD_SET_CONFIG = 0x03
CMD_COMPUTE = 0x04
CMD_READ_RESULT = 0x05
CMD_IDENTIFY = 0x06
CMD_GET_CONFIG = 0x07
CMD_READ_ARGMAX = 0x08
CMD_WRITE_ATTEN = 0x09

REG_CTRL = 0x00
REG_TILE_CFG = 0x01
REG_QUANT_LEVELS = 0x02
REG_D2D_SIGMA = 0x03
REG_READ_SIGMA = 0x04
REG_NOISE_SEED = 0x05
REG_NOISE_EN = 0x06
REG_ADC_BITS = 0x07
REG_STUCK_RATE = 0x08
REG_BAUD_INC = 0x09
REG_STATUS = 0x0A
REG_CYCLE_CNT = 0x0B
REG_QUANT_MULT = 0x0C

ST_OK = 0x00
ST_CRC_ERR = 0x01
ST_BAD_LEN = 0x02
ST_UNKNOWN_CMD = 0x03
ST_BUSY = 0x04
ST_ADDR_RANGE = 0x05


class FeCIMError(Exception):
    """Base exception for FeCIM host driver and protocol errors."""


class FeCIMTimeoutError(FeCIMError):
    """Raised when a response or expected byte times out."""


class FeCIMProtocolError(FeCIMError):
    """Raised on framing, CRC, magic, or protocol version mismatch."""


class FeCIMStatusError(FeCIMError):
    """Base exception for non-OK status codes returned by the FPGA."""
    status_code: int

    def __init__(self, message: str, status_code: int) -> None:
        super().__init__(message)
        self.status_code = status_code


class CrcError(FeCIMStatusError):
    """Status 0x01 (ST_CRC_ERR): CRC mismatch."""
    def __init__(self, message: str = "FPGA reported ST_CRC_ERR (0x01)") -> None:
        super().__init__(message, ST_CRC_ERR)


class BadLenError(FeCIMStatusError):
    """Status 0x02 (ST_BAD_LEN): Invalid payload length."""
    def __init__(self, message: str = "FPGA reported ST_BAD_LEN (0x02)") -> None:
        super().__init__(message, ST_BAD_LEN)


class UnknownCmdError(FeCIMStatusError):
    """Status 0x03 (ST_UNKNOWN_CMD): Unrecognized opcode."""
    def __init__(self, message: str = "FPGA reported ST_UNKNOWN_CMD (0x03)") -> None:
        super().__init__(message, ST_UNKNOWN_CMD)


class BusyError(FeCIMStatusError):
    """Status 0x04 (ST_BUSY): Compute in progress."""
    def __init__(self, message: str = "FPGA reported ST_BUSY (0x04)") -> None:
        super().__init__(message, ST_BUSY)


class AddrRangeError(FeCIMStatusError):
    """Status 0x05 (ST_ADDR_RANGE): Address or range outside configured tile/map."""
    def __init__(self, message: str = "FPGA reported ST_ADDR_RANGE (0x05)") -> None:
        super().__init__(message, ST_ADDR_RANGE)


STATUS_EXCEPTION_MAP: Dict[int, Type[FeCIMStatusError]] = {
    ST_CRC_ERR: CrcError,
    ST_BAD_LEN: BadLenError,
    ST_UNKNOWN_CMD: UnknownCmdError,
    ST_BUSY: BusyError,
    ST_ADDR_RANGE: AddrRangeError,
}


def raise_for_status(status: int, context: str = "") -> None:
    """Raises the specific exception for any non-OK status byte."""
    if status == ST_OK:
        return
    exc_cls = STATUS_EXCEPTION_MAP.get(status)
    suffix = f" ({context})" if context else ""
    if exc_cls is not None:
        raise exc_cls(f"{exc_cls.__name__} [status=0x{status:02X}]{suffix}")
    raise FeCIMStatusError(f"Unknown non-OK status 0x{status:02X}{suffix}", status)