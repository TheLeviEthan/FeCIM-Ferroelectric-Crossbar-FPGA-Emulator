//=============================================================================
// argmax_unit.sv -- Lane B
//
// Streaming argmax / runner-up over drained columns.
// Spec: result-buffer-spec.md
//
// SKELETON. Port list is provisional -- confirm against the spec and the
// frozen A/B seam (top-level-spec §7.1) before implementing. Outputs are
// tied to safe idle values so the top level elaborates and lints cleanly.
//
// TODO(lane b): implement, then DELETE the lint waivers below. They
// exist only because a skeleton's inputs are unused; leaving them in place
// after implementation hides real -Wall findings.
//=============================================================================

/* verilator lint_off UNUSEDSIGNAL */
/* verilator lint_off UNUSEDPARAM */

module argmax_unit
    import fecim_pkg::*;
(
    input  logic                clk,
    input  logic                rst_n,
    input  logic                clear,           // run_start
    input  logic                in_valid,
    input  logic [COL_AW-1:0]   in_idx,
    input  logic [ACC_W-1:0]    in_val,          // signed
    output logic [COL_AW-1:0]   argmax_idx,
    output logic [ACC_W-1:0]    argmax_val,
    output logic [ACC_W-1:0]    argmax_second
);

    assign argmax_idx    = '0;
    assign argmax_val    = '0;
    assign argmax_second = '0;

endmodule : argmax_unit

/* verilator lint_on UNUSEDPARAM */
/* verilator lint_on UNUSEDSIGNAL */
