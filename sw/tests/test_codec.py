"""
Pytest test suite for FeCIM codec module (sw/fecim/codec.py).
Validates response frame decoding (sync 0x5A, length, CRC-8) and status register bitfield parsing.
"""

# Run <PYTHONPATH=sw pytest -v sw/tests/test_codec.py> to test

import pytest
from fecim.codec import (
    decode_response_frame,
    decode_status_register,
    crc8,
    ST_OK
)


def test_decode_valid_response_frame():
    """Verifies successful decoding of a valid FPGA response frame."""
    # Frame format: [0x5A][STATUS:1][LEN:2 little-endian][PAYLOAD:LEN][CRC-8]
    # Status = 0x00 (ST_OK), Len = 2 (0x0200), Payload = [0x12, 0x34]
    # CRC over [0x00, 0x02, 0x00, 0x12, 0x34] is 0xDD
    valid_frame = b"\x5A\x00\x02\x00\x12\x34\xDD"
    
    status, payload = decode_response_frame(valid_frame)
    
    assert status == ST_OK
    assert payload == b"\x12\x34"


def test_decode_invalid_sync_byte():
    """Verifies that response frames using request sync (0xA5) are rejected."""
    # Using 0xA5 instead of 0x5A should raise a ValueError
    invalid_sync_frame = b"\xA5\x00\x02\x00\x12\x34\xDD"
    
    with pytest.raises(ValueError, match="Invalid response sync byte"):
        decode_response_frame(invalid_sync_frame)


def test_decode_crc_mismatch():
    """Verifies that corrupted CRC bytes raise a ValueError."""
    # Corrupted CRC byte (0xFF instead of 0x8C)
    corrupt_frame = b"\x5A\x00\x02\x00\x12\x34\xFF"
    
    with pytest.raises(ValueError, match="CRC mismatch"):
        decode_response_frame(corrupt_frame)


def test_decode_status_register_bitfields():
    """Verifies correct extraction of 32-bit STATUS config register (0x0A) bitfields."""
    # Value 0x0136:
    #   - fsm_state = 0x6 (bits [3:0])
    #   - busy = True (bit [4])
    #   - last_error = True (bit [5])
    #   - results_valid = False (bit [6])
    #   - result_seq = 1 (bits [15:8])
    reg_value = 0x0136
    fields = decode_status_register(reg_value)
    
    assert fields["fsm_state"] == 6
    assert fields["busy"] is True
    assert fields["last_error"] is True
    assert fields["results_valid"] is False
    assert fields["result_seq"] == 1
    assert fields["raw_value"] == reg_value