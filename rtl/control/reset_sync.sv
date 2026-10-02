//=============================================================================
// reset_sync.sv -- Lane A
//
// Async assert, sync deassert. KEY0 is hardware-debounced on the DE10-Lite,
// but debouncing is not metastability protection, so synchronize anyway.
// See top-level-spec §3.2.
//=============================================================================

module reset_sync (
    input  logic clk,
    input  logic arst_n,    // KEY[0], active low, asynchronous
    output logic rst_n      // synchronized, active low
);

    logic [1:0] rst_pipe;

    always_ff @(posedge clk or negedge arst_n) begin
        if (!arst_n) rst_pipe <= '0;
        else         rst_pipe <= {rst_pipe[0], 1'b1};
    end

    assign rst_n = rst_pipe[1];

endmodule : reset_sync
