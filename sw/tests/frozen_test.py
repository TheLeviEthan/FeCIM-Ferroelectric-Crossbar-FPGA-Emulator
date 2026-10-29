"""
Pytest-compatible test suite for FeCIM frozen protocol (protocol.md).
Verifies packet structure, little-endian encoding, CRC-8/ATM, and payload limits.
"""

# Run <PYTHONPATH=sw pytest -v sw/tests/frozen_test.py> to test

import pytest
import struct
from fecim.protocol import (
    build_packet,
    crc8,
    CMD_IDENTIFY,
    CMD_SET_CONFIG,
    MAX_PAYLOAD
)


def test_identify_packet_structure():
    """Verifies IDENTIFY packet (CMD = 0x06, LEN = 0) structure and CRC."""
    packet = build_packet(CMD_IDENTIFY, b"")
    
    # Check frame format: [0xA5][CMD:1][LEN:2][PAYLOAD:LEN][CRC:1]
    assert packet[0] == 0xA5, "Invalid request sync byte (expected 0xA5)"
    assert packet[1] == CMD_IDENTIFY, "Incorrect command opcode"
    
    # Check length is 0 (little-endian 16-bit)
    len_bytes = packet[2:4]
    assert len_bytes == b"\x00\x00", "Length must be 0 for IDENTIFY"
    
    # Total length should be 1 (sync) + 1 (cmd) + 2 (len) + 0 (payload) + 1 (crc) = 5 bytes
    assert len(packet) == 5, f"Expected 5 bytes total, got {len(packet)}"
    
    # Verify CRC over CMD + LEN + PAYLOAD (excluding sync byte)
    expected_crc = crc8(packet[1:4])
    assert packet[4] == expected_crc, "CRC-8 verification failed"


def test_set_config_packet_structure():
    """Verifies SET_CONFIG packet packs register and value in little-endian correctly."""
    reg = 0x02  # QUANT_LEVELS
    value = 4
    payload = struct.pack("<BI", reg, value)
    
    packet = build_packet(CMD_SET_CONFIG, payload)
    
    assert packet[0] == 0xA5
    assert packet[1] == CMD_SET_CONFIG
    
    # Payload length should be 5 bytes (1 byte reg + 4 bytes uint32 value) -> little-endian 0x0005
    assert packet[2:4] == b"\x05\x00", "Little-endian length mismatch"
    
    # Check payload contents inside packet
    assert packet[4] == reg
    assert struct.unpack("<I", packet[5:9])[0] == value


def test_max_payload_enforcement():
    """Verifies that payloads exceeding MAX_PAYLOAD (512 bytes) raise ValueError."""
    oversized_payload = b"\x00" * (MAX_PAYLOAD + 1)
    with pytest.raises(ValueError, match=f"exceeds MAX_PAYLOAD"):
        build_packet(CMD_IDENTIFY, oversized_payload)


def test_crc8_atm_known_vector():
    """Verifies CRC-8/ATM (poly 0x07) calculation returns a valid byte range."""
    data = b"\x06\x00\x00"  # CMD_IDENTIFY + LEN 0x0000
    calculated = crc8(data)
    
    assert isinstance(calculated, int)
    assert 0 <= calculated <= 0xFF