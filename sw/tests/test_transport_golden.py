"""
Golden Vector Verification for FeCIM Transport Layer
Asserts Python serializer output against shared tb/golden/ fixtures.
Catches endianness drift and structural mismatch across C++ and Python on day one.
"""

import os
import pytest
from fecim.protocol import build_packet, CMD_IDENTIFY, CMD_SET_CONFIG, CMD_COMPUTE
from fecim.transport import LoopbackTransport


def parse_hex_golden_file(filepath: str) -> bytes:
    """Extracts raw binary bytes from hex-annotated golden vector files."""
    raw_bytes = bytearray()
    with open(filepath, "r") as f:
        for line in f:
            # Strip comments (e.g. # comment)
            line = line.split("#")[0].strip()
            if not line:
                continue
            # Parse hex pairs
            for hex_code in line.split():
                raw_bytes.append(int(hex_code, 16))
    return bytes(raw_bytes)


@pytest.fixture
def golden_packet_bytes():
    """Finds and loads the shared tb/golden/packet_vectors.bin.hex fixture."""
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
    golden_path = os.path.join(repo_root, "tb", "golden", "packet_vectors.bin.hex")
    
    if not os.path.exists(golden_path):
        pytest.skip(f"Golden fixture file not found at {golden_path}")
        
    return parse_hex_golden_file(golden_path)


def test_transport_golden_endianness(golden_packet_bytes):
    """
    Asserts encoded packet bytes sent through transport match the shared golden vector.
    Verifies Little-Endian word ordering and CRC-8 calculation.
    """
    transport = LoopbackTransport()
    transport.open()

    # 1. Construct standard IDENTIFY command packet (CMD = 0x06, LEN = 0)
    packet = build_packet(CMD_IDENTIFY, b"")
    
    # 2. Transmit through transport layer
    transport.write(packet)
    
    # 3. Assert Little-Endian layout matches the first vector block in golden file
    expected_header = golden_packet_bytes[:len(packet)]
    assert bytes(transport.tx_buffer) == expected_header, (
        f"Transport Little-Endian output mismatch!\n"
        f"Got:      {bytes(transport.tx_buffer).hex()}\n"
        f"Expected: {expected_header.hex()}"
    )

    transport.close()


def test_loopback_echo_roundtrip():
    """Verifies loopback transport echo injection and frame reading."""
    transport = LoopbackTransport()
    transport.open()

    # Simulate FPGA responding with STATUS=0x00, LEN=2, PAYLOAD=[0x12, 0x34]
    # Sync byte 0x5A, Status 0x00, Length 0x02 0x00, Payload 0x12 0x34
    simulated_fpga_response = b"\x5A\x00\x02\x00\x12\x34\x8C"
    
    transport.inject_rx_data(simulated_fpga_response)
    received = transport.read(len(simulated_fpga_response))

    assert received == simulated_fpga_response
    transport.close()