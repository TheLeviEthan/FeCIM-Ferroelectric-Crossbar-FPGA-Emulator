//=============================================================================
// argmax_unit.sv -- Lane A / Lane B seam
//
// Streaming argmax / runner-up over drained columns.
// Spec: result-buffer-spec.md §5
//
// Tracks max_idx, max_val and second_val as the drain writes the result
// buffer, over active columns only. Columns at or beyond active_cols are
// masked out. Ties resolve to the lowest index, matching numpy.argmax.
//=============================================================================

module argmax_unit
    import fecim_pkg::*;
(
    input  logic                clk,
    input  logic                rst_n,
    input  logic                clear,           // run_start
    input  logic [15:0]         active_cols,
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
        end else if (in_valid && (16'(in_idx) < active_cols)) begin
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
