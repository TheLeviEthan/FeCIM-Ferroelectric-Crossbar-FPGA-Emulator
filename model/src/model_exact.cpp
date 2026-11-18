#include "fecim/model_exact.h"

#include <algorithm>
#include <stdexcept>
#include <string>

#include "fecim/primitives.h"
#include "fecim/read_path.h"
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
      atten_(TILE_ROWS, 0xFF),
      lfsr_(NUM_LANES, 0) {
    reseed();   // reset: every LFSR loads lane_seed(NOISE_SEED = 0, l)
}

void ModelExact::reseed() {
    for (int l = 0; l < NUM_LANES; ++l)
        lfsr_[static_cast<std::size_t>(l)] = lane_seed(cfg_.noise_seed, l);
}

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
    //
    // Stages 2 and 3 key their hashes on the GLOBAL cell address
    // (col * TILE_ROWS + row, the bulk_addr of the weight stream) and on the
    // NOISE_SEED in effect now. Reseeding later does not re-randomize cells
    // already written; the host reloads weights after changing the seed,
    // D2D_SIGMA or STUCK_RATE, just as with QUANT_LEVELS.
    const std::uint16_t addr = cell_addr(row, col);
    if (cfg_.noise_en & NOISE_QUANT)
        w = quantize_weight(w,
                            static_cast<std::uint8_t>(cfg_.quant_levels),
                            cfg_.quant_mult);
    if (cfg_.noise_en & NOISE_D2D)
        w = d2d_offset(w, addr, cfg_.noise_seed, cfg_.d2d_sigma);
    if (cfg_.noise_en & NOISE_STUCK)                  // overrides stages 1 and 2
        w = apply_stuck(w, addr, cfg_.noise_seed, cfg_.stuck_rate);
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
    // All six non-idealities. Quant, D2D and stuck-at are programming-time
    // effects already baked into weights_ by write_weight(); this function
    // adds the three read-time ones, each where the RTL applies it:
    //
    //   act_buffer : a = NOISE_EN.ir ? ir_attenuate(act[r], atten[r]) : act[r]
    //   mac_lane   : w_n = w + (NOISE_EN.read ? noise_q : 0);  acc += w_n * a
    //   drain      : y = NOISE_EN.adc ? adc_quantize(acc, ADC_BITS) : acc
    //
    // Iteration order is the RTL's, because the read-noise LFSRs make it
    // observable: pass-major, row-minor, and on every issued row EVERY lane
    // steps its LFSR -- all NUM_LANES lanes (including those whose column is
    // at or beyond active_cols, which still accumulate garbage that argmax
    // masks), on every pass, whether or not NOISE_EN.read is set. The row's
    // sample comes from the state after its step (read_path.h, alignment).
    const bool en_read = (cfg_.noise_en & NOISE_READ) != 0;
    const bool en_ir   = (cfg_.noise_en & NOISE_IR)   != 0;
    const bool en_adc  = (cfg_.noise_en & NOISE_ADC)  != 0;
    // ADC_BITS is a 5-bit port in the RTL; adc_quantize() uses bits [4:0].
    const auto adc_bits = static_cast<std::uint8_t>(cfg_.adc_bits & 0x1F);

    // The sequencer rounds the pass count up and drains full lane groups
    // (control-fsm-spec §3).
    const int passes = (cfg_.active_cols + NUM_LANES - 1) / NUM_LANES;

    std::vector<std::int32_t> y(static_cast<std::size_t>(cfg_.active_cols), 0);
    std::vector<std::int32_t> acc(static_cast<std::size_t>(NUM_LANES));

    for (int p = 0; p < passes; ++p) {
        std::fill(acc.begin(), acc.end(), 0);                  // CLEAR, once per pass
        for (int r = 0; r < cfg_.active_rows; ++r) {           // ACC, one row per cycle
            const std::uint8_t a_raw = act_[static_cast<std::size_t>(r)];
            const std::int32_t a = en_ir
                ? std::int32_t{ir_attenuate(a_raw, atten_[static_cast<std::size_t>(r)])}
                : std::int32_t{a_raw};
            for (int l = 0; l < NUM_LANES; ++l) {
                auto& s = lfsr_[static_cast<std::size_t>(l)];
                s = lfsr_step(s);                              // acc_en: every lane steps
                std::int32_t w_n = weights_[widx(r, p * NUM_LANES + l)];
                if (en_read) w_n += read_noise_q(s, cfg_.read_sigma);   // |w_n| <= 254
                acc[static_cast<std::size_t>(l)] += w_n * a;
            }
        }
        for (int l = 0; l < NUM_LANES; ++l) {                  // DRAIN, through the shared ADC
            const int c = p * NUM_LANES + l;
            if (c >= cfg_.active_cols) continue;               // drained but never read
            const std::int32_t v = acc[static_cast<std::size_t>(l)];
            y[static_cast<std::size_t>(c)] = en_adc ? adc_quantize(v, adc_bits) : v;
        }
    }
    return y;
}

// ---- interface stubs (week 2) ---------------------------------------------
// Signatures are the co-sim / sim-backend contract; bodies land in week 3.
// Each throws rather than returning an idle-looking default, so a harness
// written against the stub cannot report a vacuous pass.

namespace {
[[noreturn]] void not_yet(const char* fn) {
    throw std::logic_error(std::string("model_exact: ") + fn +
                           " not implemented yet (week 3)");
}
} // namespace

void ModelExact::start_compute()                     { not_yet("start_compute"); }
void ModelExact::step()                              { not_yet("step"); }
CtrlState ModelExact::state() const                  { not_yet("state"); }
bool ModelExact::busy() const                        { not_yet("busy"); }
int ModelExact::seq_cnt() const                      { not_yet("seq_cnt"); }
ModelExact::LaneProbe ModelExact::lane(int) const    { not_yet("lane"); }
std::uint32_t ModelExact::cycle_count() const        { not_yet("cycle_count"); }
std::vector<std::int32_t> ModelExact::results() const { not_yet("results"); }
ModelExact::Argmax ModelExact::argmax() const        { not_yet("argmax"); }
Status ModelExact::set_reg(std::uint8_t, std::uint32_t)        { not_yet("set_reg"); }
Status ModelExact::get_reg(std::uint8_t, std::uint32_t&) const { not_yet("get_reg"); }

} // namespace fecim
