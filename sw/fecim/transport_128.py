"""Cross-platform serial transport supporting Windows COM* and Linux/WSL /dev/ttyUSB*."""

import serial
from .exceptions import INTER_BYTE_TIMEOUT_S


class SerialTransport:
    """Wrapper around pyserial for COM ports (Windows) and /dev/ttyUSB* (WSL/Linux)."""

    def __init__(
        self,
        port: str,
        baudrate: int = 115200,
        timeout: float = INTER_BYTE_TIMEOUT_S,
    ) -> None:
        self.port = port
        self._ser = serial.Serial(
            port=port,
            baudrate=baudrate,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=timeout,
            write_timeout=timeout,
        )

    @property
    def baudrate(self) -> int:
        return int(self._ser.baudrate)

    def write(self, data: bytes) -> int:
        written = self._ser.write(data)
        self._ser.flush()
        return int(written or 0)

    def read(self, size: int) -> bytes:
        return bytes(self._ser.read(size))

    def set_baudrate(self, baudrate: int) -> None:
        self._ser.baudrate = baudrate

    def reset_input_buffer(self) -> None:
        self._ser.reset_input_buffer()

    def close(self) -> None:
        if self._ser.is_open:
            self._ser.close()

    def __enter__(self) -> "SerialTransport":
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        self.close()