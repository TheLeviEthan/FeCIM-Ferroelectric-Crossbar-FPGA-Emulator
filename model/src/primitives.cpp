#include "fecim/primitives.h"
#include "fecim/params.h"

namespace fecim {

std::uint16_t cell_hash16(std::uint16_t addr, std::uint16_t seed) {
    // Multiply in uint32_t and mask. A plain uint16_t * uint16_t promotes to
    // int, and 0xFFFF * 0x9E37 overflows a signed int (undefined behaviour).
    std::uint32_t h = static_cast<std::uint32_t>(addr ^ seed);
    h = (h * HASH_C0) & 0xFFFFu;
    h = h ^ (h >> 7);
    h = (h * HASH_C1) & 0xFFFFu;
    h = h ^ (h >> 9);
    return static_cast<std::uint16_t>(h);
}

std::uint8_t crc8_byte(std::uint8_t crc_in, std::uint8_t data) {
    unsigned c = static_cast<unsigned>(crc_in ^ data);
    for (int i = 0; i < 8; ++i)
        c = (c & 0x80u) ? (((c << 1) ^ CRC_POLY) & 0xFFu) : ((c << 1) & 0xFFu);
    return static_cast<std::uint8_t>(c);
}

std::uint8_t crc8(const std::uint8_t* data, std::size_t len) {
    std::uint8_t c = CRC_INIT;
    for (std::size_t i = 0; i < len; ++i)
        c = crc8_byte(c, data[i]);
    return c;
}

std::uint32_t lfsr_step(std::uint32_t s) {
    return (s & 1u) ? ((s >> 1) ^ LFSR_TAPS) : (s >> 1);
}

std::uint32_t lane_seed(std::uint32_t noise_seed, int lane_id) {
    const std::uint32_t salt = LANE_SALT_C * static_cast<std::uint32_t>(lane_id);
    return (noise_seed ^ salt) | 1u;
}

} // namespace fecim
