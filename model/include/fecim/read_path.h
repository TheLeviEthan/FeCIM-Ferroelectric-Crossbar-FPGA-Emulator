// fecim/read_path.h
//
// C++ mirror of the READ-TIME non-idealities: the ones applied on every
// COMPUTE rather than once when a weight is written. Counterpart to
// fecim/write_path.h. Three effects, at three places in the RTL:
//
//   activation buffer --> ir_attenuate() --> broadcast to all lanes
//   each mac_lane     --> read_noise_q() added to the stored weight
//   drain-chain output --> adc_quantize() --> result buffer / argmax
//
// All three are implemented and tested, and pinned by the golden vectors in
// tb/golden/read_noise/, tb/golden/ir_drop/ and tb/golden/adc/ that the RTL
// is tested against. ModelExact::compute() applies them in the RTL's order.
//
// Same ground rules as write_path.h (model-verification-design §2):
// integer only, argument types = register widths, no input validation in
// datapath functions (the RTL computes something for any register value).

#pragma once

#include <cstdint>

namespace fecim {

// ---------------------------------------------------------------------------
// read_noise_q(lfsr, read_sigma)  -- mac_lane, registered in cycle N+1
//
// Maps one lane-LFSR state to that cycle's noise sample (mac-array-spec
// §5.3):
//
//   ih_sum  = lfsr[7:0] + lfsr[15:8] + lfsr[23:16] + lfsr[31:24]   0..1020
//   ih9     = (ih_sum - 510) >>> 1                                 +/-255
//   scaled  = (ih9 * {0, read_sigma}) >>> 9           READ_SIGMA / 512, NOT Q1.8
//   noise_q = clamp(scaled, -127, +127)               the clamp keeps w_n 9 bits
//
// Pure function of the state. WHEN the LFSR advances (on acc_en only, never
// free-running) is ModelExact's job, via fecim::lfsr_step().
//
// Alignment (mac-array-spec §5.5, agreed with Lane B): the LFSR steps on the
// clock edge that ends the cycle a row is issued in, and that row's sample
// is computed from the NEW state in the next cycle. So the row's noise is
// read_noise_q(lfsr_step(state_before_the_row)), and the first row after a
// reset or reseed uses lfsr_step(lane_seed(...)), never the seed itself.
//
// Range: with an 8-bit sigma, scaled is -128..+127, so the clamp only ever
// turns -128 into -127 (ih_sum <= 1 at sigma 255).
std::int8_t read_noise_q(std::uint32_t lfsr, std::uint8_t read_sigma);

// ---------------------------------------------------------------------------
// ir_attenuate(act, coeff)  -- activation broadcast path, shared by all lanes
//
// Row-position IR drop applied to the activation, not the product
// (mac-array-spec §8): act_atten = (act * atten_rom[row]) >> 8, coeff in
// Q0.8. Algebraically identical to attenuating each product, at the cost of
// one multiplier instead of sixty-four. Reset coefficient 0xFF (0.996) is
// NOT the identity; the identity is NOISE_EN.ir = 0.
//
// "Algebraically identical" holds before rounding. With the >> 8 it is not
// bit-identical to attenuating each product: (w * ((a * k) >> 8)) and
// ((w * a * k) >> 8) differ in the low bits. The model and the RTL both
// attenuate the activation, so they agree with each other; the report must
// describe the approximation in those terms.
std::uint8_t ir_attenuate(std::uint8_t act, std::uint8_t coeff);

// ---------------------------------------------------------------------------
// adc_quantize(acc, adc_bits)  -- one shared instance at the drain output
//
// Round-to-nearest over the USED accumulator range ACC_USED_W, not the
// 32-bit container (mac-array-spec §9):
//
//   sh  = adc_bits >= ACC_USED_W ? 0 : ACC_USED_W - adc_bits
//   out = sh == 0 ? acc : ((acc + (1 << (sh-1))) >>> sh) << sh
//
// (x + half) >> sh, not round(x / 2^sh): they differ on negative halves.
// adc_bits >= ACC_USED_W must be an exact no-op. Only adc_bits[4:0] is
// used, because that is the RTL port's width: ADC_BITS = 32 arrives as 0 and
// quantizes to one bit. The driver keeps the register in 4..ACC_USED_W.
//
// Arithmetic is 32-bit and wraps exactly as the RTL's adder does; inside
// the accumulator's used range (+/-8,290,560) nothing wraps.
std::int32_t adc_quantize(std::int32_t acc, std::uint8_t adc_bits);

} // namespace fecim
