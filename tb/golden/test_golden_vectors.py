import os
import unittest
import struct

def crc16_ccitt(data: bytes, poly: int = 0x1021, init: int = 0xFFFF) -> int:
    """Standard CRC16-CCITT implementation for cross-verification."""
    crc = init
    for byte in data:
        crc ^= (byte << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ poly) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc

class TestGoldenVectors(unittest.TestCase):
    def setUp(self):
        # Locate the shared golden vector file
        self.golden_path = os.path.join(
            os.path.dirname(__file__), "..", "..", "tb", "golden", "packet_vectors.bin.hex"
        )

    def load_hex_vectors(self):
        packets = []
        with open(self.golden_path, "r") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                hex_str = line.replace(" ", "")
                packets.append(bytes.fromhex(hex_str))
        return packets

    def test_endianness_and_crc(self):
        packets = self.load_hex_vectors()
        self.assertGreater(len(packets), 0, "No golden vectors loaded.")

        for idx, pkt in enumerate(packets):
            self.assertEqual(len(pkt), 10, f"Packet {idx} length mismatch")
            
            header, cmd, addr, payload, expected_crc = struct.unpack("<BBHIH", pkt)
            
            # Verify Magic Header
            self.assertEqual(header, 0xA5, f"Packet {idx} bad magic header")
            
            # Compute CRC over header + cmd + addr + payload (first 8 bytes)
            computed_crc = crc16_ccitt(pkt[:8])
            
            # Assert CRC matches golden value in Little-Endian byte ordering
            self.assertEqual(
                computed_crc, expected_crc,
                f"Packet {idx} CRC failure! Computed 0x{computed_crc:04X}, expected 0x{expected_crc:04X}. "
                f"Check endianness/encoding alignment."
            )

if __name__ == "__main__":
    unittest.main()