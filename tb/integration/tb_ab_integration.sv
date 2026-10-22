`timescale 1ns/1ps
module tb_ab_integration;
    import fecim_pkg::*;

    logic clk = 0;
    logic rst_n = 0;
    always #10 clk = ~clk; // 50 MHz

    logic bulk_we;
    bulk_target_e bulk_target;
    logic [15:0] bulk_addr;
    logic [7:0] bulk_data;
    logic [SEQ_AW-1:0] seq_cnt;
    logic [ROW_AW-1:0] act_addr;
    logic acc_clear, acc_en, shift_en, drain_we;
    logic [COL_AW-1:0] drain_addr;
    noise_en_t noise_en;
    logic [7:0] quant_levels;
    logic [15:0] quant_mult;
    logic [SIGMA_W-1:0] d2d_sigma, read_sigma;
    logic [31:0] noise_seed;
    logic [4:0] adc_bits;
    logic [16:0] stuck_rate;
    logic reseed;
    logic [15:0] active_rows, active_cols;
    logic clear_results;

    lane_a_stub a (
        .clk(clk), .rst_n(rst_n),
        .bulk_we(bulk_we), .bulk_target(bulk_target),
        .bulk_addr(bulk_addr), .bulk_data(bulk_data),
        .seq_cnt(seq_cnt), .act_addr(act_addr),
        .acc_clear(acc_clear), .acc_en(acc_en),
        .shift_en(shift_en), .drain_we(drain_we),
        .drain_addr(drain_addr), .noise_en(noise_en),
        .quant_levels(quant_levels), .quant_mult(quant_mult),
        .d2d_sigma(d2d_sigma), .read_sigma(read_sigma),
        .noise_seed(noise_seed), .adc_bits(adc_bits),
        .stuck_rate(stuck_rate), .reseed(reseed),
        .active_rows(active_rows), .active_cols(active_cols),
        .clear_results(clear_results)
    );

    // A-side-only seam smoke test. Lane B DUT and result checks are TODO.
    int writes = 0;
    int clears = 0;
    int issues = 0;
    int drains = 0;
    int expected_row = 0;
    int expected_drain = 0;
    bit checking_rows = 0;
    bit checking_drains = 0;

    always @(posedge clk) if (rst_n) begin
        if (bulk_we) begin
            writes++;
            if (writes == 1 && (bulk_target != TGT_WEIGHTS || bulk_addr != 16'd0 || bulk_data != 8'hA5))
                $fatal(1, "First weight write incorrect");
            if (writes == 2 && (bulk_target != TGT_ACT || bulk_addr != 16'd0 || bulk_data != 8'h03))
                $fatal(1, "Activation write incorrect");
        end
        if (acc_clear) clears++;
        if (acc_en) begin
            issues++;
            if (checking_rows) begin
                if (seq_cnt !== SEQ_AW'(expected_row) || act_addr !== ROW_AW'(expected_row))
                    $fatal(1, "Row %0d: seq=%0d act=%0d", expected_row, seq_cnt, act_addr);
                expected_row++;
            end
        end
        if (drain_we) begin
            drains++;
            if (!shift_en) $fatal(1, "drain_we without shift_en");
            if (checking_drains) begin
                if (drain_addr !== COL_AW'(expected_drain))
                    $fatal(1, "Drain %0d: addr=%0d", expected_drain, drain_addr);
                expected_drain++;
            end
        end
    end

    initial begin
        repeat (5) @(negedge clk);
        rst_n = 1;
        if (noise_en !== NOISE_NONE || quant_levels !== 8'd255 ||
            quant_mult !== 16'd257 || adc_bits !== 5'(ACC_USED_W))
            $fatal(1, "A-side config defaults incorrect");

        a.write_byte(TGT_WEIGHTS, 16'd0, 8'hA5);
        a.write_byte(TGT_ACT, 16'd0, 8'h03);
        a.clear_accumulators();

        checking_rows = 1;
        a.issue_rows(0, TILE_ROWS);
        checking_rows = 0;

        checking_drains = 1;
        a.drain_columns(0, NUM_LANES);
        checking_drains = 0;
        repeat (2) @(negedge clk);

        if (writes != 2 || clears != 1 || issues != TILE_ROWS ||
            drains != NUM_LANES || expected_row != TILE_ROWS ||
            expected_drain != NUM_LANES)
            $fatal(1, "Counts wrong: writes=%0d clear=%0d rows=%0d drains=%0d", writes, clears, issues, drains);
        $display("PASS: Lane A stub drives frozen A/B signals (2 writes, %0d rows, %0d drains)", issues, drains);
        $display("NOTE: Lane B is not instantiated; this is an A-side smoke test only.");
        $finish;
    end
    initial begin
        #1000000;
        $fatal(1, "TIMEOUT");
    end
endmodule
