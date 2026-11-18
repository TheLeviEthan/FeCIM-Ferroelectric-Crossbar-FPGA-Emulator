// Unit tests for write-path stages 2 and 3 (fecim/write_path.h):
// device-to-device (D2D) variation and stuck-at faults, plus their hookup
// in ModelExact::write_weight().
//
// Both effects are keyed on cell_hash16 of the GLOBAL cell address, so they
// are permanent properties of a cell: the same cell, value and seed always
// give the same stored weight (write-path-spec §5).
//
// Pinned values below were computed by an independent Python
// implementation of write-path-spec §5 (plain ints, Python's floor `>>`):
//
//     def h16(a, s):  h = (a ^ s) & 0xFFFF; h = h * 0x9E37 & 0xFFFF; h ^= h >> 7
//                     h = h * 0x85EB & 0xFFFF; h ^= h >> 9; return h
//     def d2d(w, a, seed, sig):
//         h = h16(a, (seed & 0xFFFF) ^ 0xD2D0); ih9 = (h & 0xFF) + (h >> 8) - 255
//         return max(-128, min(127, w + ((ih9 * sig) >> 8)))
//
// tb/golden/write_path/gen_write_path_vectors.py is the full version of
// that reference; test_write_path_vectors.cpp replays its output.

#include <algorithm>
#include <cmath>     // tests only: statistics on the distribution, never in the model
#include <cstdint>
#include <random>
#include <set>
#include <stdexcept>
#include <vector>

#include "fecim/model_exact.h"
#include "fecim/model_ideal.h"
#include "fecim/params.h"
#include "fecim/primitives.h"
#include "fecim/write_path.h"
#include "test_harness.h"

using namespace fecim;

namespace {

constexpr int kCells = TILE_ROWS * TILE_COLS;      // 16384

// The triangular variable the D2D stage draws, recomputed here from the
// package hash so the statistics tests look at the distribution directly.
int ih9_of(std::uint16_t addr, std::uint32_t seed) {
    const std::uint16_t h =
        cell_hash16(addr, static_cast<std::uint16_t>((seed & 0xFFFFu) ^ SEED_D2D));
    return static_cast<int>(h & 0xFFu) + static_cast<int>(h >> 8) - 255;
}

// Offset actually added by the D2D stage, measured through d2d_offset().
// Offsets reach +/-255 at full sigma, so no single probe weight avoids
// saturation. Probe from both rails instead: from -128 a non-negative offset
// is exact and a negative one clips to 0; from +127 the reverse. Exactly one
// of the two terms is non-zero (or both are 0), so their sum is the offset.
int offset_of(std::uint16_t addr, std::uint32_t seed, std::uint8_t sigma) {
    const int up   = int{d2d_offset(-128, addr, seed, sigma)} + 128;   // >= 0
    const int down = int{d2d_offset( 127, addr, seed, sigma)} - 127;   // <= 0
    return up + down;
}

bool is_stuck(std::uint16_t addr, std::uint32_t seed, std::uint32_t rate) {
    // A probe value no stuck mode can produce (zero mode gives 0, rail gives
    // +/-127), so "returned unchanged" means "not stuck".
    return int{apply_stuck(55, addr, seed, rate)} != 55;
}

Config cfg_with(std::uint8_t noise_en) {
    Config cfg;
    cfg.noise_en = noise_en;
    return cfg;
}

} // namespace

// ===========================================================================
// cell_addr
// ===========================================================================

TEST(cell_addr_is_global_column_major) {
    // Same index as the bulk weight stream: col * TILE_ROWS + row.
    CHECK_EQ(cell_addr(0, 0), std::uint16_t{0});
    CHECK_EQ(cell_addr(1, 0), std::uint16_t{1});
    CHECK_EQ(cell_addr(0, 1), std::uint16_t{128});
    CHECK_EQ(cell_addr(5, 9), std::uint16_t{1157});
    CHECK_EQ(cell_addr(0, 64), std::uint16_t{8192});       // lane 0, pass 1
    CHECK_EQ(cell_addr(127, 127), std::uint16_t{16383});

    // Every cell gets its own address: lanes must not collide.
    std::set<std::uint16_t> seen;
    for (int c = 0; c < TILE_COLS; ++c)
        for (int r = 0; r < TILE_ROWS; ++r) seen.insert(cell_addr(r, c));
    CHECK_EQ(seen.size(), std::size_t{kCells});
}

TEST(cell_addr_rejects_out_of_tile) {
    CHECK_THROWS(cell_addr(-1, 0), std::out_of_range);
    CHECK_THROWS(cell_addr(TILE_ROWS, 0), std::out_of_range);
    CHECK_THROWS(cell_addr(0, -1), std::out_of_range);
    CHECK_THROWS(cell_addr(0, TILE_COLS), std::out_of_range);
}

// ===========================================================================
// Stage 2: D2D variation
// ===========================================================================

TEST(d2d_golden_values) {
    // Independent Python reference (header comment). Set A's register value,
    // D2D_SIGMA = 18, at seed 0:
    CHECK_EQ(int{d2d_offset(   0, cell_addr(  0,   0), 0u, 18)},    1);
    CHECK_EQ(int{d2d_offset( 100, cell_addr(  5,   9), 0u, 18)},   91);
    CHECK_EQ(int{d2d_offset( -50, cell_addr(127, 127), 0u, 18)},  -55);
    CHECK_EQ(int{d2d_offset( 127, cell_addr(  0,   1), 0u, 18)},  117);
    CHECK_EQ(int{d2d_offset(-128, cell_addr(  0,  64), 0u, 18)}, -128);  // -130 saturates

    // Full-scale sigma, non-zero seed:
    CHECK_EQ(int{d2d_offset(   0, cell_addr(  0,   0), 0x1234u, 255)},   11);
    CHECK_EQ(int{d2d_offset( 100, cell_addr(  5,   9), 0x1234u, 255)},   22);
    CHECK_EQ(int{d2d_offset( -50, cell_addr(127, 127), 0x1234u, 255)},  -85);
    CHECK_EQ(int{d2d_offset( 127, cell_addr(  0,   1), 0x1234u, 255)},   81);
    CHECK_EQ(int{d2d_offset(   0, cell_addr(  3,   3), 0x1234u, 255)},  -48);
}

TEST(d2d_sigma_zero_is_identity) {
    // D2D_SIGMA = 0 scales every offset to exactly 0 (0 >>> 8 = 0, and a
    // negative times 0 is 0, not -1).
    for (int a = 0; a < kCells; a += 7)
        for (int w = -128; w <= 127; ++w)
            CHECK_EQ(int{d2d_offset(static_cast<std::int8_t>(w),
                                    static_cast<std::uint16_t>(a), 0xBEEFu, 0)}, w);
}

TEST(d2d_offset_is_floor_of_ih9_times_sigma_over_256) {
    // The scaling is an arithmetic shift (rounds toward minus infinity), the
    // same as SV's >>>. Checked at mid-range weights so saturation can't hide
    // it. A truncating division would differ on every negative product that
    // is not a multiple of 256.
    for (std::uint8_t sigma : {std::uint8_t{1}, std::uint8_t{18}, std::uint8_t{100},
                               std::uint8_t{255}}) {
        for (int a = 0; a < kCells; a += 3) {
            const auto addr = static_cast<std::uint16_t>(a);
            const int p = ih9_of(addr, 0u) * int{sigma};
            const int expect = static_cast<int>(std::floor(p / 256.0));
            CHECK_EQ(offset_of(addr, 0u, sigma), expect);
        }
    }
}

TEST(d2d_saturates_never_wraps) {
    // write-path-spec §5.2: +127 plus a positive offset stays +127, -128 plus
    // a negative offset stays -128. A wrapping add flips the sign -- the
    // worst possible error in an MVM. Cells (2,0) and (7,0) have offsets of
    // +120 and -167 at sigma 255, seed 0 (Python reference).
    CHECK_EQ(offset_of(cell_addr(2, 0), 0u, 255),  120);
    CHECK_EQ(offset_of(cell_addr(7, 0), 0u, 255), -167);
    CHECK_EQ(int{d2d_offset( 127, cell_addr(2, 0), 0u, 255)},  127);
    CHECK_EQ(int{d2d_offset(  10, cell_addr(2, 0), 0u, 255)},  127);
    CHECK_EQ(int{d2d_offset(-128, cell_addr(7, 0), 0u, 255)}, -128);
    CHECK_EQ(int{d2d_offset( -10, cell_addr(7, 0), 0u, 255)}, -128);

    // Exhaustively at full sigma: the result is always the clamped sum, so in
    // particular a non-negative weight with a non-negative offset never comes
    // back negative, and vice versa.
    for (int a = 0; a < kCells; a += 5) {
        const auto addr = static_cast<std::uint16_t>(a);
        const int off = offset_of(addr, 0x0F0Fu, 255);
        for (int w = -128; w <= 127; ++w) {
            const int got = d2d_offset(static_cast<std::int8_t>(w), addr, 0x0F0Fu, 255);
            const int sum = w + off;
            CHECK_EQ(got, sum > 127 ? 127 : (sum < -128 ? -128 : sum));
        }
    }
}

TEST(d2d_offset_bounded_by_sigma) {
    // |ih9| <= 255, so the offset lies in [floor(-255 s/256), floor(255 s/256)].
    for (int s : {1, 8, 18, 32, 255}) {
        const int lo = static_cast<int>(std::floor(-255.0 * s / 256.0));
        const int hi = static_cast<int>(std::floor( 255.0 * s / 256.0));
        for (int a = 0; a < kCells; ++a) {
            const int off = offset_of(static_cast<std::uint16_t>(a), 0u,
                                      static_cast<std::uint8_t>(s));
            CHECK(off >= lo && off <= hi);
        }
    }
}

TEST(d2d_distribution_is_triangular_zero_mean) {
    // Two uniform bytes summed: triangular on -255..+255, mean 0, standard
    // deviation sqrt(2 * (256^2 - 1) / 12) = 104.5, and 75 % of the mass
    // within +/-128 (a Gaussian of the same sigma would put 78 % there and
    // have tails beyond 255; a uniform 50 %). Measured over all 16384 cells
    // for three seeds; tolerances are loose enough for one tile, tight
    // enough to catch using one byte (uniform) or dropping the centring.
    for (std::uint32_t seed : {0u, 0x1234u, 0xFFFFFFFFu}) {
        double sum = 0, sum2 = 0;
        int inner = 0, lo = 0, hi = 0;
        for (int a = 0; a < kCells; ++a) {
            const int x = ih9_of(static_cast<std::uint16_t>(a), seed);
            sum += x;
            sum2 += double(x) * x;
            if (x > -128 && x < 128) ++inner;
            lo = std::min(lo, x);
            hi = std::max(hi, x);
        }
        const double mean = sum / kCells;
        const double sd   = std::sqrt(sum2 / kCells - mean * mean);
        const double frac = double(inner) / kCells;
        CHECK(std::fabs(mean) < 3.0);               // SE of the mean ~ 0.8
        CHECK(sd > 101.0 && sd < 108.0);            // theory 104.5
        CHECK(frac > 0.73 && frac < 0.77);          // theory 0.75
        CHECK(lo >= -255 && hi <= 255);
        CHECK(lo < -230 && hi > 230);               // both tails reached
    }
}

TEST(d2d_deterministic_per_cell_and_seed) {
    // A permanent property of the cell: same inputs, same result, every time.
    for (int a = 0; a < kCells; a += 97) {
        const auto addr = static_cast<std::uint16_t>(a);
        const auto first = d2d_offset(40, addr, 0xACE1u, 64);
        for (int k = 0; k < 5; ++k) CHECK_EQ(d2d_offset(40, addr, 0xACE1u, 64), first);
    }
}

TEST(d2d_seed_changes_cells_and_only_low_16_bits_matter) {
    // A new seed is a different physical chip: most cells move.
    int differ = 0;
    for (int a = 0; a < kCells; ++a)
        if (ih9_of(static_cast<std::uint16_t>(a), 0u) != ih9_of(static_cast<std::uint16_t>(a), 1u))
            ++differ;
    CHECK(differ > kCells * 9 / 10);

    // Only NOISE_SEED[15:0] reaches the write path.
    for (int a = 0; a < kCells; a += 11) {
        const auto addr = static_cast<std::uint16_t>(a);
        CHECK_EQ(d2d_offset(0, addr, 0x00001234u, 255), d2d_offset(0, addr, 0xABCD1234u, 255));
    }
}

TEST(d2d_lanes_do_not_share_offsets) {
    // Catches hashing the lane-LOCAL address ({pass, row}) instead of the
    // global one: then all 64 columns of a pass would get identical offsets
    // for a given row.
    for (int r : {0, 1, 64, 127}) {
        std::set<int> offs;
        for (int c = 0; c < NUM_LANES; ++c) offs.insert(offset_of(cell_addr(r, c), 0u, 255));
        CHECK(offs.size() > 40u);
    }
}

// ===========================================================================
// Stage 3: stuck-at faults
// ===========================================================================

TEST(stuck_golden_values) {
    // Python reference, seed 0, rate 655 (1 %). Cell (43,0) has
    // h_stuck = 77 < 655 and h_d2d[15] = 0; cell (55,0) has h_stuck = 203
    // and h_d2d[15] = 1. Cell (0,0) has h_stuck = 0x41DC, not stuck.
    constexpr std::uint32_t kRate1pct = 655;
    constexpr std::uint32_t kRail     = 1u << 16;
    CHECK_EQ(int{apply_stuck(55, cell_addr(43, 0), 0u, kRate1pct)},          0);
    CHECK_EQ(int{apply_stuck(55, cell_addr(43, 0), 0u, kRate1pct | kRail)}, 127);
    CHECK_EQ(int{apply_stuck(55, cell_addr(55, 0), 0u, kRate1pct | kRail)}, -127);
    CHECK_EQ(int{apply_stuck(55, cell_addr(87, 0), 0u, kRate1pct | kRail)}, -127);
    CHECK_EQ(int{apply_stuck(55, cell_addr(14, 1), 0u, kRate1pct | kRail)}, -127);
    CHECK_EQ(int{apply_stuck(55, cell_addr( 0, 0), 0u, kRate1pct | kRail)},  55);
}

TEST(stuck_threshold_is_strict_less_than) {
    // hit = h_stuck < rate. At rate == h_stuck the cell is NOT stuck; one
    // more and it is. Cell (43,0) has h_stuck = 77 at seed 0.
    const auto addr = cell_addr(43, 0);
    CHECK(!is_stuck(addr, 0u, 77));
    CHECK(is_stuck(addr, 0u, 78));
}

TEST(stuck_rate_zero_never_hits) {
    for (int a = 0; a < kCells; ++a) {
        CHECK(!is_stuck(static_cast<std::uint16_t>(a), 0u, 0u));
        CHECK(!is_stuck(static_cast<std::uint16_t>(a), 0u, 1u << 16));   // rail mode, rate 0
    }
}

TEST(stuck_fraction_matches_rate) {
    // Expected fraction = rate / 65536. Tolerance is 4 binomial standard
    // errors over 16384 cells, so a correct model essentially never fails
    // and an off-by-a-factor (e.g. comparing against rate / 2) always does.
    for (std::uint32_t seed : {0u, 0xBEEFu}) {
        for (std::uint32_t rate : {655u, 3277u, 6554u, 32768u, 65535u}) {
            int n = 0;
            for (int a = 0; a < kCells; ++a) n += is_stuck(static_cast<std::uint16_t>(a), seed, rate);
            const double p   = rate / 65536.0;
            const double se  = std::sqrt(p * (1 - p) / kCells);
            const double got = double(n) / kCells;
            CHECK(std::fabs(got - p) <= 4 * se + 1.0 / kCells);
        }
    }
}

TEST(stuck_monotone_in_rate) {
    // Raising STUCK_RATE only ever adds stuck cells (fixed seed). This is
    // what makes a stuck-rate sweep a sweep over ONE chip getting worse.
    for (int a = 0; a < kCells; ++a) {
        const auto addr = static_cast<std::uint16_t>(a);
        bool was = false;
        for (std::uint32_t rate : {0u, 100u, 655u, 6554u, 20000u, 65535u}) {
            const bool now = is_stuck(addr, 0x5EEDu, rate);
            CHECK(now || !was);
            was = now;
        }
    }
}

TEST(stuck_modes_force_the_right_values) {
    // Mode 0 forces exactly 0; mode 1 forces exactly +/-127 with the sign
    // from h_d2d[15] (1 -> -127). Both whatever the incoming weight.
    int pos = 0, neg = 0;
    for (int a = 0; a < kCells; ++a) {
        const auto addr = static_cast<std::uint16_t>(a);
        if (!is_stuck(addr, 0u, 32768u)) continue;
        const std::uint16_t hd = cell_hash16(addr, SEED_D2D);
        const int rail = (hd & 0x8000u) ? -127 : 127;
        for (int w : {-128, -1, 0, 1, 127}) {
            const auto w8 = static_cast<std::int8_t>(w);
            CHECK_EQ(int{apply_stuck(w8, addr, 0u, 32768u)}, 0);
            CHECK_EQ(int{apply_stuck(w8, addr, 0u, 32768u | (1u << 16))}, rail);
        }
        (rail > 0 ? pos : neg) += 1;
    }
    // Rail signs are balanced (8186 stuck cells: 4088 / 4098 at seed 0).
    CHECK(pos > 3800 && neg > 3800);
}

TEST(stuck_rate_bits_above_16_ignored) {
    // STUCK_RATE is a 17-bit register: [15:0] rate, [16] mode. Bits above
    // are not part of the datapath.
    for (int a = 0; a < kCells; a += 13) {
        const auto addr = static_cast<std::uint16_t>(a);
        CHECK_EQ(apply_stuck(9, addr, 0u, 6554u), apply_stuck(9, addr, 0u, 6554u | 0xFFFE0000u));
    }
}

TEST(stuck_and_d2d_hashes_are_independent) {
    // The two hashes use different salts. If they were the same hash, the
    // stuck cells would all be the ones with small D2D offsets.
    double sum = 0;
    int n = 0;
    for (int a = 0; a < kCells; ++a) {
        const auto addr = static_cast<std::uint16_t>(a);
        if (!is_stuck(addr, 0u, 6554u)) continue;
        sum += ih9_of(addr, 0u);
        ++n;
    }
    CHECK(n > 1500);
    CHECK(std::fabs(sum / n) < 10.0);       // stuck cells' D2D offsets average ~0
}

// ===========================================================================
// ModelExact integration: the three stages chained in write_weight()
// ===========================================================================

TEST(model_write_chain_order_and_gating) {
    // Each stage is gated by its own NOISE_EN bit, in the order
    // quant -> D2D -> stuck, and the model's stored weight equals the free
    // functions chained by hand.
    std::mt19937 g(0xC1C1);
    for (std::uint8_t en = 0; en < 16; ++en) {
        if (en & NOISE_READ) continue;                 // read-time, not here
        Config cfg = cfg_with(en);
        cfg.quant_levels = 5;
        cfg.quant_mult   = quant_mult_for(5);
        cfg.d2d_sigma    = 40;
        cfg.noise_seed   = 0x00C0FFEEu;
        cfg.stuck_rate   = 6554u | (1u << 16);         // 10 %, rail
        ModelExact m;
        m.set_config(cfg);
        for (int k = 0; k < 400; ++k) {
            const int r = static_cast<int>(g() % TILE_ROWS);
            const int c = static_cast<int>(g() % TILE_COLS);
            const auto w0 = static_cast<std::int8_t>(static_cast<int>(g() & 0xFFu) - 128);
            m.write_weight(r, c, w0);
            std::int8_t w = w0;
            const auto addr = cell_addr(r, c);
            if (en & NOISE_QUANT) w = quantize_weight(w, 5, quant_mult_for(5));
            if (en & NOISE_D2D)   w = d2d_offset(w, addr, cfg.noise_seed, cfg.d2d_sigma);
            if (en & NOISE_STUCK) w = apply_stuck(w, addr, cfg.noise_seed, cfg.stuck_rate);
            CHECK_EQ(int{m.stored_weight(r, c)}, int{w});
        }
    }
}

TEST(model_bypass_exact_with_lossy_registers) {
    // The L0 gate: with every enable clear, stored == written, even with
    // destructive values sitting in every register.
    ModelExact m;
    Config cfg;
    cfg.noise_en     = NOISE_NONE;
    cfg.quant_levels = 2;
    cfg.quant_mult   = quant_mult_for(2);
    cfg.d2d_sigma    = 255;
    cfg.noise_seed   = 0xDEADBEEFu;
    cfg.stuck_rate   = 65535u | (1u << 16);
    m.set_config(cfg);
    for (int c = 0; c < TILE_COLS; c += 9)
        for (int w = -128; w <= 127; ++w) {
            m.write_weight(c, c, static_cast<std::int8_t>(w));
            CHECK_EQ(int{m.stored_weight(c, c)}, w);
        }
}

TEST(model_stuck_overrides_quant_and_d2d) {
    // A stuck cell ignores everything written to it, whatever the earlier
    // stages produced. Cell (43,0) is stuck at seed 0, rate 1 %, rail +127.
    ModelExact m;
    Config cfg = cfg_with(NOISE_QUANT | NOISE_D2D | NOISE_STUCK);
    cfg.quant_levels = 2;
    cfg.quant_mult   = quant_mult_for(2);
    cfg.d2d_sigma    = 255;
    cfg.stuck_rate   = 655u | (1u << 16);
    m.set_config(cfg);
    for (int w = -128; w <= 127; ++w) {
        m.write_weight(43, 0, static_cast<std::int8_t>(w));
        CHECK_EQ(int{m.stored_weight(43, 0)}, 127);
    }
    cfg.stuck_rate = 655u;                               // zero mode
    m.set_config(cfg);
    m.write_weight(43, 0, -100);
    CHECK_EQ(int{m.stored_weight(43, 0)}, 0);
}

TEST(model_d2d_stuck_applied_at_write_time_only) {
    // Changing the seed, sigma or rate after a write does not touch cells
    // already stored; the RTL has no path to do so. The host must reload.
    ModelExact m;
    Config cfg = cfg_with(NOISE_D2D | NOISE_STUCK);
    cfg.d2d_sigma  = 255;
    cfg.stuck_rate = 6554u;
    cfg.noise_seed = 1u;
    m.set_config(cfg);
    for (int r = 0; r < TILE_ROWS; ++r) m.write_weight(r, 3, 60);
    std::vector<int> before;
    for (int r = 0; r < TILE_ROWS; ++r) before.push_back(m.stored_weight(r, 3));

    cfg.noise_seed = 2u;
    cfg.d2d_sigma  = 7;
    cfg.stuck_rate = 0u;
    m.set_config(cfg);
    for (int r = 0; r < TILE_ROWS; ++r) CHECK_EQ(int{m.stored_weight(r, 3)}, before[static_cast<std::size_t>(r)]);

    // Rewriting with the new config does change them.
    int changed = 0;
    for (int r = 0; r < TILE_ROWS; ++r) {
        m.write_weight(r, 3, 60);
        changed += int{m.stored_weight(r, 3)} != before[static_cast<std::size_t>(r)];
    }
    CHECK(changed > TILE_ROWS / 2);
}

TEST(model_d2d_stuck_compute_matches_ideal_on_stored_weights) {
    // compute() now accepts D2D and stuck-at. Both are baked into the stored
    // weights, so the result must equal model_ideal run on those weights:
    // the model side of the L1 "D2D only" / "stuck only" rows and of the
    // write-time part of L2.
    std::mt19937 g(0xD2D5);
    const std::uint8_t configs[] = {NOISE_D2D, NOISE_STUCK,
                                    static_cast<std::uint8_t>(NOISE_QUANT | NOISE_D2D),
                                    static_cast<std::uint8_t>(NOISE_QUANT | NOISE_D2D | NOISE_STUCK)};
    for (std::uint8_t en : configs) {
        ModelExact m;
        Config cfg = cfg_with(en);
        cfg.quant_levels = 8;
        cfg.quant_mult   = quant_mult_for(8);
        cfg.d2d_sigma    = 18;                       // set A
        cfg.stuck_rate   = 655u | (1u << 16);        // 1 %, rail
        cfg.noise_seed   = 0x2026u;
        m.set_config(cfg);
        std::vector<std::int8_t>  w(static_cast<std::size_t>(kCells));
        std::vector<std::uint8_t> a(static_cast<std::size_t>(TILE_ROWS));
        for (int r = 0; r < TILE_ROWS; ++r)
            for (int c = 0; c < TILE_COLS; ++c) {
                m.write_weight(r, c, static_cast<std::int8_t>(static_cast<int>(g() & 0xFFu) - 128));
                w[static_cast<std::size_t>(r * TILE_COLS + c)] = m.stored_weight(r, c);
            }
        for (int r = 0; r < TILE_ROWS; ++r) {
            a[static_cast<std::size_t>(r)] = static_cast<std::uint8_t>(g() & 0xFFu);
            m.write_activation(r, a[static_cast<std::size_t>(r)]);
        }
        const auto exact = m.compute();
        const auto ideal = ideal_mvm(w, a, TILE_ROWS, TILE_COLS);
        CHECK_EQ(exact.size(), ideal.size());
        for (std::size_t c = 0; c < exact.size(); ++c)
            CHECK_EQ(std::int64_t{exact[c]}, ideal[c]);
    }
}
