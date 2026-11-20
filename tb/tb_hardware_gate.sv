//=============================================================================
// tb_hardware_gate.sv -- Lane A
//
// Integration testbench verifying hardware gate requirements (Task A2):
//   - Boots at 115200 baud (BAUD_INC = 2416)
//   - Verifies IDENTIFY response format and contents (magic 0xFEC1, PROTO_VER 0x02)
//   - Switches baud rate to 921600 (BAUD_INC = 19327) and reconnects
//   - Verifies GET_CONFIG for REG_BAUD_INC returns 19327 at 921600
//   - Performs SET_CONFIG -> GET_CONFIG round trip on every RW register
//   - Injects bad CRC packet and confirms ST_CRC_ERR response
//   - Verifies error counters in IDENTIFY update accordingly
//=============================================================================

`timescale 1ns/1ps

module tb_hardware_gate;
    import fecim_pkg::*;

    // 50 MHz Clock
    logic clk = 0;
    always #10 clk = ~clk;

    // DE10-Lite Board Pins
    logic [1:0]  key = 2'b11;
    wire  [9:0]  ledr;
    wire  [6:0]  hex0;
    wire  [7:0]  hex0_full;
    wire         board_txd;

    // Testbench UART Driver
    logic [BAUD_ACC_W-1:0] tb_baud_inc = BAUD_INC_115200;
    logic tb_tick_16x;
    logic tb_rst_n = 0;

    baud_gen u_tb_bg (
        .clk      (clk),
        .rst_n    (tb_rst_n),
        .baud_inc (tb_baud_inc),
        .tick_16x (tb_tick_16x)
    );

    logic       tb_tx_start = 0;
    logic [7:0] tb_tx_data  = 0;
    logic       tb_tx_busy;
    wire        tb_txd;

    uart_tx u_tb_tx (
        .clk      (clk),
        .rst_n    (tb_rst_n),
        .tick_16x (tb_tick_16x),
        .tx_data  (tb_tx_data),
        .tx_start (tb_tx_start),
        .tx_busy  (tb_tx_busy),
        .txd      (tb_txd)
    );

    logic [7:0] tb_rx_data;
    logic       tb_rx_valid;
    logic       tb_rx_frame_err;

    uart_rx u_tb_rx (
        .clk          (clk),
        .rst_n        (tb_rst_n),
        .tick_16x     (tb_tick_16x),
        .rxd          (board_txd),
        .rx_data      (tb_rx_data),
        .rx_valid     (tb_rx_valid),
        .rx_frame_err (tb_rx_frame_err)
    );

    // Static RX FIFO to prevent dropped bytes (fully Verilator & Questa compatible)
    localparam int FIFO_DEPTH = 256;
    logic [7:0] rx_fifo [0:255];
    logic [7:0] rx_wr_ptr = 8'd0;
    logic [7:0] rx_rd_ptr = 8'd0;

    // Single-producer: only clocked block modifies rx_wr_ptr
    always @(posedge clk) begin
        if (!tb_rst_n) begin
            rx_wr_ptr <= 8'd0;
        end else if (tb_rx_valid) begin
            rx_fifo[rx_wr_ptr] <= tb_rx_data;
            rx_wr_ptr <= rx_wr_ptr + 8'd1;
        end
    end

    // DUT: Top-level FeCIM design
    fecim_top dut (
        .MAX10_CLK1_50 (clk),
        .KEY           (key),
        .SW            (10'd0),
        .LEDR          (ledr),
        .HEX0          (hex0_full),
        .HEX1          (),
        .HEX2          (),
        .HEX3          (),
        .HEX4          (),
        .HEX5          (),
        .ARDUINO_IO0   (tb_txd),
        .ARDUINO_IO1   (board_txd)
    );

    assign hex0 = hex0_full[6:0];

    //-------------------------------------------------------------------------
    // Testbench Helper Tasks
    //-------------------------------------------------------------------------

    task automatic send_byte(input logic [7:0] b);
        wait (!tb_tx_busy);
        @(negedge clk);
        tb_tx_data  = b;
        tb_tx_start = 1'b1;
        @(negedge clk);
        tb_tx_start = 1'b0;
        wait (tb_tx_busy == 1'b1);
        wait (tb_tx_busy == 1'b0);
        repeat (5) @(posedge clk);
    endtask

    task automatic recv_byte(output logic [7:0] b, input int timeout_cycles = 500000);
        int elapsed;
        elapsed = 0;
        while (rx_wr_ptr == rx_rd_ptr) begin
            @(posedge clk);
            elapsed++;
            if (elapsed > timeout_cycles) begin
                $display("FATAL: TIMEOUT in recv_byte after %0d cycles (wr_ptr=%0d, rd_ptr=%0d, tb_rx_valid=%b, rxd=%b)",
                         timeout_cycles, rx_wr_ptr, rx_rd_ptr, tb_rx_valid, board_txd);
                $fatal(1, "TIMEOUT in recv_byte after %0d cycles", timeout_cycles);
            end
        end
        b = rx_fifo[rx_rd_ptr];
        rx_rd_ptr = rx_rd_ptr + 8'd1;
    endtask

    task automatic send_packet(
        input logic [7:0]    cmd,
        input int unsigned   n,
        input logic [7:0]    payload [0:63],
        input bit            corrupt_crc = 0
    );
        logic [7:0] crc;
        crc = crc8_byte(CRC_INIT, cmd);
        crc = crc8_byte(crc, 8'(n));
        crc = crc8_byte(crc, 8'(n >> 8));
        for (int i = 0; i < n; i++) begin
            crc = crc8_byte(crc, payload[i]);
        end
        if (corrupt_crc) crc = ~crc;

        send_byte(SYNC_H2F);
        send_byte(cmd);
        send_byte(8'(n));
        send_byte(8'(n >> 8));
        for (int i = 0; i < n; i++) begin
            send_byte(payload[i]);
        end
        send_byte(crc);
    endtask

    task automatic recv_response(
        output status_e     status,
        output logic [15:0] len,
        output logic [7:0]  payload [0:63]
    );
        logic [7:0] sync_byte, st_byte, len0, len1, crc_rx, crc_calc;

        // Hunt for SYNC_F2H
        recv_byte(sync_byte);
        while (sync_byte != SYNC_F2H) begin
            recv_byte(sync_byte);
        end

        recv_byte(st_byte);
        status = status_e'(st_byte);

        recv_byte(len0);
        recv_byte(len1);
        len = {len1, len0};

        for (int i = 0; i < 64; i++) payload[i] = 8'd0;
        for (int i = 0; i < len; i++) begin
            if (i < 64) recv_byte(payload[i]);
            else begin
                logic [7:0] dummy;
                recv_byte(dummy);
            end
        end

        recv_byte(crc_rx);

        // Verify CRC
        crc_calc = crc8_byte(CRC_INIT, st_byte);
        crc_calc = crc8_byte(crc_calc, len0);
        crc_calc = crc8_byte(crc_calc, len1);
        for (int i = 0; i < len; i++) begin
            if (i < 64) crc_calc = crc8_byte(crc_calc, payload[i]);
        end

        if (crc_rx != crc_calc) begin
            $fatal(1, "Response CRC mismatch: received 0x%02h, expected 0x%02h", crc_rx, crc_calc);
        end
    endtask

    task automatic set_config(input logic [7:0] reg_addr, input logic [31:0] val);
        logic [7:0] pl [0:63];
        status_e resp_st;
        logic [15:0] resp_len;
        logic [7:0] resp_pl [0:63];

        for (int i = 0; i < 64; i++) pl[i] = 8'd0;
        pl[0] = reg_addr;
        pl[1] = val[7:0];
        pl[2] = val[15:8];
        pl[3] = val[23:16];
        pl[4] = val[31:24];

        send_packet(CMD_SET_CONFIG, 5, pl);
        recv_response(resp_st, resp_len, resp_pl);

        if (resp_st != ST_OK || resp_len != 0) begin
            $fatal(1, "SET_CONFIG reg 0x%02h failed: st=0x%02h, len=%0d", reg_addr, resp_st, resp_len);
        end
    endtask

    task automatic get_config(input logic [7:0] reg_addr, output logic [31:0] val);
        logic [7:0] pl [0:63];
        status_e resp_st;
        logic [15:0] resp_len;
        logic [7:0] resp_pl [0:63];

        for (int i = 0; i < 64; i++) pl[i] = 8'd0;
        pl[0] = reg_addr;
        send_packet(CMD_GET_CONFIG, 1, pl);
        recv_response(resp_st, resp_len, resp_pl);

        if (resp_st != ST_OK || resp_len != 4) begin
            $fatal(1, "GET_CONFIG reg 0x%02h failed: st=0x%02h, len=%0d", reg_addr, resp_st, resp_len);
        end

        val = {resp_pl[3], resp_pl[2], resp_pl[1], resp_pl[0]};
    endtask

    task automatic test_rw_register(input logic [7:0] reg_addr, input logic [31:0] test_val, input string reg_name);
        logic [31:0] read_val;
        set_config(reg_addr, test_val);
        get_config(reg_addr, read_val);
        if (read_val !== test_val) begin
            $fatal(1, "Register %s (0x%02h) mismatch: expected 0x%08h, got 0x%08h",
                   reg_name, reg_addr, test_val, read_val);
        end
        $display("  -> %-18s (0x%02h): 0x%08h confirmed", reg_name, reg_addr, read_val);
    endtask

    //-------------------------------------------------------------------------
    // Main Test Sequence
    //-------------------------------------------------------------------------

    initial begin
        logic [7:0] empty_pl [0:63];
        status_e resp_st;
        logic [15:0] resp_len;
        logic [7:0] resp_pl [0:63];
        logic [31:0] read_val;
        logic [15:0] magic, num_lanes, tile_rows, tile_cols;
        logic [15:0] framing_errs, crc_errs, timeout_cnt;
        logic [7:0]  proto_ver, build_id;

        for (int i = 0; i < 64; i++) empty_pl[i] = 8'd0;

        $display("\n=======================================================");
        $display("  FeCIM Hardware Gate Integration Test (Task A2)");
        $display("=======================================================\n");

        // 1. Reset cycle
        tb_rst_n = 0;
        key[0]   = 0; // Assert KEY[0] reset
        #200;
        @(negedge clk);
        key[0]   = 1; // Deassert reset
        tb_rst_n = 1;
        #1000;

        // 2. Test IDENTIFY at default boot baud (115200)
        $display("[Step 1] Sending IDENTIFY at boot rate 115200 baud...");
        send_packet(CMD_IDENTIFY, 0, empty_pl);
        recv_response(resp_st, resp_len, resp_pl);

        if (resp_st != ST_OK || resp_len != 16) begin
            $fatal(1, "IDENTIFY failed: st=0x%02h, len=%0d", resp_st, resp_len);
        end

        magic        = {resp_pl[1],  resp_pl[0]};
        proto_ver    = resp_pl[2];
        build_id     = resp_pl[3];
        num_lanes    = {resp_pl[5],  resp_pl[4]};
        tile_rows    = {resp_pl[7],  resp_pl[6]};
        tile_cols    = {resp_pl[9],  resp_pl[8]};
        framing_errs = {resp_pl[11], resp_pl[10]};
        crc_errs     = {resp_pl[13], resp_pl[12]};
        timeout_cnt  = {resp_pl[15], resp_pl[14]};

        if (magic !== MAGIC) begin
            $fatal(1, "IDENTIFY magic mismatch: expected 0x%04h, got 0x%04h", MAGIC, magic);
        end
        if (proto_ver !== PROTO_VER) begin
            $fatal(1, "IDENTIFY proto_ver mismatch: expected 0x%02h, got 0x%02h", PROTO_VER, proto_ver);
        end
        if (build_id !== BUILD_ID) begin
            $fatal(1, "IDENTIFY build_id mismatch: expected 0x%02h, got 0x%02h", BUILD_ID, build_id);
        end
        if (num_lanes !== 16'(NUM_LANES) || tile_rows !== 16'(TILE_ROWS) || tile_cols !== 16'(TILE_COLS)) begin
            $fatal(1, "IDENTIFY geometry mismatch: lanes=%0d, rows=%0d, cols=%0d", num_lanes, tile_rows, tile_cols);
        end
        if (framing_errs !== 0 || crc_errs !== 0 || timeout_cnt !== 0) begin
            $fatal(1, "IDENTIFY initial error counters non-zero: framing=%0d, crc=%0d, timeout=%0d",
                   framing_errs, crc_errs, timeout_cnt);
        end

        $display("PASS: IDENTIFY verified: magic=0x%04h, proto=0x%02h, build=0x%02h, lanes=%0d, geometry=%0dx%0d",
                 magic, proto_ver, build_id, num_lanes, tile_rows, tile_cols);

        // 3. Switch baud rate to 921600 (BAUD_INC = 19327)
        $display("\n[Step 2] Sending SET_CONFIG for REG_BAUD_INC (19327) at 115200...");
        begin
            logic [7:0] pl [0:63];
            for (int i = 0; i < 64; i++) pl[i] = 8'd0;
            pl[0] = REG_BAUD_INC;
            pl[1] = 8'h7F;
            pl[2] = 8'h4B;
            pl[3] = 8'h00;
            pl[4] = 8'h00;
            send_packet(CMD_SET_CONFIG, 5, pl);

            // Reconnect testbench UART at 921600 baud (mimicking host driver reconnect)
            #200000;
            rx_rd_ptr = rx_wr_ptr;
            tb_baud_inc = BAUD_INC_921600;
            #20000;
        end
        $display("PASS: Reconnected at 921600 baud");

        // 4. Verify GET_CONFIG on REG_BAUD_INC at 921600 baud
        $display("\n[Step 3] Verifying GET_CONFIG on REG_BAUD_INC at 921600 baud...");
        get_config(REG_BAUD_INC, read_val);
        if (read_val !== 32'd19327) begin
            $fatal(1, "REG_BAUD_INC readback mismatch: expected 19327, got %0d", read_val);
        end
        $display("PASS: GET_CONFIG REG_BAUD_INC returned %0d at 921600 baud", read_val);

        // 5. SET_CONFIG -> GET_CONFIG round-trip on all RW registers at 921600 baud
        $display("\n[Step 4] Sweeping SET_CONFIG -> GET_CONFIG round-trip on all RW registers...");
        test_rw_register(REG_TILE_CFG,      32'h00300030, "REG_TILE_CFG");
        test_rw_register(REG_QUANT_LEVELS,  32'd64,       "REG_QUANT_LEVELS");
        test_rw_register(REG_D2D_SIGMA,     32'd15,       "REG_D2D_SIGMA");
        test_rw_register(REG_READ_SIGMA,    32'd25,       "REG_READ_SIGMA");
        test_rw_register(REG_NOISE_SEED,    32'hA5A55A5A, "REG_NOISE_SEED");
        test_rw_register(REG_NOISE_EN,      32'h0000001F, "REG_NOISE_EN");
        test_rw_register(REG_ADC_BITS,      32'd8,        "REG_ADC_BITS");
        test_rw_register(REG_STUCK_RATE,    32'h0001ABCD, "REG_STUCK_RATE");
        test_rw_register(REG_QUANT_MULT,    32'd1024,     "REG_QUANT_MULT");
        $display("PASS: all RW registers confirmed round-trip at 921600 baud");

        // 6. Bad CRC test
        $display("\n[Step 5] Sending frame with corrupted CRC at 921600 baud...");
        send_packet(CMD_IDENTIFY, 0, empty_pl, 1); // corrupt CRC
        recv_response(resp_st, resp_len, resp_pl);
        if (resp_st !== ST_CRC_ERR || resp_len !== 0) begin
            $fatal(1, "Bad CRC expected ST_CRC_ERR (0x01) with len 0, got st=0x%02h, len=%0d",
                   resp_st, resp_len);
        end
        $display("PASS: bad CRC rejected by board with ST_CRC_ERR (0x01)");

        // 7. Verify IDENTIFY error counter tracks the CRC error
        $display("\n[Step 6] Verifying IDENTIFY reflects incremented CRC error counter...");
        send_packet(CMD_IDENTIFY, 0, empty_pl);
        recv_response(resp_st, resp_len, resp_pl);
        if (resp_st !== ST_OK || resp_len !== 16) begin
            $fatal(1, "Post-error IDENTIFY failed: st=0x%02h, len=%0d", resp_st, resp_len);
        end

        crc_errs = {resp_pl[13], resp_pl[12]};
        framing_errs = {resp_pl[11], resp_pl[10]};
        timeout_cnt = {resp_pl[15], resp_pl[14]};

        if (crc_errs !== 16'd1) begin
            $fatal(1, "Expected crc_errors == 1, got %0d", crc_errs);
        end
        if (framing_errs !== 16'd0 || timeout_cnt !== 16'd0) begin
            $fatal(1, "Unexpected errors: framing=%0d, timeout=%0d", framing_errs, timeout_cnt);
        end
        $display("PASS: IDENTIFY reports crc_errors=1, framing_errors=0, timeout_count=0");

        $display("\n=======================================================");
        $display("  PASS: all hardware gate tests completed successfully");
        $display("=======================================================\n");
        $finish;
    end

endmodule : tb_hardware_gate
