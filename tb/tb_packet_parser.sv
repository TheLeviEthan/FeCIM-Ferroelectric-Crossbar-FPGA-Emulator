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

    localparam int TB_TIMEOUT_CYCLES = 50;
    packet_parser #(
        .TIMEOUT_CYCLES(TB_TIMEOUT_CYCLES)
    ) dut (.*);

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

    // Replays golden vectors from tb/golden/packet_vectors.bin.hex through the parser.
    task automatic replay_golden_vectors();
        int fd;
        int status;
        logic [71:0] raw_vec;
        int vec_idx;

        $display("Replaying golden vectors from tb/golden/packet_vectors.bin.hex...");
        fd = $fopen("tb/golden/packet_vectors.bin.hex", "r");
        if (fd == 0) begin
            fd = $fopen("golden/packet_vectors.bin.hex", "r");
        end

        if (fd != 0) begin
            vec_idx = 0;
            while (!$feof(fd) && vec_idx < 4) begin
                status = $fscanf(fd, "%18h\n", raw_vec);
                if (status == 1) begin
                    for (int b = 8; b >= 0; b--) begin
                        send_byte(raw_vec[b*8 +: 8]);
                    end
                    case (vec_idx)
                        0: check_response(CMD_WRITE_WEIGHTS, ST_OK, 0);
                        1: check_response(CMD_WRITE_ACT, ST_OK, 0);
                        2: check_response(CMD_SET_CONFIG, ST_BAD_LEN, 0);
                        3: check_response(CMD_WRITE_WEIGHTS, ST_OK, 0);
                        default: ;
                    endcase
                    vec_idx++;
                end
            end
            $fclose(fd);
            $display("PASS: replayed %0d golden vectors from packet_vectors.bin.hex", vec_idx);
        end else begin
            logic [71:0] fallback_vecs [0:3];
            fallback_vecs[0] = 72'ha50104000a000000e7;
            fallback_vecs[1] = 72'ha50204001001ff00c6;
            fallback_vecs[2] = 72'ha503040000000000c2;
            fallback_vecs[3] = 72'ha50104000200000057;

            for (int v = 0; v < 4; v++) begin
                for (int b = 8; b >= 0; b--) begin
                    send_byte(fallback_vecs[v][b*8 +: 8]);
                end
                case (v)
                    0: check_response(CMD_WRITE_WEIGHTS, ST_OK, 0);
                    1: check_response(CMD_WRITE_ACT, ST_OK, 0);
                    2: check_response(CMD_SET_CONFIG, ST_BAD_LEN, 0);
                    3: check_response(CMD_WRITE_WEIGHTS, ST_OK, 0);
                endcase
            end
            $display("PASS: replayed 4 golden vectors (fallback)");
        end
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

        // Test 6: ST_UNKNOWN_CMD: unmapped command opcode
        send_packet(8'hEE, 0, payload, 0);
        check_response(8'hEE, ST_UNKNOWN_CMD, 0);
        $display("PASS: unknown command rejected with ST_UNKNOWN_CMD");

        // Test 7: wrong-length SET_CONFIG (payload length != 5)
        payload[0] = 8'h02;
        payload[1] = 8'h78;
        payload[2] = 8'h56;
        send_packet(CMD_SET_CONFIG, 3, payload, 0);
        check_response(CMD_SET_CONFIG, ST_BAD_LEN, 0);
        $display("PASS: wrong-length SET_CONFIG rejected with ST_BAD_LEN");

        // Test 8: READ_RESULT out of range
        // Case 8a: start (100) + count (50) = 150 > TILE_COLS (128)
        payload[0] = 8'd100;
        payload[1] = 8'd0;
        payload[2] = 8'd50;
        payload[3] = 8'd0;
        send_packet(CMD_READ_RESULT, 4, payload, 0);
        check_response(CMD_READ_RESULT, ST_ADDR_RANGE, 0);

        // Case 8b: count = 0
        payload[0] = 8'd0;
        payload[1] = 8'd0;
        payload[2] = 8'd0;
        payload[3] = 8'd0;
        send_packet(CMD_READ_RESULT, 4, payload, 0);
        check_response(CMD_READ_RESULT, ST_ADDR_RANGE, 0);
        $display("PASS: READ_RESULT out of range rejected with ST_ADDR_RANGE");

        // Test 9: mid-frame timeout recovery
        begin
            logic [15:0] prev_err_cnt;
            prev_err_cnt = err_cnt;
            // Send partial frame (SYNC + CMD) then stall
            send_byte(SYNC_H2F);
            send_byte(CMD_IDENTIFY);
            // Wait for parser timeout (TB_TIMEOUT_CYCLES + margin)
            repeat (TB_TIMEOUT_CYCLES + 10) @(posedge clk);
            if (err_cnt != prev_err_cnt + 16'd1 || !err_latched)
                $fatal(1, "FAIL: timeout did not increment err_cnt: prev=%0d now=%0d", prev_err_cnt, err_cnt);
            if (resp_valid)
                $fatal(1, "FAIL: timeout must not produce a response packet");
            // Confirm parser recovered back to PKT_HUNT by sending a fresh packet
            send_packet(CMD_IDENTIFY, 0, payload, 0);
            check_response(CMD_IDENTIFY, ST_OK, 16);
            $display("PASS: mid-frame timeout recovery confirmed");
        end

        // Test 10: Replay golden packet vectors from packet_vectors.bin.hex
        replay_golden_vectors();

        $display("PASS: all packet parser tests completed successfully");
        $finish;
    end
endmodule
