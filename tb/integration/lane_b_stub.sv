
`timescale 1ns/1ps

// Lane B integration stub.
// Placeholder responses only; no real MAC computation.
module lane_b_stub
    import fecim_pkg::*;
(
    input  logic                 clk,
    input  logic                 rst_n,

    // Compute handshake
    input  logic                 start_compute,
    output logic                 compute_busy,

    // Result read interface
    input  logic [COL_AW-1:0]    result_raddr,
    output logic [ACC_W-1:0]    result_rdata,
    output logic                 results_valid
);

    // Placeholder busy response
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            compute_busy <= 1'b0;
        else
            compute_busy <= start_compute;
    end

    // Deterministic placeholder result data
    assign result_rdata = ACC_W'(result_raddr);
    assign results_valid = 1'b1;

endmodule : lane_b_stub
