// test_read_path_vectors.cpp
//
// Cross-checks the committed read-path golden vectors against model_exact:
//
//   tb/golden/read_noise/  read_noise_q, lane LFSR sequences, single-lane MACs
//   tb/golden/ir_drop/     ir_attenuate, exhaustive checksum, act_buffer profiles
//   tb/golden/adc/         adc_quantize
//   tb/golden/mvm/         whole computes through ModelExact, all six effects
//
// The vectors come from an independent Python generator
// (tb/golden/gen_read_path_vectors.py); Lane B's testbenches assert against
// the same files. Parsed the way $readmemh sees them: whitespace-separated hex
// words, `//` comments ignored, CR tolerated.

#include <cstdint>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "fecim/model_exact.h"
#include "fecim/params.h"
#include "fecim/primitives.h"
#include "fecim/read_path.h"
#include "test_harness.h"

#ifndef FECIM_GOLDEN_DIR
#error "FECIM_GOLDEN_DIR must point at tb/golden (set in model/CMakeLists.txt)"
#endif

using namespace fecim;

namespace {

std::vector<std::uint32_t> words(const std::string& rel) {
    const std::string path = std::string(FECIM_GOLDEN_DIR) + "/" + rel;
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot open " + path);
    std::vector<std::uint32_t> w;
    std::string line;
    while (std::getline(f, line)) {
        const auto c = line.find("//");
        if (c != std::string::npos) line.erase(c);
        std::istringstream ss(line);
        std::string tok;
        while (ss >> tok) w.push_back(static_cast<std::uint32_t>(std::stoul(tok, nullptr, 16)));
    }
    if (w.empty()) throw std::runtime_error(rel + ": empty");
    return w;
}

// Two's complement reinterpretation of a 32-bit word (portable before C++20).
std::int32_t i32(std::uint32_t u) {
    return u <= 0x7FFF'FFFFu ? static_cast<std::int32_t>(u)
                             : static_cast<std::int32_t>(-static_cast<std::int64_t>(~u) - 1);
}
int i8(std::uint32_t u) { return static_cast<std::int8_t>(static_cast<std::uint8_t>(u & 0xFFu)); }
std::uint8_t u8(std::uint32_t u) { return static_cast<std::uint8_t>(u & 0xFFu); }

} // namespace

// ===========================================================================
// read_noise/
// ===========================================================================

TEST(rn_vectors_noise_q_table) {
    const auto w = words("read_noise/noise_q.hex");
    const std::size_t n = w[0];
    CHECK_EQ(w.size(), 1 + n * 3);
    CHECK(n > 1000u);
    for (std::size_t k = 0; k < n; ++k) {
        const std::uint32_t* f = &w[1 + k * 3];
        CHECK_EQ(int{read_noise_q(f[0], u8(f[1]))}, i8(f[2]));
    }
}

TEST(rn_vectors_lane_sequences) {
    // From reseed: lfsr = lane_seed(seed, lane); then per accumulate
    // lfsr = lfsr_step(lfsr) and the sample is read_noise_q(new state).
    const auto w = words("read_noise/sequences.hex");
    const std::size_t n = w[0];
    std::size_t pos = 1;
    for (std::size_t s = 0; s < n; ++s) {
        const int lane = static_cast<int>(w[pos]);
        const std::uint32_t seed = w[pos + 1];
        const std::uint8_t sigma = u8(w[pos + 2]);
        const std::size_t steps = w[pos + 3];
        pos += 4;
        std::uint32_t st = lane_seed(seed, lane);
        CHECK(st != 0u);                                      // the | 1 guard
        for (std::size_t t = 0; t < steps; ++t, pos += 2) {
            st = lfsr_step(st);
            CHECK_EQ(st, w[pos]);
            CHECK_EQ(int{read_noise_q(st, sigma)}, i8(w[pos + 1]));
        }
    }
    CHECK_EQ(pos, w.size());
}

TEST(rn_vectors_single_lane_macs) {
    // One mac_lane per lane id, cases run in order with the LFSR carried
    // across cases unless RESEED. Mirrors what Lane B's testbench drives.
    const auto cs = words("read_noise/lane_cases.hex");
    const auto rw = words("read_noise/lane_rows.hex");
    const std::size_t n = cs[0];
    CHECK_EQ(cs.size(), 1 + n * 8);
    CHECK_EQ(rw.size(), 1 + std::size_t{rw[0]} * 5);
    std::vector<std::uint32_t> st(NUM_LANES);
    for (int l = 0; l < NUM_LANES; ++l) st[static_cast<std::size_t>(l)] = lane_seed(0, l);
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint32_t* c = &cs[1 + i * 8];
        const auto lane = static_cast<std::size_t>(c[0]);
        const bool reseed = (c[2] & 1u) != 0, read = (c[2] & 2u) != 0;
        if (reseed) st[lane] = lane_seed(c[3], static_cast<int>(lane));
        std::int32_t acc = 0;
        for (std::size_t j = c[5]; j < c[5] + c[6]; ++j) {
            const std::uint32_t* r = &rw[1 + j * 5];
            st[lane] = lfsr_step(st[lane]);
            const int q = read_noise_q(st[lane], u8(c[4]));
            const int wn = i8(r[0]) + (read ? q : 0);
            acc += wn * static_cast<int>(r[1]);
            CHECK_EQ(q, i8(r[2]));
            CHECK_EQ(wn, i32(r[3]));
            CHECK(wn >= -256 && wn <= 255);                   // fits 9 bits signed
            CHECK_EQ(acc, i32(r[4]));
        }
        CHECK_EQ(acc, i32(c[7]));
    }
}

// ===========================================================================
// ir_drop/
// ===========================================================================

TEST(ir_vectors_table_and_exhaustive_checksum) {
    const auto w = words("ir_drop/atten.hex");
    const std::size_t n = w[0];
    CHECK_EQ(w.size(), 1 + n * 3);
    for (std::size_t k = 0; k < n; ++k)
        CHECK_EQ(int{ir_attenuate(u8(w[1 + k * 3]), u8(w[2 + k * 3]))}, static_cast<int>(w[3 + k * 3]));

    std::uint32_t c = 0x811C9DC5u;
    for (unsigned a = 0; a < 256; ++a)
        for (unsigned k = 0; k < 256; ++k)
            c = (c ^ ir_attenuate(static_cast<std::uint8_t>(a), static_cast<std::uint8_t>(k))) *
                0x01000193u;
    CHECK_EQ(c, words("ir_drop/atten_checksum.hex")[0]);
}

TEST(ir_vectors_act_buffer_profiles) {
    const auto w = words("ir_drop/profiles.hex");
    const std::size_t n = w[0];
    const std::size_t per = 1 + 3 * static_cast<std::size_t>(TILE_ROWS);
    CHECK_EQ(w.size(), 1 + n * per);
    for (std::size_t p = 0; p < n; ++p) {
        const std::uint32_t* f = &w[1 + p * per];
        const bool en = f[0] != 0;
        for (int r = 0; r < TILE_ROWS; ++r) {
            const auto k = u8(f[1 + r]);
            const auto a = u8(f[1 + TILE_ROWS + r]);
            const int expect = static_cast<int>(f[1 + 2 * TILE_ROWS + r]);
            CHECK_EQ(en ? int{ir_attenuate(a, k)} : int{a}, expect);
        }
    }
}

// ===========================================================================
// adc/
// ===========================================================================

TEST(adc_vectors) {
    const auto w = words("adc/cases.hex");
    const std::size_t n = w[0];
    CHECK_EQ(w.size(), 1 + n * 4);
    CHECK(n > 32u * 20u);
    for (std::size_t k = 0; k < n; ++k) {
        const std::uint32_t* f = &w[1 + k * 4];
        const std::int32_t acc = i32(f[0]);
        const std::int32_t got = f[2] ? adc_quantize(acc, u8(f[1])) : acc;
        CHECK_EQ(got, i32(f[3]));
    }
}

// ===========================================================================
// mvm/: whole computes through ModelExact
// ===========================================================================

TEST(mvm_vectors_through_model_exact) {
    const auto cs = words("mvm/cases.hex");
    const auto d  = words("mvm/data.hex");
    const std::size_t n = cs[0];
    CHECK_EQ(cs.size(), 1 + n * 12);
    CHECK_EQ(d.size(), 1 + std::size_t{d[0]});

    ModelExact m;                                   // ONE model, reset once
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint32_t* f = &cs[1 + i * 12];
        Config cfg;
        cfg.active_rows  = static_cast<int>(f[0]);
        cfg.active_cols  = static_cast<int>(f[1]);
        cfg.noise_en     = u8(f[2]);
        cfg.quant_levels = static_cast<int>(f[3]);
        cfg.quant_mult   = static_cast<std::uint16_t>(f[4]);
        cfg.d2d_sigma    = u8(f[5]);
        cfg.read_sigma   = u8(f[6]);
        cfg.noise_seed   = f[7];
        cfg.adc_bits     = static_cast<int>(f[8]);
        cfg.stuck_rate   = f[9];
        const std::uint32_t flags = f[10];
        std::size_t pos = 1 + f[11];
        m.set_config(cfg);
        if (flags & 2u)
            for (int c = 0; c < cfg.active_cols; ++c)
                for (int r = 0; r < cfg.active_rows; ++r)
                    m.write_weight(r, c, static_cast<std::int8_t>(i8(d[pos++])));
        if (flags & 4u)
            for (int r = 0; r < cfg.active_rows; ++r) m.write_activation(r, u8(d[pos++]));
        if (flags & 8u)
            for (int r = 0; r < TILE_ROWS; ++r) m.write_atten(r, u8(d[pos++]));
        if (flags & 1u) m.reseed();
        const auto y = m.compute();
        CHECK_EQ(y.size(), static_cast<std::size_t>(cfg.active_cols));
        int bad = 0;
        for (std::size_t c = 0; c < y.size(); ++c)
            if (y[c] != i32(d[pos + c])) ++bad;
        CHECK_EQ(bad, 0);
    }
}
