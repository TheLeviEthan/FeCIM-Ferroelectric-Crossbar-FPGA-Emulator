// read_path.cpp -- see fecim/read_path.h.
//
// Week-2 interface stubs. Each throws rather than returning a pass-through
// value, so an L1/L2 comparison cannot pass for the wrong reason before the
// arithmetic exists. Replace the bodies in week 3; keep the signatures.

#include "fecim/read_path.h"

#include <stdexcept>

namespace fecim {

std::int8_t read_noise_q(std::uint32_t, std::uint8_t) {
    throw std::logic_error("read_noise_q: not implemented yet (week 3)");
}

std::uint8_t ir_attenuate(std::uint8_t, std::uint8_t) {
    throw std::logic_error("ir_attenuate: not implemented yet (week 3)");
}

std::int32_t adc_quantize(std::int32_t, std::uint8_t) {
    throw std::logic_error("adc_quantize: not implemented yet (week 3)");
}

} // namespace fecim
