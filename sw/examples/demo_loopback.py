"""
FeCIM Loopback Demo Script (sw/examples/demo_loopback.py)
Demonstrates building packets per opcode, pushing them through LoopbackTransport,
and decoding responses using protocol, codec, and transport modules.
"""

# run <PYTHONPATH=sw python3 sw/examples/demo_loopback.py> to test

from fecim.protocol import (
    build_packet, 
    crc8, 
    PacketParser,
    CMD_WRITE_WEIGHTS, 
    CMD_WRITE_ACT, 
    CMD_SET_CONFIG, 
    CMD_COMPUTE, 
    CMD_READ_RESULT, 
    CMD_IDENTIFY, 
    CMD_GET_CONFIG, 
    CMD_READ_ARGMAX, 
    CMD_WRITE_ATTEN,
    ST_OK,
    ST_CRC_ERR
)
from fecim.codec import decode_response_frame
from fecim.transport import LoopbackTransport

# Human-readable label mappings for command opcodes
CMD_NAMES = {
    CMD_IDENTIFY: "IDENTIFY",
    CMD_WRITE_WEIGHTS: "WRITE_WEIGHTS",
    CMD_WRITE_ACT: "WRITE_ACT",
    CMD_SET_CONFIG: "SET_CONFIG",
    CMD_COMPUTE: "COMPUTE",
    CMD_READ_RESULT: "READ_RESULT",
    CMD_GET_CONFIG: "GET_CONFIG",
    CMD_READ_ARGMAX: "READ_ARGMAX",
    CMD_WRITE_ATTEN: "WRITE_ATTEN",
}

# Human-readable label mappings for status codes
STATUS_NAMES = {
    ST_OK: "ST_OK",
    ST_CRC_ERR: "ST_CRC_ERR",
    0x02: "ST_BAD_LEN",
    0x03: "ST_UNKNOWN_CMD",
    0x04: "ST_BUSY",
    0x05: "ST_ADDR_RANGE",
}

def run_demo():
    transport = LoopbackTransport()
    transport.open()

    # Define test opcodes and sample payloads per protocol.md
    test_cases = [
        (CMD_IDENTIFY, b""),
        (CMD_WRITE_WEIGHTS, b"\x00\x00\x01\x02\x03"),
        (CMD_SET_CONFIG, b"\x01\x00\x00\x00\x00"),
        (CMD_COMPUTE, b""),
        (CMD_READ_RESULT, b"\x00\x00\x01\x00"),
    ]

    print(f"{'Command':<24} | {'Encoded Request Bytes (Hex)':<32} | {'CRC':<4} | {'Decoded Status / Payload'}")
    print("-" * 105)

    for cmd, payload in test_cases:
        # 1. Build request packet using protocol.build_packet[cite: 8]
        packet = build_packet(cmd=cmd, payload=payload)
        
        # 2. Push through LoopbackTransport write buffer[cite: 9]
        transport.write(packet)
        
        # Simulate FPGA response frame: [0x5A][STATUS:1][LEN:2, little-endian][PAYLOAD:LEN][CRC-1][cite: 6, 10]
        resp_status = ST_OK
        resp_payload = payload if payload else b"\xF1\xFE\xC1\x02"
        resp_len_bytes = len(resp_payload).to_bytes(2, "little")
        
        # CRC covers STATUS (1) + LEN (2) + PAYLOAD (N)[cite: 6, 10]
        crc_data = bytes([resp_status]) + resp_len_bytes + resp_payload
        resp_crc = crc8(crc_data)
        response_frame = b"\x5A" + crc_data + bytes([resp_crc])
        
        # 3. Inject simulated response into transport rx buffer[cite: 9]
        transport.inject_rx_data(response_frame)
        
        # Read back from transport and decode using codec.decode_response_frame[cite: 9, 10]
        raw_response = transport.read(len(response_frame))
        status, decoded_payload = decode_response_frame(raw_response)
        
        cmd_name = CMD_NAMES.get(cmd, f"UNKNOWN (0x{cmd:02X})")
        status_name = STATUS_NAMES.get(status, f"UNKNOWN (0x{status:02X})")
        
        print(f"0x{cmd:02X} ({cmd_name:<16}) | {packet.hex():<32} | 0x{packet[-1]:02X} | Status: 0x{status:02X} ({status_name}), Payload: {decoded_payload.hex()}")

    # 4. Test Bad-CRC frame handling (showing both Codec Exception and Parser ST_CRC_ERR State)
    print("\n--- Testing Bad-CRC Frame Injection ---")
    bad_payload = b"\x00\x00"
    bad_len_bytes = len(bad_payload).to_bytes(2, "little")
    bad_crc_data = bytes([ST_OK]) + bad_len_bytes + bad_payload
    bad_crc = (crc8(bad_crc_data) + 1) & 0xFF  # Intentionally corrupt CRC
    bad_response_frame = b"\x5A" + bad_crc_data + bytes([bad_crc])
    
    transport.inject_rx_data(bad_response_frame)
    raw_bad = transport.read(len(bad_response_frame))
    
    # Display 1: Codec Exception Error message
    try:
        decode_response_frame(raw_bad)
    except ValueError as e:
        print(f"-> Codec Exception Error: {e}")

    # Display 2: Stateful PacketParser status code and state (ST_CRC_ERR)[cite: 5, 8]
    parser = PacketParser()
    for err_status, _ in parser.feed(raw_bad):
        err_status_name = STATUS_NAMES.get(err_status, f"UNKNOWN (0x{err_status:02X})")
        print(f"-> Parser State / Status Code: 0x{err_status:02X} ({err_status_name}) - Successfully caught error state")

    transport.close()

if __name__ == "__main__":
    run_demo()