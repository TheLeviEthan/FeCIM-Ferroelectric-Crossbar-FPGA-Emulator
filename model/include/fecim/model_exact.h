// fecim/model_exact.h
//
// Bit-exact model of the RTL datapath: the oracle for co-simulation and the
// engine behind the Python `sim` backend. See model-verification-design §2
// for the rules (integer only, same clamps, same rounding, LFSR advances on
// accumulate only).
//
// STATUS (week 2):
//   * Implemented: the transaction-level path (write_* + compute()) for the
//     clean datapath and conductance quantization (NOISE_QUANT, write-path
//     stage 1, arithmetic in fecim/write_path.h). compute() throws if any
//     other non-ideality is enabled, rather than silently returning clean
//     results that would make an L1/L2 comparison pass for the wrong reason.
//   * Interface stub: the cycle-level co-sim interface, the result buffer
//     and argmax, and the register-level interface below. Signatures and
//     types are final so the Verilator harness (model-verification-design
//     §5) and Lane D's `sim` backend can be written against them now; the
//     bodies throw std::logic_error until week 3.
//
// Two levels, one model (design §10, open question 1): step() is the
// cycle-accurate core the co-sim compares against per cycle; compute() is
// the fast path for sweeps. Once step() exists, compute() must equal
// "start_compute(), step() until !busy(), results()" for every config.

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

    // ======================================================================
    // INTERFACE STUB (week 2). Everything below throws std::logic_error
    // until it is implemented in week 3. Integer types only, each the
    // narrowest standard type that holds the RTL signal, signed as in RTL.
    // ======================================================================

    // ---- cycle level: the co-sim contract --------------------------------
    //
    // The harness clocks the Verilated DUT once, calls step() once, and
    // compares lane(l) field by field in upstream-first order (w_n, then
    // prod, then acc), so the first mismatch names the lane, the cycle and
    // the pipeline stage: "lane 17 w_n diverged at cycle 412".

    // Observable state of one MAC lane after the most recent step().
    struct LaneProbe {
        std::int16_t  w_n;    // weight + read noise, W_N_W = 9 bits signed
        std::int32_t  prod;   // product register, PROD_W = 18 bits signed
        std::int32_t  acc;    // accumulator, ACC_W = 32 bits
        std::uint32_t lfsr;   // read-noise LFSR state (advances on acc_en only)
    };

    // COMPUTE accepted: the next step() leaves CTRL_IDLE for CTRL_CLEAR.
    // Uses the config in effect now, as the RTL samples its registers.
    void start_compute();

    // Advance exactly one clock edge: CLEAR (1) -> ACC (rows issued +
    // LANE_PIPE_DEPTH) -> DRAIN (NUM_LANES), per pass -> DONE -> IDLE
    // (control-fsm-spec §4). Stepping while idle is legal and changes
    // nothing, the LFSRs included.
    void step();

    CtrlState     state() const;       // STATUS[3:0]
    bool          busy() const;        // STATUS[4]
    int           seq_cnt() const;     // = weight address = {pass, row}
    LaneProbe     lane(int l) const;   // 0 <= l < NUM_LANES
    std::uint32_t cycle_count() const; // CYCLE_CNT of the last COMPUTE

    // ---- result buffer ---------------------------------------------------

    // READ_RESULT: active_cols words in column order, post-ADC, exactly as
    // the result buffer holds them after DONE. Persists until next COMPUTE.
    std::vector<std::int32_t> results() const;

    // READ_ARGMAX (protocol §4.6). Over columns < active_cols only, strict
    // '>' so ties keep the LOWEST index (numpy.argmax). second_val is
    // undefined when active_cols == 1.
    struct Argmax {
        std::uint16_t max_idx;
        std::int32_t  max_val;
        std::int32_t  second_val;
    };
    Argmax argmax() const;

    // ---- register level: SET_CONFIG / GET_CONFIG -------------------------
    //
    // What the virtual UART and the `sim` backend drive, so the model sees
    // the same register traffic as the board. Access policy (protocol
    // §6.0): an unimplemented or reserved address, or a write to STATUS or
    // CYCLE_CNT, returns ST_ADDR_RANGE and changes nothing. A write to
    // REG_CTRL fires its self-clearing strobes ([0] soft reset, [1] clear
    // results, [2] reseed LFSRs). set_config() stays as the typed shortcut.
    Status set_reg(std::uint8_t addr, std::uint32_t value);
    Status get_reg(std::uint8_t addr, std::uint32_t& value) const;

    // CTRL[2]: reload every lane LFSR with lane_seed(noise_seed, l).
    void reseed();

private:
    Config cfg_;
    std::vector<std::int8_t>  weights_;   // TILE_ROWS * TILE_COLS, [col][row]
    std::vector<std::uint8_t> act_;       // TILE_ROWS
    std::vector<std::uint8_t> atten_;     // TILE_ROWS, Q0.8, default 0xFF
};

} // namespace fecim
