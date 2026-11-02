// model/examples/demo_mvm.cpp -- pre-alpha demo for Lane C (the reference model).
//
//   cmake -S model -B build && cmake --build build
//   ./build/fecim_demo [seed]
//
// What it shows, on a full 128 x 128 tile:
//
//   1. Clean datapath (NOISE_EN = 0): model_exact, the bit-exact model of the
//      RTL, agrees with model_ideal, the mathematical oracle, on every column
//      of every activation vector. That is the model side of the L0 gate.
//   2. Conductance quantization (NOISE_EN.quant): the error it introduces at
//      N levels, using the RTL's integer arithmetic (write-path stage 1), for
//      the level counts of the two published parameter sets we have so far.
//
// Integer only and reproducible across compilers: random values come from
// std::mt19937's raw output, never std::uniform_int_distribution (whose
// mapping differs between libstdc++ and MSVC).

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "fecim/model_exact.h"
#include "fecim/model_ideal.h"
#include "fecim/params.h"
#include "fecim/write_path.h"

using namespace fecim;

namespace {

constexpr int kVectors = 1000;   // activation vectors per configuration

struct Tile {
    std::vector<std::int8_t>                weights;   // row-major, for model_ideal
    std::vector<std::vector<std::uint8_t>>  acts;      // kVectors x TILE_ROWS
};

Tile make_tile(unsigned seed) {
    std::mt19937 rng(seed);
    Tile t;
    t.weights.resize(static_cast<std::size_t>(TILE_ROWS) * TILE_COLS);
    for (auto& w : t.weights) w = static_cast<std::int8_t>(static_cast<int>(rng() % 255u) - 127);
    t.acts.assign(kVectors, std::vector<std::uint8_t>(TILE_ROWS));
    for (auto& v : t.acts)
        for (auto& a : v) a = static_cast<std::uint8_t>(rng() & 0xFFu);
    return t;
}

void load(ModelExact& m, const Tile& t) {
    for (int r = 0; r < TILE_ROWS; ++r)
        for (int c = 0; c < TILE_COLS; ++c)
            m.write_weight(r, c, t.weights[static_cast<std::size_t>(r) * TILE_COLS + static_cast<std::size_t>(c)]);
}

std::vector<std::int32_t> run(ModelExact& m, const std::vector<std::uint8_t>& a) {
    for (int r = 0; r < TILE_ROWS; ++r) m.write_activation(r, a[static_cast<std::size_t>(r)]);
    return m.compute();
}

std::size_t argmax(const std::vector<std::int64_t>& y) {
    std::size_t best = 0;
    for (std::size_t i = 1; i < y.size(); ++i) if (y[i] > y[best]) best = i;   // ties -> lowest
    return best;
}

} // namespace

int main(int argc, char** argv) {
    const unsigned seed = argc > 1 ? static_cast<unsigned>(std::strtoul(argv[1], nullptr, 0)) : 2026u;
    const Tile tile = make_tile(seed);

    std::printf("FeCIM reference model demo\n");
    std::printf("  tile %d x %d, %d lanes x %d passes, %d activation vectors, seed %u\n\n",
                TILE_ROWS, TILE_COLS, NUM_LANES, PASSES, kVectors, seed);

    // Ideal results, once per vector (int64: independent of the RTL's ACC_W).
    std::vector<std::vector<std::int64_t>> ideal;
    for (const auto& a : tile.acts) ideal.push_back(ideal_mvm(tile.weights, a, TILE_ROWS, TILE_COLS));

    // ---- 1. clean datapath: bit-exact -------------------------------------
    {
        ModelExact m;                       // reset config: NOISE_EN = 0
        load(m, tile);
        long long compared = 0, mismatches = 0;
        for (std::size_t v = 0; v < tile.acts.size(); ++v) {
            const auto y = run(m, tile.acts[v]);
            for (std::size_t c = 0; c < y.size(); ++c, ++compared)
                if (std::int64_t{y[c]} != ideal[v][c]) ++mismatches;
        }
        std::printf("1. Clean datapath (NOISE_EN = 0)\n");
        std::printf("   model_exact vs model_ideal: %lld / %lld outputs bit-exact  %s\n\n",
                    compared - mismatches, compared, mismatches == 0 ? "PASS" : "FAIL");
        if (mismatches != 0) return 1;
    }

    // ---- 2. conductance quantization --------------------------------------
    double y_rms = 0.0;
    {
        long double ss = 0;
        long long n = 0;
        for (const auto& y : ideal) for (auto v : y) { ss += static_cast<long double>(v) * v; ++n; }
        y_rms = std::sqrt(static_cast<double>(ss / n));
    }

    std::printf("2. Conductance quantization (NOISE_EN.quant = 1, write-path stage 1)\n");
    std::printf("   error vs the ideal MVM over %d vectors x %d columns; RMS |y| = %.0f\n\n",
                kVectors, TILE_COLS, y_rms);
    std::printf("   %4s  %-28s %12s %12s %10s %10s\n",
                "N", "parameter set", "max |err|", "mean |err|", "NRMSE", "top-1 agr");

    struct Row { int n; const char* label; };
    const Row rows[] = {
        {255, "(8-bit register max)"},
        { 16, ""},
        {  8, "dualport_fefet_mlc (TED'22)"},
        {  4, "gf28_hkmg (Nat. Comm.'23)"},
        {  3, ""},
        {  2, "(binary)"},
    };

    for (const auto& row : rows) {
        ModelExact m;
        Config cfg;
        cfg.noise_en     = NOISE_QUANT;
        cfg.quant_levels = row.n;
        cfg.quant_mult   = quant_mult_for(row.n);   // the pair the host always writes together
        m.set_config(cfg);
        load(m, tile);                              // quantization happens at write time

        long long max_err = 0, agree = 0;
        long double sum_abs = 0, sum_sq = 0;
        for (std::size_t v = 0; v < tile.acts.size(); ++v) {
            const auto y = run(m, tile.acts[v]);
            std::vector<std::int64_t> y64(y.begin(), y.end());
            for (std::size_t c = 0; c < y.size(); ++c) {
                const long long e = std::llabs(static_cast<long long>(y64[c] - ideal[v][c]));
                if (e > max_err) max_err = e;
                sum_abs += e;
                sum_sq  += static_cast<long double>(e) * e;
            }
            if (argmax(y64) == argmax(ideal[v])) ++agree;
        }
        const double n_out = static_cast<double>(kVectors) * TILE_COLS;
        const double nrmse = std::sqrt(static_cast<double>(sum_sq) / n_out) / y_rms;
        std::printf("   %4d  %-28s %12lld %12.0f %9.2f%% %9.0f%%\n",
                    row.n, row.label, max_err, static_cast<double>(sum_abs) / n_out,
                    100.0 * nrmse, 100.0 * static_cast<double>(agree) / kVectors);
    }

    std::printf("\n   NRMSE = RMS error / RMS ideal output. top-1 agr = the largest column\n"
                "   (argmax, what a classifier reads) matches the ideal crossbar's.\n"
                "   N = 255 is not the identity: every w > -128 maps to w - 1 (write-path\n"
                "   spec, by design), so its error is mostly a constant -sum(a) offset that\n"
                "   is identical on every column and cannot change the argmax. \"No\n"
                "   quantization\" is the bypass (part 1), not N = 255.\n");
    return 0;
}
