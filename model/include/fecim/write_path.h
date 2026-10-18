// fecim/write_path.h
//
// C++ mirror of rtl/write/write_path.sv (Lane B's RTL; spec:
// docs/plans and specs/03-lane-b-array/write-path-spec.md).
//
// The write path applies the PROGRAMMING-TIME non-idealities to a weight as
// it is written into a lane memory. Three stages, always in this order
// (write-path-spec §3):
//
//     w_ideal --> 1. quantize --> 2. d2d offset --> 3. stuck-at --> w_stored
//
// Stage 1 (quantization) is implemented and tested. Stages 2 and 3 will get
// their own functions in this header later (d2d_offset(), apply_stuck()). Each stage is its own
// free function so it can be unit-tested alone; ModelExact::write_weight()
// is the only place that chains them.
//
// Ground rules (model-verification-design §2) for everything in this file:
//
//   * INTEGER ONLY. No float, no double, no std::round, no <cmath>. Every
//     "multiply by a fraction" is an integer multiply followed by a right
//     shift, exactly as the RTL does it. A float version would agree with
//     the RTL on most inputs and disagree by 1 LSB on a few, which is the
//     worst kind of mismatch to debug.
//
//   * ARGUMENT TYPES = REGISTER WIDTHS. quant_levels is an 8-bit register,
//     quant_mult a 16-bit one, so the parameters are uint8_t / uint16_t.
//     Any narrowing happens visibly at the call site, not silently inside.
//
//   * NO INPUT VALIDATION in the datapath functions. The RTL computes
//     *something* for any register value, and the model must compute the
//     same thing, even for a nonsense config. Range checks live in
//     ModelExact::set_config(), not here.

#pragma once

#include <cstdint>

namespace fecim {

// ---------------------------------------------------------------------------
// quant_mult_for(n_levels)  -- HOST arithmetic, not datapath
//
// Returns the QUANT_MULT register value that pairs with QUANT_LEVELS = n:
//
//     QUANT_MULT = round(255 * 256 / (n - 1))        Q8.8, fits 16 bits
//
// The FPGA has no divider, so the host precomputes this reciprocal and
// writes it alongside QUANT_LEVELS (protocol.md §6.2: the two registers are
// always written together). Lane D's Python set_quantization() computes the
// same thing. This C++ copy exists so tests and the future `sim` backend can
// build a consistent (N, QUANT_MULT) pair without hard-coding numbers.
//
// It is still integer, because the C++ model, the Python driver, and the
// golden fixtures all have to agree on the exact value. "round" is written
// as integer round-half-up:
//
//     (numerator + denominator / 2) / denominator
//
// Expected values (the tests pin these):
//     n = 2 -> 65280    n = 3 -> 32640    n = 5 -> 16320
//     n = 8 -> 9326     n = 255 -> 257 (the register reset value)
//
// Precondition 2 <= n <= 255. n = 1 divides by zero. Unlike the datapath
// functions, throwing std::invalid_argument here is fine: this is host-side
// arithmetic, not a mirror of RTL behaviour.
std::uint16_t quant_mult_for(int n_levels);

// ---------------------------------------------------------------------------
// quantize_weight(w_ideal, quant_levels, quant_mult)  -- stage 1, datapath
//
// Snaps a signed 8-bit weight onto one of N evenly spaced levels spanning
// the int8 range (write-path-spec §4.1). Four integer steps, all done in
// unsigned "offset binary" so there are no signed shifts to worry about:
//
//   1. u      = w_ideal + 128                 0 .. 255    int8 -> uint8
//   2. level  = (u * N) >> 8                  0 .. N-1    which bin u is in
//   3. scaled = (level * QUANT_MULT) >> 8     0 .. 255    bin -> output code
//   4. w_q    = (scaled & 0xFF) - 128         int8        back to signed
//
// What each step means:
//   step 2 is floor(u * N / 256): "divide the 0..255 range into N equal
//          bins", done as multiply-then-shift so no divider is needed.
//   step 3 is "multiply the bin index by 255/(N-1)". QUANT_MULT is that
//          ratio pre-scaled by 256 (Q8.8), and the >> 8 removes the scale.
//          So bin 0 -> code 0 (w = -128) and bin N-1 -> code ~255 (w ~ +127).
//
// Why N = 3 and N = 5 are in the tests: for a power of two, step 2's bin
// edges fall on multiples of 256/N (e.g. N = 8 -> every 32), so a buggy
// implementation that is "really" a shift can still pass. For N = 3 and 5,
// the bin edges (-42, 43 / -76, -25, 26, 77) and QUANT_MULT values land
// nowhere near a power of two, so the reciprocal arithmetic is actually
// exercised.
//
// Properties of THIS formula (the model reproduces them; it does not fix
// them -- the RTL must match the model, and any change is a spec change):
//   * The bottom level is always -128.
//   * The top level is +127 for most N, but +126 when QUANT_MULT rounds
//     down (e.g. N = 10, 23, 255).
//   * For odd N the middle level is -1, not 0, so w = 0 quantizes to -1.
//   * N = 255 is NOT the identity: every w > -128 comes back as w - 1.
//     "No quantization" is the bypass (NOISE_EN.quant = 0), not N = 255.
//   * A mismatched (quant_levels, quant_mult) pair is not rejected: the
//     code wraps modulo 256 in step 4, because the RTL keeps scaled[7:0].
//     Only product bits [15:8] survive, so the result does not depend on
//     the RTL's product width as long as it is at least 16 bits.
std::int8_t quantize_weight(std::int8_t   w_ideal,
                            std::uint8_t  quant_levels,
                            std::uint16_t quant_mult);

} // namespace fecim
