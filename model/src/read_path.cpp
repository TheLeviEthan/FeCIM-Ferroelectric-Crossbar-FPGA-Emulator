// read_path.cpp -- see fecim/read_path.h.
//
// The three read-time non-idealities. Same rules as write_path.cpp: integer
// arithmetic only, the RTL's widths, shifts, clamps and rounding, and no
// input validation (the RTL computes something for any register value, so
// the model computes the same thing).

#include "fecim/read_path.h"

#include <cstdint>

#include "fecim/params.h"

namespace fecim {

namespace {

// Arithmetic shift right, rounding toward minus infinity like SV `>>>` on a
// signed operand. Spelled out because `>>` on a negative int is
// implementation-defined before C++20. For x < 0, ~x = -x - 1 >= 0.
constexpr int asr(int x, int n) {
    return x >= 0 ? (x >> n) : ~((~x) >> n);
}
static_assert(asr(-1, 1) == -1 && asr(-3, 1) == -2 && asr(3, 1) == 1 &&
              asr(-512, 9) == -1 && asr(-513, 9) == -2,
              "asr must floor, as SV >>> does");

// The same, for the 32-bit ADC datapath.
constexpr std::int32_t asr32(std::int32_t x, int n) {
    return x >= 0 ? static_cast<std::int32_t>(x >> n)
                  : static_cast<std::int32_t>(~((~x) >> n));
}

// uint32 -> int32 two's complement, without the implementation-defined
// out-of-range conversion of C++17. The RTL's 32-bit adder wraps; so must we.
constexpr std::int32_t to_i32(std::uint32_t u) {
    return u <= 0x7FFF'FFFFu ? static_cast<std::int32_t>(u)
                             : static_cast<std::int32_t>(-static_cast<std::int64_t>(~u) - 1);
}
static_assert(to_i32(0xFFFF'FFFFu) == -1 && to_i32(0x8000'0000u) == -2147483647 - 1,
              "two's complement wrap");

} // namespace

std::int8_t read_noise_q(std::uint32_t lfsr, std::uint8_t read_sigma) {
    // mac-array-spec §5.3. Four bytes of ONE LFSR state, summed: bell-shaped
    // and bounded on 0..1020, but the bytes are linearly related, so this is
    // not a true Irwin-Hall distribution (report measured moments).
    const int ih_sum = static_cast<int>(lfsr & 0xFFu) +
                       static_cast<int>((lfsr >> 8) & 0xFFu) +
                       static_cast<int>((lfsr >> 16) & 0xFFu) +
                       static_cast<int>(lfsr >> 24);
    const int ih9 = asr(ih_sum - 510, 1);                 // +/-510 -> +/-255, floor

    // READ_SIGMA / 512 (NOT Q1.8): 9x9 signed multiply, then >>> 9.
    // |ih9 * sigma| <= 255 * 255, so the scaled value lies in -128..+127.
    const int scaled = asr(ih9 * static_cast<int>(read_sigma), 9);

    // Clamp to +/-NOISE_CLAMP so w + noise stays 9 bits signed (§5.4). With
    // an 8-bit sigma, scaled lies in -128..+127 and the only value the clamp
    // ever changes is -128 -> -127 (ih9 = -255, i.e. ih_sum <= 1, at
    // sigma = 255). It is reachable -- LFSR state 0x00000001 -- and the RTL
    // has the clamp, so the model does too.
    const int q = scaled > NOISE_CLAMP ? NOISE_CLAMP
                : (scaled < -NOISE_CLAMP ? -NOISE_CLAMP : scaled);
    return static_cast<std::int8_t>(q);
}

std::uint8_t ir_attenuate(std::uint8_t act, std::uint8_t coeff) {
    // mac-array-spec §8: act * coeff (Q0.8), >> 8. Both operands are
    // non-negative 8-bit values, so the product is 0..65025 and the result
    // 0..254; the RTL takes p[15:8] of the same product.
    return static_cast<std::uint8_t>((static_cast<unsigned>(act) *
                                      static_cast<unsigned>(coeff)) >> 8);
}

std::int32_t adc_quantize(std::int32_t acc, std::uint8_t adc_bits) {
    // mac-array-spec §9. The RTL port is adc_bits[4:0], so only the low five
    // bits of the register value exist in hardware: ADC_BITS = 32 reaches
    // the block as 0 and quantizes to a single bit, it is NOT a no-op.
    const int bits = adc_bits & 0x1F;
    const int sh = bits >= ACC_USED_W ? 0 : ACC_USED_W - bits;   // 0 .. 24
    if (sh == 0) return acc;                                   // exact no-op

    // ((acc + half) >>> sh) <<< sh in 32-bit wrapping arithmetic, exactly as
    // the RTL's 32-bit adder and shifters. Within the accumulator's used
    // range (+/-8,290,560) nothing wraps; the wrap only defines the result
    // for arbitrary int32 inputs. (x + half) >> sh rounds negative ties up
    // (toward +infinity): that is the RTL's rounding, keep it.
    const std::uint32_t half = std::uint32_t{1} << (sh - 1);
    const std::int32_t  sum  = to_i32(static_cast<std::uint32_t>(acc) + half);
    const std::int32_t  code = asr32(sum, sh);
    return to_i32(static_cast<std::uint32_t>(code) << sh);
}

} // namespace fecim
