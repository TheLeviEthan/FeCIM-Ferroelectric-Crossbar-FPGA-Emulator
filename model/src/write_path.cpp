// write_path.cpp -- see fecim/write_path.h for what each function means.
//
// Integer arithmetic only (model-verification-design §2). Intermediates are
// std::uint32_t so the build stays clean under -Wconversion and
// -Wsign-conversion; narrowing happens once, explicitly, at each return.

#include "fecim/write_path.h"

#include <stdexcept>

#include "fecim/params.h"

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

// ---- week-3 stubs ---------------------------------------------------------
// Signatures are final; bodies land with the hash/LFSR work in week 3.
// Throwing (rather than returning the input unchanged) means nothing can
// pass an L1/L2 comparison by accident before they exist.

std::uint16_t cell_addr(int, int) {
    throw std::logic_error("cell_addr: not implemented yet (week 3)");
}

std::int8_t d2d_offset(std::int8_t, std::uint16_t, std::uint32_t, std::uint8_t) {
    throw std::logic_error("d2d_offset: not implemented yet (week 3)");
}

std::int8_t apply_stuck(std::int8_t, std::uint16_t, std::uint32_t, std::uint32_t) {
    throw std::logic_error("apply_stuck: not implemented yet (week 3)");
}

} // namespace fecim
