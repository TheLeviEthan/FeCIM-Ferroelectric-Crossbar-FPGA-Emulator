// model/cosim/run_l0.cpp -- L0 co-simulation gate, single-lane slice.
//
// Steps a Verilated mac_lane (rtl/array/mac_lane.sv) and the C++ reference
// model together on the clean datapath (NOISE_EN = 0) and requires them to
// agree EXACTLY, at two levels:
//
//   1. Every clock cycle: the DUT accumulator must equal the running sum
//      predicted from model_exact's stored weights, retired LANE_PIPE_DEPTH
//      cycles after each row issue. The first divergence names the test, the
//      cycle, the pass and the last row retired.
//   2. Every pass: the final accumulator must equal model_exact::compute() for
//      the lane's column, and model_exact must equal model_ideal (the
//      mathematical oracle, different weight layout). Three-way agreement.
//
// Scope (pre-alpha): one lane, clean datapath. The full L0 gate is the whole
// array against ModelExact::step() once the cycle-level model lands; this
// harness is written so that swap only replaces Mirror below.
//
// Stimulus follows tb/golden/mac_lane/README.md "Running a case": weights by
// global address (col * TILE_ROWS + row), acc_clear for one cycle, rows issued
// with acc_en, act_bcast driven two cycles behind w_addr, garbage (0xA5) on
// act_bcast whenever no row is in that slot.
//
// Usage:  run_l0 [seed] [n_random]     defaults: seed 1, 200 random tests
// Exit:   0 on PASS, 1 on the first mismatch.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "Vmac_lane.h"
#include "verilated.h"

#include "fecim/model_exact.h"
#include "fecim/model_ideal.h"
#include "fecim/params.h"

#ifndef COSIM_LANE_ID
#define COSIM_LANE_ID 5
#endif

using namespace fecim;

namespace {

constexpr int kLane = COSIM_LANE_ID;
static_assert(kLane >= 0 && kLane < NUM_LANES, "COSIM_LANE_ID out of range");
constexpr std::uint8_t kGarbageAct = 0xA5;

int lane_col(int pass) { return pass * NUM_LANES + kLane; }

[[noreturn]] void fail(const std::string& msg) {
    std::printf("FAIL: %s\n", msg.c_str());
    std::exit(1);
}

// Cycle-level expectation for one lane's accumulator, built only from model
// values. Replaced by ModelExact::step()/lane() when the cycle model exists.
//
// Accumulator update on each edge, with the RTL's priority
// (mac-array-spec, mac_lane.sv): clear > accumulate > shift.
class Mirror {
public:
    void issue(std::int64_t product, std::uint64_t retire_cycle) {
        inflight_.push_back({product, retire_cycle});
    }
    // Apply the clock edge numbered `cycle`, with the inputs sampled on it.
    void edge(std::uint64_t cycle, bool clear, bool shift, std::int32_t shift_in) {
        bool retiring = false;
        std::int64_t sum = 0;
        while (!inflight_.empty() && inflight_.front().at <= cycle) {
            if (inflight_.front().at < cycle) throw std::logic_error("mirror: missed a retire");
            sum += inflight_.front().p;
            retiring = true;
            inflight_.pop_front();
        }
        if (clear)         acc_ = 0;
        else if (retiring) acc_ = static_cast<std::int32_t>(acc_ + sum);   // 32-bit wrap, like ACC_W
        else if (shift)    acc_ = shift_in;
    }
    std::int32_t acc() const { return acc_; }
    bool idle() const { return inflight_.empty(); }

private:
    struct Item { std::int64_t p; std::uint64_t at; };
    std::int32_t acc_ = 0;
    std::deque<Item> inflight_;
};

class Harness {
public:
    Harness() : ctx_(new VerilatedContext), dut_(new Vmac_lane(ctx_.get())) {
        idle_inputs();
        dut_->rst_n = 0;
        for (int i = 0; i < 4; ++i) tick();
        dut_->rst_n = 1;
        tick();
    }
    ~Harness() { dut_->final(); }

    std::uint64_t cycles() const { return cycle_; }
    std::uint64_t checks() const { return checks_; }

    // ---- writes: DUT bus and model together -----------------------------
    void write_weight(int row, int col, std::int8_t w) {
        model_.write_weight(row, col, w);
        written_[static_cast<std::size_t>(col * TILE_ROWS + row)] = w;
        dut_->wt_we   = 1;
        dut_->wt_addr = static_cast<std::uint16_t>(col * TILE_ROWS + row);
        dut_->wt_data = static_cast<std::uint8_t>(w);
        tick();
        dut_->wt_we = 0;
    }
    void write_activation(int row, std::uint8_t a) {
        model_.write_activation(row, a);
        act_[static_cast<std::size_t>(row)] = a;
    }

    // ---- one pass: clear, issue rows (with optional idle gaps), drain ----
    void run_pass(const std::string& test, int pass, int rows,
                  std::mt19937& rng, int gap_pct) {
        test_ = test;
        pass_ = pass;
        const int col = lane_col(pass);

        dut_->acc_clear = 1;
        step_and_check(-1);
        dut_->acc_clear = 0;

        std::uniform_int_distribution<int> pct(0, 99), gap_len(1, 4), junk(0, 255);
        for (int r = 0; r < rows; ++r) {
            while (gap_pct > 0 && pct(rng) < gap_pct) {
                for (int g = gap_len(rng); g > 0; --g) {
                    dut_->acc_en = 0;
                    dut_->w_addr = static_cast<std::uint8_t>(junk(rng));  // must be ignored
                    step_and_check(-1);
                }
            }
            dut_->w_addr = static_cast<std::uint8_t>(pass * TILE_ROWS + r);
            dut_->acc_en = 1;
            const std::int64_t p = std::int64_t{model_.stored_weight(r, col)} *
                                   std::int64_t{act_[static_cast<std::size_t>(r)]};
            // Issued on the next edge (cycle_ + 1); in acc after LANE_PIPE_DEPTH more.
            mirror_.issue(p, cycle_ + 1 + LANE_PIPE_DEPTH);
            step_and_check(r);
        }
        dut_->acc_en = 0;
        for (int i = 0; i < LANE_PIPE_DEPTH; ++i) step_and_check(-1);
        if (!mirror_.idle()) fail(where() + ": rows still in flight after LANE_PIPE_DEPTH hold");

        // Pass level: DUT == model_exact == model_ideal.
        Config cfg = model_.config();
        cfg.active_rows = rows;
        model_.set_config(cfg);
        const std::int32_t exact = model_.compute()[static_cast<std::size_t>(col)];

        std::vector<std::int8_t> w_rm(static_cast<std::size_t>(rows));
        // Oracle input is what the harness WROTE, not what model_exact stored,
        // so the ideal check does not inherit a model storage bug.
        for (int r = 0; r < rows; ++r)
            w_rm[static_cast<std::size_t>(r)] = written_[static_cast<std::size_t>(col * TILE_ROWS + r)];
        std::vector<std::uint8_t> a(act_.begin(), act_.begin() + rows);
        const std::int64_t ideal = ideal_mvm(w_rm, a, rows, 1)[0];

        const auto got = static_cast<std::int32_t>(dut_->acc_out);
        ++checks_;
        if (got != exact || std::int64_t{exact} != ideal)
            fail(where() + ": end of pass: rtl " + std::to_string(got) + ", model_exact " +
                 std::to_string(exact) + ", model_ideal " + std::to_string(ideal));
        ++passes_checked_;
    }

    // Drain-chain load: shift_en overwrites the accumulator with shift_in.
    void check_shift(std::int32_t v) {
        test_ = "shift load";
        dut_->shift_en = 1;
        dut_->shift_in = static_cast<std::uint32_t>(v);
        step_and_check(-1);
        dut_->shift_en = 0;
        dut_->shift_in = 0;
        for (int i = 0; i < 3; ++i) step_and_check(-1);   // and it holds
    }

    // Priority, which normal FSM sequencing never exercises: hold shift_en
    // and/or acc_clear high on every edge while one row is in flight, so the
    // edge where it retires collides with them. Expect clear > acc > shift.
    void check_priority(bool clear, bool shift) {
        test_ = std::string("priority") + (clear ? " clear" : "") + (shift ? " shift" : "");
        pass_ = 0;
        const int col = lane_col(0);
        dut_->w_addr = 0;
        dut_->acc_en = 1;
        mirror_.issue(std::int64_t{model_.stored_weight(0, col)} * std::int64_t{act_[0]},
                      cycle_ + 1 + LANE_PIPE_DEPTH);
        step_and_check(0);
        dut_->acc_en = 0;
        dut_->acc_clear = clear ? 1 : 0;
        dut_->shift_en  = shift ? 1 : 0;
        dut_->shift_in  = 0x01234567u;
        for (int i = 0; i < LANE_PIPE_DEPTH + 1; ++i) step_and_check(-1);
        dut_->acc_clear = 0;
        dut_->shift_en  = 0;
        dut_->shift_in  = 0;
        step_and_check(-1);
    }

    int passes_checked() const { return passes_checked_; }

private:
    void idle_inputs() {
        dut_->clk = 0;
        dut_->wt_we = 0; dut_->wt_addr = 0; dut_->wt_data = 0;
        dut_->w_addr = 0; dut_->act_bcast = kGarbageAct;
        dut_->noise_en = 0;                       // NOISE_NONE: clean datapath
        dut_->acc_clear = 0; dut_->acc_en = 0;
        dut_->shift_en = 0; dut_->shift_in = 0;
    }

    // One rising edge. row_issued is the row whose address is sampled on this
    // edge (acc_en high), or -1. act_bcast for this edge is the activation of
    // the row issued TWO edges earlier (activation M9K + broadcast register,
    // activation-buffer-spec §4), or garbage if that slot was idle.
    void tick(int row_issued = -1) {
        hist_[0] = hist_[1];
        hist_[1] = hist_[2];
        hist_[2] = row_issued;
        dut_->act_bcast = hist_[0] >= 0 ? act_[static_cast<std::size_t>(hist_[0])] : kGarbageAct;
        dut_->clk = 1; dut_->eval(); ctx_->timeInc(1);
        dut_->clk = 0; dut_->eval(); ctx_->timeInc(1);
        ++cycle_;
    }

    void step_and_check(int row_issued) {
        const bool clear = dut_->acc_clear != 0, shift = dut_->shift_en != 0;
        const auto shift_in = static_cast<std::int32_t>(dut_->shift_in);
        tick(dut_->acc_en != 0 ? row_issued : -1);
        mirror_.edge(cycle_, clear, shift, shift_in);
        const auto got = static_cast<std::int32_t>(dut_->acc_out);
        ++checks_;
        if (got != mirror_.acc())
            fail(where() + ": cycle " + std::to_string(cycle_) + ": rtl acc " +
                 std::to_string(got) + ", model " + std::to_string(mirror_.acc()));
    }

    std::string where() const {
        return "lane " + std::to_string(kLane) + ", test '" + test_ + "', pass " +
               std::to_string(pass_);
    }

    std::unique_ptr<VerilatedContext> ctx_;
    std::unique_ptr<Vmac_lane>        dut_;
    ModelExact                        model_;
    Mirror                            mirror_;
    std::vector<std::uint8_t>         act_ = std::vector<std::uint8_t>(TILE_ROWS, 0);
    std::vector<std::int8_t>          written_ = std::vector<std::int8_t>(
        static_cast<std::size_t>(TILE_ROWS) * TILE_COLS, 0);
    int                               hist_[3] = {-1, -1, -1};   // rows issued at edges e-2, e-1, e
    std::uint64_t                     cycle_ = 0;
    std::uint64_t                     checks_ = 0;
    int                               passes_checked_ = 0;
    std::string                       test_;
    int                               pass_ = 0;
};

} // namespace

int main(int argc, char** argv) {
    const unsigned seed     = argc > 1 ? static_cast<unsigned>(std::strtoul(argv[1], nullptr, 0)) : 1u;
    const int      n_random = argc > 2 ? std::atoi(argv[2]) : 200;
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> w8(-128, 127), a8(0, 255), rows_d(1, TILE_ROWS);

    Harness h;
    int tests = 0;

    auto fill_lane = [&](int w0, int w1, int a) {
        for (int p = 0; p < PASSES; ++p)
            for (int r = 0; r < TILE_ROWS; ++r)
                h.write_weight(r, lane_col(p), static_cast<std::int8_t>(p == 0 ? w0 : w1));
        for (int r = 0; r < TILE_ROWS; ++r) h.write_activation(r, static_cast<std::uint8_t>(a));
    };
    auto run_all_passes = [&](const std::string& name, int rows, int gap_pct) {
        for (int p = 0; p < PASSES; ++p) h.run_pass(name, p, rows, rng, gap_pct);
        ++tests;
    };

    // 1. Accumulator headroom at both rails (largest |acc| the tile can produce).
    fill_lane(127, -128, 255);
    run_all_passes("extremes", TILE_ROWS, 0);

    // 2. Whole-tile write: every column written, so 63 of every 64 writes on
    //    the bus belong to other lanes and must be ignored by this one.
    for (int c = 0; c < TILE_COLS; ++c)
        for (int r = 0; r < TILE_ROWS; ++r)
            h.write_weight(r, c, static_cast<std::int8_t>(w8(rng)));
    for (int r = 0; r < TILE_ROWS; ++r) h.write_activation(r, static_cast<std::uint8_t>(a8(rng)));
    run_all_passes("full-tile write / lane select", TILE_ROWS, 0);

    // 3. Same weights, new activations, no rewrite: memory retention.
    for (int r = 0; r < TILE_ROWS; ++r) h.write_activation(r, static_cast<std::uint8_t>(a8(rng)));
    run_all_passes("reread without rewrite", TILE_ROWS, 0);

    // 4. Random: lane columns rewritten, random active rows, random idle gaps.
    for (int t = 0; t < n_random; ++t) {
        for (int p = 0; p < PASSES; ++p)
            for (int r = 0; r < TILE_ROWS; ++r)
                h.write_weight(r, lane_col(p), static_cast<std::int8_t>(w8(rng)));
        for (int r = 0; r < TILE_ROWS; ++r) h.write_activation(r, static_cast<std::uint8_t>(a8(rng)));
        const int rows = (t % 4 == 0) ? TILE_ROWS : rows_d(rng);
        run_all_passes("random " + std::to_string(t), rows, (t % 2) ? 10 : 0);
    }

    // 5. Drain chain: shift loads, then holds.
    h.check_shift(-12345);
    h.check_shift(0x7FFFFFFF);

    // 6. Priority collisions: clear > accumulate > shift. Row 0 of the lane's
    //    pass-0 column must hold a non-zero product for these to bite.
    h.write_weight(0, lane_col(0), 77);
    h.write_activation(0, 200);
    h.check_priority(false, true);    // accumulate beats shift
    h.check_priority(true,  false);   // clear beats accumulate
    h.check_priority(true,  true);    // clear beats both

    std::printf("PASS: L0 co-sim, lane %d, seed %u: %d tests, %d pass results, "
                "%llu cycle checks, rtl == model_exact == model_ideal\n",
                kLane, seed, tests, h.passes_checked(),
                static_cast<unsigned long long>(h.checks()));
    return 0;
}
