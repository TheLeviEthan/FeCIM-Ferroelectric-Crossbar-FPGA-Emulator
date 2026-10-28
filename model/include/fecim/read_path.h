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
// STATUS (week 2): INTERFACE STUB. Signatures and types are final so the
// co-sim harness and ModelExact::step() can be written against them; every
// body throws std::logic_error until the week-3 non-ideality work fills it.
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
// STUB (week 3): throws std::logic_error.
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
// STUB (week 3): throws std::logic_error.
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
// adc_bits >= ACC_USED_W must be an exact no-op. adc_bits is the 5-bit
// register value.
//
// STUB (week 3): throws std::logic_error.
std::int32_t adc_quantize(std::int32_t acc, std::uint8_t adc_bits);

} // namespace fecim
