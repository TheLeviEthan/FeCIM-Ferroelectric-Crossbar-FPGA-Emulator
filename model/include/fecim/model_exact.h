// fecim/model_exact.h
//
// Bit-exact model of the RTL datapath: the oracle for co-simulation and the
// engine behind the Python `sim` backend. See model-verification-design §2
// for the rules (integer only, same clamps, same rounding, LFSR advances on
// accumulate only).
//
// STATUS: complete for semester 1.
//   * Transaction level: write_* + compute(), with all six non-idealities --
//     quantization, D2D and stuck-at at write time (fecim/write_path.h);
//     read noise, IR drop and ADC at compute time (fecim/read_path.h).
//   * Cycle level: start_compute() / step() and the probes, a clock-edge twin
//     of control_fsm + act_buffer + mac_array + ADC + result buffer + argmax.
//   * Register level: set_reg() / get_reg(), mirroring config_regs.
//
// Two levels, one model (design §10, open question 1): step() is the
// cycle-accurate core the co-sim compares against per cycle; compute() is
// the fast path for sweeps. They are written independently, and the tests
// require compute() to leave the model in the same observable state as
// "start_compute(), step() until state() == CTRL_IDLE" for every config:
// same results, argmax, STATUS, CYCLE_CNT, LFSRs and lane probes.
//
// What the cycle model mirrors, and where each choice comes from, is listed
// in model-verification-design §5.3 ("Cycle-model conventions"). Blocks still
// skeletons in RTL (act_buffer, read_noise, result_buffer, argmax_unit) follow
// their specs; every assumption made for them is listed there for the owner
// to confirm.

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
    //
    // NOT const and NOT repeatable: every issued row advances every lane's
    // read-noise LFSR (all NUM_LANES lanes, every pass, whether or not
    // NOISE_EN.read is set -- the RTL's LFSR has no enable). With read noise
    // on, two computes on the same data differ unless reseed() runs between
    // them. Changing noise_seed with set_config() does NOT reload the LFSRs;
    // only construction (reset) and reseed() (CTRL[2]) do, as in the RTL.
    std::vector<std::int32_t> compute();

    // Stored (post write-path) weight, for tests and co-sim inspection.
    std::int8_t stored_weight(int row, int col) const;

    // CTRL[2]: reload every lane LFSR with lane_seed(noise_seed, l), using
    // the noise_seed in effect now. Implemented.
    void reseed();

    // ---- cycle level: the co-sim contract --------------------------------
    //
    // After k calls to step(), every probe equals the RTL's value after k
    // rising clock edges, read after the edge has settled. The harness clocks
    // the Verilated DUT once, calls step() once, and compares lane(l) field
    // by field in upstream-first order (w_n, then prod, then acc), so the
    // first mismatch names the lane, the cycle and the pipeline stage:
    // "lane 17 w_n diverged at cycle 412".

    // Observable state of one MAC lane after the most recent step().
    struct LaneProbe {
        // mac_lane's combinational w_n in the cycle after the edge:
        // w_align + (NOISE_EN.read ? noise_q : 0). It is a wire, so it is
        // valid every cycle, not only on issued rows; it carries row r's value
        // in cycle N+2, the cycle prod samples it.
        std::int16_t  w_n;    // W_N_W = 9 bits signed
        std::int32_t  prod;   // product register, PROD_W = 18 bits signed; holds when not v2
        std::int32_t  acc;    // accumulator, ACC_W = 32 bits
        std::uint32_t lfsr;   // read-noise LFSR state (advances on acc_en only)
    };

    // The sequencer's registered outputs -- the A/B seam -- and the
    // activation broadcast register, after the most recent step(). These are
    // what the array sees as inputs in the NEXT cycle: a harness playing
    // control_fsm and act_buffer drives the DUT from ctrl() before each edge.
    struct CtrlProbe {
        int          seq_cnt;     // {pass, row}: weight address, act_addr = seq_cnt[ROW_AW-1:0]
        bool         acc_clear;
        bool         acc_en;      // row issued this cycle
        bool         shift_en;
        bool         drain_we;    // result buffer write, this cycle
        int          drain_addr;  // {pass, drain_cnt}
        bool         run_start;   // 1-cycle pulse: first CLEAR cycle of a COMPUTE
        bool         compute_done;// 1-cycle pulse: the DONE cycle
        std::uint8_t act_bcast;   // act_buffer output register (post-IR)
    };

    // COMPUTE: a one-cycle start pulse sampled by the next step(). Accepted
    // only if the FSM is in CTRL_IDLE on that edge, as in the RTL; otherwise
    // the pulse is lost (the board's parser answers ST_BUSY). Uses the config
    // in effect on each edge, as the RTL reads its registers live.
    void start_compute();

    // Advance exactly one clock edge. Per COMPUTE: 1 edge IDLE -> CLEAR, then
    // per pass CLEAR (1) + ACC (rows + LANE_PIPE_DEPTH) + DRAIN (NUM_LANES)
    // edges, ending in DONE, then 1 edge DONE -> IDLE. busy() drops on the
    // edge into DONE, when the last result is already written. Stepping while
    // idle with no host writes changes nothing, the LFSRs included.
    void step();

    CtrlState     state() const;       // STATUS[3:0]
    bool          busy() const;        // STATUS[4]
    int           seq_cnt() const;     // = weight address = {pass, row}
    LaneProbe     lane(int l) const;   // 0 <= l < NUM_LANES
    CtrlProbe     ctrl() const;
    std::uint32_t cycle_count() const; // CYCLE_CNT of the last COMPUTE:
                                       // passes x (1 + rows + LANE_PIPE_DEPTH + NUM_LANES)

    // ---- result buffer ---------------------------------------------------

    // READ_RESULT: active_cols words in column order, post-ADC, exactly as
    // the result buffer holds them. Persists until the next COMPUTE
    // overwrites it; CTRL[1] clears results_valid, not the data.
    std::vector<std::int32_t> results() const;

    // READ_ARGMAX (protocol §4.6). Over columns < active_cols only, strict
    // '>' so ties keep the LOWEST index (numpy.argmax). second_val is the
    // largest value left after removing ONE instance of max_val, so a tie
    // gives second_val == max_val. With active_cols == 1 it is INT32_MIN
    // (protocol: undefined, the host must not use it).
    struct Argmax {
        std::uint16_t max_idx;
        std::int32_t  max_val;
        std::int32_t  second_val;
    };
    Argmax argmax() const;

    // ---- register level: SET_CONFIG / GET_CONFIG -------------------------
    //
    // What the virtual UART and the `sim` backend drive, so the model sees
    // the same register traffic as the board. Mirrors rtl/control/config_regs.sv:
    //
    //   * Access policy (protocol §6.0): an address not in the map (0x0D-0xFF,
    //     reserved 0x10-0x1F included), or a write to STATUS or CYCLE_CNT,
    //     returns ST_ADDR_RANGE and changes nothing. CTRL reads as 0.
    //   * Values are stored as the RTL stores them: TILE_CFG raw (0 or more
    //     than the tile means the full tile), sigmas [7:0], NOISE_EN [5:0],
    //     STUCK_RATE [16:0]. Out-of-range QUANT_LEVELS (not 2-255) and
    //     ADC_BITS (not 4-ACC_USED_W) writes are ignored and return ST_OK:
    //     config_regs drops them silently and has no status path.
    //   * CTRL fires its strobes at once: [0] soft reset (sequencer to IDLE,
    //     CYCLE_CNT to 0; registers, weights, LFSRs and results kept), [1]
    //     clear results_valid, [2] reseed the LFSRs from NOISE_SEED.
    //   * STATUS = {16'0, result_seq, 1'b0, results_valid, last_err = 0,
    //     busy, state}. The model has no UART, so last_err is always 0; mask
    //     STATUS[5] in hardware comparisons.
    //
    // A write takes effect before the next step(), i.e. the bus write and the
    // one-cycle strobe are folded into the gap between edges. set_config()
    // stays as the typed shortcut and validates instead of storing raw.
    Status set_reg(std::uint8_t addr, std::uint32_t value);
    Status get_reg(std::uint8_t addr, std::uint32_t& value) const;

private:
    // ---- datapath storage --------------------------------------------------
    Config cfg_;
    std::uint16_t baud_inc_ = BAUD_INC_115200;
    std::vector<std::int8_t>  weights_;   // TILE_ROWS * TILE_COLS, [col][row]
    std::vector<std::uint8_t> act_;       // TILE_ROWS
    std::vector<std::uint8_t> atten_;     // TILE_ROWS, Q0.8, default 0xFF
    std::vector<std::uint32_t> lfsr_;     // NUM_LANES read-noise LFSRs; persist across computes

    // ---- cycle-level registers (step()) ------------------------------------
    // control_fsm, one for one with rtl/control/control_fsm.sv.
    struct Seq {
        CtrlState     st          = CTRL_IDLE;
        int           pass        = 0;      // pass_idx_q
        int           row_cnt     = 0;      // row_cnt_q, 0 .. rows
        int           pipe_cnt    = 0;      // pipe_cnt_q, hold counter
        int           drain_cnt   = 0;      // drain_cnt_q
        std::uint32_t run_cyc     = 0;      // run_cycle_cnt_q
        int           seq_cnt     = 0;
        bool          acc_clear   = false;
        bool          acc_en      = false;
        bool          shift_en    = false;
        bool          drain_we    = false;
        int           drain_addr  = 0;
        bool          busy        = false;  // compute_busy
        bool          done        = false;  // compute_done pulse
        bool          run_start   = false;  // run_start pulse
        std::uint32_t cycle_cnt   = 0;
    };
    // One mac_lane's datapath registers (the LFSR lives in lfsr_).
    struct LaneRegs {
        std::int8_t  w_rd    = 0;   // M9K registered read, N+1
        std::int8_t  w_align = 0;   // alignment register, N+2
        std::int8_t  noise_q = 0;   // registered noise sample, N+2
        std::int32_t prod    = 0;   // N+2 -> N+3, no reset, holds unless v2
        std::int32_t acc     = 0;
    };

    Seq                   seq_;
    bool                  start_pending_ = false;
    std::vector<LaneRegs> lanes_;
    // acc_en delayed 1..LANE_PIPE_DEPTH cycles: v1, v2, v3. Every lane has
    // its own copy in RTL; they are identical, so the model keeps one.
    bool                  valid_[LANE_PIPE_DEPTH] = {};
    // act_buffer: M9K registered read, then the broadcast register (IR applied
    // between them, mac-array-spec §8).
    std::uint8_t          act_rd_     = 0;
    int                   act_rd_row_ = 0;
    std::uint8_t          act_bcast_  = 0;

    // ---- result buffer + argmax --------------------------------------------
    std::vector<std::int32_t> rbuf_;      // TILE_COLS, post-ADC
    bool          results_valid_ = false;
    std::uint8_t  result_seq_    = 0;
    std::int32_t  max_val_       = INT32_MIN_;
    std::int32_t  second_val_    = INT32_MIN_;
    std::uint16_t max_idx_       = 0;
    static constexpr std::int32_t INT32_MIN_ = -2147483647 - 1;

    int  rows_eff() const;     // control_fsm's num_rows
    int  passes_eff() const;   // control_fsm's total_passes
    int  cols_eff() const;     // columns read back and seen by argmax
    std::int16_t w_n_of(const LaneRegs& r) const;
    void argmax_reset();
    void argmax_update(int addr, std::int32_t v);
    void soft_reset();
};

} // namespace fecim
