module uart_tx (
    input logic clk, rst_n, tick_16x,
    input logic [7:0] tx_data,
    input logic tx_start,
    output logic tx_busy,
    output logic txd
);
    logic [9:0] frame;
    logic [3:0] bit_idx, tick_idx;
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            frame <= 10'h3ff; bit_idx <= 0; tick_idx <= 0;
            tx_busy <= 0; txd <= 1;
        end else if (!tx_busy) begin
            txd <= 1;
            if (tx_start) begin
                frame <= {1'b1, tx_data, 1'b0};
                bit_idx <= 0; tick_idx <= 0;
                tx_busy <= 1; txd <= 0;
            end
        end else if (tick_16x) begin
            if (tick_idx == 15) begin
                tick_idx <= 0;
                if (bit_idx == 9) begin
                    tx_busy <= 0; txd <= 1;
                end else begin
                    bit_idx <= bit_idx + 1'b1;
                    txd <= frame[bit_idx + 1'b1];
                end
            end else tick_idx <= tick_idx + 1'b1;
        end
    end
endmodule
