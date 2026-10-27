#include "fecim/model_exact.h"

#include <stdexcept>

#include "fecim/write_path.h"

namespace fecim {

namespace {
std::size_t widx(int row, int col) {
    return static_cast<std::size_t>(col) * TILE_ROWS + static_cast<std::size_t>(row);
}
void check_row(int row) {
    if (row < 0 || row >= TILE_ROWS) throw std::out_of_range("row out of range");
}
void check_col(int col) {
    if (col < 0 || col >= TILE_COLS) throw std::out_of_range("col out of range");
}
} // namespace

ModelExact::ModelExact()
    : weights_(static_cast<std::size_t>(TILE_ROWS) * TILE_COLS, 0),
      act_(TILE_ROWS, 0),
      atten_(TILE_ROWS, 0xFF) {}

void ModelExact::set_config(const Config& cfg) {
    if (cfg.active_rows < 1 || cfg.active_rows > TILE_ROWS)
        throw std::invalid_argument("active_rows out of range");
    if (cfg.active_cols < 1 || cfg.active_cols > TILE_COLS)
        throw std::invalid_argument("active_cols out of range");
    if (cfg.quant_levels < 2 || cfg.quant_levels > QUANT_LEVELS_MAX)
        throw std::invalid_argument("quant_levels out of range");
    cfg_ = cfg;
}

void ModelExact::write_weight(int row, int col, std::int8_t w) {
    check_row(row);
    check_col(col);
    // Write-path transform, in the RTL's stage order (write-path-spec §3).
    // Applied once, at write time, with the config in effect NOW: changing
    // quant_levels later does not re-quantize cells already written, exactly
    // as in the RTL. With every enable clear this is a pure pass-through,
    // which is what the L0 gate depends on.
    //
    // Stage 1 uses the QUANT_MULT *register* value, not
    // quant_mult_for(quant_levels): the RTL uses whatever the host wrote,
    // so the model must too, even for a mismatched pair.
    if (cfg_.noise_en & NOISE_QUANT)
        w = quantize_weight(w,
                            static_cast<std::uint8_t>(cfg_.quant_levels),
                            cfg_.quant_mult);
    // TODO(lane C, week 3): stage 2, d2d via cell_hash16 on the GLOBAL cell
    //                       address (col * TILE_ROWS + row).
    // TODO(lane C, week 3): stage 3, stuck-at. Overrides stages 1 and 2.
    weights_[widx(row, col)] = w;
}

void ModelExact::write_activation(int row, std::uint8_t a) {
    check_row(row);
    act_[static_cast<std::size_t>(row)] = a;
}

void ModelExact::write_atten(int row, std::uint8_t coeff) {
    check_row(row);
    atten_[static_cast<std::size_t>(row)] = coeff;
}

std::int8_t ModelExact::stored_weight(int row, int col) const {
    check_row(row);
    check_col(col);
    return weights_[widx(row, col)];
}

std::vector<std::int32_t> ModelExact::compute() {
    // Non-idealities implemented so far. Anything else still throws rather
    // than silently returning clean results, which would let an L1/L2
    // comparison pass for the wrong reason. Add each bit as it lands.
    //
    // Quant needs nothing in this function: it was applied to the stored
    // weights in write_weight(), so the clean MAC loop below is already
    // correct for it.
    constexpr unsigned kImplemented = NOISE_QUANT;
    if ((static_cast<unsigned>(cfg_.noise_en) & ~kImplemented) != 0u)
        throw std::logic_error("model_exact: non-idealities not implemented yet");

    // Clean path. Integer addition is associative, so row order does not
    // matter for the clean case; once read noise lands, iterate in the RTL's
    // sequence (pass-major, row-minor) because the LFSR advances per row.
    std::vector<std::int32_t> y(static_cast<std::size_t>(cfg_.active_cols), 0);
    for (int c = 0; c < cfg_.active_cols; ++c) {
        std::int32_t acc = 0;
        for (int r = 0; r < cfg_.active_rows; ++r)
            acc += std::int32_t{weights_[widx(r, c)]} *
                   std::int32_t{act_[static_cast<std::size_t>(r)]};
        y[static_cast<std::size_t>(c)] = acc;
    }
    return y;
}

} // namespace fecim
