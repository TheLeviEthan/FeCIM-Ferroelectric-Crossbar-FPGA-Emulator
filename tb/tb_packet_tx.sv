`timescale 1ns/1ps
module tb_packet_tx;
    import fecim_pkg::*;
    logic clk = 0;
    always #10 clk = ~clk; // 50 MHz
    logic rst_n = 0;
    logic resp_valid = 0;
    logic [7:0] resp_cmd = 0;
    status_e resp_status = ST_OK;
    logic [15:0] resp_len = 0;
    logic resp_ready;
    logic [7:0] pl_data = 0;
    logic pl_valid = 0;
    logic pl_next;
    logic [7:0] tx_data;
    logic tx_start;
    logic tx_busy = 0;

    packet_tx dut (.*);

    byte unsigned actual [0:63];
    int sent = 0;
    int payload_index = 0;
    int busy_ticks = 0;
    logic [7:0] payload [0:2];
    logic pending_payload = 0;
    int requests = 0;

    // A mock UART that acknowledges tx_start and stays busy for 3 clocks.
    always @(posedge clk) begin
        if (!rst_n) begin
            tx_busy <= 0;
            busy_ticks <= 0;
            sent <= 0;
        end else if (tx_start && !tx_busy) begin
            if (sent >= 64) $fatal(1, "Captured-byte buffer overflow");
            actual[sent] <= tx_data;
            sent <= sent + 1;
            tx_busy <= 1;
            busy_ticks <= 3;
        end else if (tx_busy) begin
            if (busy_ticks == 1) begin
                tx_busy <= 0;
                busy_ticks <= 0;
            end else busy_ticks <= busy_ticks - 1;
        end
    end

    // Respond to each payload request one clock later.
    always @(posedge clk) begin
        if (!rst_n) begin
            pl_valid <= 0;
            pl_data <= 0;
            payload_index <= 0;
            requests <= 0;
        end else begin
            pl_valid <= 0;
            if (pl_next) begin
                if (payload_index >= 3) $fatal(1, "Too many payload requests");
                pl_data <= payload[payload_index];
                pl_valid <= 1;
                payload_index <= payload_index + 1;
                requests <= requests + 1;
            end
        end
    end

    task automatic send_response(input status_e status, input logic [15:0] length);
        @(negedge clk);
        if (!resp_ready) $fatal(1, "TX not ready for response");
        resp_status = status;
        resp_len = length;
        resp_valid = 1;
        @(negedge clk);
        resp_valid = 0;
    endtask

    task automatic wait_for_bytes(input int count);
        int timeout;
        timeout = 0;
        while (sent < count && timeout < 1000) begin
            @(negedge clk);
            timeout++;
        end
        if (sent != count) $fatal(1, "Expected %0d bytes, got %0d", count, sent);
        timeout = 0;
        while (!resp_ready && timeout < 1000) begin
            @(negedge clk);
            timeout++;
        end
        if (!resp_ready) $fatal(1, "Response never completed");
    endtask

    task automatic check_byte(input int index, input logic [7:0] expected);
        if (actual[index] !== expected)
            $fatal(1, "Byte %0d: expected %02h, got %02h", index, expected, actual[index]);
    endtask

    logic [7:0] expected_crc;
    initial begin
        payload[0] = 8'h12;
        payload[1] = 8'h34;
        payload[2] = 8'h56;
        repeat (5) @(negedge clk);
        rst_n = 1;
        repeat (2) @(negedge clk);

        send_response(ST_OK, 0);
        wait_for_bytes(5);
        check_byte(0, 8'h5A);
        check_byte(1, 8'h00);
        check_byte(2, 8'h00);
        check_byte(3, 8'h00);
        check_byte(4, 8'h00);
        $display("PASS: empty response");

        send_response(ST_CRC_ERR, 0);
        wait_for_bytes(10);
        expected_crc = CRC_INIT;
        expected_crc = crc8_byte(expected_crc, 8'h01);
        expected_crc = crc8_byte(expected_crc, 8'h00);
        expected_crc = crc8_byte(expected_crc, 8'h00);
        check_byte(5, 8'h5A);
        check_byte(6, 8'h01);
        check_byte(7, 8'h00);
        check_byte(8, 8'h00);
        check_byte(9, expected_crc);
        $display("PASS: CRC-error response");

        send_response(ST_OK, 3);
        wait_for_bytes(18);
        expected_crc = CRC_INIT;
        expected_crc = crc8_byte(expected_crc, 8'h00);
        expected_crc = crc8_byte(expected_crc, 8'h03);
        expected_crc = crc8_byte(expected_crc, 8'h00);
        expected_crc = crc8_byte(expected_crc, 8'h12);
        expected_crc = crc8_byte(expected_crc, 8'h34);
        expected_crc = crc8_byte(expected_crc, 8'h56);
        check_byte(10, 8'h5A);
        check_byte(11, 8'h00);
        check_byte(12, 8'h03);
        check_byte(13, 8'h00);
        check_byte(14, 8'h12);
        check_byte(15, 8'h34);
        check_byte(16, 8'h56);
        check_byte(17, expected_crc);
        if (requests != 3) $fatal(1, "Expected 3 payload requests, got %0d", requests);
        $display("PASS: payload response and CRC");
        $display("PASS: all packet_tx tests completed successfully");
        $finish;
    end
endmodule
