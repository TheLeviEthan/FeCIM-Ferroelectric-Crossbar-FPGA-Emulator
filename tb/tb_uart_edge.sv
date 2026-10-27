`timescale 1ns/1ps
module tb_uart_edge;
    localparam integer BIT_NS = 1085; // 921600 baud, ~54.25 clocks/bit at 50 MHz
    logic clk = 0;
    logic rst_n = 0;
    logic rxd = 1;
    logic tick_16x;
    wire [7:0] rx_data;
    wire rx_valid, rx_frame_err;
    integer received = 0;
    integer frame_errors = 0;
    logic [7:0] log_bytes [0:15];

    always #10 clk = ~clk; // 50 MHz

    baud_gen #(.BAUD_INC(16'd19327)) bg (
        .clk(clk), .rst_n(rst_n), .tick_16x(tick_16x)
    );
    uart_rx dut (
        .clk(clk), .rst_n(rst_n), .tick_16x(tick_16x),
        .rxd(rxd), .rx_data(rx_data),
        .rx_valid(rx_valid), .rx_frame_err(rx_frame_err)
    );

    always @(posedge clk) if (rst_n) begin
        if (rx_valid) begin
            if (received < 16) log_bytes[received] = rx_data;
            received = received + 1;
        end
        if (rx_frame_err) frame_errors = frame_errors + 1;
    end

    // UART frame, LSB first. No delay after stop bit: supports back-to-back frames.
    task automatic send_frame(input logic [7:0] data_byte,
                              input logic stop_bit);
        rxd = 0;
        #(BIT_NS);
        for (integer i = 0; i < 8; i++) begin
            rxd = data_byte[i];
            #(BIT_NS);
        end
        rxd = stop_bit;
        #(BIT_NS);
    endtask

    initial begin
        repeat (10) @(negedge clk);
        rst_n = 1;
        #(BIT_NS * 3);

        // 1) Back-to-back bytes: stop bit immediately followed by next start bit
        send_frame(8'hA5, 1'b1);
        send_frame(8'h3C, 1'b1);
        send_frame(8'hF0, 1'b1);
        rxd = 1;
        #(BIT_NS * 3);
        if (received != 3 || log_bytes[0] !== 8'hA5 ||
            log_bytes[1] !== 8'h3C || log_bytes[2] !== 8'hF0 ||
            frame_errors != 0)
            $fatal(1, "FAIL back-to-back: received=%0d errors=%0d", received, frame_errors);
        $display("PASS: back-to-back bytes, no extra idle (921600 baud)");

        // 2) Invalid low stop bit must cause frame error, not rx_valid
        send_frame(8'h55, 1'b0);
        rxd = 1;
        #(BIT_NS * 3);
        if (frame_errors != 1 || received != 3)
            $fatal(1, "FAIL framing error: received=%0d errors=%0d", received, frame_errors);
        $display("PASS: framing error flagged for low stop bit");

        // 3) Low glitch shorter than half a bit must not count as a frame
        rxd = 0;
        #200; // 200 ns, less than half of 1085 ns bit period
        rxd = 1;
        #(BIT_NS * 12);
        if (received != 3 || frame_errors != 1)
            $fatal(1, "FAIL glitch rejection: received=%0d errors=%0d", received, frame_errors);
        $display("PASS: short start-bit glitch rejected");

        $display("PASS: all three UART RX edge-case tests");
        $finish;
    end

    initial begin
        #2000000;
        $fatal(1, "FAIL: simulation timeout");
    end
endmodule
