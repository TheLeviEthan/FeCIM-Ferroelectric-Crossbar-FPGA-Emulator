"""
FeCIM Host Protocol Parser and Serializer
Implements protocol.md specifications for wire protocol version 0x02.
"""

import struct
from typing import Tuple, Generator, Dict, Any

MAX_PAYLOAD = 512

# Command Opcodes
CMD_WRITE_WEIGHTS = 0x01
CMD_WRITE_ACT     = 0x02
CMD_SET_CONFIG    = 0x03
CMD_COMPUTE       = 0x04
CMD_READ_RESULT   = 0x05
CMD_IDENTIFY      = 0x06
CMD_GET_CONFIG    = 0x07
CMD_READ_ARGMAX   = 0x08
CMD_WRITE_ATTEN   = 0x09

# Status Codes (FPGA -> Host)
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


def build_packet(cmd: int, payload: bytes = b"") -> bytes:
    """
    Constructs a Host -> FPGA packet: [0xA5][CMD:1][LEN:2][PAYLOAD:LEN][CRC:1]
    LEN is little-endian 16-bit.
    CRC covers CMD (1 byte) + LEN (2 bytes) + PAYLOAD (N bytes). Sync byte is excluded.
    """
    if len(payload) > MAX_PAYLOAD:
        raise ValueError(f"Payload length {len(payload)} exceeds MAX_PAYLOAD ({MAX_PAYLOAD})")
    
    sync = b"\xA5"
    cmd_byte = struct.pack("B", cmd)
    length_bytes = struct.pack("<H", len(payload))
    
    crc_data = cmd_byte + length_bytes + payload
    crc_byte = struct.pack("B", crc8(crc_data))
    
    return sync + cmd_byte + length_bytes + payload + crc_byte


class PacketParser:
    """
    Stateful stream parser for FPGA -> Host responses:
    [0x5A][STATUS:1][LEN:2][PAYLOAD:LEN][CRC:1]
    Robust against noise and framing errors via sync recovery.
    """
    def __init__(self):
        self.state = "WAIT_SYNC"
        self.status = 0
        self.payload_len = 0
        self.payload = bytearray()
        self.header_buffer = bytearray()
        self.buffer = bytearray()

    def feed(self, data: bytes) -> Generator[Tuple[int, bytes], None, None]:
        """
        Feeds raw serial bytes into the parser and yields successfully parsed (status, payload) tuples.
        """
        self.buffer.extend(data)
        
        while len(self.buffer) > 0:
            if self.state == "WAIT_SYNC":
                if self.buffer[0] == 0x5A:
                    self.buffer.pop(0)
                    self.state = "READ_HEADER"
                    self.header_buffer = bytearray()
                else:
                    self.buffer.pop(0)  # Discard framing noise / wait for sync
            
            elif self.state == "READ_HEADER":
                # Header = STATUS (1 byte) + LEN (2 bytes little-endian) = 3 bytes
                needed = 3 - len(self.header_buffer)
                if len(self.buffer) >= needed:
                    self.header_buffer.extend(self.buffer[:needed])
                    del self.buffer[:needed]
                    
                    self.status = self.header_buffer[0]
                    self.payload_len = struct.unpack("<H", bytes(self.header_buffer[1:3]))[0]
                    
                    if self.payload_len > MAX_PAYLOAD:
                        self.state = "WAIT_SYNC"
                        yield (ST_BAD_LEN, b"")
                    else:
                        self.payload = bytearray()
                        self.state = "READ_PAYLOAD"
                else:
                    self.header_buffer.extend(self.buffer)
                    self.buffer.clear()
                    break
            
            elif self.state == "READ_PAYLOAD":
                needed = self.payload_len - len(self.payload)
                if len(self.buffer) >= needed:
                    self.payload.extend(self.buffer[:needed])
                    del self.buffer[:needed]
                    self.state = "READ_CRC"
                else:
                    self.payload.extend(self.buffer)
                    self.buffer.clear()
                    break
            
            elif self.state == "READ_CRC":
                if len(self.buffer) >= 1:
                    rcvd_crc = self.buffer.pop(0)
                    
                    # CRC covers STATUS (1) + LEN (2) + PAYLOAD (N)
                    status_byte = struct.pack("B", self.status)
                    len_bytes = struct.pack("<H", self.payload_len)
                    calc_crc = crc8(status_byte + len_bytes + bytes(self.payload))
                    
                    self.state = "WAIT_SYNC"
                    
                    if calc_crc == rcvd_crc:
                        yield (self.status, bytes(self.payload))
                    else:
                        yield (ST_CRC_ERR, b"")
                else:
                    break


# --- Response Payload Deserializers ---

def parse_identify_response(payload: bytes) -> Dict[str, Any]:
    """Parses 16-byte IDENTIFY response (§4.5)."""
    if len(payload) != 16:
        raise ValueError(f"IDENTIFY response must be 16 bytes, got {len(payload)}")
    
    magic, proto_ver, rtl_build, num_lanes, rows, cols, uart_err, crc_err, timeout_err = struct.unpack("<H B B H H H H H H", payload)
    return {
        "magic": hex(magic),
        "proto_ver": hex(proto_ver),
        "rtl_build_id": rtl_build,
        "num_lanes": num_lanes,
        "tile_rows": rows,
        "tile_cols": cols,
        "uart_framing_errors": uart_err,
        "crc_errors": crc_err,
        "timeout_count": timeout_err
    }


def parse_read_argmax_response(payload: bytes) -> Dict[str, Any]:
    """Parses 10-byte READ_ARGMAX response (§4.6)."""
    if len(payload) != 10:
        raise ValueError(f"READ_ARGMAX response must be 10 bytes, got {len(payload)}")
    
    max_idx, max_val, second_val = struct.unpack("<H i i", payload)
    return {
        "max_idx": max_idx,
        "max_val": max_val,
        "second_val": second_val
    }


def parse_get_config_response(payload: bytes) -> int:
    """Parses 4-byte GET_CONFIG response."""
    if len(payload) != 4:
        raise ValueError(f"GET_CONFIG response must be 4 bytes, got {len(payload)}")
    return struct.unpack("<I", payload)[0]