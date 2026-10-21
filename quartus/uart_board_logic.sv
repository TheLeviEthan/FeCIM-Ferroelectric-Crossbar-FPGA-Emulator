module uart_board_logic (
    input  logic clk,
    input  logic rst_n,
    input  logic uart_rx_pin,
    output logic uart_tx_pin,
    output logic [7:0] leds,
    output logic error_led
);
    logic tick_16x;
    logic tx_start, tx_busy;
    logic [7:0] rx_data;
    logic rx_valid, rx_frame_err;
    logic [23:0] interval_count;
    logic [7:0] next_byte;

    baud_gen baud_inst (.clk(clk), .rst_n(rst_n), .tick_16x(tick_16x));
    uart_tx tx_inst (.clk(clk), .rst_n(rst_n), .tick_16x(tick_16x),
                     .tx_data(next_byte), .tx_start(tx_start),
                     .tx_busy(tx_busy), .txd(uart_tx_pin));
    uart_rx rx_inst (.clk(clk), .rst_n(rst_n), .tick_16x(tick_16x),
                     .rxd(uart_rx_pin), .rx_data(rx_data),
                     .rx_valid(rx_valid), .rx_frame_err(rx_frame_err));

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            interval_count <= 0;
            next_byte <= 8'hA5;
            tx_start <= 0;
            leds <= 0;
            error_led <= 0;
        end else begin
            tx_start <= 0;
            if (interval_count == 24'd12_499_999) begin
                interval_count <= 0;
                if (!tx_busy) begin
                    tx_start <= 1;
                    next_byte <= next_byte + 8'h01;
                end
            end else interval_count <= interval_count + 1'b1;

            if (rx_valid) leds <= rx_data;
            if (rx_frame_err) error_led <= 1;
        end
    end
endmodule
