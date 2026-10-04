// fecim/model_ideal.h
//
// The mathematical oracle: what a perfect crossbar computes. Plain int64
// matrix-vector product, no non-idealities, no hardware structure.
// Used only to validate model_exact in the clean case (L0).

#pragma once

#include <cstdint>
#include <vector>

namespace fecim {

// weights: rows x cols, row-major, signed 8-bit range.
// act:     rows, unsigned 8-bit range.
// returns: cols, y[c] = sum_r w[r][c] * a[r]
inline std::vector<std::int64_t> ideal_mvm(const std::vector<std::int8_t>& weights,
                                           const std::vector<std::uint8_t>& act,
                                           int rows, int cols) {
    std::vector<std::int64_t> y(static_cast<std::size_t>(cols), 0);
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
            y[static_cast<std::size_t>(c)] +=
                std::int64_t{weights[static_cast<std::size_t>(r * cols + c)]} *
                std::int64_t{act[static_cast<std::size_t>(r)]};
    return y;
}

} // namespace fecim
