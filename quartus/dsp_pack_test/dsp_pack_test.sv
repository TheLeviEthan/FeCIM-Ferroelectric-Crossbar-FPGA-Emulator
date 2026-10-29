//=============================================================================
// dsp_pack_test.sv
//
// Week-1 experiment: confirm two 9x9 signed multiplies share ONE MAX 10
// embedded multiplier block. Shaped like two mac_lane MACs:
//   w_n (9-bit signed, per lane) x act (8-bit unsigned, broadcast,
//   zero-extended to 9-bit signed-positive).
// Not part of the design. Synthesis-only; all data ports are virtual pins.
//=============================================================================
module dsp_pack_test
    import fecim_pkg::*;
(
    input  logic                      clk,
    input  logic                      rst_n,
    input  logic signed [W_N_W-1:0]   w_n0,
    input  logic signed [W_N_W-1:0]   w_n1,
    input  logic        [ACT_W-1:0]   act,      // broadcast, unsigned 0..255
    output logic signed [PROD_W-1:0]  prod0,
    output logic signed [PROD_W-1:0]  prod1
);

    // Input registers -- absorbable into the multiplier block's input regs.
    logic signed [W_N_W-1:0] w0_q, w1_q;
    logic signed [W_N_W-1:0] act_q;              // 9-bit signed-positive

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            w0_q  <= '0;
            w1_q  <= '0;
            act_q <= '0;
        end else begin
            w0_q  <= w_n0;
            w1_q  <= w_n1;
            act_q <= $signed({1'b0, act});      // zero-extend: SIGNED x SIGNED
        end
    end

    // Both multiplies signed x signed so they share the block's signa/signb.
    (* multstyle = "dsp" *) logic signed [PROD_W-1:0] p0, p1;
    assign p0 = w0_q * act_q;
    assign p1 = w1_q * act_q;

    // Output registers -- without these Quartus may still map, but registered
    // outputs match the real lane and keep the test realistic.
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            prod0 <= '0;
            prod1 <= '0;
        end else begin
            prod0 <= p0;
            prod1 <= p1;
        end
    end

endmodule : dsp_pack_test