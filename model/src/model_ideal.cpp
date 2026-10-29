#include "fecim/model_ideal.h"

#include <cstddef>
#include <stdexcept>

namespace fecim {

namespace {

// Shared by both overloads, so the integer and float oracles can never
// disagree about what a valid shape is.
void check_shape(std::size_t n_weights, std::size_t n_act, int rows, int cols) {
    if (rows < 1 || cols < 1)
        throw std::invalid_argument("ideal_mvm: rows and cols must be >= 1");
    const auto r = static_cast<std::size_t>(rows);
    const auto c = static_cast<std::size_t>(cols);
    if (n_weights != r * c)
        throw std::invalid_argument("ideal_mvm: weights.size() != rows * cols");
    if (n_act != r)
        throw std::invalid_argument("ideal_mvm: act.size() != rows");
}

} // namespace

std::vector<std::int64_t> ideal_mvm(const std::vector<std::int8_t>&  weights,
                                    const std::vector<std::uint8_t>& act,
                                    int rows, int cols) {
    check_shape(weights.size(), act.size(), rows, cols);
    const auto R = static_cast<std::size_t>(rows);
    const auto C = static_cast<std::size_t>(cols);

    std::vector<std::int64_t> y(C, 0);
    for (std::size_t r = 0; r < R; ++r)
        for (std::size_t c = 0; c < C; ++c)
            y[c] += std::int64_t{weights[r * C + c]} * std::int64_t{act[r]};
    return y;
}

std::vector<double> ideal_mvm(const std::vector<double>& weights,
                              const std::vector<double>& act,
                              int rows, int cols) {
    check_shape(weights.size(), act.size(), rows, cols);
    const auto R = static_cast<std::size_t>(rows);
    const auto C = static_cast<std::size_t>(cols);

    std::vector<double> y(C, 0.0);
    for (std::size_t r = 0; r < R; ++r)
        for (std::size_t c = 0; c < C; ++c)
            y[c] += weights[r * C + c] * act[r];
    return y;
}

} // namespace fecim
