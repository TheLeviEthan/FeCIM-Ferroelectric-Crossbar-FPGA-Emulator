// model_exact tests. The clean-path comparison against model_ideal is the
// model side of the L0 gate (model-verification-design §3.1).

#include <cstdint>
#include <random>
#include <stdexcept>
#include <vector>

#include "fecim/model_exact.h"
#include "fecim/model_ideal.h"
#include "test_harness.h"

using namespace fecim;

namespace {

// std::mt19937 output is specified by the standard; the <random>
// distributions are not, so derive values from raw draws to keep the test
// identical on every compiler.
struct Rng {
    std::mt19937 g;
    explicit Rng(std::uint32_t seed) : g(seed) {}
    std::int8_t  weight() { return static_cast<std::int8_t>(static_cast<int>(g() % 255u) - 127); }
    std::uint8_t act()    { return static_cast<std::uint8_t>(g() & 0xFFu); }
};

// Loads the same random tile into both models; returns {exact, ideal}.
void run_clean(std::uint32_t seed, int rows, int cols,
               std::vector<std::int32_t>& exact, std::vector<std::int64_t>& ideal) {
    Rng rng(seed);
    ModelExact m;
    Config cfg;
    cfg.active_rows = rows;
    cfg.active_cols = cols;
    m.set_config(cfg);

    std::vector<std::int8_t>  w(static_cast<std::size_t>(rows * cols));
    std::vector<std::uint8_t> a(static_cast<std::size_t>(rows));
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            const auto v = rng.weight();
            w[static_cast<std::size_t>(r * cols + c)] = v;
            m.write_weight(r, c, v);
        }
    for (int r = 0; r < rows; ++r) {
        a[static_cast<std::size_t>(r)] = rng.act();
        m.write_activation(r, a[static_cast<std::size_t>(r)]);
    }
    exact = m.compute();
    ideal = ideal_mvm(w, a, rows, cols);
}

} // namespace

TEST(clean_exact_matches_ideal_random) {
    for (std::uint32_t seed = 1; seed <= 200; ++seed) {
        std::vector<std::int32_t> exact;
        std::vector<std::int64_t> ideal;
        run_clean(seed, TILE_ROWS, TILE_COLS, exact, ideal);
        CHECK_EQ(exact.size(), ideal.size());
        for (std::size_t c = 0; c < exact.size(); ++c)
            CHECK_EQ(std::int64_t{exact[c]}, ideal[c]);
    }
}

TEST(clean_exact_matches_ideal_partial_tile) {
    std::vector<std::int32_t> exact;
    std::vector<std::int64_t> ideal;
    run_clean(42, 64, 64, exact, ideal);
    CHECK_EQ(exact.size(), std::size_t{64});
    for (std::size_t c = 0; c < exact.size(); ++c)
        CHECK_EQ(std::int64_t{exact[c]}, ideal[c]);
}

TEST(clean_worst_case_magnitude_fits) {
    // Every weight at -127, every activation at 255: largest clean magnitude.
    ModelExact m;
    for (int r = 0; r < TILE_ROWS; ++r) {
        m.write_activation(r, 255);
        for (int c = 0; c < TILE_COLS; ++c) m.write_weight(r, c, -127);
    }
    const auto y = m.compute();
    for (auto v : y) CHECK_EQ(v, std::int32_t{-127 * 255 * TILE_ROWS});
}

TEST(identity_one_hot) {
    // Seam test 3 analogue: identity weights, one-hot activation.
    for (int hot = 0; hot < TILE_ROWS; hot += 17) {
        ModelExact m;
        for (int i = 0; i < TILE_ROWS; ++i) m.write_weight(i, i, 1);
        m.write_activation(hot, 200);
        const auto y = m.compute();
        for (int c = 0; c < TILE_COLS; ++c)
            CHECK_EQ(y[static_cast<std::size_t>(c)], std::int32_t{c == hot ? 200 : 0});
    }
}

TEST(nonidealities_not_silently_ignored) {
    // Every enable now has an implementation. The original intent of this
    // test still holds: a non-ideality that is switched on with a non-zero
    // strength must change the result. Each read-time bit, alone, against
    // the clean result on the same data (LFSRs reseeded before each run).
    ModelExact m;
    for (int r = 0; r < TILE_ROWS; ++r) {
        m.write_activation(r, static_cast<std::uint8_t>(37 + r));
        for (int c = 0; c < TILE_COLS; ++c)
            m.write_weight(r, c, static_cast<std::int8_t>((r * 7 + c * 13) % 200 - 100));
    }
    for (int r = 0; r < TILE_ROWS; ++r)
        m.write_atten(r, static_cast<std::uint8_t>(255 - r));
    Config cfg;
    m.set_config(cfg);
    m.reseed();
    const auto clean = m.compute();
    for (std::uint8_t bit : {NOISE_READ, NOISE_IR, NOISE_ADC}) {
        Config c2;
        c2.noise_en   = bit;
        c2.read_sigma = 128;
        c2.adc_bits   = 8;
        m.set_config(c2);
        m.reseed();
        CHECK(m.compute() != clean);
    }
}

TEST(bounds_checked) {
    ModelExact m;
    CHECK_THROWS(m.write_weight(TILE_ROWS, 0, 1), std::out_of_range);
    CHECK_THROWS(m.write_weight(0, -1, 1), std::out_of_range);
    CHECK_THROWS(m.write_activation(-1, 1), std::out_of_range);
    Config bad;
    bad.active_rows = 0;
    CHECK_THROWS(m.set_config(bad), std::invalid_argument);
    bad = Config{};
    bad.quant_levels = 256;
    CHECK_THROWS(m.set_config(bad), std::invalid_argument);
}

TEST(config_reset_defaults_match_protocol) {
    // protocol.md §6: the reset state is an ideal crossbar with a consistent
    // QUANT_LEVELS/QUANT_MULT pair and a lossless ADC setting.
    const Config c;
    CHECK_EQ(c.quant_levels, 255);
    CHECK_EQ(int{c.quant_mult}, (255 * 256 + (c.quant_levels - 1) / 2) / (c.quant_levels - 1));
    CHECK_EQ(c.adc_bits, ACC_USED_W);
    CHECK_EQ(int{c.noise_en}, int{NOISE_NONE});
    CHECK_EQ(c.noise_seed, 0u);
}
