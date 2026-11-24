//=============================================================================
// argmax_unit.sv -- Lane B
//
// Streaming argmax / runner-up over drained columns.
// Spec: result-buffer-spec.md §5
//=============================================================================

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

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            argmax_idx    <= '0;
            argmax_val    <= 32'sh8000_0000;
            argmax_second <= 32'sh8000_0000;
        end else if (clear) begin
            argmax_idx    <= '0;
            argmax_val    <= 32'sh8000_0000;
            argmax_second <= 32'sh8000_0000;
        end else if (in_valid) begin
            if ($signed(in_val) > $signed(argmax_val)) begin
                argmax_second <= argmax_val;
                argmax_val    <= in_val;
                argmax_idx    <= in_idx;
            end else if ($signed(in_val) > $signed(argmax_second)) begin
                argmax_second <= in_val;
            end
        end
    end

endmodule : argmax_unit
