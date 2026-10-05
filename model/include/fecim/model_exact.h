// fecim/model_exact.h
//
// Bit-exact model of the RTL datapath: the oracle for co-simulation and the
// engine behind the Python `sim` backend. See model-verification-design §2
// for the rules (integer only, same clamps, same rounding, LFSR advances on
// accumulate only).
//
// SKELETON (week 1): the clean path is implemented. Every non-ideality is a
// TODO, and compute() throws if one is enabled, rather than silently
// returning clean results that would make an L1/L2 comparison pass
// for the wrong reason.

#pragma once

#include <cstdint>
#include <vector>

#include "fecim/params.h"

namespace fecim {

// Mirrors the config register file (control-fsm-spec §6, cfg_addr_e).
struct Config {
    int           active_rows  = TILE_ROWS;
    int           active_cols  = TILE_COLS;
    int           quant_levels = QUANT_LEVELS_MAX;
    std::uint16_t quant_mult   = 257;   // round(255*256/(N-1)); reset pairs with N = 255
    std::uint8_t  d2d_sigma    = 0;     // scale /256 (>>> 8). Not Q1.8
    std::uint8_t  read_sigma   = 0;     // scale /512 (>>> 9). Not Q1.8
    std::uint32_t noise_seed   = 0;     // 0 is valid, no remap
    std::uint8_t  noise_en     = NOISE_NONE;
    int           adc_bits     = ACC_USED_W;  // >= ACC_USED_W is a no-op
    std::uint32_t stuck_rate   = 0;
};

class ModelExact {
public:
    ModelExact();

    void set_config(const Config& cfg);
    const Config& config() const { return cfg_; }

    // Host-visible writes, addressed as the bulk stream addresses them.
    // Weights are column-major: bulk_addr = col * TILE_ROWS + row.
    void write_weight(int row, int col, std::int8_t w);
    void write_activation(int row, std::uint8_t a);
    void write_atten(int row, std::uint8_t coeff);   // Q0.8, IR drop

    // One COMPUTE: returns active_cols results as the result buffer holds them.
    std::vector<std::int32_t> compute();

    // Stored (post write-path) weight, for tests and co-sim inspection.
    std::int8_t stored_weight(int row, int col) const;

private:
    Config cfg_;
    std::vector<std::int8_t>  weights_;   // TILE_ROWS * TILE_COLS, [col][row]
    std::vector<std::uint8_t> act_;       // TILE_ROWS
    std::vector<std::uint8_t> atten_;     // TILE_ROWS, Q0.8, default 0xFF
};

} // namespace fecim
