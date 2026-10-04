//=============================================================================
// readout_ser.sv -- Lane A
//
// Serializes result words / argmax block into the response payload byte stream.
// Spec: result-buffer-spec.md (readout)
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

module readout_ser
    import fecim_pkg::*;
(
    input  logic                clk,
    input  logic                rst_n,

    // request from packet_parser
    input  logic                rd_go,
    input  logic                rd_argmax,
    input  logic [COL_AW-1:0]   rd_start,
    input  logic [COL_AW:0]     rd_count,

    // result buffer read port
    output logic [COL_AW-1:0]   result_raddr,
    input  logic [ACC_W-1:0]    result_rdata,

    // argmax block
    input  logic [COL_AW-1:0]   argmax_idx,
    input  logic [ACC_W-1:0]    argmax_val,
    input  logic [ACC_W-1:0]    argmax_second,

    // payload byte stream -> packet_tx
    output logic [7:0]          rd_data,
    output logic                rd_valid,
    input  logic                rd_next
);

    assign result_raddr = '0;
    assign rd_data      = '0;
    assign rd_valid     = 1'b0;

endmodule : readout_ser

/* verilator lint_on UNUSEDPARAM */
/* verilator lint_on UNUSEDSIGNAL */
