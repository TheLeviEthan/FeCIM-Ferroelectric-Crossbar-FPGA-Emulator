//=============================================================================
// baud_gen.sv -- Lane A
//
// Fractional (phase-accumulator) 16x baud tick generator.
//   baud_inc = round(2^16 * 16 * baud / CLK_HZ)
// See uart-spec §3.2 and rtl-conventions §4.1.
//=============================================================================

module baud_gen
    import fecim_pkg::*;
(
    input  logic                  clk,
    input  logic                  rst_n,
    input  logic [BAUD_ACC_W-1:0] baud_inc,
    output logic                  tick_16x
);

    logic [BAUD_ACC_W:0] acc;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) acc <= '0;
        else        acc <= {1'b0, acc[BAUD_ACC_W-1:0]} + {1'b0, baud_inc};
    end

    assign tick_16x = acc[BAUD_ACC_W];

endmodule : baud_gen
