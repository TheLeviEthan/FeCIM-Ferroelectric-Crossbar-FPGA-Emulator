// Tests for the cycle-level and register-level halves of ModelExact (task C3):
// step() and the sequencer, the lane pipeline, result buffer, argmax with
// runner-up, STATUS / CYCLE_CNT, and set_reg / get_reg.
//
// The central property: the fast transaction path, compute(), and the
// cycle-accurate path, start_compute() + step(), are written independently
// and must leave the model in the same observable state for every
// configuration -- results, argmax, STATUS, CYCLE_CNT, sequencer outputs,
// LFSRs and every lane probe.

#include <algorithm>
#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <type_traits>
#include <vector>

#include "fecim/model_exact.h"
#include "fecim/params.h"
#include "fecim/primitives.h"
#include "test_harness.h"

using namespace fecim;

// ---- shape: types are pinned to the RTL widths -----------------------------
// (Moved from the retired test_model_exact_stub.cpp.) Narrowest standard type
// holding each signal, signed as in the RTL.
using Probe = ModelExact::LaneProbe;
static_assert(std::is_same_v<decltype(Probe::w_n),  std::int16_t>,  "w_n is 9-bit signed");
static_assert(std::is_same_v<decltype(Probe::prod), std::int32_t>,  "prod is 18-bit signed");
static_assert(std::is_same_v<decltype(Probe::acc),  std::int32_t>,  "acc is ACC_W = 32 signed");
static_assert(std::is_same_v<decltype(Probe::lfsr), std::uint32_t>, "LFSR is 32-bit");
static_assert(std::is_same_v<decltype(ModelExact::Argmax::max_idx), std::uint16_t>,
              "READ_ARGMAX max_idx is 2 bytes on the wire");
static_assert(sizeof(Status) == 1 && sizeof(CfgAddr) == 1 && sizeof(CtrlState) == 1,
              "protocol enums are one wire byte");
static_assert(W_N_W <= 16 && PROD_W <= 32 && ACC_W <= 32,
              "LaneProbe field types no longer hold the RTL widths");

namespace {

constexpr std::int32_t kInt32Min = std::numeric_limits<std::int32_t>::min();

// Steps to run one COMPUTE from IDLE back to IDLE. Returns the step count;
// `busy_steps` gets the number of steps until busy() first drops.
int run_stepped(ModelExact& m, int* busy_steps = nullptr) {
    m.start_compute();
    int n = 0, b = -1;
    do {
        m.step();
        ++n;
        if (b < 0 && n > 1 && !m.busy()) b = n;
    } while (m.state() != CTRL_IDLE && n < 10000);
    if (busy_steps) *busy_steps = b;
    return n;
}

std::uint32_t reg(const ModelExact& m, std::uint8_t a) {
    std::uint32_t v = 0xDEADBEEF;
    m.get_reg(a, v);
    return v;
}

// Everything a co-sim harness or host can observe. Equal snapshots mean the
// two paths are indistinguishable from outside.
struct Snapshot {
    std::vector<std::int32_t> results;
    ModelExact::Argmax am;
    std::uint32_t status, cycle_cnt;
    int seq;
    std::vector<ModelExact::LaneProbe> lanes;
    ModelExact::CtrlProbe ctrl;
};
Snapshot snap(const ModelExact& m) {
    Snapshot s;
    s.results = m.results();
    s.am = m.argmax();
    s.status = reg(m, REG_STATUS);
    s.cycle_cnt = reg(m, REG_CYCLE_CNT);
    s.seq = m.seq_cnt();
    for (int l = 0; l < NUM_LANES; ++l) s.lanes.push_back(m.lane(l));
    s.ctrl = m.ctrl();
    return s;
}
// Field-by-field so a failure says which field.
void check_same(const Snapshot& a, const Snapshot& b, const std::string& what) {
    int bad = 0;
    auto expect = [&](bool ok, const std::string& field) {
        if (!ok && bad++ < 6) th::fail(__FILE__, __LINE__, what + ": " + field + " differs");
    };
    expect(a.results == b.results, "results");
    expect(a.am.max_idx == b.am.max_idx && a.am.max_val == b.am.max_val &&
           a.am.second_val == b.am.second_val, "argmax");
    expect(a.status == b.status, "STATUS");
    expect(a.cycle_cnt == b.cycle_cnt, "CYCLE_CNT");
    expect(a.seq == b.seq, "seq_cnt");
    for (int l = 0; l < NUM_LANES; ++l) {
        const auto& x = a.lanes[static_cast<std::size_t>(l)];
        const auto& y = b.lanes[static_cast<std::size_t>(l)];
        expect(x.w_n == y.w_n, "lane " + std::to_string(l) + " w_n");
        expect(x.prod == y.prod, "lane " + std::to_string(l) + " prod");
        expect(x.acc == y.acc, "lane " + std::to_string(l) + " acc");
        expect(x.lfsr == y.lfsr, "lane " + std::to_string(l) + " lfsr");
    }
    const auto &p = a.ctrl, &q = b.ctrl;
    expect(p.seq_cnt == q.seq_cnt && p.acc_clear == q.acc_clear && p.acc_en == q.acc_en &&
           p.shift_en == q.shift_en && p.drain_we == q.drain_we && p.drain_addr == q.drain_addr &&
           p.run_start == q.run_start && p.compute_done == q.compute_done &&
           p.act_bcast == q.act_bcast, "ctrl probe");
}

// Same deterministic contents into a model, through the write path with the
// config in effect (so quant / D2D / stuck bake in exactly as on hardware).
void load(ModelExact& m, const Config& cfg, std::uint32_t salt) {
    m.set_config(cfg);
    std::mt19937 g(salt);
    for (int c = 0; c < TILE_COLS; ++c)
        for (int r = 0; r < TILE_ROWS; ++r)
            m.write_weight(r, c, static_cast<std::int8_t>(static_cast<int>(g() & 0xFFu) - 128));
    for (int r = 0; r < TILE_ROWS; ++r) m.write_activation(r, static_cast<std::uint8_t>(g() & 0xFFu));
    for (int r = 0; r < TILE_ROWS; ++r) m.write_atten(r, static_cast<std::uint8_t>(160 + (g() % 96)));
    m.reseed();
}

Config cfg_of(int rows, int cols, std::uint8_t en) {
    Config c;
    c.active_rows  = rows;
    c.active_cols  = cols;
    c.noise_en     = en;
    c.quant_levels = 5;
    c.quant_mult   = 16320;      // round(255*256/4)
    c.d2d_sigma    = 18;
    c.read_sigma   = 64;
    c.noise_seed   = 0xC0FFEEu;
    c.adc_bits     = 8;
    c.stuck_rate   = 0x10000u | 1311u;   // 2 % stuck-at-rail
    return c;
}

std::uint32_t expected_cycles(int rows, int cols) {
    const int passes = (cols + NUM_LANES - 1) / NUM_LANES;
    return static_cast<std::uint32_t>(passes * (1 + rows + LANE_PIPE_DEPTH + NUM_LANES));
}

} // namespace

// ===========================================================================
// compute() == start_compute() + step()
// ===========================================================================

TEST(step_equals_compute_every_configuration) {
    const std::uint8_t enables[] = {
        NOISE_NONE, NOISE_QUANT, NOISE_D2D, NOISE_READ, NOISE_STUCK, NOISE_IR, NOISE_ADC,
        NOISE_QUANT | NOISE_D2D | NOISE_READ | NOISE_STUCK | NOISE_IR | NOISE_ADC};
    const std::pair<int, int> tiles[] = {
        {8, 8}, {32, 32}, {48, 48}, {64, 64}, {128, 128}, {128, 48}, {100, 100}, {1, 1}, {17, 65}};
    int combos = 0;
    for (auto [rows, cols] : tiles)
        for (std::uint8_t en : enables) {
            const Config cfg = cfg_of(rows, cols, en);
            ModelExact fast, slow;
            load(fast, cfg, 1000u + static_cast<std::uint32_t>(combos));
            load(slow, cfg, 1000u + static_cast<std::uint32_t>(combos));
            const std::string what = std::to_string(rows) + "x" + std::to_string(cols) +
                                     " en=" + std::to_string(en);
            // Twice: the second compute starts from wherever the first left
            // the LFSRs, the result buffer and the pipeline registers.
            for (int k = 0; k < 2; ++k) {
                const auto y = fast.compute();
                const int steps = run_stepped(slow);
                CHECK(y == slow.results());
                CHECK_EQ(static_cast<std::uint32_t>(steps), expected_cycles(rows, cols) + 2);
                check_same(snap(fast), snap(slow), what + " compute " + std::to_string(k));
            }
            ++combos;
        }
    CHECK_EQ(combos, 9 * 8);
}

TEST(step_and_compute_mix_freely) {
    // Alternating paths on one model must equal either path alone.
    const Config cfg = cfg_of(48, 100, NOISE_READ | NOISE_IR | NOISE_ADC);
    ModelExact mix, ref;
    load(mix, cfg, 7);
    load(ref, cfg, 7);
    for (int k = 0; k < 4; ++k) {
        if (k % 2) mix.compute(); else run_stepped(mix);
        ref.compute();
        check_same(snap(mix), snap(ref), "mixed compute " + std::to_string(k));
    }
}

TEST(compute_refuses_while_stepped_compute_runs) {
    ModelExact m;
    m.start_compute();
    CHECK_THROWS(m.compute(), std::logic_error);      // pending start
    m.step();
    CHECK_THROWS(m.compute(), std::logic_error);      // mid-compute
    while (m.state() != CTRL_IDLE) m.step();
    CHECK_EQ(m.compute().size(), std::size_t{TILE_COLS});
}

// ===========================================================================
// Sequencer: timing, CYCLE_CNT, busy, states
// ===========================================================================

TEST(cycle_count_full_tile_is_392) {
    ModelExact m;
    int busy_steps = 0;
    const int steps = run_stepped(m, &busy_steps);
    CHECK_EQ(m.cycle_count(), std::uint32_t{392});
    CHECK_EQ(reg(m, REG_CYCLE_CNT), std::uint32_t{392});
    CHECK_EQ(busy_steps, 1 + 392);     // start edge, then 392 counted cycles into DONE
    CHECK_EQ(steps, 1 + 392 + 1);      // and DONE -> IDLE
    m.compute();
    CHECK_EQ(m.cycle_count(), std::uint32_t{392});
}

TEST(cycle_count_formula_all_shapes) {
    ModelExact m;
    for (int rows : {1, 2, 3, 8, 47, 64, 127, 128})
        for (int cols : {1, 2, 48, 63, 64, 65, 100, 127, 128}) {
            Config c;
            c.active_rows = rows;
            c.active_cols = cols;
            m.set_config(c);
            run_stepped(m);
            CHECK_EQ(m.cycle_count(), expected_cycles(rows, cols));
        }
}

TEST(state_sequence_one_pass) {
    // 2 rows, 1 column (one pass): IDLE -1-> CLEAR -1-> ACC (2 + 3) -> DRAIN (64) -> DONE -> IDLE.
    ModelExact m;
    Config c;
    c.active_rows = 2;
    c.active_cols = 1;
    m.set_config(c);
    std::vector<CtrlState> seen;
    m.start_compute();
    do { m.step(); seen.push_back(m.state()); } while (m.state() != CTRL_IDLE);
    std::vector<CtrlState> want;
    want.push_back(CTRL_CLEAR);
    for (int i = 0; i < 2 + LANE_PIPE_DEPTH; ++i) want.push_back(CTRL_ACC);
    for (int i = 0; i < NUM_LANES; ++i) want.push_back(CTRL_DRAIN);
    want.push_back(CTRL_DONE);
    want.push_back(CTRL_IDLE);
    CHECK(seen == want);
}

TEST(acc_en_and_seq_cnt_per_pass) {
    // acc_en is high for exactly `rows` cycles per pass, with seq_cnt
    // {pass, 0 .. rows-1}; then seq_cnt parks on the last row.
    ModelExact m;
    Config c;
    c.active_rows = 5;
    c.active_cols = 70;     // two passes
    m.set_config(c);
    std::vector<int> issued;
    m.start_compute();
    do {
        m.step();
        if (m.ctrl().acc_en) issued.push_back(m.ctrl().seq_cnt);
    } while (m.state() != CTRL_IDLE);
    const std::vector<int> want = {0, 1, 2, 3, 4, 128, 129, 130, 131, 132};
    CHECK(issued == want);
    CHECK_EQ(m.seq_cnt(), 132);
}

TEST(pipeline_acc_first_changes_at_n_plus_3) {
    // One row, one weight. The row is issued in cycle N (the first ACC
    // cycle); the accumulator takes the product on the edge ending N+3.
    ModelExact m;
    Config c;
    c.active_rows = 1;
    c.active_cols = 1;
    m.set_config(c);
    m.write_weight(0, 0, -7);
    m.write_activation(0, 9);
    m.start_compute();
    m.step();                                   // IDLE -> CLEAR, acc_clear high
    CHECK(m.ctrl().acc_clear);
    m.step();                                   // CLEAR -> ACC: cycle N, row 0 issued
    CHECK(m.ctrl().acc_en);
    CHECK_EQ(m.ctrl().seq_cnt, 0);
    m.step();                                   // edge ending N: w_rd loaded
    CHECK_EQ(m.lane(0).acc, 0);
    m.step();                                   // edge ending N+1: w_align, act_bcast
    CHECK_EQ(int{m.lane(0).w_n}, -7);           // w_n wire valid in N+2
    CHECK_EQ(int{m.ctrl().act_bcast}, 9);
    CHECK_EQ(m.lane(0).acc, 0);
    m.step();                                   // edge ending N+2: prod
    CHECK_EQ(m.lane(0).prod, -63);
    CHECK_EQ(m.lane(0).acc, 0);
    m.step();                                   // edge ending N+3: acc
    CHECK_EQ(m.lane(0).acc, -63);
    CHECK_EQ(m.state(), CTRL_DRAIN);            // and the hold ends on the same edge
}

TEST(drain_streams_lane_order_into_result_addresses) {
    // During DRAIN, lane 0 holds column {pass, drain_cnt} in the cycle that
    // writes it (result-buffer-spec §3): the shift lands lane k+1's value in
    // lane 0 one edge after lane k's was written.
    ModelExact m;
    Config c;
    c.active_rows = 1;
    m.set_config(c);
    for (int col = 0; col < TILE_COLS; ++col) m.write_weight(0, col, static_cast<std::int8_t>(col - 64));
    m.write_activation(0, 3);
    m.start_compute();
    int writes = 0;
    do {
        m.step();
        const auto k = m.ctrl();
        if (k.drain_we) {
            CHECK_EQ(m.lane(0).acc, (k.drain_addr - 64) * 3);
            ++writes;
        }
    } while (m.state() != CTRL_IDLE);
    CHECK_EQ(writes, TILE_COLS);
    for (int col = 0; col < TILE_COLS; ++col)
        CHECK_EQ(m.results()[static_cast<std::size_t>(col)], (col - 64) * 3);
}

TEST(idle_stepping_changes_nothing) {
    // From reset, and after a COMPUTE: every observable stays put, the LFSRs
    // included.
    for (bool after_compute : {false, true}) {
        ModelExact m;
        if (after_compute) {
            load(m, cfg_of(128, 128, NOISE_READ | NOISE_IR), 3);
            run_stepped(m);
        }
        const Snapshot before = snap(m);
        for (int i = 0; i < 300; ++i) m.step();
        check_same(before, snap(m), after_compute ? "idle after compute" : "idle from reset");
        CHECK_EQ(m.state(), CTRL_IDLE);
    }
}

TEST(idle_after_host_writes_only_the_memory_reads_follow) {
    // The weight and activation M9Ks are read every cycle at seq_cnt, as in
    // the RTL, so new memory contents show up on w_n / act_bcast within two
    // edges. Nothing else moves: no LFSR step, no accumulate, no result.
    ModelExact m;
    load(m, cfg_of(128, 128, NOISE_READ), 4);
    const Snapshot before = snap(m);
    for (int i = 0; i < 2; ++i) m.step();
    const Snapshot settled = snap(m);
    for (int l = 0; l < NUM_LANES; ++l) {
        const auto& a = before.lanes[static_cast<std::size_t>(l)];
        const auto& b = settled.lanes[static_cast<std::size_t>(l)];
        CHECK_EQ(a.lfsr, b.lfsr);
        CHECK_EQ(a.acc, b.acc);
        CHECK_EQ(a.prod, b.prod);
    }
    CHECK(before.results == settled.results);
    CHECK_EQ(before.status, settled.status);
    for (int i = 0; i < 300; ++i) m.step();
    check_same(settled, snap(m), "idle after settling");
}

TEST(start_ignored_unless_idle) {
    ModelExact m;
    Config c;
    c.active_rows = 4;
    c.active_cols = 4;
    m.set_config(c);
    m.start_compute();
    for (int i = 0; i < 10; ++i) m.step();
    m.start_compute();                          // mid-compute: lost, as in the RTL
    int n = 0;
    while (m.state() != CTRL_IDLE) { m.step(); ++n; }
    for (int i = 0; i < 5; ++i) m.step();
    CHECK_EQ(m.state(), CTRL_IDLE);             // did not start a second compute
    CHECK_EQ(reg(m, REG_STATUS) >> 8, 1u);      // one compute completed
}

TEST(lfsr_advances_rows_times_passes_only) {
    ModelExact m;
    Config c;
    c.active_rows = 10;
    c.active_cols = 65;   // 2 passes
    m.set_config(c);
    run_stepped(m);
    for (int l : {0, 63}) {
        std::uint32_t s = lane_seed(0, l);
        for (int i = 0; i < 20; ++i) s = lfsr_step(s);
        CHECK_EQ(m.lane(l).lfsr, s);
    }
}

// ===========================================================================
// Result buffer, argmax
// ===========================================================================

TEST(argmax_matches_brute_force_with_ties_and_mask) {
    std::mt19937 g(42);
    int cases = 0;
    for (int t = 0; t < 120; ++t) {
        ModelExact m;
        Config c;
        c.active_rows = 1 + static_cast<int>(g() % 16);
        c.active_cols = 1 + static_cast<int>(g() % TILE_COLS);
        c.noise_en    = (t % 2) ? std::uint8_t{NOISE_ADC} : NOISE_NONE;
        c.adc_bits    = 4;                      // coarse ADC: many exact ties
        m.set_config(c);
        // Small weights so ties are common even without the ADC, and big
        // positive values beyond active_cols that must be masked out.
        for (int col = 0; col < TILE_COLS; ++col)
            for (int r = 0; r < c.active_rows; ++r)
                m.write_weight(r, col, col >= c.active_cols ? std::int8_t{127}
                                                            : static_cast<std::int8_t>(static_cast<int>(g() % 5) - 2));
        for (int r = 0; r < c.active_rows; ++r) m.write_activation(r, static_cast<std::uint8_t>(g() & 0xFFu));
        if (t % 3 == 0) run_stepped(m); else m.compute();

        const auto y = m.results();
        CHECK_EQ(y.size(), static_cast<std::size_t>(c.active_cols));
        std::size_t idx = 0;
        for (std::size_t i = 1; i < y.size(); ++i) if (y[i] > y[idx]) idx = i;   // first max
        std::vector<std::int32_t> sorted = y;
        std::sort(sorted.begin(), sorted.end(), std::greater<>());
        const auto am = m.argmax();
        CHECK_EQ(std::size_t{am.max_idx}, idx);
        CHECK_EQ(am.max_val, y[idx]);
        CHECK_EQ(am.second_val, y.size() > 1 ? sorted[1] : kInt32Min);
        ++cases;
    }
    CHECK_EQ(cases, 120);
}

TEST(results_persist_and_clear_results_keeps_data) {
    ModelExact m;
    load(m, cfg_of(16, 16, NOISE_NONE), 5);
    const auto y = m.compute();
    CHECK(reg(m, REG_STATUS) & (1u << 6));
    m.set_reg(REG_CTRL, 0x2);                   // CTRL[1]: clear results
    CHECK(!(reg(m, REG_STATUS) & (1u << 6)));
    CHECK(m.results() == y);                    // data kept: a retried READ_RESULT is safe
    for (int i = 0; i < 50; ++i) m.step();
    CHECK(m.results() == y);
}

// ===========================================================================
// Registers
// ===========================================================================

TEST(register_reset_defaults) {
    // protocol.md §6 reset column.
    const ModelExact m;
    CHECK_EQ(reg(m, REG_CTRL), 0u);
    CHECK_EQ(reg(m, REG_TILE_CFG), (std::uint32_t{TILE_COLS} << 16) | TILE_ROWS);
    CHECK_EQ(reg(m, REG_QUANT_LEVELS), 255u);
    CHECK_EQ(reg(m, REG_D2D_SIGMA), 0u);
    CHECK_EQ(reg(m, REG_READ_SIGMA), 0u);
    CHECK_EQ(reg(m, REG_NOISE_SEED), 0u);
    CHECK_EQ(reg(m, REG_NOISE_EN), 0u);
    CHECK_EQ(reg(m, REG_ADC_BITS), static_cast<std::uint32_t>(ACC_USED_W));
    CHECK_EQ(reg(m, REG_STUCK_RATE), 0u);
    CHECK_EQ(reg(m, REG_BAUD_INC), 2416u);
    CHECK_EQ(reg(m, REG_STATUS), 0u);           // IDLE, not busy, no results, seq 0
    CHECK_EQ(reg(m, REG_CYCLE_CNT), 0u);
    CHECK_EQ(reg(m, REG_QUANT_MULT), 257u);
}

TEST(register_access_policy) {
    ModelExact m;
    std::uint32_t v = 0;
    // Read-only registers refuse writes and keep their value.
    CHECK_EQ(m.set_reg(REG_STATUS, 0xFFFFFFFFu), ST_ADDR_RANGE);
    CHECK_EQ(m.set_reg(REG_CYCLE_CNT, 123u), ST_ADDR_RANGE);
    CHECK_EQ(reg(m, REG_STATUS), 0u);
    CHECK_EQ(reg(m, REG_CYCLE_CNT), 0u);
    // Unmapped and reserved: both commands, every address past the map.
    for (int a = 0x0D; a <= 0xFF; ++a) {
        CHECK_EQ(m.set_reg(static_cast<std::uint8_t>(a), 1u), ST_ADDR_RANGE);
        CHECK_EQ(m.get_reg(static_cast<std::uint8_t>(a), v), ST_ADDR_RANGE);
    }
    for (int a = 0x00; a <= 0x0C; ++a) CHECK_EQ(m.get_reg(static_cast<std::uint8_t>(a), v), ST_OK);
    // Field widths, as config_regs stores them.
    CHECK_EQ(m.set_reg(REG_TILE_CFG, (48u << 16) | 100u), ST_OK);
    CHECK_EQ(reg(m, REG_TILE_CFG), (48u << 16) | 100u);
    CHECK_EQ(m.config().active_rows, 100);
    CHECK_EQ(m.config().active_cols, 48);
    m.set_reg(REG_D2D_SIGMA, 0x1FFu);    CHECK_EQ(reg(m, REG_D2D_SIGMA), 0xFFu);
    m.set_reg(REG_READ_SIGMA, 0x123u);   CHECK_EQ(reg(m, REG_READ_SIGMA), 0x23u);
    m.set_reg(REG_NOISE_SEED, 0xFFFFFFFFu); CHECK_EQ(reg(m, REG_NOISE_SEED), 0xFFFFFFFFu);
    m.set_reg(REG_NOISE_EN, 0xFFu);      CHECK_EQ(reg(m, REG_NOISE_EN), 0x3Fu);
    m.set_reg(REG_STUCK_RATE, 0xFFFFFFFFu); CHECK_EQ(reg(m, REG_STUCK_RATE), 0x1FFFFu);
    m.set_reg(REG_BAUD_INC, 0x12345678u); CHECK_EQ(reg(m, REG_BAUD_INC), 0x5678u);
    m.set_reg(REG_QUANT_MULT, 0xABCDEF01u); CHECK_EQ(reg(m, REG_QUANT_MULT), 0xEF01u);
    // Out-of-range values: config_regs ignores the write, no status path.
    m.set_reg(REG_QUANT_LEVELS, 8);
    CHECK_EQ(m.set_reg(REG_QUANT_LEVELS, 1), ST_OK);    CHECK_EQ(reg(m, REG_QUANT_LEVELS), 8u);
    CHECK_EQ(m.set_reg(REG_QUANT_LEVELS, 256), ST_OK);  CHECK_EQ(reg(m, REG_QUANT_LEVELS), 8u);
    m.set_reg(REG_ADC_BITS, 8);
    CHECK_EQ(m.set_reg(REG_ADC_BITS, 3), ST_OK);        CHECK_EQ(reg(m, REG_ADC_BITS), 8u);
    CHECK_EQ(m.set_reg(REG_ADC_BITS, 25), ST_OK);       CHECK_EQ(reg(m, REG_ADC_BITS), 8u);
    CHECK_EQ(m.set_reg(REG_ADC_BITS, 32), ST_OK);       CHECK_EQ(reg(m, REG_ADC_BITS), 8u);
}

TEST(tile_cfg_zero_or_oversize_means_full_tile) {
    // control_fsm decodes 0 or > tile as the full tile; the model computes
    // the same thing either way.
    ModelExact a, b;
    load(a, cfg_of(128, 128, NOISE_NONE), 9);
    load(b, cfg_of(128, 128, NOISE_NONE), 9);
    a.set_reg(REG_TILE_CFG, 0u);
    b.set_reg(REG_TILE_CFG, (500u << 16) | 300u);
    run_stepped(a);
    run_stepped(b);
    CHECK_EQ(a.cycle_count(), std::uint32_t{392});
    CHECK_EQ(b.cycle_count(), std::uint32_t{392});
    CHECK(a.results() == b.results());
    CHECK_EQ(a.results().size(), std::size_t{TILE_COLS});
}

TEST(status_fields_before_during_after) {
    ModelExact m;
    Config c;
    c.active_rows = 4;
    c.active_cols = 4;
    m.set_config(c);
    CHECK_EQ(reg(m, REG_STATUS), 0u);
    m.start_compute();
    CHECK_EQ(reg(m, REG_STATUS), 0u);            // a pending start is not yet visible
    bool saw_busy_states = false;
    std::uint32_t done_status = 0;
    do {
        m.step();
        const std::uint32_t s = reg(m, REG_STATUS);
        const auto st = static_cast<CtrlState>(s & 0xFu);
        CHECK_EQ(st, m.state());
        CHECK_EQ(s & (1u << 5), 0u);              // last_err: no UART in the model
        if (st == CTRL_CLEAR || st == CTRL_ACC || st == CTRL_DRAIN) {
            CHECK(s & (1u << 4));                  // busy
            saw_busy_states = true;
        }
        if (st == CTRL_DONE) done_status = s;
    } while (m.state() != CTRL_IDLE);
    CHECK(saw_busy_states);
    // In DONE: not busy; results_valid and result_seq update on the
    // compute_done pulse, so they show from the next cycle (IDLE).
    CHECK_EQ(done_status, std::uint32_t{CTRL_DONE});
    CHECK_EQ(reg(m, REG_STATUS), (1u << 8) | (1u << 6));   // seq 1, valid, IDLE
    // A second compute clears results_valid at its start and sets it again.
    m.start_compute();
    m.step();
    m.step();                                     // past the run_start pulse
    CHECK_EQ(reg(m, REG_STATUS) & (1u << 6), 0u);
    while (m.state() != CTRL_IDLE) m.step();
    CHECK_EQ(reg(m, REG_STATUS), (2u << 8) | (1u << 6));
}

TEST(result_seq_wraps_at_8_bits) {
    ModelExact m;
    Config c;
    c.active_rows = 1;
    c.active_cols = 1;
    m.set_config(c);
    for (int i = 0; i < 257; ++i) m.compute();
    CHECK_EQ((reg(m, REG_STATUS) >> 8) & 0xFFu, 1u);
}

TEST(ctrl_soft_reset_mid_compute) {
    ModelExact m;
    load(m, cfg_of(64, 64, NOISE_NONE), 11);
    run_stepped(m);
    const std::uint32_t tile = reg(m, REG_TILE_CFG);
    m.start_compute();
    for (int i = 0; i < 40; ++i) m.step();
    CHECK(m.busy());
    std::vector<std::uint32_t> lfsr;
    for (int l = 0; l < NUM_LANES; ++l) lfsr.push_back(m.lane(l).lfsr);
    CHECK_EQ(m.set_reg(REG_CTRL, 0x1), ST_OK);
    for (int l = 0; l < NUM_LANES; ++l)           // soft reset does not reach the lanes
        CHECK_EQ(m.lane(l).lfsr, lfsr[static_cast<std::size_t>(l)]);
    CHECK_EQ(m.state(), CTRL_IDLE);
    CHECK(!m.busy());
    CHECK_EQ(m.cycle_count(), 0u);               // control_fsm clears cycle_cnt
    CHECK_EQ(reg(m, REG_TILE_CFG), tile);        // registers kept
    // Let the rows in flight retire (soft reset does not reach the lanes'
    // valid pipeline); then the next COMPUTE is unaffected by the aborted one.
    for (int i = 0; i < LANE_PIPE_DEPTH; ++i) m.step();
    ModelExact ref;
    load(ref, cfg_of(64, 64, NOISE_NONE), 11);
    run_stepped(m);
    CHECK(m.results() == ref.compute());
}

TEST(ctrl_reseed_strobe) {
    ModelExact m;
    m.set_reg(REG_NOISE_SEED, 0xBEEFu);
    CHECK_EQ(m.lane(5).lfsr, lane_seed(0, 5));   // seed write alone does not reload
    m.set_reg(REG_CTRL, 0x4);
    for (int l : {0, 5, 63}) CHECK_EQ(m.lane(l).lfsr, lane_seed(0xBEEFu, l));
}

TEST(register_path_equals_typed_path) {
    // set_reg drives the same model set_config does: the sim backend and the
    // C++ tests see one machine.
    const Config cfg = cfg_of(48, 100, NOISE_QUANT | NOISE_D2D | NOISE_READ | NOISE_IR | NOISE_ADC);
    ModelExact a, b;
    a.set_config(cfg);
    b.set_reg(REG_TILE_CFG, (std::uint32_t(cfg.active_cols) << 16) | std::uint32_t(cfg.active_rows));
    b.set_reg(REG_QUANT_LEVELS, static_cast<std::uint32_t>(cfg.quant_levels));
    b.set_reg(REG_QUANT_MULT, cfg.quant_mult);
    b.set_reg(REG_D2D_SIGMA, cfg.d2d_sigma);
    b.set_reg(REG_READ_SIGMA, cfg.read_sigma);
    b.set_reg(REG_NOISE_SEED, cfg.noise_seed);
    b.set_reg(REG_NOISE_EN, cfg.noise_en);
    b.set_reg(REG_ADC_BITS, static_cast<std::uint32_t>(cfg.adc_bits));
    b.set_reg(REG_STUCK_RATE, cfg.stuck_rate);
    for (std::uint8_t r = 0x01; r <= 0x0C; ++r) CHECK_EQ(reg(a, r), reg(b, r));
    std::mt19937 g(3);
    for (int c = 0; c < TILE_COLS; ++c)
        for (int r = 0; r < TILE_ROWS; ++r) {
            const auto w = static_cast<std::int8_t>(static_cast<int>(g() & 0xFFu) - 128);
            a.write_weight(r, c, w);
            b.write_weight(r, c, w);
        }
    for (int r = 0; r < TILE_ROWS; ++r) {
        const auto x = static_cast<std::uint8_t>(g() & 0xFFu);
        a.write_activation(r, x);
        b.write_activation(r, x);
    }
    a.reseed();
    b.set_reg(REG_CTRL, 0x4);
    CHECK(a.compute() == b.compute());
}
