#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include <cassert>

// Standard CRC16-CCITT (Poly: 0x1021, Init: 0xFFFF)
uint16_t compute_crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (static_cast<uint16_t>(data[i]) << 8);
        for (int b = 0; b < 8; ++b) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc = (crc << 1);
            }
        }
    }
    return crc;
}

#pragma pack(push, 1)
struct PacketHeader {
    uint8_t  header;   // Expected: 0xA5
    uint8_t  cmd;
    uint16_t addr;     // Little-endian
    uint32_t payload;  // Little-endian
    uint16_t crc;      // Little-endian
};
#pragma pack(pop)

std::vector<uint8_t> parse_hex_line(const std::string& line) {
    std::vector<uint8_t> bytes;
    std::istringstream iss(line);
    std::string byte_str;
    while (iss >> byte_str) {
        if (byte_str[0] == '#') break; // Ignore comments
        uint8_t b = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
        bytes.push_back(b);
    }
    return bytes;
}

int main(int argc, char** argv) {
    std::string golden_file = "../golden/packet_vectors.bin.hex";
    if (argc > 1) {
        golden_file = argv[1];
    }

    std::ifstream infile(golden_file);
    if (!infile.is_open()) {
        std::cerr << "FATAL: Could not open golden file: " << golden_file << std::endl;
        return 1;
    }

    std::string line;
    size_t packet_count = 0;

    while (std::getline(infile, line)) {
        // Strip leading whitespace
        size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#') {
            continue; // Skip empty lines and full-line comments
        }

        std::vector<uint8_t> raw_bytes = parse_hex_line(line);
        if (raw_bytes.empty()) continue;

        if (raw_bytes.size() != sizeof(PacketHeader)) {
            std::cerr << "ERROR: Invalid packet length " << raw_bytes.size() 
                      << " (expected " << sizeof(PacketHeader) << ")" << std::endl;
            return 1;
        }

        const auto* pkt = reinterpret_cast<const PacketHeader*>(raw_bytes.data());

        // Check magic header
        if (pkt->header != 0xA5) {
            std::cerr << "ERROR: Invalid header byte 0x" << std::hex << (int)pkt->header << std::endl;
            return 1;
        }

        // Verify CRC
        uint16_t computed_crc = compute_crc16(raw_bytes.data(), 8);
        if (computed_crc != pkt->crc) {
            std::cerr << "FATAL [Packet " << packet_count << "]: Endianness or Encoding Drift Detected!" << std::endl;
            std::cerr << "Computed CRC: 0x" << std::hex << computed_crc 
                      << " | Expected CRC in File: 0x" << pkt->crc << std::endl;
            return 1;
        }

        packet_count++;
    }

    std::cout << "SUCCESS: Verified " << packet_count << " golden packet vectors. Zero encoding/endianness drift detected." << std::endl;
    return 0;
}