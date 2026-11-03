#!/usr/bin/env python3
"""
Generator script for FeCIM packet vectors.
Outputs pure hex strings adhering to the frozen v0x02 protocol:
[A5][CMD][LEN:2][PAYLOAD][CRC-8/ATM]
"""

# run <PYTHONPATH=sw python3 tb/golden/gen_packet_vectors.py> to test

import sys
import os

# Add sw directory to path to import protocol
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '../../sw')))
from fecim.protocol import build_packet

def generate_vectors():
    # Define test vectors: (cmd, payload_bytes)
    vectors = [
        (0x01, b"\x0A\x00\x00\x00"),  # READ Status Register
        (0x02, b"\x10\x01\xFF\x00"),  # WRITE Configuration
        (0x03, b"\x00\x00\x00\x00"),  # EXEC Compute trigger
        (0x01, b"\x02\x00\x00\x00"),  # READ Error Register (Bad CRC test vector case)
    ]
    
    hex_lines = []
    for cmd, payload in vectors:
        # Build packet using protocol.py (handles [A5][CMD][LEN:2][PAYLOAD][CRC-8])
        packet = build_packet(cmd=cmd, payload=payload)
        hex_lines.append(packet.hex())

    output_path = os.path.join(os.path.dirname(__file__), "packet_vectors.bin.hex")
    with open(output_path, "w") as f:
        for line in hex_lines:
            f.write(f"{line}\n")
            
    print(f"Successfully generated pure hex vectors at {output_path}")

if __name__ == "__main__":
    generate_vectors()