`timescale 1ns/1ps

module tb_packet_parser;
    import fecim_pkg::*;

    logic clk = 0;
    always #10 clk = ~clk; // 50 MHz

    logic rst_n = 0;
    logic [7:0] rx_data = 0;
    logic rx_valid = 0;
    logic bulk_we;
    bulk_target_e bulk_target;
    logic [15:0] bulk_addr;
    logic [7:0] bulk_data;
    logic cfg_we;
    logic [7:0] cfg_addr;
    logic [31:0] cfg_wdata;
    logic [31:0] cfg_rdata = 32'h12345678;
    logic start_compute;
    logic compute_busy = 0, compute_done = 0;
    logic rd_go, rd_argmax;
    logic [COL_AW-1:0] rd_start;
    logic [COL_AW:0] rd_count;
    logic resp_valid;
    logic [7:0] resp_cmd;
    status_e resp_status;
    logic [15:0] resp_len;
    logic resp_ready = 0;
    logic [15:0] rx_byte_cnt, err_cnt;
    logic err_latched;

    packet_parser dut (.*);

    int bulk_writes = 0;
    int cfg_writes = 0;
    logic [15:0] seen_addr [0:3];
    logic [7:0] seen_data [0:3];
    logic [7:0] seen_cfg_addr;
    logic [31:0] seen_cfg_data;

    always @(posedge clk) begin
        if (rst_n) begin
            if (bulk_we) begin
                if (bulk_writes < 4) begin
                    seen_addr[bulk_writes] = bulk_addr;
                    seen_data[bulk_writes] = bulk_data;
                end
                bulk_writes = bulk_writes + 1;
            end
            if (cfg_we) begin
                cfg_writes = cfg_writes + 1;
                seen_cfg_addr = cfg_addr;
                seen_cfg_data = cfg_wdata;
            end
        end
    end

    task automatic send_byte(input logic [7:0] value);
        @(negedge clk);
        rx_data = value;
        rx_valid = 1;
        @(negedge clk);
        rx_valid = 0;
    endtask

    // Sends a complete packet with CRC-8 over CMD, LEN0, LEN1, and payload.
    task automatic send_packet(
        input logic [7:0] cmd,
        input int unsigned n,
        input logic [7:0] payload [0:511],
        input bit corrupt_crc
    );
        logic [7:0] checksum;
        checksum = CRC_INIT;
        send_byte(SYNC_H2F);
        send_byte(cmd);
        checksum = crc8_byte(checksum, cmd);
        send_byte(8'(n));
        checksum = crc8_byte(checksum, 8'(n));
        send_byte(8'(n >> 8));
        checksum = crc8_byte(checksum, 8'(n >> 8));
        for (int i = 0; i < n; i++) begin
            send_byte(payload[i]);
            checksum = crc8_byte(checksum, payload[i]);
        end
        send_byte(corrupt_crc ? (checksum ^ 8'hFF) : checksum);
    endtask

    task automatic check_response(
        input logic [7:0] expected_cmd,
        input status_e expected_status,
        input logic [15:0] expected_len
    );
        int cycles;
        cycles = 0;
        while (!resp_valid && cycles < 20) begin
            @(negedge clk);
            cycles++;
        end
        if (!resp_valid)
            $fatal(1, "Timeout waiting for response to command %02h", expected_cmd);
        if (resp_cmd !== expected_cmd || resp_status !== expected_status ||
            resp_len !== expected_len)
            $fatal(1, "Wrong response: cmd=%02h status=%0d len=%0d (expected %02h %0d %0d)",
                resp_cmd, resp_status, resp_len,
                expected_cmd, expected_status, expected_len);
        @(negedge clk);
        resp_ready = 1;
        @(negedge clk);
        resp_ready = 0;
        // Let the parser return to HUNT before sending another frame.
        repeat (2) @(negedge clk);
    endtask

    logic [7:0] payload [0:511];

    initial begin
        repeat (5) @(negedge clk);
        rst_n = 1;
        repeat (2) @(negedge clk);

        // Test 1: IDENTIFY, zero-byte payload, response length 16.
        send_packet(CMD_IDENTIFY, 0, payload, 0);
        check_response(CMD_IDENTIFY, ST_OK, 16);
        $display("PASS: IDENTIFY packet accepted");

        // Test 2: CRC mismatch must not dispatch a command.
        send_packet(CMD_IDENTIFY, 0, payload, 1);
        check_response(CMD_IDENTIFY, ST_CRC_ERR, 0);
        $display("PASS: invalid CRC rejected");

        // Test 3: SET_CONFIG: register 0x02, value 0x12345678 (little endian).
        payload[0] = 8'h02;
        payload[1] = 8'h78;
        payload[2] = 8'h56;
        payload[3] = 8'h34;
        payload[4] = 8'h12;
        send_packet(CMD_SET_CONFIG, 5, payload, 0);
        check_response(CMD_SET_CONFIG, ST_OK, 0);
        if (cfg_writes != 1 || seen_cfg_addr !== 8'h02 ||
            seen_cfg_data !== 32'h12345678)
            $fatal(1, "SET_CONFIG incorrect: writes=%0d addr=%02h data=%08h",
                cfg_writes, seen_cfg_addr, seen_cfg_data);
        $display("PASS: SET_CONFIG writes correct register and value");

        // Test 4: WRITE_WEIGHTS, starting address 0x0010, two data bytes.
        payload[0] = 8'h10;
        payload[1] = 8'h00;
        payload[2] = 8'hAB;
        payload[3] = 8'hCD;
        send_packet(CMD_WRITE_WEIGHTS, 4, payload, 0);
        check_response(CMD_WRITE_WEIGHTS, ST_OK, 0);
        if (bulk_writes != 2 || seen_addr[0] !== 16'h0010 ||
            seen_addr[1] !== 16'h0011 || seen_data[0] !== 8'hAB ||
            seen_data[1] !== 8'hCD)
            $fatal(1, "WRITE_WEIGHTS incorrect: writes=%0d addr0=%04h addr1=%04h data0=%02h data1=%02h",
                bulk_writes, seen_addr[0], seen_addr[1], seen_data[0], seen_data[1]);
        $display("PASS: WRITE_WEIGHTS streams two bytes at correct addresses");

        // Test 5: oversized declared payload, rejected immediately after LEN1.
        send_byte(SYNC_H2F);
        send_byte(CMD_WRITE_WEIGHTS);
        send_byte(8'h01); // 513 bytes = 0x0201
        send_byte(8'h02);
        check_response(CMD_WRITE_WEIGHTS, ST_BAD_LEN, 0);
        $display("PASS: oversized payload rejected");

        $display("PASS: all packet parser tests completed successfully");
        $finish;
    end
endmodule
