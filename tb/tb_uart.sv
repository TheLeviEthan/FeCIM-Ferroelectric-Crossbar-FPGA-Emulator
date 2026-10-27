
`timescale 1ns/1ps

module tb_uart;

    logic clk = 0;
    logic rst_n = 0;
    logic tx_start = 0;
    logic [7:0] tx_data = 0;

    logic tick_16x, tx_busy, txd;
    logic [7:0] rx_data;
    logic rx_valid, rx_frame_err;

    integer received = 0;
    integer errors = 0;

    // 50 MHz clock
    always #10 clk = ~clk;

    // 921600 baud at 50 MHz, 16x oversampling
    localparam logic [15:0] BAUD_INC = 16'd19327;

    baud_gen bg (
        .clk(clk),
        .rst_n(rst_n),
        .baud_inc(BAUD_INC),
        .tick_16x(tick_16x)
    );

    uart_tx tx (
        .clk(clk),
        .rst_n(rst_n),
        .tick_16x(tick_16x),
        .tx_data(tx_data),
        .tx_start(tx_start),
        .tx_busy(tx_busy),
        .txd(txd)
    );

    uart_rx rx (
        .clk(clk),
        .rst_n(rst_n),
        .tick_16x(tick_16x),
        .rxd(txd),
        .rx_data(rx_data),
        .rx_valid(rx_valid),
        .rx_frame_err(rx_frame_err)
    );

    // Check received data
    always @(posedge clk) begin
        if (rst_n) begin
            if (rx_valid) begin
                received = received + 1;

                if (rx_data !== tx_data) begin
                    $display(
                        "FAIL: got %02h expected %02h",
                        rx_data, tx_data
                    );
                    errors = errors + 1;
                end
            end

            if (rx_frame_err) begin
                $display("FAIL: framing error");
                errors = errors + 1;
            end
        end
    end

    // Transmit one byte and wait for reception
    task automatic send(input logic [7:0] value);
        integer previous_received;
        begin
            previous_received = received;

            @(negedge clk);
            tx_data = value;
            tx_start = 1;

            @(negedge clk);
            tx_start = 0;

            // Wait for TX to start and finish
            wait(tx_busy == 1);
            wait(tx_busy == 0);

            // Wait for RX to receive the byte
            wait(received > previous_received);

            repeat (20) @(posedge clk);
        end
    endtask

    initial begin
        repeat (10) @(negedge clk);
        rst_n = 1;

        // Test patterns
        send(8'hA5);
        send(8'h00);
        send(8'hFF);
        send(8'h55);

        // Test all 256 byte values
        for (integer i = 0; i < 256; i = i + 1)
            send(i[7:0]);

        repeat (100) @(posedge clk);

        if (errors == 0 && received == 260)
            $display(
                "PASS: 260 TX->RX loopback bytes at 921600 baud"
            );
        else
            $fatal(
                1,
                "FAIL: received=%0d expected=260 errors=%0d",
                received, errors
            );

        $finish;
    end

    // Simulation timeout
    initial begin
        #300000000;
        $fatal(1, "TIMEOUT");
    end

endmodule
