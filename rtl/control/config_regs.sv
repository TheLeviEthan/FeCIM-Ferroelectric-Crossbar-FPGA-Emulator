//=============================================================================
// config_regs.sv -- Lane A
//
// Config register file. Map is frozen -- see cfg_addr_e in fecim_pkg.
// Spec: control-fsm-spec.md §6
//
// SKELETON. Port list is provisional -- confirm against the spec and the
// frozen A/B seam (top-level-spec §7.1) before implementing. Outputs are
// tied to safe idle values so the top level elaborates and lints cleanly.
//
// TODO(lane a): implement, then DELETE the lint waivers below. They
// exist only because a skeleton's inputs are unused; leaving them in place
// after implementation hides real -Wall findings.
//=============================================================================

/* verilator lint_off UNUSEDSIGNAL */
/* verilator lint_off UNUSEDPARAM */

module config_regs
    import fecim_pkg::*;
(
    input  logic                clk,
    input  logic                rst_n,

    // write/read port from packet_parser
    input  logic                cfg_we,
    input  logic [7:0]          cfg_addr,
    input  logic [31:0]         cfg_wdata,
    output logic [31:0]         cfg_rdata,

    // read-only status sources
    input  ctrl_state_e         ctrl_state,
    input  logic                compute_busy,
    input  logic                last_err,
    input  logic                results_valid,
    input  logic [31:0]         cycle_cnt,

    // CTRL strobes (self-clearing)
    output logic                soft_reset,
    output logic                clear_results,
    output logic                reseed,

    // register values
    output logic [15:0]         active_rows,
    output logic [15:0]         active_cols,
    output logic [7:0]          quant_levels,
    output logic [15:0]         quant_mult,      // Q8.8
    output logic [15:0]         d2d_sigma,
    output logic [SIGMA_W-1:0]  read_sigma,      // Q1.8
    output logic [31:0]         noise_seed,
    output noise_en_t           noise_en,
    output logic [4:0]          adc_bits,
    output logic [16:0]         stuck_rate,      // [15:0] rate, [16] mode
    output logic [BAUD_ACC_W-1:0] baud_inc
);

    // Reset defaults: ideal crossbar, full tile, 115200 baud.
    assign cfg_rdata     = '0;
    assign soft_reset    = 1'b0;
    assign clear_results = 1'b0;
    assign reseed        = 1'b0;
    assign active_rows   = 16'(TILE_ROWS);
    assign active_cols   = 16'(TILE_COLS);
    assign quant_levels  = 8'(QUANT_LEVELS_MAX);
    assign quant_mult    = '0;
    assign d2d_sigma     = '0;
    assign read_sigma    = '0;
    assign noise_seed    = '0;
    assign noise_en      = NOISE_NONE;
    assign adc_bits      = 5'(ACC_USED_W);
    assign stuck_rate    = '0;
    assign baud_inc      = BAUD_INC_115200;

endmodule : config_regs

/* verilator lint_on UNUSEDPARAM */
/* verilator lint_on UNUSEDSIGNAL */
