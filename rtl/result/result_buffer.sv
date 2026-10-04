//=============================================================================
// result_buffer.sv -- Lane B
//
// Result buffer (one M9K, 256x32): written by the drain, read by readout_ser.
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

module result_buffer
    import fecim_pkg::*;
(
    input  logic                clk,
    input  logic                rst_n,
    input  logic                clear_results,
    input  logic                run_start,
    input  logic                compute_done,

    // write port (drain)
    input  logic                we,
    input  logic [COL_AW-1:0]   waddr,
    input  logic [ACC_W-1:0]    wdata,

    // read port
    input  logic [COL_AW-1:0]   raddr,
    output logic [ACC_W-1:0]    rdata,

    output logic                results_valid
);

    assign rdata         = '0;
    assign results_valid = 1'b0;

endmodule : result_buffer

/* verilator lint_on UNUSEDPARAM */
/* verilator lint_on UNUSEDSIGNAL */
