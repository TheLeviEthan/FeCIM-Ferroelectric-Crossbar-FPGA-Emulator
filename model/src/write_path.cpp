// write_path.cpp -- see fecim/write_path.h for what each function means.
//
// Integer arithmetic only (model-verification-design §2). Intermediates are
// std::uint32_t so the build stays clean under -Wconversion and
// -Wsign-conversion; narrowing happens once, explicitly, at each return.

#include "fecim/write_path.h"

#include <stdexcept>

#include "fecim/params.h"
#include "fecim/primitives.h"

namespace fecim {

std::uint16_t quant_mult_for(int n_levels) {
    // n = 1 divides by zero; n > 255 breaks the 9x9 multiply in step 2.
    if (n_levels < 2 || n_levels > QUANT_LEVELS_MAX)
        throw std::invalid_argument("quant_mult_for: n_levels must be 2..255");

    // round(255 * 256 / (n - 1)), as integer round-half-up.
    constexpr int kNum = 255 * 256;                 // 65280
    const int den = n_levels - 1;
    const int q = (kNum + den / 2) / den;           // 257 .. 65280

    return static_cast<std::uint16_t>(q);
}

std::int8_t quantize_weight(std::int8_t   w_ideal,
                            std::uint8_t  quant_levels,
                            std::uint16_t quant_mult) {
    // Step 1: int8 -> offset binary, 0..255. Flipping the sign bit is
    // exactly w + 128 for every int8 (-128 -> 0, -1 -> 127, 0 -> 128,
    // 127 -> 255) with no signed arithmetic involved.
    const std::uint32_t u =
        static_cast<std::uint32_t>(static_cast<std::uint8_t>(w_ideal)) ^ 0x80u;

    // Step 2: which of the N bins u falls in: floor(u * N / 256).
    // u * N <= 255 * 255 = 65025.
    const std::uint32_t level = (u * quant_levels) >> 8;              // 0..N-1

    // Step 3: bin index -> output code: level * (255 / (N-1)), with the
    // reciprocal supplied pre-scaled by 256 in QUANT_MULT (Q8.8).
    // level * quant_mult <= 254 * 65535, so uint32_t cannot overflow. For a
    // consistent pair the result is 0..255; for a mismatched pair it can be
    // larger, and step 4 wraps it exactly as the RTL does.
    const std::uint32_t scaled = (level * quant_mult) >> 8;

    // Step 4: keep scaled[7:0] as the RTL does, then back to signed.
    // scaled[7:0] is bits [15:8] of the product, so the result is the same
    // for ANY RTL product width >= 16 bits: the spec's 17-bit `scaled` and
    // this 32-bit intermediate agree on every input, matched pair or not.
    const int w_q = static_cast<int>(scaled & 0xFFu) - 128;           // -128..127

    return static_cast<std::int8_t>(w_q);
}

// ---- stages 2 and 3: D2D variation and stuck-at faults --------------------

namespace {

// Arithmetic shift right that rounds toward minus infinity, like SV's `>>>`
// on a signed operand. Written out because `>>` on a negative int is
// implementation-defined before C++20 (every mainstream compiler does the
// arithmetic shift, but the model should not depend on that). For x < 0,
// ~x = -x - 1 >= 0, so the shift below is on a non-negative value.
constexpr int asr(int x, int n) {
    return x >= 0 ? (x >> n) : ~((~x) >> n);
}
static_assert(asr(-1, 8) == -1 && asr(-256, 8) == -1 && asr(-257, 8) == -2 &&
              asr(255, 8) == 0 && asr(256, 8) == 1,
              "asr must floor, as SV >>> does");

// The 16-bit hash seed for one salt: noise_seed[15:0] ^ salt. Only the low
// 16 bits of the 32-bit NOISE_SEED register reach the write path.
std::uint16_t hash_seed(std::uint32_t noise_seed, std::uint16_t salt) {
    return static_cast<std::uint16_t>((noise_seed & 0xFFFFu) ^ salt);
}

} // namespace

std::uint16_t cell_addr(int row, int col) {
    // Global, column-major: the same index as the bulk weight stream's
    // bulk_addr, so the RTL can hash bulk_addr directly. Unlike the datapath
    // functions this is an index helper, so out-of-range arguments are a
    // caller bug and throw (a silently wrapped address would hash the wrong
    // cell and pass every self-consistency test).
    if (row < 0 || row >= TILE_ROWS || col < 0 || col >= TILE_COLS)
        throw std::out_of_range("cell_addr: row/col outside the tile");
    return static_cast<std::uint16_t>(col * TILE_ROWS + row);
}

std::int8_t d2d_offset(std::int8_t   w_q,
                       std::uint16_t addr,
                       std::uint32_t noise_seed,
                       std::uint8_t  d2d_sigma) {
    const std::uint16_t h = cell_hash16(addr, hash_seed(noise_seed, SEED_D2D));

    // Two hash bytes summed: triangular on 0..510, centred to -255..+255.
    // write-path-spec §5.1: ih9 = h[7:0] + h[15:8] - 255.
    const int ih9 = static_cast<int>(h & 0xFFu) + static_cast<int>(h >> 8) - 255;

    // Scale by D2D_SIGMA / 256 (§5.2): a 9x9 signed multiply, then >>> 8.
    // |ih9 * sigma| <= 255 * 255 = 65025, so the product fits in an int and
    // the scaled offset lies in -255..+254.
    const int scaled = asr(ih9 * static_cast<int>(d2d_sigma), 8);

    // Saturating add (§5.2). Never wrap: +127 plus a positive offset must
    // stay +127, not become -128.
    const int sum = static_cast<int>(w_q) + scaled;
    const int sat = sum > 127 ? 127 : (sum < -128 ? -128 : sum);
    return static_cast<std::int8_t>(sat);
}

std::int8_t apply_stuck(std::int8_t   w_var,
                        std::uint16_t addr,
                        std::uint32_t noise_seed,
                        std::uint32_t stuck_rate) {
    // Hit test (§5.3): a cell is stuck when its stuck hash falls below
    // STUCK_RATE[15:0], so the fault rate is rate / 65536. A rate of 0 never
    // hits; the largest rate, 65535, misses only the one cell whose hash is
    // 0xFFFF. Raising the rate only ever adds stuck cells.
    const std::uint16_t h_stuck = cell_hash16(addr, hash_seed(noise_seed, SEED_STUCK));
    if (h_stuck >= (stuck_rate & 0xFFFFu))
        return w_var;                                   // not stuck: unchanged

    // Mode, STUCK_RATE[16]: 0 = stuck-at-zero (fully depolarized cell).
    if (((stuck_rate >> 16) & 1u) == 0u)
        return 0;

    // Mode 1 = stuck-at-rail, w = +/-127. The sign comes from the D2D hash's
    // top bit, because every bit of h_stuck is used by the compare above.
    // Convention (recorded in write-path-spec §5.3): h_d2d[15] = 1 means
    // -127, i.e. the sign of h_d2d read as an int16. The D2D hash is computed
    // here whether or not NOISE_EN.d2d is set; that is deliberate, and the
    // RTL must do the same.
    const std::uint16_t h_d2d = cell_hash16(addr, hash_seed(noise_seed, SEED_D2D));
    return (h_d2d & 0x8000u) ? static_cast<std::int8_t>(-127)
                             : static_cast<std::int8_t>(127);
}

} // namespace fecim
