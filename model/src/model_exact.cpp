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
      lfsr_(NUM_LANES, 0),
      lanes_(NUM_LANES),
      rbuf_(TILE_COLS, 0) {
    reseed();   // reset: every LFSR loads lane_seed(NOISE_SEED = 0, l)
}

// ---- effective geometry: control_fsm's decode of TILE_CFG -------------------
// The registers hold any 16-bit value (set_reg stores TILE_CFG raw, as
// config_regs does). The FSM reads 0 or more than the tile as the full tile:
//   num_rows     = (active_rows == 0 || active_rows > TILE_ROWS) ? TILE_ROWS : active_rows
//   total_passes = (active_cols == 0 || active_cols > NUM_LANES) ? 2 : 1
// The result count and the argmax mask use the same reading of active_cols.
int ModelExact::rows_eff() const {
    const int r = cfg_.active_rows;
    return (r <= 0 || r > TILE_ROWS) ? TILE_ROWS : r;
}
int ModelExact::cols_eff() const {
    const int c = cfg_.active_cols;
    return (c <= 0 || c > TILE_COLS) ? TILE_COLS : c;
}
int ModelExact::passes_eff() const {
    return (cols_eff() + NUM_LANES - 1) / NUM_LANES;
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
    if (seq_.st != CTRL_IDLE || start_pending_)
        throw std::logic_error("model_exact: compute() while a stepped COMPUTE is in progress");

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
    const int rows   = rows_eff();
    const int passes = passes_eff();

    std::vector<std::int32_t> acc(static_cast<std::size_t>(NUM_LANES));
    argmax_reset();                                            // run_start
    results_valid_ = false;
    std::int32_t last_prod_a = 0;                              // act of the last row issued

    for (int p = 0; p < passes; ++p) {
        std::fill(acc.begin(), acc.end(), 0);                  // CLEAR, once per pass
        for (int r = 0; r < rows; ++r) {                       // ACC, one row per cycle
            const std::uint8_t a_raw = act_[static_cast<std::size_t>(r)];
            const std::int32_t a = en_ir
                ? std::int32_t{ir_attenuate(a_raw, atten_[static_cast<std::size_t>(r)])}
                : std::int32_t{a_raw};
            for (int l = 0; l < NUM_LANES; ++l) {
                auto& s = lfsr_[static_cast<std::size_t>(l)];
                s = lfsr_step(s);                              // acc_en: every lane steps
                std::int32_t w_n = weights_[widx(r, p * NUM_LANES + l)];
                if (en_read) w_n += read_noise_q(s, cfg_.read_sigma);   // -255 .. +254
                acc[static_cast<std::size_t>(l)] += w_n * a;
            }
            last_prod_a = a;
        }
        for (int l = 0; l < NUM_LANES; ++l) {                  // DRAIN, through the shared ADC
            // Every lane drains into the result buffer, including columns at
            // or beyond active_cols; those are stored but never read back and
            // masked out of argmax.
            const int c = p * NUM_LANES + l;
            const std::int32_t v = acc[static_cast<std::size_t>(l)];
            const std::int32_t q = en_adc ? adc_quantize(v, adc_bits) : v;
            rbuf_[static_cast<std::size_t>(c)] = q;
            argmax_update(c, q);
        }
    }

    // ---- leave the model exactly where the stepped path would ---------------
    // So the two paths can be mixed freely and compared field by field: the
    // sequencer back in IDLE with its counters where control_fsm leaves them,
    // and every pipeline register holding what the last cycles wrote into it.
    const int p_last = passes - 1, r_last = rows - 1;
    const std::uint32_t total = static_cast<std::uint32_t>(
        passes * (1 + rows + LANE_PIPE_DEPTH + NUM_LANES));
    Seq q;                                  // every strobe and pulse low
    q.pass       = p_last;
    q.row_cnt    = rows;
    q.pipe_cnt   = LANE_PIPE_DEPTH - 1;
    q.drain_cnt  = NUM_LANES - 1;
    q.run_cyc    = total;
    q.seq_cnt    = (p_last << ROW_AW) | r_last;
    q.drain_addr = p_last * NUM_LANES + NUM_LANES - 1;
    q.cycle_cnt  = total;
    seq_ = q;
    for (bool& v : valid_) v = false;
    act_rd_     = act_[static_cast<std::size_t>(r_last)];
    act_rd_row_ = r_last;
    act_bcast_  = static_cast<std::uint8_t>(last_prod_a);
    for (int l = 0; l < NUM_LANES; ++l) {
        LaneRegs& g = lanes_[static_cast<std::size_t>(l)];
        g.w_rd    = weights_[widx(r_last, p_last * NUM_LANES + l)];   // seq_cnt parked on the last row
        g.w_align = g.w_rd;
        g.noise_q = read_noise_q(lfsr_[static_cast<std::size_t>(l)], cfg_.read_sigma);
        g.prod    = std::int32_t{w_n_of(g)} * last_prod_a;            // the last row's product
        g.acc     = 0;                                                // 64 shifts of the zero tail
    }
    results_valid_ = true;                                     // compute_done
    ++result_seq_;

    return results();
}

// ============================================================================
// Cycle level
// ============================================================================

std::int16_t ModelExact::w_n_of(const LaneRegs& r) const {
    // mac_lane, cycle N+2 (combinational): sign-extend the aligned weight and
    // add the registered noise sample. -128 + -127 = -255 still fits 9 bits.
    int w = r.w_align;
    if (cfg_.noise_en & NOISE_READ) w += r.noise_q;
    return static_cast<std::int16_t>(w);
}

void ModelExact::argmax_reset() {
    max_val_    = INT32_MIN_;
    second_val_ = INT32_MIN_;
    max_idx_    = 0;
}

void ModelExact::argmax_update(int addr, std::int32_t v) {
    // result-buffer-spec §5: compare as values stream past during DRAIN,
    // strict '>' (ties keep the lowest index), columns >= active_cols masked.
    if (addr >= cols_eff()) return;
    if (v > max_val_) {
        second_val_ = max_val_;
        max_val_    = v;
        max_idx_    = static_cast<std::uint16_t>(addr);
    } else if (v > second_val_) {
        second_val_ = v;
    }
}

void ModelExact::start_compute() { start_pending_ = true; }

void ModelExact::step() {
    // Non-blocking assignment, by hand: every next value below is computed
    // from the CURRENT registers (seq_, lanes_, valid_, act_*, lfsr_ read
    // before written), and all of them are committed together at the end.
    const Seq& q = seq_;
    Seq n = q;

    // ---- control_fsm (rtl/control/control_fsm.sv, line for line) -----------
    const int rows   = rows_eff();
    const int passes = passes_eff();
    n.run_start = false;
    n.done      = false;
    switch (q.st) {
    case CTRL_IDLE:
        n.acc_clear = n.acc_en = n.shift_en = n.drain_we = false;
        n.busy = false;
        if (start_pending_) {
            n.st        = CTRL_CLEAR;
            n.pass      = 0;
            n.run_start = true;
            n.busy      = true;
            n.acc_clear = true;
            n.run_cyc   = 0;
        }
        break;
    case CTRL_CLEAR:
        n.acc_clear = false;
        n.run_cyc   = q.run_cyc + 1;
        n.st        = CTRL_ACC;
        n.acc_en    = true;
        n.row_cnt   = 0;
        n.pipe_cnt  = 0;
        n.seq_cnt   = q.pass << ROW_AW;
        break;
    case CTRL_ACC:
        n.run_cyc = q.run_cyc + 1;
        if (q.row_cnt < rows - 1) {                 // issue the next row
            n.row_cnt = q.row_cnt + 1;
            n.seq_cnt = (q.seq_cnt + 1) & ((1 << SEQ_AW) - 1);
            n.acc_en  = true;
        } else if (q.row_cnt == rows - 1) {         // last row issued this cycle
            n.row_cnt  = q.row_cnt + 1;
            n.acc_en   = false;
            n.pipe_cnt = 0;
        } else {                                    // pipeline hold, LANE_PIPE_DEPTH cycles
            n.acc_en = false;
            if (q.pipe_cnt < LANE_PIPE_DEPTH - 1) {
                n.pipe_cnt = q.pipe_cnt + 1;
            } else {
                n.st         = CTRL_DRAIN;
                n.shift_en   = true;
                n.drain_we   = true;
                n.drain_cnt  = 0;
                n.drain_addr = q.pass * NUM_LANES;
            }
        }
        break;
    case CTRL_DRAIN:
        n.run_cyc  = q.run_cyc + 1;
        n.shift_en = true;
        n.drain_we = true;
        if (q.drain_cnt < NUM_LANES - 1) {
            n.drain_cnt  = q.drain_cnt + 1;
            n.drain_addr = q.pass * NUM_LANES + q.drain_cnt + 1;
        } else {
            n.shift_en = false;
            n.drain_we = false;
            if (q.pass < passes - 1) {
                n.pass      = q.pass + 1;
                n.st        = CTRL_CLEAR;
                n.acc_clear = true;
            } else {
                n.st        = CTRL_DONE;
                n.done      = true;
                n.busy      = false;
                n.cycle_cnt = q.run_cyc + 1;
            }
        }
        break;
    case CTRL_DONE:
        n.busy = false;
        n.st   = CTRL_IDLE;
        break;
    }
    start_pending_ = false;                          // the start input is a pulse

    // ---- act_buffer: N -> N+1 M9K read, N+1 -> N+2 broadcast register ------
    const int act_addr = q.seq_cnt & (TILE_ROWS - 1);
    const std::uint8_t act_rd_n = act_[static_cast<std::size_t>(act_addr)];
    const std::uint8_t act_bcast_n = (cfg_.noise_en & NOISE_IR)
        ? ir_attenuate(act_rd_, atten_[static_cast<std::size_t>(act_rd_row_)])
        : act_rd_;

    // ---- mac_array: 64 lanes and the drain chain ----------------------------
    // Pipeline (mac_lane.sv): N issue -> N+1 w_rd -> N+2 w_align/noise_q, w_n,
    // prod <= w_n * act_bcast when v2 -> N+3 acc += prod when v3.
    // If LANE_PIPE_DEPTH becomes 4 (A7 fallback: a register between the noise
    // add and the multiply), the change is here and in valid_'s use below.
    static_assert(LANE_PIPE_DEPTH == 3,
                  "model_exact::step(): lane datapath written for a 3-stage pipe");
    const bool v_prod = valid_[LANE_PIPE_DEPTH - 2];   // v2
    const bool v_acc  = valid_[LANE_PIPE_DEPTH - 1];   // v3
    const int  w_row  = q.seq_cnt & (TILE_ROWS - 1);
    const int  w_pass = q.seq_cnt >> ROW_AW;
    const std::int32_t a = act_bcast_;                 // zero-extended to 9-bit signed

    std::vector<LaneRegs> nl = lanes_;
    for (int l = 0; l < NUM_LANES; ++l) {
        const auto li = static_cast<std::size_t>(l);
        const LaneRegs& c = lanes_[li];
        LaneRegs& x = nl[li];
        x.w_rd    = weights_[widx(w_row, w_pass * NUM_LANES + l)];
        x.w_align = c.w_rd;
        x.noise_q = read_noise_q(lfsr_[li], cfg_.read_sigma);   // from the state this cycle
        if (v_prod) x.prod = std::int32_t{w_n_of(c)} * a;
        // Priority: clear > accumulate > shift (mac_lane.sv). 32-bit wrap.
        if (q.acc_clear)     x.acc = 0;
        else if (v_acc)      x.acc = static_cast<std::int32_t>(
                                 static_cast<std::uint32_t>(c.acc) + static_cast<std::uint32_t>(c.prod));
        else if (q.shift_en) x.acc = (l + 1 < NUM_LANES) ? lanes_[li + 1].acc : 0;
        // LFSR: advance on acc_en only (mac-array-spec §5.2). Read above first.
        if (q.acc_en) lfsr_[li] = lfsr_step(lfsr_[li]);
    }

    // ---- shared ADC + result buffer + argmax: write on the edge -------------
    // chain_out is lane 0's accumulator THIS cycle (result-buffer-spec §3).
    if (q.run_start) {                       // results_valid cleared, argmax reset
        results_valid_ = false;
        argmax_reset();
    }
    if (q.drain_we) {
        const std::int32_t chain_out = lanes_[0].acc;
        const std::int32_t v = (cfg_.noise_en & NOISE_ADC)
            ? adc_quantize(chain_out, static_cast<std::uint8_t>(cfg_.adc_bits & 0x1F))
            : chain_out;
        rbuf_[static_cast<std::size_t>(q.drain_addr)] = v;
        argmax_update(q.drain_addr, v);
    }
    if (q.done) {                            // compute_done pulse, the DONE cycle
        results_valid_ = true;
        ++result_seq_;
    }

    // ---- commit -------------------------------------------------------------
    for (int i = LANE_PIPE_DEPTH - 1; i > 0; --i) valid_[i] = valid_[i - 1];
    valid_[0]   = q.acc_en;
    act_rd_     = act_rd_n;
    act_rd_row_ = act_addr;
    act_bcast_  = act_bcast_n;
    lanes_      = std::move(nl);
    seq_        = n;
}

CtrlState ModelExact::state() const   { return seq_.st; }
bool ModelExact::busy() const         { return seq_.busy; }
int ModelExact::seq_cnt() const       { return seq_.seq_cnt; }
std::uint32_t ModelExact::cycle_count() const { return seq_.cycle_cnt; }

ModelExact::LaneProbe ModelExact::lane(int l) const {
    if (l < 0 || l >= NUM_LANES) throw std::out_of_range("lane out of range");
    const LaneRegs& r = lanes_[static_cast<std::size_t>(l)];
    return {w_n_of(r), r.prod, r.acc, lfsr_[static_cast<std::size_t>(l)]};
}

ModelExact::CtrlProbe ModelExact::ctrl() const {
    return {seq_.seq_cnt, seq_.acc_clear, seq_.acc_en, seq_.shift_en,
            seq_.drain_we, seq_.drain_addr, seq_.run_start, seq_.done, act_bcast_};
}

std::vector<std::int32_t> ModelExact::results() const {
    return {rbuf_.begin(), rbuf_.begin() + cols_eff()};
}

ModelExact::Argmax ModelExact::argmax() const {
    return {max_idx_, max_val_, second_val_};
}

// ============================================================================
// Register level (rtl/control/config_regs.sv)
// ============================================================================

void ModelExact::soft_reset() {
    // control_fsm's soft_reset branch: every sequencer register to its reset
    // value. Nothing else in the design takes soft_reset: config registers,
    // weight/activation memories, LFSRs, lane registers and the result buffer
    // keep their contents. That includes each lane's valid pipeline, so rows
    // in flight at a mid-compute reset still retire over the next
    // LANE_PIPE_DEPTH edges; a COMPUTE started inside that window would see
    // them land after its CLEAR. Over UART the next packet is thousands of
    // cycles away, so it cannot happen on the board; the model mirrors it.
    seq_ = Seq{};
    start_pending_ = false;
}

Status ModelExact::set_reg(std::uint8_t addr, std::uint32_t v) {
    switch (addr) {
    case REG_CTRL:
        if (v & 0x1u) soft_reset();
        if (v & 0x2u) results_valid_ = false;
        if (v & 0x4u) reseed();
        return ST_OK;
    case REG_TILE_CFG:
        cfg_.active_rows = static_cast<int>(v & 0xFFFFu);
        cfg_.active_cols = static_cast<int>(v >> 16);
        return ST_OK;
    case REG_QUANT_LEVELS:
        if (v >= 2 && v <= QUANT_LEVELS_MAX) cfg_.quant_levels = static_cast<int>(v);
        return ST_OK;
    case REG_D2D_SIGMA:  cfg_.d2d_sigma  = static_cast<std::uint8_t>(v & 0xFFu); return ST_OK;
    case REG_READ_SIGMA: cfg_.read_sigma = static_cast<std::uint8_t>(v & 0xFFu); return ST_OK;
    case REG_NOISE_SEED: cfg_.noise_seed = v;                                    return ST_OK;
    case REG_NOISE_EN:   cfg_.noise_en   = static_cast<std::uint8_t>(v & 0x3Fu); return ST_OK;
    case REG_ADC_BITS:
        if (v >= 4 && v <= static_cast<std::uint32_t>(ACC_USED_W)) cfg_.adc_bits = static_cast<int>(v);
        return ST_OK;
    case REG_STUCK_RATE: cfg_.stuck_rate = v & 0x1FFFFu;                         return ST_OK;
    case REG_BAUD_INC:   baud_inc_ = static_cast<std::uint16_t>(v & 0xFFFFu);    return ST_OK;
    case REG_QUANT_MULT: cfg_.quant_mult = static_cast<std::uint16_t>(v & 0xFFFFu); return ST_OK;
    default:             return ST_ADDR_RANGE;   // STATUS, CYCLE_CNT, unmapped, reserved
    }
}

Status ModelExact::get_reg(std::uint8_t addr, std::uint32_t& v) const {
    switch (addr) {
    case REG_CTRL:         v = 0; return ST_OK;   // write-only, reads 0
    case REG_TILE_CFG:     v = (static_cast<std::uint32_t>(cfg_.active_cols & 0xFFFF) << 16) |
                               static_cast<std::uint32_t>(cfg_.active_rows & 0xFFFF);
                           return ST_OK;
    case REG_QUANT_LEVELS: v = static_cast<std::uint32_t>(cfg_.quant_levels); return ST_OK;
    case REG_D2D_SIGMA:    v = cfg_.d2d_sigma;  return ST_OK;
    case REG_READ_SIGMA:   v = cfg_.read_sigma; return ST_OK;
    case REG_NOISE_SEED:   v = cfg_.noise_seed; return ST_OK;
    case REG_NOISE_EN:     v = cfg_.noise_en & 0x3Fu; return ST_OK;
    case REG_ADC_BITS:     v = static_cast<std::uint32_t>(cfg_.adc_bits) & 0x1Fu; return ST_OK;
    case REG_STUCK_RATE:   v = cfg_.stuck_rate & 0x1FFFFu; return ST_OK;
    case REG_BAUD_INC:     v = baud_inc_; return ST_OK;
    case REG_STATUS:
        v = (static_cast<std::uint32_t>(result_seq_) << 8) |
            (results_valid_ ? 1u << 6 : 0u) |
            /* [5] last_err: no UART in the model */ 0u |
            (seq_.busy ? 1u << 4 : 0u) |
            static_cast<std::uint32_t>(seq_.st);
        return ST_OK;
    case REG_CYCLE_CNT:    v = seq_.cycle_cnt; return ST_OK;
    case REG_QUANT_MULT:   v = cfg_.quant_mult; return ST_OK;
    default:               v = 0; return ST_ADDR_RANGE;
    }
}

} // namespace fecim
