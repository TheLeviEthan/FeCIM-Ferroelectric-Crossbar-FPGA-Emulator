// Unit tests for the write-path stages (fecim/write_path.h).
//
// Stage 1, quantization. Two groups of N values are tested:
//
//   Spec set   N in {2, 3, 5, 8, 255}   (week-1 board; "Quant only" row of
//              the model-verification-design §6.1 matrix). 3 and 5 exercise
//              the reciprocal (QUANT_MULT) path that a power of two can let
//              slip through; 255 is the register reset value.
//
//   Literature N values from the parameter-set work
//              (docs/plans and specs/06-physics-calibration/literature-survey.md):
//                * 8     Set A, Chatterjee et al., IEEE TED 69(9), 2022,
//                        §III-C / Fig. 15d: 3 bits at P(error) < 1 %.
//                        sw/fecim/parameter_sets/dualport_fefet_mlc.yaml.
//                * 9-15  Set A's 8 is a lower bound: 16 fails, so the true
//                        maximum lies in 8..15 (survey §2.2). This is the
//                        range a QUANT_LEVELS sweep for Set A has to cover,
//                        and every value but 8 is a non-power of two.
//                * 16    the point Chatterjee et al. test and reject.
//                * 4     measured arrays: Soliman et al., Nat. Commun. 14,
//                        6348 (2023), and Duan et al., IEDM 2024 (survey
//                        §2.4, Levels table). Set B candidate.
//
// These mirror write-path-spec §9 tests 3, 4, 5 and 12, so the RTL
// testbench can pin the SAME vectors. The golden table is the shared
// contract; keep it in sync with whatever tb/ uses.

#include <cstdint>
#include <map>
#include <random>
#include <set>
#include <stdexcept>
#include <vector>

#include "fecim/model_exact.h"
#include "fecim/model_ideal.h"
#include "fecim/params.h"
#include "fecim/write_path.h"
#include "test_harness.h"

using namespace fecim;

namespace {

// Spec set: the five N values every quant test sweeps.
constexpr int kQuantN[] = {2, 3, 5, 8, 255};

// Literature-derived N values (sources in the header comment).
constexpr int kSetA_N          = 8;    // Chatterjee et al. 2022, Fig. 15d
constexpr int kSetA_N_Rejected = 16;   // same figure: fails P(error) < 1 %
constexpr int kMeasuredArray_N = 4;    // Soliman et al. 2023; Duan et al. 2024

// ---------------------------------------------------------------------------
// Golden vectors, computed independently in Python from the write-path-spec
// §4.1 definition (NOT from this C++ code -- an independent implementation
// is the point):
//
//     def qm(n): return (255*256 + (n-1)//2) // (n-1)
//     def q(w, n): return (((((w+128)*n) >> 8) * qm(n)) >> 8 & 0xFF) - 128
//
// The inputs straddle the N = 3 bin edges (-43|-42, 42|43) and the int8
// extremes. Regenerate this table if the spec formula changes.
constexpr std::int8_t kGoldenIn[] = {
    -128, -127, -86, -43, -42, -1, 0, 1, 42, 43, 85, 86, 126, 127};
constexpr int kGoldenLen = sizeof(kGoldenIn) / sizeof(kGoldenIn[0]);

struct GoldenRow {
    int           n;
    std::uint16_t quant_mult;
    std::int8_t   out[kGoldenLen];
};

constexpr GoldenRow kGolden[] = {
    {  2, 65280, {-128, -128, -128, -128, -128, -128,  127,  127,  127,  127,  127,  127,  127,  127}},
    {  3, 32640, {-128, -128, -128, -128,   -1,   -1,   -1,   -1,   -1,  127,  127,  127,  127,  127}},
    {  4, 21760, {-128, -128, -128,  -43,  -43,  -43,   42,   42,   42,   42,  127,  127,  127,  127}},
    {  5, 16320, {-128, -128, -128,  -65,  -65,   -1,   -1,   -1,   63,   63,  127,  127,  127,  127}},
    {  8,  9326, {-128, -128,  -92,  -56,  -56,  -19,   17,   17,   54,   54,   90,   90,  127,  127}},
    {255,   257, {-128, -128,  -87,  -44,  -43,   -2,   -1,    0,   41,   42,   84,   85,  125,  126}},
};

// Every distinct output level, in order, for the literature N values
// (same Python reference). These are the conductance states FeCIM
// programs for each parameter set.
const std::map<int, std::vector<int>> kLevels = {
    { 4, {-128, -43, 42, 127}},
    { 8, {-128, -92, -56, -19, 17, 54, 90, 127}},
    { 9, {-128, -97, -65, -33, -1, 31, 63, 95, 127}},
    {10, {-128, -100, -72, -44, -15, 13, 41, 70, 98, 126}},
    {11, {-128, -103, -77, -52, -26, -1, 25, 50, 76, 101, 127}},
    {12, {-128, -105, -82, -59, -36, -13, 11, 34, 57, 80, 103, 127}},
    {13, {-128, -107, -86, -65, -43, -22, -1, 20, 42, 63, 84, 105, 127}},
    {14, {-128, -109, -89, -70, -50, -30, -11, 9, 28, 48, 68, 87, 107, 127}},
    {15, {-128, -110, -92, -74, -56, -37, -19, -1, 17, 35, 54, 72, 90, 108, 127}},
    {16, {-128, -111, -94, -77, -60, -43, -26, -9, 8, 25, 42, 59, 76, 93, 110, 127}},
};

// Quantize with the consistent QUANT_MULT for N.
std::int8_t q(int w, int n) {
    return quantize_weight(static_cast<std::int8_t>(w),
                           static_cast<std::uint8_t>(n),
                           quant_mult_for(n));
}

// Sorted distinct outputs over the full int8 input range.
std::vector<int> levels_of(int n) {
    std::set<int> seen;
    for (int w = -128; w <= 127; ++w) seen.insert(q(w, n));
    return {seen.begin(), seen.end()};
}

// First input w at which the output changes level (the bin edges).
std::vector<int> bin_edges_of(int n) {
    std::vector<int> edges;
    for (int w = -127; w <= 127; ++w)
        if (q(w, n) != q(w - 1, n)) edges.push_back(w);
    return edges;
}

Config quant_config(int n) {
    Config cfg;
    cfg.noise_en     = NOISE_QUANT;
    cfg.quant_levels = n;
    cfg.quant_mult   = quant_mult_for(n);
    return cfg;
}

} // namespace

// ===========================================================================
// quant_mult_for
// ===========================================================================

TEST(quant_mult_values) {
    // round(65280 / (N - 1)).
    CHECK_EQ(quant_mult_for(2),   std::uint16_t{65280});
    CHECK_EQ(quant_mult_for(3),   std::uint16_t{32640});
    CHECK_EQ(quant_mult_for(4),   std::uint16_t{21760});
    CHECK_EQ(quant_mult_for(5),   std::uint16_t{16320});
    CHECK_EQ(quant_mult_for(8),   std::uint16_t{9326});   // 9325.71 rounds up
    CHECK_EQ(quant_mult_for(255), std::uint16_t{257});    // 257.007 rounds down

    // The register reset pair must be self-consistent (protocol.md §6).
    CHECK_EQ(quant_mult_for(QUANT_LEVELS_MAX), Config{}.quant_mult);

    // Out of range is a host error, not RTL behaviour, so it throws.
    CHECK_THROWS(quant_mult_for(1),   std::invalid_argument);
    CHECK_THROWS(quant_mult_for(0),   std::invalid_argument);
    CHECK_THROWS(quant_mult_for(256), std::invalid_argument);
}

TEST(quant_mult_matches_literature_survey_set_a) {
    // literature-survey.md §2.3 states the Set A register value by hand:
    // QUANT_LEVELS = 8 -> QUANT_MULT = round(255 * 256 / 7) = 9326.
    // If the survey and the model ever disagree, one of them is wrong.
    CHECK_EQ(quant_mult_for(kSetA_N), std::uint16_t{9326});
}

TEST(quant_mult_product_fits_rtl_width) {
    // write-path-spec §9 test 5: no overflow at N = 2 (largest QUANT_MULT),
    // correct behaviour at N = 255 (smallest). Every N, not a sample.
    for (int n = 2; n <= QUANT_LEVELS_MAX; ++n) {
        const std::uint32_t top =
            static_cast<std::uint32_t>(n - 1) * quant_mult_for(n);
        CHECK(top < (1u << 17));                       // fits RTL `scaled`
        CHECK((top >> 8) == 254u || (top >> 8) == 255u); // top code 254/255
    }
}

// ===========================================================================
// quantize_weight
// ===========================================================================

TEST(quant_golden_vectors) {
    // THE bit-exactness test: C++ against independently computed values.
    for (const auto& row : kGolden) {
        CHECK_EQ(quant_mult_for(row.n), row.quant_mult);   // table consistent
        for (int i = 0; i < kGoldenLen; ++i)
            CHECK_EQ(int{quantize_weight(kGoldenIn[i],
                                         static_cast<std::uint8_t>(row.n),
                                         row.quant_mult)},
                     int{row.out[i]});
    }
}

TEST(quant_exactly_n_levels) {
    // write-path-spec §9 test 4: exactly N distinct outputs. Exhaustive
    // over all 256 inputs, for EVERY legal N -- 254 x 256 calls is nothing,
    // and random sampling can miss a level that only a few inputs reach.
    for (int n = 2; n <= QUANT_LEVELS_MAX; ++n)
        CHECK_EQ(levels_of(n).size(), static_cast<std::size_t>(n));
}

TEST(quant_levels_evenly_spaced) {
    // "Correct spacing" half of §9 test 4: adjacent output levels differ by
    // floor or ceil of 255/(N-1), never more, never less. A wrong
    // reciprocal shows up as an uneven gap.
    for (int n = 2; n <= QUANT_LEVELS_MAX; ++n) {
        const auto lv = levels_of(n);
        const int lo = 255 / (n - 1);
        const int hi = (255 + n - 2) / (n - 1);
        for (std::size_t i = 1; i < lv.size(); ++i) {
            const int gap = lv[i] - lv[i - 1];
            CHECK(gap == lo || gap == hi);
        }
    }
}

TEST(quant_bins_equally_populated) {
    // The input side of the same property: each level is reached by
    // floor or ceil of 256/N inputs. A bad step 2 gives lopsided bins.
    for (int n = 2; n <= QUANT_LEVELS_MAX; ++n) {
        std::map<int, int> count;
        for (int w = -128; w <= 127; ++w) ++count[q(w, n)];
        const int lo = 256 / n;
        const int hi = (256 + n - 1) / n;
        for (const auto& kv : count)
            CHECK(kv.second == lo || kv.second == hi);
    }
}

TEST(quant_endpoints) {
    // Bottom is always -128. Top is +127 except where QUANT_MULT rounds
    // down; N = 255 is the case in the spec set.
    for (int n : kQuantN) CHECK_EQ(int{q(-128, n)}, -128);
    for (int n : {2, 3, 4, 5, 8}) CHECK_EQ(int{q(127, n)}, 127);
    CHECK_EQ(int{q(127, 255)}, 126);
    CHECK_EQ(int{q(127, 10)},  126);   // another round-down case
}

TEST(quant_monotonic) {
    // A quantizer must never reorder weights. A wrap or sign error in
    // step 1 or 4 shows up here as a large negative jump.
    for (int n = 2; n <= QUANT_LEVELS_MAX; ++n)
        for (int w = -128; w < 127; ++w)
            CHECK(q(w, n) <= q(w + 1, n));
}

TEST(quant_bin_edges_non_pow2) {
    // The reciprocal path, tested at the bin boundaries where an
    // off-by-one in step 2 or 3 would show.
    CHECK(bin_edges_of(3) == (std::vector<int>{-42, 43}));
    CHECK(bin_edges_of(5) == (std::vector<int>{-76, -25, 26, 77}));
    // Contrast: power-of-two edges are multiples of 256/N. This is why a
    // power of two cannot catch a reciprocal bug.
    CHECK(bin_edges_of(8) == (std::vector<int>{-96, -64, -32, 0, 32, 64, 96}));
    CHECK(bin_edges_of(4) == (std::vector<int>{-64, 0, 64}));
}

TEST(quant_idempotent_below_255) {
    // Every output level is a fixed point for N in the spec and literature
    // sets below 255. N = 255 is NOT idempotent (every w > -128 maps to
    // w - 1 and keeps drifting); pinned so the property stays documented.
    for (int n : {2, 3, 4, 5, 8, 16})
        for (int w = -128; w <= 127; ++w)
            CHECK_EQ(int{q(q(w, n), n)}, int{q(w, n)});
    CHECK(q(q(0, 255), 255) != q(0, 255));
}

TEST(quant_zero_weight_mapping) {
    // Documents a property of the spec formula, not a model choice: there
    // is no exact zero level. Odd N put the middle level at -1; even N
    // have no middle level at all. (Raised with the team; if the spec
    // changes, these expectations change with it.)
    CHECK_EQ(int{q(0, 3)},   -1);
    CHECK_EQ(int{q(0, 5)},   -1);
    CHECK_EQ(int{q(0, 255)}, -1);
    CHECK_EQ(int{q(0, 2)},  127);
    CHECK_EQ(int{q(0, 4)},   42);
    CHECK_EQ(int{q(0, 8)},   17);
}

TEST(quant_mismatched_pair_not_rejected) {
    // protocol.md §6.2 says the host always writes QUANT_LEVELS and
    // QUANT_MULT together, but the RTL cannot enforce it, so the model
    // must compute what the RTL would for a mismatched pair rather than
    // throw. Values from the same Python reference with the pair held
    // fixed. Result is independent of the RTL product width (>= 16 bits),
    // because only product bits [15:8] survive step 4.
    auto qp = [](int w, int n, int m) {
        return int{quantize_weight(static_cast<std::int8_t>(w),
                                   static_cast<std::uint8_t>(n),
                                   static_cast<std::uint16_t>(m))};
    };
    // Reciprocal too small: levels bunch at the bottom of the range.
    CHECK_EQ(qp(-128, 3, 257), -128);
    CHECK_EQ(qp(   0, 3, 257), -127);
    CHECK_EQ(qp( 127, 3, 257), -126);
    // Reciprocal far too large: the code wraps modulo 256.
    CHECK_EQ(qp(-128, 255, 65280), -128);
    CHECK_EQ(qp(  -1, 255, 65280),    2);
    CHECK_EQ(qp(   0, 255, 65280),    1);
    CHECK_EQ(qp( 127, 255, 65280), -126);
}

// ===========================================================================
// Literature-derived configurations
// ===========================================================================

TEST(quant_literature_levels) {
    // The exact conductance states each literature configuration programs.
    // Set A (8), its 9-15 sweep range, the rejected 16, and the measured
    // arrays' 4. Pins the RTL testbench to the same states.
    for (const auto& kv : kLevels) {
        const int n = kv.first;
        CHECK(levels_of(n) == kv.second);
        CHECK_EQ(kv.second.size(), static_cast<std::size_t>(n));
    }
}

TEST(quant_set_a_sweep_range_well_formed) {
    // Set A's true level count lies in 8..15 (8 passes, 16 fails;
    // literature-survey §2.2), so a Set A QUANT_LEVELS sweep runs over
    // that range. Every point must be a well-formed quantizer: N levels,
    // monotonic, full range (bottom -128, top 126 or 127).
    for (int n = kSetA_N; n <= kSetA_N_Rejected; ++n) {
        const auto lv = levels_of(n);
        CHECK_EQ(lv.size(), static_cast<std::size_t>(n));
        CHECK_EQ(lv.front(), -128);
        CHECK(lv.back() == 126 || lv.back() == 127);
    }
}

TEST(quant_measured_array_four_levels) {
    // Soliman et al. 2023 / Duan et al. 2024: 4 states, demonstrated in
    // measured arrays. Bins are the four quarters of the int8 range.
    CHECK(levels_of(kMeasuredArray_N) == (std::vector<int>{-128, -43, 42, 127}));
    CHECK_EQ(int{q(-65, kMeasuredArray_N)}, -128);
    CHECK_EQ(int{q(-64, kMeasuredArray_N)},  -43);
    CHECK_EQ(int{q( -1, kMeasuredArray_N)},  -43);
    CHECK_EQ(int{q(  0, kMeasuredArray_N)},   42);
    CHECK_EQ(int{q( 63, kMeasuredArray_N)},   42);
    CHECK_EQ(int{q( 64, kMeasuredArray_N)},  127);
}

// ===========================================================================
// ModelExact integration: quantization hooked into write_weight()
// ===========================================================================

TEST(model_quant_applied_at_write_time) {
    // Quantization is a property of the cell, applied once when the weight
    // is written (write-path-spec §1).
    ModelExact m;
    for (int n : kQuantN) {
        m.set_config(quant_config(n));
        for (int w = -128; w <= 127; ++w) {
            m.write_weight(5, 9, static_cast<std::int8_t>(w));
            CHECK_EQ(int{m.stored_weight(5, 9)}, int{q(w, n)});
        }
    }

    // Write-time semantics: a later config change does not re-quantize
    // cells already written. The RTL has no path to do so.
    // w = 30 is -1 at N = 3 but 63 at N = 5, so the check discriminates.
    CHECK(q(30, 3) != q(30, 5));
    m.set_config(quant_config(3));
    m.write_weight(0, 1, 30);
    CHECK_EQ(int{m.stored_weight(0, 1)}, -1);
    m.set_config(quant_config(5));                    // no rewrite
    CHECK_EQ(int{m.stored_weight(0, 1)}, -1);         // still the N = 3 value
    m.set_config(Config{});                           // quant off
    CHECK_EQ(int{m.stored_weight(0, 1)}, -1);
}

TEST(model_quant_uses_register_not_recomputed_mult) {
    // write_weight() must use cfg.quant_mult as written, not recompute it
    // from quant_levels, or it would hide a host-side pairing bug that the
    // RTL would expose.
    ModelExact m;
    Config cfg = quant_config(3);
    cfg.quant_mult = 257;                             // mismatched on purpose
    m.set_config(cfg);
    m.write_weight(0, 0, 127);
    CHECK_EQ(int{m.stored_weight(0, 0)}, -126);       // not 127
}

TEST(model_quant_bypass_is_exact) {
    // write-path-spec §9 test 3, the L0 gate: with NOISE_EN.quant clear the
    // write path is a pure pass-through, even with a lossy QUANT_LEVELS
    // sitting in the register.
    ModelExact m;
    Config cfg = quant_config(3);
    cfg.noise_en = NOISE_NONE;
    m.set_config(cfg);
    for (int w = -128; w <= 127; ++w) {
        m.write_weight(127, 127, static_cast<std::int8_t>(w));
        CHECK_EQ(int{m.stored_weight(127, 127)}, w);
    }
}

TEST(model_quant_activations_untouched) {
    // write-path-spec §1.2 / §9 test 12: quantization applies to WEIGHTS
    // only. Identity weights are stored with quant off, then quant is
    // turned on at its most destructive (N = 2) and activations written.
    // If write_activation() quantized, a[r] would collapse to two values.
    ModelExact m;
    for (int i = 0; i < TILE_ROWS; ++i) m.write_weight(i, i, 1);
    m.set_config(quant_config(2));
    for (int r = 0; r < TILE_ROWS; ++r)
        m.write_activation(r, static_cast<std::uint8_t>(r));
    const auto y = m.compute();
    for (int c = 0; c < TILE_COLS; ++c)
        CHECK_EQ(y[static_cast<std::size_t>(c)], std::int32_t{c});
}

TEST(model_quant_compute_enabled_others_still_throw) {
    // compute() accepts NOISE_QUANT now; every other enable still throws
    // until implemented, alone or combined with quant.
    ModelExact m;
    m.set_config(quant_config(8));
    CHECK(m.compute().size() == static_cast<std::size_t>(TILE_COLS));
    for (std::uint8_t bit : {NOISE_D2D, NOISE_READ, NOISE_STUCK, NOISE_IR, NOISE_ADC}) {
        Config cfg = quant_config(8);
        cfg.noise_en = static_cast<std::uint8_t>(NOISE_QUANT | bit);
        m.set_config(cfg);
        CHECK_THROWS(m.compute(), std::logic_error);
    }
}

TEST(model_quant_compute_matches_ideal_on_quantized_weights) {
    // Chains the two models (model-verification-design §1): with only
    // quant enabled, compute() equals model_ideal run on the PRE-QUANTIZED
    // weights. The model side of the L1 "quant only" row, run for the spec
    // set plus the literature configurations.
    std::mt19937 g(0xFEC1);                 // raw draws only: portable
    for (int n : {2, 3, 4, 5, 8, 12, 16, 255}) {
        ModelExact m;
        m.set_config(quant_config(n));
        std::vector<std::int8_t>  wq(static_cast<std::size_t>(TILE_ROWS * TILE_COLS));
        std::vector<std::uint8_t> a(static_cast<std::size_t>(TILE_ROWS));
        for (int r = 0; r < TILE_ROWS; ++r)
            for (int c = 0; c < TILE_COLS; ++c) {
                const int w = static_cast<int>(g() & 0xFFu) - 128;  // full int8
                m.write_weight(r, c, static_cast<std::int8_t>(w));
                wq[static_cast<std::size_t>(r * TILE_COLS + c)] = q(w, n);
            }
        for (int r = 0; r < TILE_ROWS; ++r) {
            a[static_cast<std::size_t>(r)] = static_cast<std::uint8_t>(g() & 0xFFu);
            m.write_activation(r, a[static_cast<std::size_t>(r)]);
        }
        const auto exact = m.compute();
        const auto ideal = ideal_mvm(wq, a, TILE_ROWS, TILE_COLS);
        CHECK_EQ(exact.size(), ideal.size());
        for (std::size_t c = 0; c < exact.size(); ++c)
            CHECK_EQ(std::int64_t{exact[c]}, ideal[c]);
    }
}
