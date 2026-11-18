// Unit tests for the read-time non-idealities (fecim/read_path.h) and their
// use in ModelExact::compute(): read noise, IR drop, ADC quantization.
//
// Pinned values below were computed by the independent Python reference in
// tb/golden/gen_read_path_vectors.py (plain ints, Python's floor `>>`);
// test_read_path_vectors.cpp replays its full output.

#include <cmath>     // tests only: statistics, never in the model
#include <cstdint>
#include <limits>
#include <random>
#include <vector>

#include "fecim/model_exact.h"
#include "fecim/params.h"
#include "fecim/primitives.h"
#include "fecim/read_path.h"
#include "test_harness.h"

using namespace fecim;

namespace {

// Mean and standard deviation of n consecutive samples of one lane.
struct Moments { double mean, sd; };
Moments lane_moments(std::uint32_t seed, int lane, std::uint8_t sigma, int n) {
    std::uint32_t s = lane_seed(seed, lane);
    double sum = 0, sum2 = 0;
    for (int i = 0; i < n; ++i) {
        s = lfsr_step(s);
        const int q = read_noise_q(s, sigma);
        sum += q;
        sum2 += double(q) * q;
    }
    const double mean = sum / n;
    return {mean, std::sqrt(sum2 / n - mean * mean)};
}

// A model with deterministic, non-trivial weights and activations.
void fill(ModelExact& m, std::uint32_t salt) {
    std::mt19937 g(salt);
    for (int c = 0; c < TILE_COLS; ++c)
        for (int r = 0; r < TILE_ROWS; ++r)
            m.write_weight(r, c, static_cast<std::int8_t>(static_cast<int>(g() & 0xFFu) - 128));
    for (int r = 0; r < TILE_ROWS; ++r)
        m.write_activation(r, static_cast<std::uint8_t>(g() & 0xFFu));
}

Config read_cfg(std::uint8_t sigma, std::uint32_t seed = 0) {
    Config c;
    c.noise_en   = NOISE_READ;
    c.read_sigma = sigma;
    c.noise_seed = seed;
    return c;
}

} // namespace

// ===========================================================================
// read_noise_q
// ===========================================================================

TEST(read_noise_golden_values) {
    // Python reference. 0x00000001 has ih_sum = 1 -> ih9 = -255, and at
    // sigma 255 scaled = -128: the one input the +/-127 clamp changes.
    CHECK_EQ(int{read_noise_q(0x0000'0001u, 255)}, -127);
    CHECK_EQ(int{read_noise_q(0x0000'0001u, 128)},  -64);
    CHECK_EQ(int{read_noise_q(0xFFFF'FFFFu, 255)},  127);
    CHECK_EQ(int{read_noise_q(0x7F7F'7F80u, 255)},   -1);   // ih_sum 509: -1 >>> 1 = -1 (floor)
    CHECK_EQ(int{read_noise_q(0x1234'5678u, 200)},  -46);
    CHECK_EQ(int{read_noise_q(0xDEAD'BEEFu,  64)},   19);
    CHECK_EQ(int{read_noise_q(LFSR_TAPS,    255)},  -87);
}

TEST(read_noise_sigma_zero_is_zero) {
    std::mt19937 g(1);
    for (int i = 0; i < 20000; ++i) CHECK_EQ(int{read_noise_q(static_cast<std::uint32_t>(g()), 0)}, 0);
}

TEST(read_noise_stays_inside_clamp) {
    // The 9-bit guarantee for w_n = w + noise depends on |noise| <= 127.
    std::mt19937 g(2);
    for (int i = 0; i < 200000; ++i) {
        const int q = read_noise_q(static_cast<std::uint32_t>(g()), static_cast<std::uint8_t>(g() & 0xFFu));
        CHECK(q >= -NOISE_CLAMP && q <= NOISE_CLAMP);
    }
}

TEST(read_noise_sd_matches_spec_scale) {
    // Noise sigma = 0.144 x READ_SIGMA LSB (mac-array-spec §5.3,
    // cell-physics-derivation §4.3). Measured over 200,000 consecutive
    // samples of one lane; within 2 %.
    for (std::uint8_t sigma : {std::uint8_t{64}, std::uint8_t{128}, std::uint8_t{255}}) {
        const Moments mo = lane_moments(0x1234u, 7, sigma, 200000);
        const double target = 0.144 * sigma;
        CHECK(std::fabs(mo.sd - target) < 0.02 * target);
    }
}

TEST(read_noise_mean_bias_is_minus_half_lsb_by_spec) {
    // NOT zero-mean, by construction of the spec's arithmetic: both shifts
    // (ih_centered >>> 1 and the scale >>> 9) round toward minus infinity,
    // which offsets every sample by about -0.5 LSB at every sigma. At small
    // sigma the offset dominates: at READ_SIGMA = 1 the sample is 0 or -1
    // with equal odds (sd 0.5 against a target of 0.14).
    //
    // This test pins the behaviour the RTL is specified to have, so the
    // finding stays visible. If the team switches both lanes to
    // round-to-nearest, this is the test that must change.
    for (std::uint8_t sigma : {std::uint8_t{1}, std::uint8_t{12}, std::uint8_t{64}, std::uint8_t{255}}) {
        const Moments mo = lane_moments(0x1234u, 7, sigma, 200000);
        CHECK(mo.mean > -0.6 && mo.mean < -0.35);
    }
    const Moments one = lane_moments(0x1234u, 7, 1, 200000);
    CHECK(std::fabs(one.sd - 0.5) < 0.01);
}

TEST(read_noise_lanes_decorrelated) {
    // mac-array-spec §5.1 / §12 test 5: each lane's LFSR is seeded with its
    // own salt, so the sample sequences of different lanes are uncorrelated.
    const int n = 50000;
    for (auto [la, lb] : {std::pair{0, 1}, std::pair{0, 63}, std::pair{5, 6}, std::pair{31, 32}}) {
        std::uint32_t sa = lane_seed(0, la), sb = lane_seed(0, lb);
        double xa = 0, xb = 0, xab = 0, xaa = 0, xbb = 0;
        for (int i = 0; i < n; ++i) {
            sa = lfsr_step(sa);
            sb = lfsr_step(sb);
            const double a = read_noise_q(sa, 255), b = read_noise_q(sb, 255);
            xa += a; xb += b; xab += a * b; xaa += a * a; xbb += b * b;
        }
        const double cov = xab / n - (xa / n) * (xb / n);
        const double r = cov / std::sqrt((xaa / n - (xa / n) * (xa / n)) * (xbb / n - (xb / n) * (xb / n)));
        CHECK(std::fabs(r) < 0.03);
    }
}

TEST(read_noise_lane_sequence_from_seed) {
    // The alignment convention (read_path.h): the first accumulate after a
    // reseed uses lfsr_step(seed_eff), never seed_eff itself.
    CHECK_EQ(lane_seed(0, 0), 0x0000'0001u);              // lane 0, seed 0: the | 1 guard
    std::uint32_t s = lane_seed(0, 5);
    CHECK_EQ(s, 0x1715'6075u);
    const std::uint32_t expect_state[] = {0x8BAA'B039u, 0xC5F5'581Fu, 0xE2DA'AC0Cu};
    const int expect_q[] = {7, 12, 29};
    for (int i = 0; i < 3; ++i) {
        s = lfsr_step(s);
        CHECK_EQ(s, expect_state[i]);
        CHECK_EQ(int{read_noise_q(s, 255)}, expect_q[i]);
    }
}

// ===========================================================================
// ir_attenuate
// ===========================================================================

TEST(ir_golden_values) {
    CHECK_EQ(int{ir_attenuate(255, 255)}, 254);
    CHECK_EQ(int{ir_attenuate(200, 230)}, 179);
    CHECK_EQ(int{ir_attenuate(  1, 255)},   0);
    CHECK_EQ(int{ir_attenuate(128, 128)},  64);
    CHECK_EQ(int{ir_attenuate(255,   0)},   0);
}

TEST(ir_properties) {
    // Never amplifies; coefficient 255 is NOT the identity (255/256 floors
    // every activation from 1 upward down by one step's worth); monotone.
    for (int a = 0; a < 256; ++a) {
        const auto a8 = static_cast<std::uint8_t>(a);
        CHECK(int{ir_attenuate(a8, 255)} == (a * 255) / 256);
        CHECK(int{ir_attenuate(a8, 255)} <= a);
        if (a > 0) CHECK(int{ir_attenuate(a8, 255)} < a);
        for (int k = 1; k < 256; ++k)
            CHECK(ir_attenuate(a8, static_cast<std::uint8_t>(k)) >=
                  ir_attenuate(a8, static_cast<std::uint8_t>(k - 1)));
    }
}

// ===========================================================================
// adc_quantize
// ===========================================================================

TEST(adc_golden_values) {
    // ACC_USED_W = 24. bits 20 -> shift 4, step 16, half 8.
    CHECK_EQ(adc_quantize(1000, 20),  1008);
    CHECK_EQ(adc_quantize(-1000, 20), -992);
    CHECK_EQ(adc_quantize(8, 20),       16);     // positive half rounds up
    CHECK_EQ(adc_quantize(-8, 20),       0);     // negative half rounds up too: (x + half) >> sh
    CHECK_EQ(adc_quantize(7, 20),        0);
    CHECK_EQ(adc_quantize(-9, 20),     -16);
    CHECK_EQ(adc_quantize( 8290560, 8),  8323072);   // +/-ACC_MAX_MAG at 8 bits
    CHECK_EQ(adc_quantize(-8290560, 8), -8323072);
    CHECK_EQ(adc_quantize(123456, 0), 0);
    CHECK_EQ(adc_quantize(-1, 4), 0);
}

TEST(adc_no_op_at_and_above_used_width) {
    // ADC_BITS >= ACC_USED_W: exact pass-through for every value, including
    // the extremes of the 32-bit container.
    std::mt19937 g(3);
    for (int bits = ACC_USED_W; bits < 32; ++bits) {
        for (int i = 0; i < 2000; ++i) {
            const auto v = static_cast<std::int32_t>(g());
            CHECK_EQ(adc_quantize(v, static_cast<std::uint8_t>(bits)), v);
        }
        CHECK_EQ(adc_quantize(std::numeric_limits<std::int32_t>::max(), static_cast<std::uint8_t>(bits)),
                 std::numeric_limits<std::int32_t>::max());
    }
}

TEST(adc_uses_only_five_bits_of_the_register) {
    // The RTL port is adc_bits[4:0]: 32 arrives as 0, 40 as 8.
    for (std::int32_t v : {5, -77, 123456, -8290560}) {
        CHECK_EQ(adc_quantize(v, 32), adc_quantize(v, 0));
        CHECK_EQ(adc_quantize(v, 40), adc_quantize(v, 8));
        CHECK_EQ(adc_quantize(v, 56), adc_quantize(v, 24));
    }
}

TEST(adc_wraps_like_the_32_bit_rtl) {
    // Outside the physical range the RTL's 32-bit adder wraps; pinned so the
    // model and RTL agree for any input (the driver never produces these).
    CHECK_EQ(adc_quantize(std::numeric_limits<std::int32_t>::max(), 4),
             std::numeric_limits<std::int32_t>::min());
}

TEST(adc_rounding_error_bounded_and_unbiased) {
    // |error| <= half a step, and over the physical range the mean error is
    // ~0 (truncation would give -half a step). Error is (out - in).
    std::mt19937 g(4);
    for (int bits = 4; bits < ACC_USED_W; ++bits) {
        const int sh = ACC_USED_W - bits;
        const double half = std::ldexp(1.0, sh - 1);
        double sum = 0;
        const int n = 20000;
        for (int i = 0; i < n; ++i) {
            const auto v = static_cast<std::int32_t>(static_cast<std::int64_t>(g() % (2u * 8290560u + 1u)) - 8290560);
            const std::int32_t out = adc_quantize(v, static_cast<std::uint8_t>(bits));
            const double err = double(out) - double(v);
            CHECK(err > -half - 0.5 && err <= half);
            CHECK(out % (std::int32_t{1} << sh) == 0);
            sum += err;
        }
        CHECK(std::fabs(sum / n) < 0.05 * half + 1.0);
    }
}

// ===========================================================================
// ModelExact::compute(): the three effects in place, and the LFSR rules
// ===========================================================================

TEST(model_read_noise_single_cell_by_hand) {
    // 1 row, 1 column: y = (w + q) * a, q from lane 0's first step.
    ModelExact m;
    Config c = read_cfg(255, 0xABCDu);
    c.active_rows = 1;
    c.active_cols = 1;
    m.set_config(c);
    m.reseed();
    m.write_weight(0, 0, 100);
    m.write_activation(0, 200);
    const int q = read_noise_q(lfsr_step(lane_seed(0xABCDu, 0)), 255);
    CHECK_EQ(m.compute()[0], std::int32_t{(100 + q) * 200});
}

TEST(model_read_noise_pass_major_order) {
    // Column 64 is lane 0 on pass 1. With 2 rows and 128 columns, lane 0
    // steps on rows 0,1 of pass 0 (column 0) then rows 0,1 of pass 1
    // (column 64): column 64 uses lane 0's 3rd and 4th states.
    ModelExact m;
    Config c = read_cfg(255, 77u);
    c.active_rows = 2;
    m.set_config(c);
    m.reseed();
    for (int r = 0; r < 2; ++r) {
        m.write_weight(r, 0, 10);
        m.write_weight(r, 64, -20);
        m.write_activation(r, static_cast<std::uint8_t>(100 + r));
    }
    std::uint32_t s = lane_seed(77u, 0);
    int q[4];
    for (int i = 0; i < 4; ++i) { s = lfsr_step(s); q[i] = read_noise_q(s, 255); }
    const auto y = m.compute();
    CHECK_EQ(y[0],  std::int32_t{(10 + q[0]) * 100 + (10 + q[1]) * 101});
    CHECK_EQ(y[64], std::int32_t{(-20 + q[2]) * 100 + (-20 + q[3]) * 101});
}

TEST(model_read_noise_reproducible_only_with_reseed) {
    ModelExact m;
    fill(m, 11);
    m.set_config(read_cfg(128, 5u));
    m.reseed();
    const auto a = m.compute();
    const auto b = m.compute();            // no reseed: the chip has moved on
    m.reseed();
    const auto c = m.compute();
    CHECK(a != b);
    CHECK(a == c);
}

TEST(model_lfsr_advances_when_read_noise_disabled) {
    // The RTL's LFSR has no enable: a clean compute still steps it.
    ModelExact m1, m2;
    fill(m1, 12);
    fill(m2, 12);
    m1.set_config(Config{});
    m1.compute();                          // clean, but every lane steps 256 times
    m1.set_config(read_cfg(128));
    m2.set_config(read_cfg(128));          // both models start from lane_seed(0, l)
    CHECK(m1.compute() != m2.compute());
}

TEST(model_lfsr_advances_in_lanes_beyond_active_cols) {
    // Every lane gets acc_en, whatever active_cols is: a 1-column compute and
    // a 64-column compute (both one pass) leave identical LFSR states.
    ModelExact m1, m2;
    fill(m1, 13);
    fill(m2, 13);
    Config narrow, wide;
    narrow.active_cols = 1;
    wide.active_cols   = 64;
    m1.set_config(narrow);
    m2.set_config(wide);
    m1.compute();
    m2.compute();
    m1.set_config(read_cfg(255));
    m2.set_config(read_cfg(255));
    CHECK(m1.compute() == m2.compute());
}

TEST(model_seed_change_does_not_reseed) {
    // set_config(noise_seed = X) alone does not reload the LFSRs; only
    // reseed() (CTRL[2]) does, as in the RTL.
    ModelExact m1, m2;
    fill(m1, 14);
    fill(m2, 14);
    m1.set_config(read_cfg(200, 0xBEEFu));      // seed changed, no reseed
    m2.set_config(read_cfg(200, 0u));
    CHECK(m1.compute() == m2.compute());
    m1.reseed();
    m2.reseed();
    CHECK(m1.compute() != m2.compute());
}

TEST(model_read_sigma_zero_equals_clean) {
    ModelExact m;
    fill(m, 15);
    m.set_config(Config{});
    const auto clean = m.compute();
    m.set_config(read_cfg(0));
    CHECK(m.compute() == clean);
}

TEST(model_ir_applied_to_activation_before_broadcast) {
    // a_eff[r] = (act[r] * atten[r]) >> 8, then the ordinary MAC. Checked
    // against the free function for columns in both passes.
    ModelExact m;
    fill(m, 16);
    std::vector<std::uint8_t> act(static_cast<std::size_t>(TILE_ROWS));
    for (int r = 0; r < TILE_ROWS; ++r) {
        act[static_cast<std::size_t>(r)] = static_cast<std::uint8_t>(255 - r);
        m.write_activation(r, act[static_cast<std::size_t>(r)]);
        m.write_atten(r, static_cast<std::uint8_t>(255 - 2 * r));
    }
    Config c;
    c.noise_en = NOISE_IR;
    m.set_config(c);
    const auto y = m.compute();
    for (int col : {0, 1, 63, 64, 127}) {
        std::int32_t acc = 0;
        for (int r = 0; r < TILE_ROWS; ++r)
            acc += std::int32_t{m.stored_weight(r, col)} *
                   std::int32_t{ir_attenuate(act[static_cast<std::size_t>(r)],
                                             static_cast<std::uint8_t>(255 - 2 * r))};
        CHECK_EQ(y[static_cast<std::size_t>(col)], acc);
    }
    // IR disabled: the coefficients are ignored entirely.
    m.set_config(Config{});
    const auto clean = m.compute();
    for (int col : {0, 64}) {
        std::int32_t acc = 0;
        for (int r = 0; r < TILE_ROWS; ++r)
            acc += std::int32_t{m.stored_weight(r, col)} * std::int32_t{act[static_cast<std::size_t>(r)]};
        CHECK_EQ(clean[static_cast<std::size_t>(col)], acc);
    }
}

TEST(model_adc_applied_per_column_at_drain) {
    ModelExact m;
    fill(m, 17);
    m.set_config(Config{});
    const auto clean = m.compute();
    for (int bits : {4, 8, 12, 23, 24, 31}) {
        Config c;
        c.noise_en = NOISE_ADC;
        c.adc_bits = bits;
        m.set_config(c);
        const auto y = m.compute();
        for (std::size_t i = 0; i < y.size(); ++i)
            CHECK_EQ(y[i], adc_quantize(clean[i], static_cast<std::uint8_t>(bits)));
    }
    // ADC disabled: adc_bits is ignored.
    Config off;
    off.adc_bits = 4;
    m.set_config(off);
    CHECK(m.compute() == clean);
}

TEST(model_worst_case_magnitude_no_overflow) {
    // Every weight -128, every activation 255, maximum read noise. Note
    // w_n can reach -128 + -127 = -255, one step past the +/-254 that
    // ACC_MAX_MAG (254 * 255 * rows) assumes; the true bound 255 * 255 * 128 =
    // 8,323,200 still fits ACC_USED_W = 24 bits (mac-array-spec §12 test 7).
    ModelExact m;
    for (int c = 0; c < TILE_COLS; ++c)
        for (int r = 0; r < TILE_ROWS; ++r) m.write_weight(r, c, -128);
    for (int r = 0; r < TILE_ROWS; ++r) m.write_activation(r, 255);
    m.set_config(read_cfg(255, 9u));
    m.reseed();
    const auto y = m.compute();
    // Same sequence, summed in 64 bits.
    std::vector<std::uint32_t> s(static_cast<std::size_t>(NUM_LANES));
    for (int l = 0; l < NUM_LANES; ++l) s[static_cast<std::size_t>(l)] = lane_seed(9u, l);
    for (int p = 0; p < 2; ++p) {
        std::vector<std::int64_t> acc(static_cast<std::size_t>(NUM_LANES), 0);
        for (int r = 0; r < TILE_ROWS; ++r)
            for (int l = 0; l < NUM_LANES; ++l) {
                auto& st = s[static_cast<std::size_t>(l)];
                st = lfsr_step(st);
                acc[static_cast<std::size_t>(l)] += std::int64_t{-128 + read_noise_q(st, 255)} * 255;
            }
        for (int l = 0; l < NUM_LANES; ++l) {
            const auto v = acc[static_cast<std::size_t>(l)];
            CHECK(v >= -(std::int64_t{255} * 255 * TILE_ROWS) && v <= ACC_MAX_MAG);
            CHECK(v > -(std::int64_t{1} << (ACC_USED_W - 1)));
            CHECK_EQ(std::int64_t{y[static_cast<std::size_t>(p * NUM_LANES + l)]}, v);
        }
    }
}
