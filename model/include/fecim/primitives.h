// fecim/primitives.h
//
// Bit-exact C++ versions of the functions defined in rtl/fecim_pkg.sv.
// These are fully specified by the package, so they are implemented (not
// stubbed) and unit-tested now.

#pragma once

#include <cstddef>
#include <cstdint>

namespace fecim {

// fecim_pkg::cell_hash16 -- two 16-bit multiply/xorshift rounds, wrapping.
std::uint16_t cell_hash16(std::uint16_t addr, std::uint16_t seed);

// fecim_pkg::crc8_byte -- CRC-8/ATM: poly 0x07, init 0x00, no reflection.
std::uint8_t crc8_byte(std::uint8_t crc_in, std::uint8_t data);

// CRC-8 over a buffer, starting from CRC_INIT.
std::uint8_t crc8(const std::uint8_t* data, std::size_t len);

// One step of the 32-bit Galois read-noise LFSR (x^32 + x^22 + x^2 + x + 1).
// Advance only on acc_en -- never free-running.
std::uint32_t lfsr_step(std::uint32_t state);

// Per-lane effective LFSR seed: (seed ^ LANE_SALT_C * lane) | 1, never zero.
std::uint32_t lane_seed(std::uint32_t noise_seed, int lane_id);

} // namespace fecim
