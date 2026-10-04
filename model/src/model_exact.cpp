#include "fecim/model_exact.h"

#include <stdexcept>

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
    // TODO(lane C): write-path transform (quant, d2d via cell_hash16 on the
    // GLOBAL cell address, stuck-at). Applied at write time, as in the RTL.
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
    if (cfg_.noise_en != NOISE_NONE)
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
