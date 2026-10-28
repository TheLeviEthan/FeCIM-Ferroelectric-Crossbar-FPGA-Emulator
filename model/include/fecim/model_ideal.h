// fecim/model_ideal.h
//
// The mathematical oracle: what a perfect crossbar computes. A plain
// matrix-vector product with no non-idealities and no hardware structure --
// no lanes, no passes, no pipeline, no tile size. It answers "what SHOULD
// this compute?" (model-verification-design §1), and is used to validate
// model_exact in the clean case: the model side of the L0 gate.
//
// Keep it simple and obviously correct. Everything else is measured against
// it, so a clever implementation here would be the one thing nobody checks.
//
// Conventions (both overloads):
//
//   weights  rows x cols, ROW-MAJOR: weights[r * cols + c] is the cell on
//            word line r, bit line c. (model_exact stores column-major, as
//            the bulk stream does; using a different layout here is
//            deliberate, so a shared indexing bug cannot hide.)
//   act      one value per row (word line).
//   result   one value per column (bit line):  y[c] = sum_r w[r][c] * a[r]
//
// Shape errors (rows or cols < 1, or vector sizes that do not match)
// throw std::invalid_argument. Unlike model_exact's datapath functions,
// this is not mirroring RTL behaviour, so validating is the right call.

#pragma once

#include <cstdint>
#include <vector>

namespace fecim {

// Integer-code oracle. Weights in the int8 range, activations in the uint8
// range, exactly as the host writes them. Accumulates in int64, so it cannot
// overflow for any geometry this project will ever use, which makes it
// independent of the RTL's 32-bit accumulator: if ACC_W were ever wrong,
// this oracle would not share the bug.
std::vector<std::int64_t> ideal_mvm(const std::vector<std::int8_t>&  weights,
                                    const std::vector<std::uint8_t>& act,
                                    int rows, int cols);

// Floating-point oracle: real-valued ("ideal") weights and activations, for
// asking what an unquantized network would compute (accuracy baselines for
// Lane D's sweeps).
//
// Exactness note. On integer-valued inputs in the clean range this returns
// exactly the integer oracle's result: every product and partial sum is an
// integer below 2^53 (|y| <= 128 * 128 * 255 ~ 2^22), so float64 represents
// it exactly and summation order cannot matter. The same argument is why a
// NumPy float64 matmul can serve as an L0 reference. On general real inputs
// the usual floating-point caveats apply: results are accurate to rounding,
// not bit-reproducible across summation orders.
std::vector<double> ideal_mvm(const std::vector<double>& weights,
                              const std::vector<double>& act,
                              int rows, int cols);

} // namespace fecim
