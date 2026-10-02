//=============================================================================
// uart_rx.sv -- Lane A
//
// UART receiver: 3-flop synchronizer, 16x oversampling, majority vote.
// Spec: uart-spec.md §4
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

module uart_rx
    import fecim_pkg::*;
(
    input  logic       clk,
    input  logic       rst_n,
    input  logic       tick_16x,
    input  logic       rxd,            // asynchronous, from pin
    output logic [7:0] rx_data,        // valid when rx_valid
    output logic       rx_valid,       // single-cycle strobe
    output logic       rx_frame_err    // single-cycle, stop bit was low
);

    // TODO: rx_sync must reset to '1 (line idles high) -- rtl-conventions §4.2
    assign rx_data      = '0;
    assign rx_valid     = 1'b0;
    assign rx_frame_err = 1'b0;

endmodule : uart_rx

/* verilator lint_on UNUSEDPARAM */
/* verilator lint_on UNUSEDSIGNAL */
