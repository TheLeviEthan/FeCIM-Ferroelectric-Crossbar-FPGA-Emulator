"""
FeCIM Codec Module (sw/fecim/codec.py)
Pure functions for decoding response frames and status registers.
No I/O, no serial ports, no side effects.
"""

import struct
from typing import Tuple, Dict, Any

# Status Code Constants (Response frame status byte)
ST_OK             = 0x00
ST_CRC_ERR        = 0x01
ST_BAD_LEN        = 0x02
ST_UNKNOWN_CMD    = 0x03
ST_BUSY           = 0x04
ST_ADDR_RANGE     = 0x05


def crc8(data: bytes) -> int:
    """
    Computes CRC-8/ATM (polynomial 0x07, init 0x00, no reflection, no final XOR).
    Excludes the sync byte per protocol.md §3.1.
    """
    crc = 0x00
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if (crc & 0x80) else ((crc << 1) & 0xFF)
    return crc


def decode_response_frame(frame: bytes) -> Tuple[int, bytes]:
    """
    Decodes and validates a full FPGA response frame:
    Format: [0x5A][STATUS:1][LEN:2, little-endian][PAYLOAD:LEN][CRC-8]
    
    Note: Response sync is 0x5A (unlike request sync 0xA5).
    CRC covers STATUS, LEN, and PAYLOAD (sync is excluded).
    
    Args:
        frame: Raw bytes of the complete response frame.
        
    Returns:
        A tuple of (status_code, payload_bytes).
        
    Raises:
        ValueError: If sync is invalid, frame is too short, length mismatch, or CRC fails.
    """
    if len(frame) < 4:  # Sync(1) + Status(1) + Len(2) + CRC(1) min
        raise ValueError(f"Frame too short: {len(frame)} bytes")
    
    # 1. Validate response sync byte (must be 0x5A, NOT 0xA5)
    if frame[0] != 0x5A:
        raise ValueError(f"Invalid response sync byte: 0x{frame[0]:02X} (expected 0x5A)")
    
    status = frame[1]
    payload_len = struct.unpack("<H", frame[2:4])[0]
    
    expected_total_len = 1 (sync) + 1 (status) + 2 (len) + payload_len + 1 (crc)
    if len(frame) != expected_total_len:
        raise ValueError(f"Frame length mismatch: got {len(frame)} bytes, expected {expected_total_len}")
    
    # Extract payload and received CRC
    payload = frame[4 : 4 + payload_len]
    rcvd_crc = frame[-1]
    
    # 2. Verify CRC-8 over [STATUS:1][LEN:2][PAYLOAD:LEN]
    crc_data = frame[1 : 4 + payload_len]
    calc_crc = crc8(crc_data)
    
    if calc_crc != rcvd_crc:
        raise ValueError(f"CRC mismatch: calculated 0x{calc_crc:02X}, received 0x{rcvd_crc:02X}")
    
    return status, payload


def decode_status_register(reg_value: int) -> Dict[str, Any]:
    """
    Decodes the 32-bit STATUS config register (Address 0x0A) bitfields 
    per protocol.md §6.4:
      - [3:0]   Control FSM state
      - [4]     Busy flag
      - [5]     Last error flag
      - [6]     Results valid flag
      - [15:8]  Result sequence counter (increments on each completed COMPUTE)
      - [31:16] Reserved / unused
      
    Args:
        reg_value: 32-bit integer value read from register 0x0A.
        
    Returns:
        A dictionary containing the parsed semantic fields.
    """
    fsm_state = reg_value & 0x0F
    busy = bool((reg_value >> 4) & 0x01)
    last_error = bool((reg_value >> 5) & 0x01)
    results_valid = bool((reg_value >> 6) & 0x01)
    result_seq = (reg_value >> 8) & 0xFF
    
    return {
        "fsm_state": fsm_state,
        "busy": busy,
        "last_error": last_error,
        "results_valid": results_valid,
        "result_seq": result_seq,
        "raw_value": reg_value
    }