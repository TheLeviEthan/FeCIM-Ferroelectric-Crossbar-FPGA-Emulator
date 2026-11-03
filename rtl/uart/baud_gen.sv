
// ============================================================================
// baud_gen.sv
// FeCIM Ferroelectric Crossbar FPGA Emulator
//
// 16-bit fractional baud-rate generator
// Clock: 50 MHz
// Output: 16x UART oversampling tick
//
// Baud increment formula:
//   baud_inc = round(baud_rate * 16 * 65536 / 50_000_000)
//
// Examples:
//   115200 baud -> 2416
//   230400 baud -> 4832
//   460800 baud -> 9664
//   921600 baud -> 19327
// ============================================================================

module baud_gen #(
    parameter logic [15:0] BAUD_INC = 16'd2416
)(
    input logic clk,
    input logic rst_n,
    output logic tick_16x
);
    logic [16:0] phase;
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin phase <= '0; tick_16x <= 1'b0; end
        else begin
            phase <= {1'b0, phase[15:0]} + {1'b0, BAUD_INC};
            tick_16x <= phase[16];
        end
    end
endmodule
