//=============================================================================
// display_driver.sv -- Lane A
//
// HEX0-5 / LEDR status display. HEX segments are ACTIVE LOW.
// Spec: top-level-spec.md §6
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

module display_driver
    import fecim_pkg::*;
(
    input  logic                clk,
    input  logic                rst_n,
    input  logic [9:0]          sw,              // synchronized
    input  ctrl_state_e         ctrl_state,
    input  logic                compute_busy,
    input  logic                err_latched,
    input  logic                rx_activity,
    input  logic [SEQ_AW-1:0]   seq_cnt,
    input  logic [15:0]         rx_byte_cnt,
    input  logic [15:0]         err_cnt,
    input  logic [ACC_W-1:0]    result_word,
    input  logic [COL_AW-1:0]   argmax_idx,
    output logic [7:0]          hex0, hex1, hex2, hex3, hex4, hex5,
    output logic [9:0]          ledr
);

    // All segments OFF (active low) until implemented.
    assign hex0 = '1;
    assign hex1 = '1;
    assign hex2 = '1;
    assign hex3 = '1;
    assign hex4 = '1;
    assign hex5 = '1;
    assign ledr = '0;

endmodule : display_driver

/* verilator lint_on UNUSEDPARAM */
/* verilator lint_on UNUSEDSIGNAL */
