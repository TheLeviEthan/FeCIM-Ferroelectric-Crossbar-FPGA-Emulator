//=============================================================================
// control_fsm.sv -- Lane A
//
// MVM sequencer: IDLE -> CLEAR -> ACC -> DRAIN -> DONE, one counter drives the array.
// Spec: control-fsm-spec.md §3-4, rtl-conventions §5
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

module control_fsm
    import fecim_pkg::*;
(
    input  logic                clk,
    input  logic                rst_n,
    input  logic                soft_reset,

    input  logic                start_compute,   // from packet_parser
    input  logic                manual_compute,  // KEY1, synchronized pulse
    input  logic [15:0]         active_rows,
    input  logic [15:0]         active_cols,

    // A -> B seam
    output logic [SEQ_AW-1:0]   seq_cnt,
    output logic [ROW_AW-1:0]   act_addr,
    output logic                acc_clear,
    output logic                acc_en,
    output logic                shift_en,

    // result buffer write (drain)
    output logic                drain_we,
    output logic [COL_AW-1:0]   drain_addr,

    // status
    output ctrl_state_e         state,
    output logic                compute_busy,
    output logic                compute_done,    // 1-cycle pulse in DONE
    output logic                run_start,       // 1-cycle pulse on IDLE -> CLEAR
    output logic [31:0]         cycle_cnt
);

    // TODO: hold in ACC for LANE_PIPE_DEPTH cycles after the last row.
    assign seq_cnt      = '0;
    assign act_addr     = '0;
    assign acc_clear    = 1'b0;
    assign acc_en       = 1'b0;
    assign shift_en     = 1'b0;
    assign drain_we     = 1'b0;
    assign drain_addr   = '0;
    assign state        = CTRL_IDLE;
    assign compute_busy = 1'b0;
    assign compute_done = 1'b0;
    assign run_start    = 1'b0;
    assign cycle_cnt    = '0;

endmodule : control_fsm

/* verilator lint_on UNUSEDPARAM */
/* verilator lint_on UNUSEDSIGNAL */
