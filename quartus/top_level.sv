module top_level (
    input  wire       MAX10_CLK1_50,
    input  wire [1:0] KEY,
    output wire [9:0] LEDR,
    inout  wire [15:0] ARDUINO_IO
);

    wire uart_txd;
    wire [7:0] uart_leds;
    wire uart_error;

    // UART RX input
    assign ARDUINO_IO[0] = 1'bz;

    // UART TX output
    assign ARDUINO_IO[1] = uart_txd;

    // Leave unused Arduino pins undriven
    assign ARDUINO_IO[15:2] = 14'bz;

    // Display received UART data on LEDs
    assign LEDR[7:0] = uart_leds;
    assign LEDR[8] = uart_error;
    assign LEDR[9] = 1'b0;

    // Instantiate UART board logic
    uart_board_logic uart_demo (
        .clk(MAX10_CLK1_50),
        .rst_n(KEY[0]),
        .uart_rx_pin(ARDUINO_IO[0]),
        .uart_tx_pin(uart_txd),
        .leds(uart_leds),
        .error_led(uart_error)
    );

endmodule