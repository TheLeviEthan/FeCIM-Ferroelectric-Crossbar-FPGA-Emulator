`timescale 1ns/1ps

/* verilator lint_off INITIALDLY */

module tb_readout;
    import fecim_pkg::*;

    logic clk = 0;
    always #10 clk = ~clk; // 50 MHz

    logic rst_n = 0;

    // Capture buffer & mock UART
    byte unsigned captured_frame [0:1023];
    int captured_len = 0;
    logic clear_capture = 0;

    logic [7:0] tx_data;
    logic       tx_start;
    logic       tx_busy = 0;
    int         busy_ticks = 0;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            tx_busy        <= 0;
            busy_ticks     <= 0;
            captured_len   <= 0;
        end else if (clear_capture) begin
            captured_len   <= 0;
            tx_busy        <= 0;
            busy_ticks     <= 0;
        end else if (tx_start && !tx_busy) begin
            if (captured_len < 1024) begin
                captured_frame[captured_len] <= tx_data;
                captured_len <= captured_len + 1;
            end
            tx_busy        <= 1;
            busy_ticks     <= 2;
        end else if (tx_busy) begin
            if (busy_ticks == 1) begin
                tx_busy    <= 0;
                busy_ticks <= 0;
            end else begin
                busy_ticks <= busy_ticks - 1;
            end
        end
    end

    // Interface signals
    logic [15:0]        active_cols;
    logic               parser_rx_valid;
    logic [7:0]         parser_rx_data;

    logic               bulk_we;
    bulk_target_e       bulk_target;
    logic [15:0]        bulk_addr;
    logic [7:0]         bulk_data;
    logic               cfg_we;
    logic [7:0]         cfg_addr;
    logic [31:0]        cfg_wdata;
    logic [31:0]        cfg_rdata;
    logic               start_compute;
    logic               compute_busy;
    logic               compute_done;
    logic               rd_go;
    logic               rd_argmax;
    logic [COL_AW-1:0]  rd_start;
    logic [COL_AW:0]    rd_count;

    logic               resp_valid;
    logic [7:0]         resp_cmd;
    status_e            resp_status;
    logic [15:0]        resp_len;
    logic               resp_ready;

    logic [15:0]        rx_byte_cnt, err_cnt, crc_err_cnt, timeout_cnt;
    logic               err_latched;

    // Result Buffer & Argmax signals
    logic               rb_clear_results;
    logic               rb_run_start;
    logic               rb_compute_done;
    logic               rb_we;
    logic [COL_AW-1:0]  rb_waddr;
    logic [ACC_W-1:0]   rb_wdata;
    logic [COL_AW-1:0]  rb_raddr;
    logic [ACC_W-1:0]   rb_rdata;
    logic               rb_results_valid;

    logic               au_clear;
    logic               au_in_valid;
    logic [COL_AW-1:0]  au_in_idx;
    logic [ACC_W-1:0]   au_in_val;
    logic [COL_AW-1:0]  au_argmax_idx;
    logic [ACC_W-1:0]   au_argmax_val;
    logic [ACC_W-1:0]   au_argmax_second;

    // Stub mode multiplexer signals
    logic               stub_mode;
    logic [ACC_W-1:0]   stub_rdata;
    logic [COL_AW-1:0]  stub_argmax_idx;
    logic [ACC_W-1:0]   stub_argmax_val;
    logic [ACC_W-1:0]   stub_argmax_second;

    logic [ACC_W-1:0]   mux_result_rdata;
    logic [COL_AW-1:0]  mux_argmax_idx;
    logic [ACC_W-1:0]   mux_argmax_val;
    logic [ACC_W-1:0]   mux_argmax_second;

    assign mux_result_rdata  = stub_mode ? stub_rdata         : rb_rdata;
    assign mux_argmax_idx    = stub_mode ? stub_argmax_idx    : au_argmax_idx;
    assign mux_argmax_val    = stub_mode ? stub_argmax_val    : au_argmax_val;
    assign mux_argmax_second = stub_mode ? stub_argmax_second : au_argmax_second;

    // Readout serializer & Packet TX
    logic [7:0]         pl_data;
    logic               pl_valid;
    logic               pl_next;

    // DUT instances
    packet_parser #(
        .TIMEOUT_LIMIT(1000)
    ) u_parser (
        .clk           (clk),
        .rst_n         (rst_n),
        .rx_data       (parser_rx_data),
        .rx_valid      (parser_rx_valid),
        .active_cols   (active_cols),
        .bulk_we       (bulk_we),
        .bulk_target   (bulk_target),
        .bulk_addr     (bulk_addr),
        .bulk_data     (bulk_data),
        .cfg_we        (cfg_we),
        .cfg_addr      (cfg_addr),
        .cfg_wdata     (cfg_wdata),
        .cfg_rdata     (cfg_rdata),
        .start_compute (start_compute),
        .compute_busy  (compute_busy),
        .compute_done  (compute_done),
        .rd_go         (rd_go),
        .rd_argmax     (rd_argmax),
        .rd_start      (rd_start),
        .rd_count      (rd_count),
        .resp_valid    (resp_valid),
        .resp_cmd      (resp_cmd),
        .resp_status   (resp_status),
        .resp_len      (resp_len),
        .resp_ready    (resp_ready),
        .rx_byte_cnt   (rx_byte_cnt),
        .err_cnt       (err_cnt),
        .crc_err_cnt   (crc_err_cnt),
        .timeout_cnt   (timeout_cnt),
        .err_latched   (err_latched)
    );

    readout_ser u_readout_ser (
        .clk             (clk),
        .rst_n           (rst_n),
        .resp_valid      (resp_valid),
        .resp_cmd        (resp_cmd),
        .resp_status     (resp_status),
        .resp_len        (resp_len),
        .resp_ready      (resp_ready),
        .cfg_rdata       (cfg_rdata),
        .framing_err_cnt (16'd0),
        .crc_err_cnt     (crc_err_cnt),
        .timeout_cnt     (timeout_cnt),
        .rd_go           (rd_go),
        .rd_argmax       (rd_argmax),
        .rd_start        (rd_start),
        .rd_count        (rd_count),
        .result_raddr    (rb_raddr),
        .result_rdata    (mux_result_rdata),
        .argmax_idx      (mux_argmax_idx),
        .argmax_val      (mux_argmax_val),
        .argmax_second   (mux_argmax_second),
        .rd_data         (pl_data),
        .rd_valid        (pl_valid),
        .rd_next         (pl_next)
    );

    packet_tx u_packet_tx (
        .clk         (clk),
        .rst_n       (rst_n),
        .resp_valid  (resp_valid),
        .resp_cmd    (resp_cmd),
        .resp_status (resp_status),
        .resp_len    (resp_len),
        .resp_ready  (resp_ready),
        .pl_data     (pl_data),
        .pl_valid    (pl_valid),
        .pl_next     (pl_next),
        .tx_data     (tx_data),
        .tx_start    (tx_start),
        .tx_busy     (tx_busy)
    );

    result_buffer u_result_buffer (
        .clk           (clk),
        .rst_n         (rst_n),
        .clear_results (rb_clear_results),
        .run_start     (rb_run_start),
        .compute_done  (rb_compute_done),
        .we            (rb_we),
        .waddr         (rb_waddr),
        .wdata         (rb_wdata),
        .raddr         (rb_raddr),
        .rdata         (rb_rdata),
        .results_valid (rb_results_valid)
    );

    argmax_unit u_argmax_unit (
        .clk           (clk),
        .rst_n         (rst_n),
        .clear         (au_clear),
        .active_cols   (active_cols),
        .in_valid      (au_in_valid),
        .in_idx        (au_in_idx),
        .in_val        (au_in_val),
        .argmax_idx    (au_argmax_idx),
        .argmax_val    (au_argmax_val),
        .argmax_second (au_argmax_second)
    );

    // Stub buffer memory (256 words)
    logic [31:0] stub_mem [0:255];
    always_ff @(posedge clk) begin
        stub_rdata <= stub_mem[rb_raddr];
    end

    // Send single byte on negedge clk (matching tb_packet_parser convention)
    task automatic send_byte(input logic [7:0] b);
        @(negedge clk);
        parser_rx_data  = b;
        parser_rx_valid = 1;
        @(negedge clk);
        parser_rx_valid = 0;
    endtask

    // Send complete request packet
    task automatic send_request(
        input logic [7:0] cmd,
        input int         n,
        input logic [7:0] payload [0:3]
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
        send_byte(checksum);
    endtask

    // Clear capture buffer
    task automatic reset_capture();
        @(negedge clk);
        clear_capture = 1;
        @(negedge clk);
        clear_capture = 0;
    endtask

    // Wait for packet_tx to complete response
    task automatic wait_for_response(output int len_out);
        int timeout_cnt;
        timeout_cnt = 0;
        while (captured_len == 0 && timeout_cnt < 20000) begin
            @(posedge clk);
            timeout_cnt++;
        end
        if (timeout_cnt >= 20000) $fatal(1, "TIMEOUT: no response frame started");

        repeat (5) @(posedge clk);
        timeout_cnt = 0;
        while ((tx_busy || !resp_ready) && timeout_cnt < 20000) begin
            @(posedge clk);
            timeout_cnt++;
        end
        if (timeout_cnt >= 20000) $fatal(1, "TIMEOUT: response frame not completed");
        repeat (5) @(posedge clk);
        len_out = captured_len;
    endtask

    // Validate captured response frame structure and CRC-8
    task automatic check_frame(
        input string   name,
        input status_e exp_status,
        input int      exp_payload_len
    );
        logic [7:0] calc_crc;
        int exp_total;
        logic [15:0] pl_len;

        exp_total = 1 + 1 + 2 + exp_payload_len + 1; // SYNC + STATUS + LEN(2) + PAYLOAD + CRC
        if (captured_len != exp_total) begin
            $fatal(1, "FAIL [%s]: Total frame length mismatch: got %0d, expected %0d",
                   name, captured_len, exp_total);
        end

        if (captured_frame[0] != SYNC_F2H) begin
            $fatal(1, "FAIL [%s]: Sync byte mismatch: got 0x%02X, expected 0x%02X",
                   name, captured_frame[0], SYNC_F2H);
        end

        if (captured_frame[1] != 8'(exp_status)) begin
            $fatal(1, "FAIL [%s]: Status byte mismatch: got 0x%02X, expected 0x%02X",
                   name, captured_frame[1], 8'(exp_status));
        end

        pl_len = {captured_frame[3], captured_frame[2]};
        if (int'(pl_len) != exp_payload_len) begin
            $fatal(1, "FAIL [%s]: Payload length mismatch: got %0d, expected %0d",
                   name, pl_len, exp_payload_len);
        end

        calc_crc = CRC_INIT;
        for (int i = 1; i < captured_len - 1; i++) begin
            calc_crc = crc8_byte(calc_crc, captured_frame[i]);
        end
        if (captured_frame[captured_len - 1] != calc_crc) begin
            $fatal(1, "FAIL [%s]: CRC-8 mismatch: calculated 0x%02X, received 0x%02X",
                   name, calc_crc, captured_frame[captured_len - 1]);
        end

        $display("  -> [%s] Frame format & CRC-8 valid (status=0x%02X, len=%0d)",
                 name, captured_frame[1], pl_len);
    endtask

    function automatic int get_word(int idx);
        int offset;
        logic [31:0] w;
        offset = 4 + (idx * 4);
        w = {captured_frame[offset+3], captured_frame[offset+2],
             captured_frame[offset+1], captured_frame[offset]};
        return int'(w);
    endfunction

    task automatic check_argmax(
        input string name,
        input int    exp_idx,
        input int    exp_val,
        input int    exp_sec
    );
        logic [15:0] idx;
        int val, sec;

        idx = {captured_frame[5], captured_frame[4]};
        val = int'({captured_frame[9], captured_frame[8], captured_frame[7], captured_frame[6]});
        sec = int'({captured_frame[13], captured_frame[12], captured_frame[11], captured_frame[10]});

        if (int'(idx) != exp_idx || val != exp_val || sec != exp_sec) begin
            $fatal(1, "FAIL [%s]: Argmax record mismatch: got (idx=%0d, val=%0d, sec=%0d), expected (idx=%0d, val=%0d, sec=%0d)",
                   name, idx, val, sec, exp_idx, exp_val, exp_sec);
        end
        $display("  -> [%s] Decoded argmax: idx=%0d, max=%0d, second=%0d [MATCH]",
                 name, idx, val, sec);
    endtask

    int total_bytes;
    logic [7:0] req_payload [0:3];

    initial begin
        $display("=======================================================");
        $display("  FeCIM Readout Path Verification (Task A3)");
        $display("=======================================================");

        rst_n            = 0;
        stub_mode        = 1;
        active_cols      = 16'd48;
        parser_rx_valid  = 0;
        parser_rx_data   = 0;
        clear_capture    = 0;
        cfg_rdata        = 32'h0;
        compute_busy     = 0;
        compute_done     = 0;
        rb_clear_results = 0;
        rb_run_start     = 0;
        rb_compute_done  = 0;
        rb_we            = 0;
        rb_waddr         = 0;
        rb_wdata         = 0;
        au_clear         = 0;
        au_in_valid      = 0;
        au_in_idx        = 0;
        au_in_val        = 0;

        repeat (5) @(posedge clk);
        rst_n = 1;
        repeat (5) @(posedge clk);

        // Pre-fill stub memory
        for (int i = 0; i < 256; i++) begin
            stub_mem[i] = 32'(32'h1000_0000 + (i * 32'h0102_0304));
        end

        //=====================================================================
        // Phase 1: Pre-filled stub buffer
        //=====================================================================
        $display("\n[Phase 1] READ_RESULT on pre-filled stub buffer...");

        // 1.1: Read 1 word from start = 0
        reset_capture();
        req_payload[0] = 8'd0; req_payload[1] = 8'd0;
        req_payload[2] = 8'd1; req_payload[3] = 8'd0;
        send_request(CMD_READ_RESULT, 4, req_payload);
        wait_for_response(total_bytes);
        check_frame("Stub Read 1 Word", ST_OK, 4);
        if (get_word(0) != int'(stub_mem[0])) $fatal(1, "FAIL: word 0 mismatch");
        $display("  -> Word 0 little-endian unpack: 0x%08X == 0x%08X [OK]", get_word(0), stub_mem[0]);

        // 1.2: Read slice (start = 5, count = 4)
        reset_capture();
        req_payload[0] = 8'd5; req_payload[1] = 8'd0;
        req_payload[2] = 8'd4; req_payload[3] = 8'd0;
        send_request(CMD_READ_RESULT, 4, req_payload);
        wait_for_response(total_bytes);
        check_frame("Stub Read 4 Words Slice", ST_OK, 16);
        for (int i = 0; i < 4; i++) begin
            if (get_word(i) != int'(stub_mem[5 + i])) $fatal(1, "FAIL: slice mismatch");
        end
        $display("  -> Slice [5..8] verified against Natalie codec format [OK]");

        // 1.3: READ_ARGMAX on stub inputs
        $display("\n[Phase 1.3] READ_ARGMAX on stub inputs...");
        stub_argmax_idx    = 7'd27;
        stub_argmax_val    = 32'sd543210;
        stub_argmax_second = -32'sd98765;
        reset_capture();
        send_request(CMD_READ_ARGMAX, 0, req_payload);
        wait_for_response(total_bytes);
        check_frame("Stub READ_ARGMAX", ST_OK, 10);
        check_argmax("Stub READ_ARGMAX", 27, 543210, -98765);

        //=====================================================================
        // Phase 2: Active cols address range checks
        //=====================================================================
        $display("\n[Phase 2] Verifying address range checks (active_cols = 48)...");
        active_cols = 16'd48;

        // 2.1: Valid 48 columns (0 + 48 == 48)
        reset_capture();
        req_payload[0] = 8'd0;  req_payload[1] = 8'd0;
        req_payload[2] = 8'd48; req_payload[3] = 8'd0;
        send_request(CMD_READ_RESULT, 4, req_payload);
        wait_for_response(total_bytes);
        check_frame("Boundary Valid 48 cols", ST_OK, 48 * 4);
        $display("  -> Full active_cols (48 words, 192 bytes) accepted with ST_OK [OK]");

        // 2.2: Exceed count: start = 0, count = 49 (49 > 48)
        reset_capture();
        req_payload[0] = 8'd0;  req_payload[1] = 8'd0;
        req_payload[2] = 8'd49; req_payload[3] = 8'd0;
        send_request(CMD_READ_RESULT, 4, req_payload);
        wait_for_response(total_bytes);
        check_frame("Exceed count=49", ST_ADDR_RANGE, 0);

        // 2.3: Exceed sum: start = 40, count = 10 (50 > 48)
        reset_capture();
        req_payload[0] = 8'd40; req_payload[1] = 8'd0;
        req_payload[2] = 8'd10; req_payload[3] = 8'd0;
        send_request(CMD_READ_RESULT, 4, req_payload);
        wait_for_response(total_bytes);
        check_frame("Sum exceeds active_cols (40+10)", ST_ADDR_RANGE, 0);

        // 2.4: Start at active_cols (48 + 1 > 48)
        reset_capture();
        req_payload[0] = 8'd48; req_payload[1] = 8'd0;
        req_payload[2] = 8'd1;  req_payload[3] = 8'd0;
        send_request(CMD_READ_RESULT, 4, req_payload);
        wait_for_response(total_bytes);
        check_frame("Start at active_cols", ST_ADDR_RANGE, 0);

        // 2.5: Zero count
        reset_capture();
        req_payload[0] = 8'd0; req_payload[1] = 8'd0;
        req_payload[2] = 8'd0; req_payload[3] = 8'd0;
        send_request(CMD_READ_RESULT, 4, req_payload);
        wait_for_response(total_bytes);
        check_frame("Zero count", ST_ADDR_RANGE, 0);
        $display("  -> All address range checks rejected with ST_ADDR_RANGE [OK]");

        //=====================================================================
        // Phase 3: Real result_buffer and argmax_unit
        //=====================================================================
        $display("\n[Phase 3] Integration with REAL result_buffer and argmax_unit...");
        stub_mode = 0;

        @(negedge clk);
        rb_clear_results = 1;
        au_clear         = 1;
        @(negedge clk);
        rb_clear_results = 0;
        au_clear         = 0;
        @(negedge clk);

        // Write columns 0..47
        for (int c = 0; c < 48; c++) begin
            logic [31:0] val;
            case (c)
                10: val = 32'sd12345;
                15: val = 32'sd999999;  // WINNER
                20: val = 32'sd777777;  // SECOND
                25: val = -32'sd45678;
                default: val = 32'sd100 + 32'(c);
            endcase

            @(negedge clk);
            rb_we    = 1;
            rb_waddr = 7'(c);
            rb_wdata = val;

            au_in_valid = 1;
            au_in_idx   = 7'(c);
            au_in_val   = val;
        end

        @(negedge clk);
        rb_we           = 0;
        au_in_valid     = 0;
        rb_compute_done = 1;
        @(negedge clk);
        rb_compute_done = 0;
        repeat (2) @(posedge clk);

        if (!rb_results_valid) $fatal(1, "FAIL: rb_results_valid not asserted");
        $display("  -> Real result_buffer results_valid asserted [OK]");

        // Read real argmax
        reset_capture();
        send_request(CMD_READ_ARGMAX, 0, req_payload);
        wait_for_response(total_bytes);
        check_frame("Real READ_ARGMAX", ST_OK, 10);
        check_argmax("Real READ_ARGMAX", 15, 999999, 777777);

        // Read real slice (cols 14..16)
        reset_capture();
        req_payload[0] = 8'd14; req_payload[1] = 8'd0;
        req_payload[2] = 8'd3;  req_payload[3] = 8'd0;
        send_request(CMD_READ_RESULT, 4, req_payload);
        wait_for_response(total_bytes);
        check_frame("Real READ_RESULT slice", ST_OK, 12);
        if (get_word(0) != (100 + 14)) $fatal(1, "FAIL: col 14 mismatch");
        if (get_word(1) != 999999)     $fatal(1, "FAIL: col 15 mismatch");
        if (get_word(2) != (100 + 16)) $fatal(1, "FAIL: col 16 mismatch");
        $display("  -> Readback from real result_buffer: cols 14..16 verified [OK]");

        // Read signed negative word from col 25
        reset_capture();
        req_payload[0] = 8'd25; req_payload[1] = 8'd0;
        req_payload[2] = 8'd1;  req_payload[3] = 8'd0;
        send_request(CMD_READ_RESULT, 4, req_payload);
        wait_for_response(total_bytes);
        check_frame("Real READ_RESULT negative", ST_OK, 4);
        if (get_word(0) != -45678) $fatal(1, "FAIL: col 25 negative mismatch");
        $display("  -> Signed negative int32 readback verified: %0d [OK]", get_word(0));

        // Test 3.2: Tie breaking in real argmax_unit (lowest index wins)
        $display("\n[Phase 3.2] Testing argmax tie-breaking in real argmax_unit...");
        @(negedge clk);
        au_clear = 1;
        @(negedge clk);
        au_clear = 0;
        @(negedge clk);

        @(negedge clk);
        au_in_valid = 1; au_in_idx = 7'd2; au_in_val = 32'sd1000;
        @(negedge clk);
        au_in_valid = 1; au_in_idx = 7'd4; au_in_val = 32'sd5000;
        @(negedge clk);
        au_in_valid = 1; au_in_idx = 7'd8; au_in_val = 32'sd5000;
        @(negedge clk);
        au_in_valid = 0;
        repeat (2) @(posedge clk);

        reset_capture();
        send_request(CMD_READ_ARGMAX, 0, req_payload);
        wait_for_response(total_bytes);
        check_frame("Argmax Tie Breaking", ST_OK, 10);
        check_argmax("Argmax Tie Breaking", 4, 5000, 5000);
        $display("  -> Lowest-index tie resolution confirmed: idx=4, max=5000, second=5000 [OK]");

        // Test 3.3: All negative values
        $display("\n[Phase 3.3] Testing all-negative values in real argmax_unit...");
        @(negedge clk);
        au_clear = 1;
        @(negedge clk);
        au_clear = 0;
        @(negedge clk);

        @(negedge clk);
        au_in_valid = 1; au_in_idx = 7'd0; au_in_val = -32'sd500;
        @(negedge clk);
        au_in_valid = 1; au_in_idx = 7'd1; au_in_val = -32'sd100;
        @(negedge clk);
        au_in_valid = 1; au_in_idx = 7'd2; au_in_val = -32'sd250;
        @(negedge clk);
        au_in_valid = 0;
        repeat (2) @(posedge clk);

        reset_capture();
        send_request(CMD_READ_ARGMAX, 0, req_payload);
        wait_for_response(total_bytes);
        check_frame("All Negative Argmax", ST_OK, 10);
        check_argmax("All Negative Argmax", 1, -100, -250);
        $display("  -> All-negative signed comparison confirmed: idx=1, max=-100, second=-250 [OK]");

        $display("\n=======================================================");
        $display("PASS: all readout tests completed successfully");
        $display("=======================================================");
        $finish;
    end

endmodule : tb_readout
