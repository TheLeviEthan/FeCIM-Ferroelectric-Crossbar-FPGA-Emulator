
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

module baud_gen (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [15:0] baud_inc,
    output logic        tick_16x
);

    // 16-bit phase accumulator
    logic [15:0] phase;

    // 17-bit sum to preserve carry
    logic [16:0] phase_sum;

    // Add programmable baud increment
    assign phase_sum = {1'b0, phase} + {1'b0, baud_inc};

    // Generate one-clock-wide tick on accumulator overflow
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            phase    <= 16'd0;
            tick_16x <= 1'b0;
        end else begin
            phase    <= phase_sum[15:0];
            tick_16x <= phase_sum[16];
        end
    end

endmodule
