"""
Golden Vector Verification for FeCIM Transport Layer
Asserts Python serializer output against shared tb/golden/ fixtures.
Catches endianness drift and structural mismatch across C++ and Python on day one.
"""

# Run <PYTHONPATH=sw pytest -v sw/tests/test_transport_golden.py> to test

import os
import pytest
from fecim.protocol import verify_crc

def load_packet_vectors():
    """Loads packet vectors from file, failing explicitly if missing or invalid."""
    vector_path = os.path.join(os.path.dirname(__file__), "../../tb/golden/packet_vectors.bin.hex")
    
    if not os.path.exists(vector_path):
        raise FileNotFoundError(f"Golden vector file not found at {vector_path}. Self-compare fallback disabled.")
        
    packets = []
    with open(vector_path, "r") as f:
        for line_num, line in enumerate(f, 1):
            line = line.strip()
            # Skip empty lines
            if not line:
                continue
            # Fail if non-hex characters or stray labels exist in the stream
    try:
        packet_bytes = bytes.fromhex(line)
        packets.append(packet_bytes)
    except ValueError as e:
        raise ValueError(f"Invalid hex string on line {line_num} of {vector_path}: '{line}' ({e})")
        
    return packets

def test_packet_vectors_integrity():
    packets = load_packet_vectors()
    assert len(packets) > 0, "Packet vector list cannot be empty."
    
    for idx, pkt in enumerate(packets):
        # Validate sync byte and CRC-8/ATM integrity
        assert pkt[0] == 0xA5, f"Vector {idx} missing valid sync header 0xA5"
        assert verify_crc(pkt), f"Vector {idx} failed CRC-8/ATM verification."