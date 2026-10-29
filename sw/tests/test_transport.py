"""
Pytest test suite for FeCIM Loopback Transport module (sw/fecim/transport.py).
Validates connection lifecycle, transmit buffering, receive queue injection, and safety checks.
"""

# Run <PYTHONPATH=sw pytest -v sw/tests/test_transport.py> to test

import pytest
from fecim.transport import LoopbackTransport


def test_transport_initial_state():
    """Verifies transport initializes in a closed state with empty buffers."""
    transport = LoopbackTransport(port="loopback", baudrate=921600)
    
    assert transport.is_open is False
    assert transport.port == "loopback"
    assert transport.baudrate == 921600
    assert len(transport.tx_buffer) == 0
    assert len(transport.rx_buffer) == 0


def test_transport_open_and_close():
    """Verifies transport open and close lifecycle methods."""
    transport = LoopbackTransport()
    
    transport.open()
    assert transport.is_open is True
    
    transport.close()
    assert transport.is_open is False
    assert len(transport.tx_buffer) == 0
    assert len(transport.rx_buffer) == 0


def test_transport_write_accumulates_tx_buffer():
    """Verifies that writing bytes properly appends to the transmit buffer when open."""
    transport = LoopbackTransport()
    transport.open()
    
    packet = b"\xA5\x06\x00\x00\x7D"
    bytes_written = transport.write(packet)
    
    assert bytes_written == len(packet)
    assert bytes(transport.tx_buffer) == packet
    
    transport.close()


def test_transport_write_fails_when_closed():
    """Verifies writing to a closed transport raises a RuntimeError."""
    transport = LoopbackTransport()
    
    with pytest.raises(RuntimeError, match="Transport connection is not open"):
        transport.write(b"\xA5\x06\x00\x00\x7D")


def test_loopback_rx_injection_and_read():
    """Verifies injecting mock FPGA response bytes and reading them back correctly."""
    transport = LoopbackTransport()
    transport.open()
    
    # Simulate FPGA response frame
    simulated_response = b"\x5A\x00\x02\x00\x12\x34\xDD"
    transport.inject_rx_data(simulated_response)
    
    # Read back chunks
    read_chunk = transport.read(len(simulated_response))
    assert read_chunk == simulated_response
    assert len(transport.rx_buffer) == 0  # Buffer should be consumed
    
    transport.close()


def test_transport_read_fails_when_closed():
    """Verifies reading from a closed transport raises a RuntimeError."""
    transport = LoopbackTransport()
    
    with pytest.raises(RuntimeError, match="Transport connection is not open"):
        transport.read(4)


def test_transport_flush():
    """Verifies flushing clears both transmission and reception buffers."""
    transport = LoopbackTransport()
    transport.open()
    
    transport.write(b"\x01\x02\x03")
    transport.inject_rx_data(b"\x04\x05\x06")
    
    assert len(transport.tx_buffer) > 0
    assert len(transport.rx_buffer) > 0
    
    transport.flush()
    
    assert len(transport.tx_buffer) == 0
    assert len(transport.rx_buffer) == 0
    
    transport.close()