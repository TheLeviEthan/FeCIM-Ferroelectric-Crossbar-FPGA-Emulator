"""
FeCIM Transport Module (sw/fecim/transport.py)
Provides LoopbackTransport for offline simulation and SerialTransport for physical UART hardware.
"""

import time
import serial
from typing import Optional

class LoopbackTransport:
    """Fake serial transport that echoes back predefined or computed responses."""
    def __init__(self, port: str = "loopback", baudrate: int = 921600, timeout: float = 1.0):
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.is_open = False
        self.tx_buffer = bytearray()
        self.rx_buffer = bytearray()

    def open(self) -> None:
        self.is_open = True

    def close(self) -> None:
        self.is_open = False
        self.tx_buffer.clear()
        self.rx_buffer.clear()

    def write(self, data: bytes) -> int:
        if not self.is_open:
            raise RuntimeError("Transport connection is not open.")
        self.tx_buffer.extend(data)
        return len(data)

    def inject_rx_data(self, data: bytes) -> None:
        self.rx_buffer.extend(data)

    def read(self, size: int = 1) -> bytes:
        if not self.is_open:
            raise RuntimeError("Transport connection is not open.")
        chunk = bytes(self.rx_buffer[:size])
        del self.rx_buffer[:size]
        return chunk

    def flush(self) -> None:
        self.tx_buffer.clear()
        self.rx_buffer.clear()


class SerialTransport:
    """Physical UART serial transport wrapper using PySerial for hardware bring-up."""
    def __init__(self, port: str, baudrate: int = 921600, timeout: float = 0.1):
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self._serial: Optional[serial.Serial] = None

    @property
    def is_open(self) -> bool:
        return self._serial is not None and self._serial.is_open

    def open(self) -> None:
        """Opens the physical serial port."""
        if not self.is_open:
            self._serial = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=self.timeout
            )
            self._serial.reset_input_buffer()
            self._serial.reset_output_buffer()

    def close(self) -> None:
        """Closes the physical serial port."""
        if self._serial and self._serial.is_open:
            self._serial.close()
            self._serial = None

    def write(self, data: bytes) -> int:
        if not self.is_open:
            raise RuntimeError("Serial transport connection is not open.")
        return self._serial.write(data)

    def read(self, size: int = 1) -> bytes:
        if not self.is_open:
            raise RuntimeError("Serial transport connection is not open.")
        return self._serial.read(size)

    def flush(self) -> None:
        if self._serial and self._serial.is_open:
            self._serial.reset_input_buffer()
            self._serial.reset_output_buffer()