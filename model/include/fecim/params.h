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
inline constexpr int SIGMA_W         = 9;     // {0, reg[7:0]} signed-positive operand
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

// ---- protocol and state enums ---------------------------------------------
// Mirrors status_e, cfg_addr_e and ctrl_state_e. Unscoped with an explicit
// width, like NoiseBit, so values pass straight through as wire bytes.
enum Status : std::uint8_t {
    ST_OK          = 0x00,
    ST_CRC_ERR     = 0x01,
    ST_BAD_LEN     = 0x02,
    ST_UNKNOWN_CMD = 0x03,
    ST_BUSY        = 0x04,
    ST_ADDR_RANGE  = 0x05,
};

enum CfgAddr : std::uint8_t {
    REG_CTRL         = 0x00,   // W: [0] soft reset, [1] clear results, [2] reseed
    REG_TILE_CFG     = 0x01,   // [15:0] active rows, [31:16] active cols
    REG_QUANT_LEVELS = 0x02,
    REG_D2D_SIGMA    = 0x03,   // [7:0] used, scale reg/256 (>>> 8). Not Q1.8
    REG_READ_SIGMA   = 0x04,   // [7:0] used, scale reg/512 (>>> 9). Not Q1.8
    REG_NOISE_SEED   = 0x05,
    REG_NOISE_EN     = 0x06,
    REG_ADC_BITS     = 0x07,
    REG_STUCK_RATE   = 0x08,   // [15:0] rate, [16] mode (0 zero, 1 rail)
    REG_BAUD_INC     = 0x09,
    REG_STATUS       = 0x0A,   // read-only
    REG_CYCLE_CNT    = 0x0B,   // read-only
    REG_QUANT_MULT   = 0x0C,
    // 0x10-0x1F reserved (semester 2): ST_ADDR_RANGE until assigned
};

// Encoded explicitly in the package so STATUS[3:0] shows documented values.
enum CtrlState : std::uint8_t {
    CTRL_IDLE  = 0,
    CTRL_CLEAR = 1,
    CTRL_ACC   = 2,
    CTRL_DRAIN = 3,
    CTRL_DONE  = 4,
};

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
