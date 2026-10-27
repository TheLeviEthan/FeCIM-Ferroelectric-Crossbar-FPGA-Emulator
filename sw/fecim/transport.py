"""
FeCIM Loopback Transport Module (sw/fecim/transport.py)
Provides a fake transport interface for offline testing and golden vector verification.
"""

from typing import Optional
import os


class LoopbackTransport:
    """
    Fake serial transport that echoes back predefined or computed responses.
    Allows testing host-driver logic and golden packet correctness without physical hardware.
    """

    def __init__(self, port: str = "loopback", baudrate: int = 921600, timeout: float = 1.0):
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.is_open = False
        self.tx_buffer = bytearray()
        self.rx_buffer = bytearray()

    def open(self) -> None:
        """Simulates opening the transport connection."""
        self.is_open = True

    def close(self) -> None:
        """Simulates closing the transport connection."""
        self.is_open = False
        self.tx_buffer.clear()
        self.rx_buffer.clear()

    def write(self, data: bytes) -> int:
        """
        Writes outgoing request bytes to the transport buffer.
        In loopback mode, bytes are recorded in `tx_buffer`.
        """
        if not self.is_open:
            raise RuntimeError("Transport connection is not open.")
        self.tx_buffer.extend(data)
        return len(data)

    def inject_rx_data(self, data: bytes) -> None:
        """Helper to inject simulated FPGA response bytes into the read stream."""
        self.rx_buffer.extend(data)

    def read(self, size: int = 1) -> bytes:
        """Reads incoming response bytes from the simulated receive buffer."""
        if not self.is_open:
            raise RuntimeError("Transport connection is not open.")
        chunk = bytes(self.rx_buffer[:size])
        del self.rx_buffer[:size]
        return chunk

    def flush(self) -> None:
        """Flushes buffers."""
        self.tx_buffer.clear()
        self.rx_buffer.clear()