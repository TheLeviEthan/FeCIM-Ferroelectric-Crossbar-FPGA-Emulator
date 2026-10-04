//=============================================================================
// uart_tx.sv -- Lane A
//
// UART transmitter, 8N1, LSB first. No FIFO -- tx_busy handshake.
// Spec: uart-spec.md §5
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

module uart_tx
    import fecim_pkg::*;
(
    input  logic       clk,
    input  logic       rst_n,
    input  logic       tick_16x,
    input  logic [7:0] tx_data,        // captured on tx_start
    input  logic       tx_start,       // single-cycle, ignored while busy
    output logic       tx_busy,
    output logic       txd             // to pin, idle HIGH
);

    // TODO: txd must reset to 1 -- a low reset looks like a start bit.
    assign tx_busy = 1'b0;
    assign txd     = 1'b1;

endmodule : uart_tx

/* verilator lint_on UNUSEDPARAM */
/* verilator lint_on UNUSEDSIGNAL */
