//=============================================================================
// tb_mac_lane.sv -- self-checking directed test, lane 0, clean datapath.
// Stimulus on negedge; DUT samples on posedge.
//=============================================================================
`timescale 1ns/1ps
module tb_mac_lane;
    import fecim_pkg::*;

    logic clk = 1'b0;
    logic rst_n = 1'b0;
    always #10 clk = ~clk;                          // 50 MHz

    logic                       wt_we, acc_clear, acc_en, shift_en;
    logic [ROW_AW+COL_AW-1:0]   wt_addr;
    logic signed [WEIGHT_W-1:0] wt_data;
    logic [SEQ_AW-1:0]          w_addr;
    logic [ACT_W-1:0]           act_bcast;
    logic signed [ACC_W-1:0]    shift_in, acc_out;

    mac_lane #(.LANE_ID(0)) dut (
        .clk, .rst_n, .wt_we, .wt_addr, .wt_data, .w_addr, .act_bcast,
        .noise_en(NOISE_NONE), .acc_clear, .acc_en, .shift_en, .shift_in,
        .acc_out
    );

    // Reference data: lane 0 owns column 0 (pass 0) and column NUM_LANES (pass 1)
    int         w_ref   [PASSES][TILE_ROWS];
    logic [7:0] act_vec [TILE_ROWS];
    int         errors = 0;

    // Activation buffer stand-in: M9K read + broadcast register = valid at N+2
    logic [ROW_AW-1:0] row_d1;
    always_ff @(posedge clk) begin
        row_d1    <= w_addr[ROW_AW-1:0];
        act_bcast <= act_vec[row_d1];
    end

    task automatic write_w(int col, int row, int val);
        wt_we = 1; wt_addr = col*TILE_ROWS + row; wt_data = val;
        @(negedge clk);
        wt_we = 0;
    endtask

    task automatic load_weights();
        for (int p = 0; p < PASSES; p++)
            for (int r = 0; r < TILE_ROWS; r++)
                write_w(p*NUM_LANES, r, w_ref[p][r]);
    endtask

    // gap_at >= 0 inserts idle cycles (acc_en low) before that row
    task automatic run_pass(int p, int gap_at = -1);
        acc_clear = 1; @(negedge clk); acc_clear = 0;
        for (int r = 0; r < TILE_ROWS; r++) begin
            if (r == gap_at) begin acc_en = 0; repeat (5) @(negedge clk); end
            w_addr = SEQ_AW'(p*TILE_ROWS + r);
            acc_en = 1;
            @(negedge clk);
        end
        acc_en = 0;
        repeat (LANE_PIPE_DEPTH) @(negedge clk);    // exact depth: catches off-by-one
    endtask

    function automatic longint expected(int p);
        longint s = 0;
        for (int r = 0; r < TILE_ROWS; r++)
            s += longint'(w_ref[p][r]) * longint'(int'(act_vec[r]));  // act is unsigned
        return s;
    endfunction

    task automatic run_and_check(string name, int gap_at = -1);
        for (int p = 0; p < PASSES; p++) begin
            run_pass(p, gap_at);
            if (longint'(acc_out) !== expected(p)) begin
                $error("%s pass %0d: got %0d, expected %0d",
                       name, p, acc_out, expected(p));
                errors++;
            end
        end
    endtask

    task automatic fill(int w0, int w1, int a);
        for (int r = 0; r < TILE_ROWS; r++) begin
            w_ref[0][r] = w0; w_ref[1][r] = w1; act_vec[r] = 8'(a);
        end
    endtask

    initial begin
        wt_we = 0; acc_clear = 0; acc_en = 0; shift_en = 0;
        wt_addr = '0; wt_data = '0; w_addr = '0; shift_in = '0;
        foreach (act_vec[r]) act_vec[r] = '0;
        repeat (3) @(negedge clk); rst_n = 1; @(negedge clk);

        // 1-2. Accumulator headroom: +127 x 255 x 128 = 4,145,280; -128 -> -4,177,920
        fill(127, -128, 255); load_weights(); run_and_check("extremes");

        // 3-4. One-hot at first and last row (pipeline inclusion)
       for (int t = 0; t < 2; t++) begin
            automatic int k = (t == 0) ? 0 : TILE_ROWS-1;
            fill(100, -77, 0); act_vec[k] = 8'd255;
            load_weights(); run_and_check($sformatf("one-hot row %0d", k));
        end

        // 5. Random, both passes (also checks pass addressing)
        for (int t = 0; t < 200; t++) begin
            for (int r = 0; r < TILE_ROWS; r++) begin
                w_ref[0][r] = int'($urandom_range(255)) - 128;
                w_ref[1][r] = int'($urandom_range(255)) - 128;
                act_vec[r]  = 8'($urandom_range(255));
            end
            load_weights(); run_and_check($sformatf("random %0d", t));
        end

        // 6. Idle gap mid-stream: acc must hold while acc_en is low
        run_and_check("gap", 64);

        // 7. Writes to lane 1's columns must not touch lane 0
        for (int r = 0; r < TILE_ROWS; r++) begin
            write_w(1, r, 55); write_w(NUM_LANES+1, r, 55);
        end
        run_and_check("lane select");

        // 8. Hold, then shift load, then clear
        repeat (10) @(negedge clk);
        if (longint'(acc_out) !== expected(PASSES-1)) begin $error("hold"); errors++; end
        shift_in = 32'sd12345; shift_en = 1; @(negedge clk); shift_en = 0;
        if (acc_out !== 32'sd12345) begin $error("shift"); errors++; end
        acc_clear = 1; @(negedge clk); acc_clear = 0;
        if (acc_out !== '0) begin $error("clear"); errors++; end

        if (errors == 0) $display("PASS tb_mac_lane");
        else             $display("FAIL tb_mac_lane: %0d errors", errors);
        $finish;
    end
endmodule : tb_mac_lane
