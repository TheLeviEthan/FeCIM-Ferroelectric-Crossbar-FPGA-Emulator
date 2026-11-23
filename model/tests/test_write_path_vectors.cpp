// test_write_path_vectors.cpp
//
// Cross-checks the committed write-path golden vectors (tb/golden/write_path/)
// against model_exact. The vectors come from an independent Python generator;
// Lane B's write_path testbench asserts against the same files. If the Python
// reference and the C++ model agree, a mismatch in the RTL is an RTL bug, not
// a disagreement about what the right answer is.
//
// Parses the files the way a $readmemh reader sees them: whitespace-separated
// hex words, `//` comments ignored, CR tolerated.

#include <cstdint>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "fecim/model_exact.h"
#include "fecim/params.h"
#include "fecim/primitives.h"
#include "fecim/write_path.h"
#include "test_harness.h"

#ifndef FECIM_GOLDEN_DIR
#error "FECIM_GOLDEN_DIR must point at tb/golden (set in model/CMakeLists.txt)"
#endif

using namespace fecim;

namespace {

constexpr int kCells = TILE_ROWS * TILE_COLS;

std::vector<std::uint32_t> read_hex_words(const std::string& name) {
    const std::string path = std::string(FECIM_GOLDEN_DIR) + "/write_path/" + name;
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot open " + path);
    std::vector<std::uint32_t> words;
    std::string line;
    while (std::getline(f, line)) {
        const auto c = line.find("//");
        if (c != std::string::npos) line.erase(c);
        std::istringstream ss(line);
        std::string tok;
        while (ss >> tok)
            words.push_back(static_cast<std::uint32_t>(std::stoul(tok, nullptr, 16)));
    }
    return words;
}

// Reads a file whose word 0 is a record count followed by `width` words per
// record, and checks the length agrees.
std::vector<std::uint32_t> read_records(const std::string& name, std::size_t width,
                                        std::size_t& count) {
    auto w = read_hex_words(name);
    if (w.empty()) throw std::runtime_error(name + ": empty");
    count = w[0];
    if (w.size() != 1 + count * width) throw std::runtime_error(name + ": length");
    return w;
}

std::int8_t as_i8(std::uint32_t w) {
    return static_cast<std::int8_t>(static_cast<std::uint8_t>(w & 0xFFu));
}

Config to_config(const std::uint32_t* f) {
    Config cfg;
    cfg.noise_en     = static_cast<std::uint8_t>(f[0]);
    cfg.quant_levels = static_cast<int>(f[1]);
    cfg.quant_mult   = static_cast<std::uint16_t>(f[2]);
    cfg.d2d_sigma    = static_cast<std::uint8_t>(f[3]);
    cfg.noise_seed   = f[4];
    cfg.stuck_rate   = f[5];
    return cfg;
}

struct Case { std::size_t cfg; std::uint16_t addr; std::int8_t w_in, w_out; };

struct Vectors {
    std::vector<Config> configs;
    std::vector<Case>   cases;
};

Vectors load() {
    std::size_t n_cfg = 0, n_case = 0;
    const auto cw = read_records("configs.hex", 6, n_cfg);
    const auto sw = read_records("cases.hex", 4, n_case);
    Vectors v;
    for (std::size_t i = 0; i < n_cfg; ++i) v.configs.push_back(to_config(&cw[1 + i * 6]));
    for (std::size_t j = 0; j < n_case; ++j) {
        const std::uint32_t* f = &sw[1 + j * 4];
        if (f[0] >= n_cfg || f[1] >= static_cast<std::uint32_t>(kCells) ||
            f[2] > 0xFFu || f[3] > 0xFFu)
            throw std::runtime_error("cases.hex: field out of range at case " + std::to_string(j));
        v.cases.push_back({f[0], static_cast<std::uint16_t>(f[1]), as_i8(f[2]), as_i8(f[3])});
    }
    return v;
}

} // namespace

TEST(write_path_vectors_well_formed) {
    const Vectors v = load();
    CHECK(v.configs.size() >= 30u);
    CHECK(v.cases.size() >= 5000u);
    // Every config has cases, and the ones that enable a write-time effect
    // with a non-zero strength actually change some weights (otherwise the
    // vectors would not test the stage at all).
    std::vector<int> n(v.configs.size(), 0), changed(v.configs.size(), 0);
    for (const auto& c : v.cases) {
        ++n[c.cfg];
        changed[c.cfg] += (c.w_in != c.w_out);
    }
    for (std::size_t i = 0; i < v.configs.size(); ++i) {
        CHECK(n[i] > 100);
        const Config& cfg = v.configs[i];
        const bool active = ((cfg.noise_en & NOISE_QUANT) != 0) ||
                            ((cfg.noise_en & NOISE_D2D) && cfg.d2d_sigma > 0) ||
                            ((cfg.noise_en & NOISE_STUCK) && (cfg.stuck_rate & 0xFFFFu) > 0);
        if (active) CHECK(changed[i] > 0);
        else        CHECK_EQ(changed[i], 0);
    }
}

TEST(write_path_vectors_hash_table_matches) {
    // Every cell address, both salts, NOISE_SEED = 0.
    std::size_t n = 0;
    const auto w = read_records("hash_seed0.hex", 1, n);
    CHECK_EQ(n, std::size_t{kCells});
    for (std::size_t a = 0; a < n; ++a) {
        const auto addr = static_cast<std::uint16_t>(a);
        CHECK_EQ(static_cast<std::uint32_t>(w[1 + a] & 0xFFFFu),
                 std::uint32_t{cell_hash16(addr, SEED_D2D)});
        CHECK_EQ(static_cast<std::uint32_t>(w[1 + a] >> 16),
                 std::uint32_t{cell_hash16(addr, SEED_STUCK)});
    }
}

TEST(write_path_vectors_hash_checksums_match) {
    // Order-dependent fold over all 65,536 addresses for 8 seeds x 2 salts.
    std::size_t n = 0;
    const auto w = read_records("hash_checksums.hex", 3, n);
    CHECK_EQ(n, std::size_t{16});
    for (std::size_t k = 0; k < n; ++k) {
        const auto seed = static_cast<std::uint16_t>(w[1 + k * 3]);
        const auto salt = static_cast<std::uint16_t>(w[1 + k * 3 + 1]);
        std::uint32_t c = 0x811C9DC5u;
        for (std::uint32_t a = 0; a < 0x10000u; ++a)
            c = (c ^ cell_hash16(static_cast<std::uint16_t>(a),
                                 static_cast<std::uint16_t>(seed ^ salt))) * 0x01000193u;
        CHECK_EQ(c, w[1 + k * 3 + 2]);
    }
}

TEST(write_path_vectors_match_free_functions) {
    // The three stage functions, chained by hand in the RTL's order.
    const Vectors v = load();
    for (const auto& c : v.cases) {
        const Config& cfg = v.configs[c.cfg];
        std::int8_t w = c.w_in;
        if (cfg.noise_en & NOISE_QUANT)
            w = quantize_weight(w, static_cast<std::uint8_t>(cfg.quant_levels), cfg.quant_mult);
        if (cfg.noise_en & NOISE_D2D)
            w = d2d_offset(w, c.addr, cfg.noise_seed, cfg.d2d_sigma);
        if (cfg.noise_en & NOISE_STUCK)
            w = apply_stuck(w, c.addr, cfg.noise_seed, cfg.stuck_rate);
        CHECK_EQ(int{w}, int{c.w_out});
    }
}

TEST(write_path_vectors_match_model_exact) {
    // Through ModelExact::write_weight(), with the address split back into
    // row and column the way the bulk stream defines it.
    const Vectors v = load();
    ModelExact m;
    std::size_t current = v.configs.size();
    for (const auto& c : v.cases) {
        if (c.cfg != current) {
            m.set_config(v.configs[c.cfg]);
            current = c.cfg;
        }
        const int row = c.addr % TILE_ROWS;
        const int col = c.addr / TILE_ROWS;
        CHECK_EQ(int{cell_addr(row, col)}, int{c.addr});
        m.write_weight(row, col, c.w_in);
        CHECK_EQ(int{m.stored_weight(row, col)}, int{c.w_out});
    }
}
