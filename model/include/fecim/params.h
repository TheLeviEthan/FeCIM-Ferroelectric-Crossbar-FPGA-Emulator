// fecim/params.h
//
// C++ mirror of rtl/fecim_pkg.sv. Every value here MUST match the package;
// model_exact is bit-exact against the RTL only if the constants agree.
//
// TODO(lane C): generate this file from docs/protocol.yaml together with the
// SV package (rtl-conventions §3.1) so the two cannot drift. Until then, any
// change to fecim_pkg.sv needs the matching change here in the same PR.

#pragma once

#include <cstdint>

namespace fecim {

// ---- board ----------------------------------------------------------------
inline constexpr int CLK_HZ          = 50'000'000;
inline constexpr int DEV_M9K         = 182;

// ---- geometry -------------------------------------------------------------
inline constexpr int TILE_ROWS       = 128;
inline constexpr int TILE_COLS       = 128;
inline constexpr int NUM_LANES       = 64;
inline constexpr int PASSES          = TILE_COLS / NUM_LANES;

// ---- datapath widths ------------------------------------------------------
inline constexpr int WEIGHT_W        = 8;     // signed, +/-127
inline constexpr int ACT_W           = 8;     // unsigned, 0..255
inline constexpr int NOISE_W         = 8;
inline constexpr int W_N_W           = 9;
inline constexpr int SIGMA_W         = 9;     // Q1.8
inline constexpr int PROD_W          = 18;
inline constexpr int ACC_W           = 32;

inline constexpr int NOISE_CLAMP      = 127;
inline constexpr int QUANT_LEVELS_MAX = 255;

inline constexpr std::int64_t ACC_MAX_MAG = 254LL * 255LL * TILE_ROWS;

constexpr int clog2(std::int64_t v) {
    int n = 0;
    std::int64_t p = 1;
    while (p < v) { p <<= 1; ++n; }
    return n;
}
inline constexpr int ACC_USED_W      = clog2(ACC_MAX_MAG) + 1;   // 24 at 128 rows

inline constexpr int LANE_PIPE_DEPTH = 3;

// ---- host interface -------------------------------------------------------
inline constexpr std::uint8_t SYNC_H2F = 0xA5;
inline constexpr std::uint8_t SYNC_F2H = 0x5A;
inline constexpr std::uint8_t CRC_POLY = 0x07;
inline constexpr std::uint8_t CRC_INIT = 0x00;

// ---- cell hash / LFSR -----------------------------------------------------
inline constexpr std::uint16_t HASH_C0    = 0x9E37;
inline constexpr std::uint16_t HASH_C1    = 0x85EB;
inline constexpr std::uint16_t SEED_D2D   = 0xD2D0;
inline constexpr std::uint16_t SEED_STUCK = 0x57C0;

inline constexpr std::uint32_t LFSR_TAPS   = 0x8020'0003u;
inline constexpr std::uint32_t LANE_SALT_C = 0x9E37'79B1u;

// ---- non-ideality enables (REG_NOISE_EN bit order, quant = bit 0) ---------
enum NoiseBit : std::uint8_t {
    NOISE_QUANT = 1u << 0,
    NOISE_D2D   = 1u << 1,
    NOISE_READ  = 1u << 2,
    NOISE_STUCK = 1u << 3,
    NOISE_IR    = 1u << 4,
    NOISE_ADC   = 1u << 5,
};
inline constexpr std::uint8_t NOISE_NONE = 0;

// ---- same checks as rtl/fecim_pkg_checks.sv -------------------------------
constexpr bool is_pow2(int v) { return v > 0 && (v & (v - 1)) == 0; }
static_assert(is_pow2(TILE_ROWS), "TILE_ROWS must be a power of two");
static_assert(is_pow2(NUM_LANES), "NUM_LANES must be a power of two");
static_assert(W_N_W <= 9,  "w_n > 9 bits: MAC multiply no longer packs 2 per block");
static_assert(SIGMA_W <= 9, "sigma > 9 bits: noise multiply no longer packs 2 per block");
static_assert(ACC_MAX_MAG < (std::int64_t{1} << (ACC_W - 1)), "accumulator can overflow");
static_assert(NUM_LANES <= DEV_M9K - 2, "not enough M9K");
static_assert(ACC_USED_W <= ACC_W, "ACC_USED_W exceeds ACC_W");
static_assert(QUANT_LEVELS_MAX <= 255, "QUANT_LEVELS > 255 breaks the 9x9 multiply");

} // namespace fecim
