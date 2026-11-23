// model/cosim/run_l0_array.cpp -- L0 co-simulation gate, full array (task C4).
//
// Steps a Verilated mac_array (all NUM_LANES mac_lanes and the drain chain,
// rtl/array/mac_array.sv) and ModelExact::step() in lockstep on the clean
// datapath (NOISE_EN = 0) and requires them to agree EXACTLY:
//
//   1. Every clock cycle of every COMPUTE, every lane, upstream first:
//      w_n (N+2 wire), prod (N+2 register), acc (N+3 register). The first
//      mismatch stops the run and names the matrix, cycle, lane, signal and
//      FSM state -- the first divergence explains everything after it.
//   2. Every COMPUTE, three ways: the results the DUT drains out of its chain
//      (lane 0, captured on each drain_we cycle at drain_addr) ==
//      ModelExact::results() == model_ideal fed the weights as WRITTEN.
//
// The harness plays control_fsm and act_buffer: it drives the DUT's seq_cnt,
// acc_clear, acc_en and shift_en from ModelExact::ctrl() before each edge,
// and drives act_bcast itself from the rows it issued (see kActDelay). So
// this proves the array, the lane decode, both passes and the drain chain;
// it does NOT test control_fsm or act_buffer RTL (that is C5).
//
// Lane internals are read through Verilator's --public-flat-rw scope table
// ("TOP.mac_array.g_lane[<l>].u_lane" -> "w_n" / "prod" / "acc"), so the
// RTL is untouched and a renamed signal fails at start-up with its name.
//
// Usage:  run_l0_array [seed] [n_matrices] [rows] [cols]
//         defaults: seed 1, 50 matrices, 128 x 128
// Exit:   0 on PASS, 1 on the first mismatch.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "Vmac_array.h"
#include "verilated.h"
#include "verilated_syms.h"

#include "fecim/model_exact.h"
#include "fecim/model_ideal.h"
#include "fecim/params.h"

using namespace fecim;

namespace {

// act_buffer latency, activation-buffer-spec §4: the row's address is issued
// in cycle N (seq_cnt -> act M9K), the M9K output is registered at N+1 and
// the broadcast register drives act_bcast in N+2. The DUT sees row r's
// activation on the edge TWO edges after the one that sampled r's address.
constexpr int kActDelay = 2;
// Driven on act_bcast in every cycle that carries no issued row: the lanes
// must ignore it (their prod register is gated by v2).
constexpr std::uint8_t kGarbageAct = 0xA5;

const char* state_name(CtrlState s) {
    switch (s) {
    case CTRL_IDLE:  return "IDLE";
    case CTRL_CLEAR: return "CLEAR";
    case CTRL_ACC:   return "ACC";
    case CTRL_DRAIN: return "DRAIN";
    case CTRL_DONE:  return "DONE";
    }
    return "?";
}

[[noreturn]] void fail(const std::string& msg) {
    std::printf("FAIL: %s\n", msg.c_str());
    std::exit(1);
}

std::int32_t sext(std::uint32_t v, int bits) {
    const std::uint32_t m = 1u << (bits - 1);
    v &= (bits == 32) ? 0xFFFFFFFFu : ((1u << bits) - 1u);
    return static_cast<std::int32_t>((v ^ m) - m);
}

// Pointers into one lane's Verilated signals, found once by name.
struct LaneSignals {
    const std::uint16_t* w_n;    // SData, 9 bits signed
    const std::uint32_t* prod;   // IData, 18 bits signed
    const std::uint32_t* acc;    // IData, 32 bits
};

class Harness {
public:
    Harness() : ctx_(new VerilatedContext), dut_(new Vmac_array(ctx_.get())) {
        for (int l = 0; l < NUM_LANES; ++l) lanes_.push_back(find_lane(l));
        idle_inputs();
        dut_->rst_n = 0;
        for (int i = 0; i < 4; ++i) tick();
        dut_->rst_n = 1;
        tick();
    }
    ~Harness() { dut_->final(); }

    std::uint64_t cycles() const { return cycle_; }
    std::uint64_t checks() const { return checks_; }

    // ---- program: the same tile into the DUT bus and the model -------------
    // Every cell of the PHYSICAL tile is written, whatever the active size, so
    // the cells outside the active region hold junk that must not leak in.
    void program(const std::vector<std::int8_t>& w_cm, const std::vector<std::uint8_t>& act,
                 const std::string& label) {
        label_ = label;
        written_ = w_cm;
        act_ = act;
        for (int c = 0; c < TILE_COLS; ++c)
            for (int r = 0; r < TILE_ROWS; ++r) {
                const std::int8_t w = w_cm[static_cast<std::size_t>(c * TILE_ROWS + r)];
                model_.write_weight(r, c, w);
                // mac_array's write port: lane select + lane-local {pass, row}.
                dut_->lane_we    = 1;
                dut_->lane_sel   = static_cast<std::uint8_t>(c % NUM_LANES);
                dut_->lane_waddr = static_cast<std::uint8_t>(((c / NUM_LANES) << ROW_AW) | r);
                dut_->lane_wdata = static_cast<std::uint8_t>(w);
                tick();
            }
        dut_->lane_we = 0;
        for (int r = 0; r < TILE_ROWS; ++r) model_.write_activation(r, act[static_cast<std::size_t>(r)]);
        // Settle: both sides' continuously-read registers (w_rd, w_align) catch
        // up with the new memory contents; then they must agree before compute.
        for (int i = 0; i < kActDelay + 1; ++i) lockstep();
        compare_lanes("settle");
    }

    // ---- one COMPUTE, both sides, compared every cycle ---------------------
    std::vector<std::int32_t> compute(int rows, int cols) {
        Config cfg = model_.config();
        cfg.active_rows = rows;
        cfg.active_cols = cols;
        model_.set_config(cfg);

        std::vector<std::int32_t> drained(static_cast<std::size_t>(TILE_COLS), 0);
        model_.start_compute();
        int guard = 0;
        do {
            const auto k = model_.ctrl();
            if (k.drain_we)   // the result buffer would write chain_out on this edge
                drained[static_cast<std::size_t>(k.drain_addr)] = static_cast<std::int32_t>(dut_->chain_out);
            lockstep();
            compare_lanes(state_name(model_.state()));
        } while (model_.state() != CTRL_IDLE && ++guard < 100000);
        if (model_.state() != CTRL_IDLE) fail(where() + ": model never returned to IDLE");

        const auto exact = model_.results();
        std::vector<std::int8_t> w_rm(static_cast<std::size_t>(rows * cols));
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c)
                w_rm[static_cast<std::size_t>(r * cols + c)] = written_[static_cast<std::size_t>(c * TILE_ROWS + r)];
        const std::vector<std::uint8_t> a(act_.begin(), act_.begin() + rows);
        const auto ideal = ideal_mvm(w_rm, a, rows, cols);
        for (int c = 0; c < cols; ++c) {
            const auto ci = static_cast<std::size_t>(c);
            ++checks_;
            if (drained[ci] != exact[ci] || std::int64_t{exact[ci]} != ideal[ci])
                fail(where() + ": result column " + std::to_string(c) + ": rtl " +
                     std::to_string(drained[ci]) + ", model_exact " + std::to_string(exact[ci]) +
                     ", model_ideal " + std::to_string(ideal[ci]));
        }
        ++computes_;
        return exact;
    }

    int computes() const { return computes_; }

private:
    LaneSignals find_lane(int l) {
        const std::string scope = "TOP.mac_array.g_lane[" + std::to_string(l) + "].u_lane";
        const VerilatedScope* s = ctx_->scopeFind(scope.c_str());
        if (!s) fail("scope '" + scope + "' not found: was the model built with --public-flat-rw, "
                     "and is the generate block still named g_lane / u_lane in mac_array.sv?");
        auto var = [&](const char* name) -> void* {
            VerilatedVar* v = s->varFind(name);
            if (!v) fail("signal '" + std::string(name) + "' not found in " + scope);
            return v->datap();
        };
        return {static_cast<const std::uint16_t*>(var("w_n")),
                static_cast<const std::uint32_t*>(var("prod")),
                static_cast<const std::uint32_t*>(var("acc"))};
    }

    void idle_inputs() {
        dut_->clk = 0;
        dut_->lane_we = 0; dut_->lane_sel = 0; dut_->lane_waddr = 0; dut_->lane_wdata = 0;
        dut_->seq_cnt = 0; dut_->act_bcast = kGarbageAct;
        dut_->noise_en = NOISE_NONE;                   // L0: clean datapath
        dut_->read_sigma = 0; dut_->noise_seed = 0; dut_->reseed = 0;
        dut_->acc_clear = 0; dut_->acc_en = 0; dut_->shift_en = 0;
    }

    // One rising edge on the DUT. act_bcast carries the activation of the row
    // whose address was sampled kActDelay edges before this one.
    void tick() {
        for (int i = 0; i < kActDelay; ++i) hist_[i] = hist_[i + 1];
        hist_[kActDelay] = dut_->acc_en ? (dut_->seq_cnt & (TILE_ROWS - 1)) : -1;
        dut_->act_bcast = hist_[0] >= 0 ? act_[static_cast<std::size_t>(hist_[0])] : kGarbageAct;
        dut_->clk = 1; dut_->eval(); ctx_->timeInc(1);
        dut_->clk = 0; dut_->eval(); ctx_->timeInc(1);
        ++cycle_;
    }

    // Drive the DUT from the model's sequencer outputs, then clock both once.
    void lockstep() {
        const auto k = model_.ctrl();
        dut_->seq_cnt   = static_cast<std::uint8_t>(k.seq_cnt);
        dut_->acc_clear = k.acc_clear;
        dut_->acc_en    = k.acc_en;
        dut_->shift_en  = k.shift_en;
        tick();
        model_.step();
    }

    void compare_lanes(const char* phase) {
        for (int l = 0; l < NUM_LANES; ++l) {
            const auto& s = lanes_[static_cast<std::size_t>(l)];
            const auto  p = model_.lane(l);
            check(l, "w_n",  sext(*s.w_n, W_N_W),   p.w_n,  phase);
            check(l, "prod", sext(*s.prod, PROD_W), p.prod, phase);
            check(l, "acc",  static_cast<std::int32_t>(*s.acc), p.acc, phase);
        }
        check(0, "chain_out", static_cast<std::int32_t>(dut_->chain_out), model_.lane(0).acc, phase);
    }

    void check(int lane, const char* sig, std::int32_t rtl, std::int32_t model, const char* phase) {
        ++checks_;
        if (rtl != model)
            fail(where() + ", cycle " + std::to_string(cycle_) + " (" + phase + ", seq_cnt " +
                 std::to_string(model_.seq_cnt()) + "): lane " + std::to_string(lane) + " " + sig +
                 ": rtl " + std::to_string(rtl) + ", model " + std::to_string(model));
    }

    std::string where() const { return "matrix '" + label_ + "'"; }

    std::unique_ptr<VerilatedContext> ctx_;
    std::unique_ptr<Vmac_array>       dut_;
    ModelExact                        model_;
    std::vector<LaneSignals>          lanes_;
    std::vector<std::int8_t>          written_ = std::vector<std::int8_t>(
        static_cast<std::size_t>(TILE_ROWS) * TILE_COLS, 0);
    std::vector<std::uint8_t>         act_ = std::vector<std::uint8_t>(TILE_ROWS, 0);
    int                               hist_[kActDelay + 1] = {-1, -1, -1};
    std::uint64_t                     cycle_ = 0;
    std::uint64_t                     checks_ = 0;
    int                               computes_ = 0;
    std::string                       label_ = "reset";
};
static_assert(kActDelay == 2, "hist_ initialiser assumes kActDelay == 2");

} // namespace

int main(int argc, char** argv) {
    const unsigned seed = argc > 1 ? static_cast<unsigned>(std::strtoul(argv[1], nullptr, 0)) : 1u;
    const int n         = argc > 2 ? std::atoi(argv[2]) : 50;
    const int rows      = argc > 3 ? std::atoi(argv[3]) : TILE_ROWS;
    const int cols      = argc > 4 ? std::atoi(argv[4]) : TILE_COLS;
    if (rows < 1 || rows > TILE_ROWS || cols < 1 || cols > TILE_COLS || n < 0) {
        std::printf("usage: run_l0_array [seed] [n_matrices] [rows 1-%d] [cols 1-%d]\n",
                    TILE_ROWS, TILE_COLS);
        return 2;
    }
    const auto t0 = std::chrono::steady_clock::now();
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> w8(-128, 127), a8(0, 255);
    const std::size_t cells = static_cast<std::size_t>(TILE_ROWS) * TILE_COLS;

    Harness h;
    std::vector<std::int8_t> w(cells);
    std::vector<std::uint8_t> a(TILE_ROWS);

    // 1. Accumulator rails: every column at the largest |acc| the tile can
    //    produce, alternating sign by column so the drain order shows.
    for (std::size_t i = 0; i < cells; ++i)
        w[i] = ((i / TILE_ROWS) % 2) ? std::int8_t{-128} : std::int8_t{127};
    std::fill(a.begin(), a.end(), std::uint8_t{255});
    h.program(w, a, "rails");
    h.compute(rows, cols);

    // 2. A distinct value per column (w = col - 64, a = 1): any lane-select,
    //    pass or drain-order slip moves a recognisable number.
    for (std::size_t i = 0; i < cells; ++i)
        w[i] = static_cast<std::int8_t>(static_cast<int>(i / TILE_ROWS) - 64);
    std::fill(a.begin(), a.end(), std::uint8_t{1});
    h.program(w, a, "column ramp");
    h.compute(rows, cols);

    // 3. Random tiles. Every fourth keeps the weights and changes only the
    //    activations (memory retention, no rewrite).
    for (int m = 0; m < n; ++m) {
        if (m % 4 != 3)
            for (auto& x : w) x = static_cast<std::int8_t>(w8(rng));
        for (auto& x : a) x = static_cast<std::uint8_t>(a8(rng));
        h.program(w, a, "random " + std::to_string(m));
        h.compute(rows, cols);
    }

    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("PASS: L0 full-array co-sim, seed %u, %dx%d: %d computes (%d random), "
                "%llu cycles, %llu checks, rtl == model_exact == model_ideal, %.1f s\n",
                seed, rows, cols, h.computes(), n,
                static_cast<unsigned long long>(h.cycles()),
                static_cast<unsigned long long>(h.checks()), secs);
    return 0;
}
