// Interface-stub tests (week 2).
//
// Two jobs:
//   1. Compile-time: this file is written the way the co-sim harness and the
//      `sim` backend will call the model. If a signature changes, this stops
//      compiling, which is the point of settling the shape early.
//   2. Run-time: every stub throws std::logic_error. A stub that quietly
//      returned an idle-looking default (state() == IDLE, acc == 0, ...)
//      would let a harness report a vacuous pass.
//
// As each function is implemented in week 3, DELETE its line here and give
// it real tests next to its neighbours (test_write_path.cpp, test_model.cpp).
// When this file is empty, remove it from CMakeLists.txt.

#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "fecim/model_exact.h"
#include "fecim/read_path.h"
#include "fecim/write_path.h"
#include "test_harness.h"

using namespace fecim;

// ---- shape: types are pinned to the RTL widths -----------------------------
// Narrowest standard type holding each signal, signed as in the RTL.
using Probe = ModelExact::LaneProbe;
static_assert(std::is_same_v<decltype(Probe::w_n),  std::int16_t>,  "w_n is 9-bit signed");
static_assert(std::is_same_v<decltype(Probe::prod), std::int32_t>,  "prod is 18-bit signed");
static_assert(std::is_same_v<decltype(Probe::acc),  std::int32_t>,  "acc is ACC_W = 32 signed");
static_assert(std::is_same_v<decltype(Probe::lfsr), std::uint32_t>, "LFSR is 32-bit");
static_assert(std::is_same_v<decltype(ModelExact::Argmax::max_idx), std::uint16_t>,
              "READ_ARGMAX max_idx is 2 bytes on the wire");
static_assert(sizeof(Status) == 1 && sizeof(CfgAddr) == 1 && sizeof(CtrlState) == 1,
              "protocol enums are one wire byte");
static_assert(W_N_W <= 16 && PROD_W <= 32 && ACC_W <= 32,
              "LaneProbe field types no longer hold the RTL widths");

namespace {

// What one harness cycle looks like (model-verification-design §5), minus
// the DUT. Kept here so the shape is compiled even while it cannot run.
int harness_cycle(ModelExact& m) {
    m.step();
    int mismatches = 0;
    for (int l = 0; l < NUM_LANES; ++l) {
        const Probe p = m.lane(l);
        if (p.w_n != 0 || p.prod != 0 || p.acc != 0) ++mismatches;
    }
    return mismatches;
}

} // namespace

// ---- run-time: every stub refuses ------------------------------------------

TEST(stub_cycle_interface_throws) {
    ModelExact m;
    CHECK_THROWS(m.start_compute(), std::logic_error);
    CHECK_THROWS(m.step(), std::logic_error);
    CHECK_THROWS(m.state(), std::logic_error);
    CHECK_THROWS(m.busy(), std::logic_error);
    CHECK_THROWS(m.seq_cnt(), std::logic_error);
    CHECK_THROWS(m.lane(0), std::logic_error);
    CHECK_THROWS(m.cycle_count(), std::logic_error);
    CHECK_THROWS(harness_cycle(m), std::logic_error);
}

TEST(stub_result_buffer_throws) {
    const ModelExact m;
    CHECK_THROWS(m.results(), std::logic_error);
    CHECK_THROWS(m.argmax(), std::logic_error);
}

TEST(stub_register_interface_throws) {
    ModelExact m;
    std::uint32_t v = 0;
    CHECK_THROWS(m.set_reg(REG_NOISE_EN, NOISE_NONE), std::logic_error);
    CHECK_THROWS(m.get_reg(REG_STATUS, v), std::logic_error);
    CHECK_THROWS(m.reseed(), std::logic_error);
}

TEST(stub_write_path_stages_throw) {
    CHECK_THROWS(cell_addr(0, 0), std::logic_error);
    CHECK_THROWS(d2d_offset(0, 0, 0u, 0), std::logic_error);
    CHECK_THROWS(apply_stuck(0, 0, 0u, 0u), std::logic_error);
}

TEST(stub_read_path_throws) {
    CHECK_THROWS(read_noise_q(1u, 0), std::logic_error);
    CHECK_THROWS(ir_attenuate(255, 0xFF), std::logic_error);
    CHECK_THROWS(adc_quantize(0, static_cast<std::uint8_t>(ACC_USED_W)), std::logic_error);
}

TEST(stub_does_not_disturb_implemented_path) {
    // Adding the interface must not change the working transaction path.
    ModelExact m;
    Config cfg;
    cfg.active_rows = 2;
    cfg.active_cols = 1;
    m.set_config(cfg);
    m.write_weight(0, 0, -3);
    m.write_weight(1, 0, 5);
    m.write_activation(0, 10);
    m.write_activation(1, 4);
    const std::vector<std::int32_t> y = m.compute();
    CHECK_EQ(y.size(), std::size_t{1});
    CHECK_EQ(y[0], std::int32_t{-3 * 10 + 5 * 4});
}
