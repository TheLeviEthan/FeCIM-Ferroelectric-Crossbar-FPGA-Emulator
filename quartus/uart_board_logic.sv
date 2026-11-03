
module uart_board_logic (
    input  logic       clk,
    input  logic       rst_n,
    input  logic       uart_rx_pin,
    output logic       uart_tx_pin,
    output logic [7:0] leds,
    output logic       error_led
);

    logic tick_16x;

    // UART transmitter signals
    logic tx_start;
    logic tx_busy;
    logic [7:0] tx_data;

    // UART receiver signals
    logic [7:0] rx_data;
    logic rx_valid;
    logic rx_frame_err;

    // One-byte echo buffer
    logic [7:0] echo_data;
    logic echo_pending;

    // Framing error counter
    logic [7:0] frame_error_count;

    // 50 MHz clock, 921600 baud, 16x oversampling
    localparam logic [15:0] BAUD_INC = 16'd19327;

    // Baud generator: runtime input interface
    baud_gen baud_inst (
        .clk      (clk),
        .rst_n    (rst_n),
        .baud_inc (BAUD_INC),
        .tick_16x (tick_16x)
    );

    // UART transmitter
    uart_tx tx_inst (
        .clk      (clk),
        .rst_n    (rst_n),
        .tick_16x (tick_16x),
        .tx_data  (tx_data),
        .tx_start (tx_start),
        .tx_busy  (tx_busy),
        .txd     (uart_tx_pin)
    );

    // UART receiver
    uart_rx rx_inst (
        .clk          (clk),
        .rst_n        (rst_n),
        .tick_16x     (tick_16x),
        .rxd          (uart_rx_pin),
        .rx_data      (rx_data),
        .rx_valid     (rx_valid),
        .rx_frame_err (rx_frame_err)
    );

    // Echo controller
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            tx_start          <= 1'b0;
            tx_data           <= 8'h00;
            echo_data         <= 8'h00;
            echo_pending      <= 1'b0;
            leds              <= 8'h00;
            frame_error_count <= 8'd0;
        end else begin
            tx_start <= 1'b0;

            // Store received byte
            if (rx_valid) begin
                leds         <= rx_data;
                echo_data    <= rx_data;
                echo_pending <= 1'b1;
            end

            // Echo when transmitter is ready
            if (echo_pending && !tx_busy && !tx_start) begin
                tx_data      <= echo_data;
                tx_start     <= 1'b1;
                echo_pending <= 1'b0;
            end

            // Count framing errors
            if (rx_frame_err) begin
                frame_error_count <= frame_error_count + 1'b1;
            end
        end
    end

    assign error_led = frame_error_count[0];

endmodule
