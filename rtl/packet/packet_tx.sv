//=============================================================================
// packet_tx.sv -- Lane A
//
// Response generator: SYNC_F2H, cmd, status, length, payload, CRC-8.
// Spec: packet-parser-spec.md
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

module packet_tx
    import fecim_pkg::*;
(
    input  logic        clk,
    input  logic        rst_n,

    // response header from packet_parser
    input  logic        resp_valid,
    input  logic [7:0]  resp_cmd,
    input  status_e     resp_status,
    input  logic [15:0] resp_len,
    output logic        resp_ready,

    // payload byte stream from readout_ser
    input  logic [7:0]  pl_data,
    input  logic        pl_valid,
    output logic        pl_next,

    // to uart_tx
    output logic [7:0]  tx_data,
    output logic        tx_start,
    input  logic        tx_busy
);

    assign resp_ready = 1'b1;
    assign pl_next    = 1'b0;
    assign tx_data    = '0;
    assign tx_start   = 1'b0;

endmodule : packet_tx

/* verilator lint_on UNUSEDPARAM */
/* verilator lint_on UNUSEDSIGNAL */
