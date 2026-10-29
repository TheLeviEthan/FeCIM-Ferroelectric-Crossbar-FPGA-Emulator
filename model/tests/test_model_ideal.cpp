// model_ideal tests. The oracle is only worth anything if it is right by
// inspection, so these check it against results a person can verify by hand
// (identity, zero, a small worked example), plus algebraic properties that
// hold for ANY correct MVM and do not depend on a hand computation.
//
// The clean model_exact == model_ideal comparison lives in test_model.cpp.

#include <cstdint>
#include <random>
#include <stdexcept>
#include <vector>

#include "fecim/model_ideal.h"
#include "fecim/params.h"
#include "test_harness.h"

using namespace fecim;

namespace {

using WVec = std::vector<std::int8_t>;
using AVec = std::vector<std::uint8_t>;
using YVec = std::vector<std::int64_t>;
using DVec = std::vector<double>;

std::size_t sz(int v) { return static_cast<std::size_t>(v); }

// Raw mt19937 draws only: its output is fixed by the standard, the
// <random> distributions are not.
struct Rng {
    std::mt19937 g;
    explicit Rng(std::uint32_t seed) : g(seed) {}
    // Full int8 range, -128 included: the oracle must not assume the +/-127
    // the host normally writes (the write path can saturate to -128).
    std::int8_t  weight() { return static_cast<std::int8_t>(static_cast<int>(g() & 0xFFu) - 128); }
    std::uint8_t act()    { return static_cast<std::uint8_t>(g() & 0xFFu); }
};

WVec random_weights(Rng& rng, int rows, int cols) {
    WVec w(sz(rows * cols));
    for (auto& v : w) v = rng.weight();
    return w;
}

AVec random_act(Rng& rng, int rows) {
    AVec a(sz(rows));
    for (auto& v : a) v = rng.act();
    return a;
}

WVec identity(int n) {
    WVec w(sz(n * n), 0);
    for (int i = 0; i < n; ++i) w[sz(i * n + i)] = 1;
    return w;
}

} // namespace

// ---- identity -------------------------------------------------------------

TEST(ideal_identity_returns_activations) {
    // W = I  =>  y = a. Full tile, plus a small non-power-of-two size: the
    // oracle has no notion of tile geometry and must not acquire one.
    for (int n : {5, TILE_ROWS}) {
        Rng rng(static_cast<std::uint32_t>(n));
        const AVec a = random_act(rng, n);
        const YVec y = ideal_mvm(identity(n), a, n, n);
        CHECK_EQ(y.size(), sz(n));
        for (int i = 0; i < n; ++i)
            CHECK_EQ(y[sz(i)], std::int64_t{a[sz(i)]});
    }
}

TEST(ideal_identity_float) {
    const int n = 4;
    DVec w(sz(n * n), 0.0);
    for (int i = 0; i < n; ++i) w[sz(i * n + i)] = 1.0;
    const DVec a = {0.75, -3.5, 1e-3, 1234.5};
    const DVec y = ideal_mvm(w, a, n, n);
    for (int i = 0; i < n; ++i) CHECK_EQ(y[sz(i)], a[sz(i)]);
}

// ---- all-zero -------------------------------------------------------------

TEST(ideal_zero_weights_give_zero) {
    Rng rng(7);
    const AVec a = random_act(rng, TILE_ROWS);
    const YVec y = ideal_mvm(WVec(sz(TILE_ROWS * TILE_COLS), 0), a, TILE_ROWS, TILE_COLS);
    CHECK_EQ(y.size(), sz(TILE_COLS));
    for (auto v : y) CHECK_EQ(v, std::int64_t{0});
}

TEST(ideal_zero_activations_give_zero) {
    Rng rng(8);
    const WVec w = random_weights(rng, TILE_ROWS, TILE_COLS);
    const YVec y = ideal_mvm(w, AVec(sz(TILE_ROWS), 0), TILE_ROWS, TILE_COLS);
    for (auto v : y) CHECK_EQ(v, std::int64_t{0});
}

// ---- hand-checked ---------------------------------------------------------

TEST(ideal_hand_checked_int) {
    // Non-square (4 rows x 3 cols) so a row/column swap cannot pass, with
    // -128, +127, 0, and activations 0 and 255 in it.
    //
    //            c0     c1     c2        a
    //   r0  [   17,  -128,   127 ]       3
    //   r1  [  -45,    90,    -1 ]     255
    //   r2  [    0,   -77,    64 ]       0
    //   r3  [  127,    33,  -100 ]     200
    //
    //   y0 =   17*3 + (-45)*255 +   0*0 +  127*200 =    51 - 11475 + 0 + 25400 =  13976
    //   y1 = -128*3 +    90*255 + -77*0 +   33*200 =  -384 + 22950 + 0 +  6600 =  29166
    //   y2 =  127*3 +  (-1)*255 +  64*0 + -100*200 =   381 -   255 + 0 - 20000 = -19874
    const WVec w = {  17, -128,  127,
                     -45,   90,   -1,
                       0,  -77,   64,
                     127,   33, -100 };
    const AVec a = {3, 255, 0, 200};
    const YVec y = ideal_mvm(w, a, 4, 3);
    CHECK_EQ(y.size(), std::size_t{3});
    CHECK_EQ(y[0], std::int64_t{13976});
    CHECK_EQ(y[1], std::int64_t{29166});
    CHECK_EQ(y[2], std::int64_t{-19874});
}

TEST(ideal_hand_checked_float) {
    // Dyadic values, so every product and sum is exact and == is fair.
    //
    //   W = [  0.5    -1.0  ]     a = [ 1.0 ]
    //       [  0.25    0.75 ]         [ 0.5 ]
    //       [ -0.125   2.0  ]         [ 4.0 ]
    //
    //   y0 = 0.5 + 0.125 - 0.5 = 0.125
    //   y1 = -1.0 + 0.375 + 8.0 = 7.375
    const DVec w = {  0.5,   -1.0,
                      0.25,   0.75,
                     -0.125,  2.0 };
    const DVec a = {1.0, 0.5, 4.0};
    const DVec y = ideal_mvm(w, a, 3, 2);
    CHECK_EQ(y.size(), std::size_t{2});
    CHECK_EQ(y[0], 0.125);
    CHECK_EQ(y[1], 7.375);
}

TEST(ideal_single_cell_lands_in_its_column) {
    // One nonzero cell at (r = 2, c = 5) on a 4 x 8 tile: only y[5] moves.
    // Pins down orientation independently of the worked example.
    WVec w(4 * 8, 0);
    w[2 * 8 + 5] = -3;
    const AVec a = {9, 9, 11, 9};
    const YVec y = ideal_mvm(w, a, 4, 8);
    for (int c = 0; c < 8; ++c)
        CHECK_EQ(y[sz(c)], std::int64_t{c == 5 ? -33 : 0});
}

// ---- corners --------------------------------------------------------------

TEST(ideal_full_scale_corners) {
    // Largest clean magnitudes at full tile: -128 is only reachable through
    // write-path saturation, but the oracle must still be exact there.
    const AVec a(sz(TILE_ROWS), 255);
    const YVec neg = ideal_mvm(WVec(sz(TILE_ROWS * TILE_COLS), -128), a, TILE_ROWS, TILE_COLS);
    const YVec pos = ideal_mvm(WVec(sz(TILE_ROWS * TILE_COLS),  127), a, TILE_ROWS, TILE_COLS);
    for (auto v : neg) CHECK_EQ(v, std::int64_t{-128 * 255 * TILE_ROWS});   // -4,177,920
    for (auto v : pos) CHECK_EQ(v, std::int64_t{ 127 * 255 * TILE_ROWS});   //  4,145,280
}

// ---- properties (hold for any correct MVM) --------------------------------

TEST(ideal_linear_in_weights) {
    // y(W1 + W2, a) == y(W1, a) + y(W2, a). Halves are kept in -64..63 so
    // the sum stays a valid int8.
    for (std::uint32_t seed = 1; seed <= 20; ++seed) {
        Rng rng(seed);
        const int rows = 1 + static_cast<int>(rng.g() % 128u);
        const int cols = 1 + static_cast<int>(rng.g() % 128u);
        WVec w1(sz(rows * cols)), w2(sz(rows * cols)), w12(sz(rows * cols));
        for (std::size_t i = 0; i < w1.size(); ++i) {
            w1[i]  = static_cast<std::int8_t>(rng.weight() / 2);
            w2[i]  = static_cast<std::int8_t>(rng.weight() / 2);
            w12[i] = static_cast<std::int8_t>(w1[i] + w2[i]);
        }
        const AVec a = random_act(rng, rows);
        const YVec y1 = ideal_mvm(w1, a, rows, cols);
        const YVec y2 = ideal_mvm(w2, a, rows, cols);
        const YVec y12 = ideal_mvm(w12, a, rows, cols);
        for (std::size_t c = 0; c < y12.size(); ++c)
            CHECK_EQ(y12[c], y1[c] + y2[c]);
    }
}

TEST(ideal_float_exact_on_integer_codes) {
    // The float64 oracle on integer-valued inputs must equal the int64 oracle
    // exactly (header: every partial sum is an integer < 2^53). This is what
    // lets a float64 reference (here, or NumPy in Lane D) serve as L0.
    for (std::uint32_t seed = 100; seed < 120; ++seed) {
        Rng rng(seed);
        const WVec w = random_weights(rng, TILE_ROWS, TILE_COLS);
        const AVec a = random_act(rng, TILE_ROWS);
        const DVec wd(w.begin(), w.end());
        const DVec ad(a.begin(), a.end());
        const YVec yi = ideal_mvm(w, a, TILE_ROWS, TILE_COLS);
        const DVec yd = ideal_mvm(wd, ad, TILE_ROWS, TILE_COLS);
        for (std::size_t c = 0; c < yi.size(); ++c)
            CHECK_EQ(yd[c], static_cast<double>(yi[c]));
    }
}

// ---- shape checks ---------------------------------------------------------

TEST(ideal_rejects_bad_shapes) {
    const WVec w(6, 1);
    const AVec a(3, 1);
    CHECK_THROWS(ideal_mvm(w, a, 0, 2), std::invalid_argument);
    CHECK_THROWS(ideal_mvm(w, a, 3, 0), std::invalid_argument);
    CHECK_THROWS(ideal_mvm(w, a, -3, -2), std::invalid_argument);   // product is +6
    CHECK_THROWS(ideal_mvm(w, a, 2, 3), std::invalid_argument);     // act too long
    CHECK_THROWS(ideal_mvm(w, AVec(2, 1), 3, 2), std::invalid_argument);
    CHECK_THROWS(ideal_mvm(WVec(5, 1), a, 3, 2), std::invalid_argument);
    CHECK_THROWS(ideal_mvm(DVec(6, 1.0), DVec(2, 1.0), 3, 2), std::invalid_argument);
    // Valid shape still works after all that.
    CHECK_EQ(ideal_mvm(w, a, 3, 2).size(), std::size_t{2});
}
