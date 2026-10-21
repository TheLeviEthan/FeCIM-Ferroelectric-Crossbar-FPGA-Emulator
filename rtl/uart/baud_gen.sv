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
