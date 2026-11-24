//=============================================================================
// result_buffer.sv -- Lane B
//
// Result buffer (one M9K, 256x32): written by the drain, read by readout_ser.
// Spec: result-buffer-spec.md
//=============================================================================

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

    localparam int MEM_DEPTH = 1 << COL_AW;

    // Simple dual-port RAM
    logic [ACC_W-1:0] mem [0:MEM_DEPTH-1];

    always_ff @(posedge clk) begin
        if (we) begin
            mem[waddr] <= wdata;
        end
        rdata <= mem[raddr];
    end

    // results_valid tracking
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            results_valid <= 1'b0;
        end else if (clear_results || run_start) begin
            results_valid <= 1'b0;
        end else if (compute_done) begin
            results_valid <= 1'b1;
        end
    end

endmodule : result_buffer
