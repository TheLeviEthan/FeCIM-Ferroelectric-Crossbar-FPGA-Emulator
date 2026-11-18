`timescale 1ns/1ps
//=============================================================================
// tb_control_fsm.sv -- Testbench for control_fsm
//
// Tests:
//   1. Reset defaults
//   2. 8x8 tile (76 cycles)
//   3. 48x48 tile (116 cycles)
//   4. 64x64 tile (132 cycles)
//   5. 128x128 full tile (392 cycles, 2 passes)
//   6. Back-to-back runs (zero idle cycles between runs)
//   7. manual_compute (KEY1) trigger
//   8. Busy rejection (start_compute / manual_compute while busy)
//   9. Soft reset recovery
//=============================================================================

module tb_control_fsm;
    import fecim_pkg::*;

    logic                clk = 0;
    always #10 clk = ~clk; // 50 MHz (20 ns clock period)

    logic                rst_n = 0;
    logic                soft_reset = 0;
    logic                start_compute = 0;
    logic                manual_compute = 0;
    logic [15:0]         active_rows = 16'd128;
    logic [15:0]         active_cols = 16'd128;

    logic [SEQ_AW-1:0]   seq_cnt;
    logic [ROW_AW-1:0]   act_addr;
    logic                acc_clear;
    logic                acc_en;
    logic                shift_en;
    logic                drain_we;
    logic [COL_AW-1:0]   drain_addr;
    ctrl_state_e         state;
    logic                compute_busy;
    logic                compute_done;
    logic                run_start;
    logic [31:0]         cycle_cnt;

    control_fsm dut (.*);

    // Monitoring counters
    int acc_clear_count = 0;
    int acc_en_count = 0;
    int drain_we_count = 0;
    int shift_en_count = 0;
    int run_start_pulses = 0;
    int compute_done_pulses = 0;
    int expected_drain_addr = 0;
    int total_busy_cycles = 0;

    // Hold pipe check
    int pipe_hold_cycles = 0;
    bit pipe_hold_checked = 0;

    always @(posedge clk) begin
        if (rst_n && !soft_reset) begin
            if (run_start) run_start_pulses++;
            if (compute_done) compute_done_pulses++;
            if (acc_clear) acc_clear_count++;

            if (acc_en) begin
                acc_en_count++;
                pipe_hold_cycles  <= 0;
                pipe_hold_checked <= 0;
                if (act_addr !== seq_cnt[ROW_AW-1:0])
                    $fatal(1, "FAIL: act_addr (%0d) does not match seq_cnt wire slice (%0d)",
                           act_addr, seq_cnt[ROW_AW-1:0]);
            end else if (state == CTRL_ACC) begin
                pipe_hold_cycles <= pipe_hold_cycles + 1;
            end

            if (drain_we && !pipe_hold_checked) begin
                pipe_hold_checked <= 1'b1;
                if (pipe_hold_cycles != LANE_PIPE_DEPTH)
                    $fatal(1, "FAIL: Pipe hold duration was %0d cycles (expected %0d)",
                           pipe_hold_cycles, LANE_PIPE_DEPTH);
            end

            if (drain_we) begin
                drain_we_count++;
                if (!shift_en)
                    $fatal(1, "FAIL: drain_we asserted without shift_en");
                if (drain_addr !== COL_AW'(expected_drain_addr))
                    $fatal(1, "FAIL: drain_addr=%0d expected=%0d", drain_addr, expected_drain_addr);
                expected_drain_addr++;
            end

            if (shift_en) shift_en_count++;
            if (compute_busy) total_busy_cycles++;
        end else begin
            pipe_hold_cycles <= 0;
            pipe_hold_checked <= 0;
        end
    end

    // Task to run an MVM and verify timing & sequence
    task automatic run_and_verify(
        input logic [15:0] rows,
        input logic [15:0] cols,
        input bit           use_manual,
        input int           expected_cycles
    );
        int expected_passes;
        expected_passes = (cols == 0 || cols > 16'(NUM_LANES)) ? 2 : 1;

        active_rows = rows;
        active_cols = cols;

        // Reset tracking metrics
        acc_clear_count     = 0;
        acc_en_count        = 0;
        drain_we_count      = 0;
        shift_en_count      = 0;
        run_start_pulses    = 0;
        compute_done_pulses = 0;
        expected_drain_addr = 0;
        total_busy_cycles   = 0;

        @(negedge clk);
        if (use_manual)
            manual_compute = 1'b1;
        else
            start_compute = 1'b1;

        @(negedge clk);
        manual_compute = 1'b0;
        start_compute = 1'b0;

        // Wait until computation completes and FSM returns to IDLE
        while (compute_busy || state != CTRL_IDLE) begin
            @(posedge clk);
            #1;
        end

        // Assertions
        if (run_start_pulses != 1)
            $fatal(1, "FAIL: run_start pulsed %0d times (expected 1)", run_start_pulses);

        if (compute_done_pulses != 1)
            $fatal(1, "FAIL: compute_done pulsed %0d times (expected 1)", compute_done_pulses);

        if (acc_clear_count != expected_passes)
            $fatal(1, "FAIL: acc_clear count = %0d (expected %0d)", acc_clear_count, expected_passes);

        if (acc_en_count != expected_passes * int'(rows))
            $fatal(1, "FAIL: acc_en cycles = %0d (expected %0d)", acc_en_count, expected_passes * int'(rows));

        if (drain_we_count != expected_passes * NUM_LANES)
            $fatal(1, "FAIL: drain_we count = %0d (expected %0d)", drain_we_count, expected_passes * NUM_LANES);

        if (shift_en_count != expected_passes * NUM_LANES)
            $fatal(1, "FAIL: shift_en count = %0d (expected %0d)", shift_en_count, expected_passes * NUM_LANES);

        if (cycle_cnt != 32'(expected_cycles))
            $fatal(1, "FAIL: cycle_cnt = %0d (expected %0d)", cycle_cnt, expected_cycles);

        if (total_busy_cycles != expected_cycles)
            $fatal(1, "FAIL: total_busy_cycles = %0d (expected %0d)", total_busy_cycles, expected_cycles);
    endtask

    initial begin
        // Apply reset
        rst_n = 0;
        soft_reset = 0;
        start_compute = 0;
        manual_compute = 0;
        active_rows = 16'd128;
        active_cols = 16'd128;
        repeat (5) @(posedge clk);
        rst_n = 1;
        @(posedge clk);

        //---------------------------------------------------------------------
        // Test 1: Reset defaults
        //---------------------------------------------------------------------
        if (state !== CTRL_IDLE || compute_busy !== 1'b0 || compute_done !== 1'b0 ||
            run_start !== 1'b0 || acc_clear !== 1'b0 || acc_en !== 1'b0 ||
            shift_en !== 1'b0 || drain_we !== 1'b0 || cycle_cnt !== 32'd0)
            $fatal(1, "FAIL: Reset state incorrect");
        $display("PASS: Reset defaults verified");

        //---------------------------------------------------------------------
        // Test 2: 8x8 tile (76 cycles)
        // 1 pass * (1 clear + 8 acc + 3 hold + 64 drain) = 76 cycles
        //---------------------------------------------------------------------
        run_and_verify(16'd8, 16'd8, 1'b0, 76);
        $display("PASS: 8x8 tile (76 cycles, 1 pass)");

        //---------------------------------------------------------------------
        // Test 3: 48x48 tile (116 cycles)
        // 1 pass * (1 clear + 48 acc + 3 hold + 64 drain) = 116 cycles
        //---------------------------------------------------------------------
        run_and_verify(16'd48, 16'd48, 1'b0, 116);
        $display("PASS: 48x48 tile (116 cycles, 1 pass)");

        //---------------------------------------------------------------------
        // Test 4: 64x64 tile (132 cycles)
        // 1 pass * (1 clear + 64 acc + 3 hold + 64 drain) = 132 cycles
        //---------------------------------------------------------------------
        run_and_verify(16'd64, 16'd64, 1'b0, 132);
        $display("PASS: 64x64 tile (132 cycles, 1 pass)");

        //---------------------------------------------------------------------
        // Test 5: 128x128 full tile (392 cycles)
        // 2 passes * (1 clear + 128 acc + 3 hold + 64 drain) = 392 cycles
        //---------------------------------------------------------------------
        run_and_verify(16'd128, 16'd128, 1'b0, 392);
        $display("PASS: 128x128 tile (392 cycles, 2 passes)");

        //---------------------------------------------------------------------
        // Test 6: Back-to-back runs
        // Run 8x8, then immediately trigger 64x64 on the exact cycle it returns to IDLE
        //---------------------------------------------------------------------
        run_and_verify(16'd8, 16'd8, 1'b0, 76);
        // We are now in IDLE on posedge clk. Immediately start next run:
        active_rows = 16'd64;
        active_cols = 16'd64;
        acc_clear_count = 0;
        acc_en_count = 0;
        drain_we_count = 0;
        shift_en_count = 0;
        run_start_pulses = 0;
        compute_done_pulses = 0;
        expected_drain_addr = 0;
        total_busy_cycles = 0;

        @(negedge clk);
        start_compute = 1'b1;
        @(negedge clk);
        start_compute = 1'b0;

        while (compute_busy || state != CTRL_IDLE) begin
            @(posedge clk);
            #1;
        end

        if (cycle_cnt != 32'd132 || acc_en_count != 64 || drain_we_count != 64)
            $fatal(1, "FAIL: Back-to-back 64x64 run failed");
        $display("PASS: Back-to-back runs");

        //---------------------------------------------------------------------
        // Test 7: manual_compute (KEY1) trigger
        //---------------------------------------------------------------------
        run_and_verify(16'd8, 16'd8, 1'b1, 76);
        $display("PASS: manual_compute trigger (KEY1)");

        //---------------------------------------------------------------------
        // Test 8: Busy rejection
        // Start 128x128 run, attempt start_compute & manual_compute while busy
        //---------------------------------------------------------------------
        active_rows = 16'd128;
        active_cols = 16'd128;
        acc_clear_count = 0;
        acc_en_count = 0;
        drain_we_count = 0;
        shift_en_count = 0;
        run_start_pulses = 0;
        compute_done_pulses = 0;
        expected_drain_addr = 0;
        total_busy_cycles = 0;

        @(negedge clk);
        start_compute = 1'b1;
        @(negedge clk);
        start_compute = 1'b0;

        // Wait until in CTRL_ACC
        while (state != CTRL_ACC) @(posedge clk);
        repeat (10) @(posedge clk);

        // Now attempt starts while busy
        @(negedge clk);
        start_compute = 1'b1;
        manual_compute = 1'b1;
        @(negedge clk);
        start_compute = 1'b0;
        manual_compute = 1'b0;

        // Ensure run completes properly
        while (compute_busy || state != CTRL_IDLE) begin
            @(posedge clk);
            #1;
        end

        if (run_start_pulses != 1 || compute_done_pulses != 1 || cycle_cnt != 32'd392)
            $fatal(1, "FAIL: Busy rejection failed or corrupted computation");
        $display("PASS: Busy rejection confirmed (in-flight run unaffected)");

        //---------------------------------------------------------------------
        // Test 9: Soft reset recovery
        //---------------------------------------------------------------------
        @(negedge clk);
        start_compute = 1'b1;
        @(negedge clk);
        start_compute = 1'b0;

        while (state != CTRL_ACC) @(posedge clk);
        repeat (5) @(posedge clk);

        @(negedge clk);
        soft_reset = 1'b1;
        @(negedge clk);
        soft_reset = 1'b0;
        @(posedge clk);

        if (state !== CTRL_IDLE || compute_busy !== 1'b0 || acc_en !== 1'b0)
            $fatal(1, "FAIL: Soft reset did not return FSM to IDLE cleanly");

        // Verify next run succeeds after soft reset
        run_and_verify(16'd8, 16'd8, 1'b0, 76);
        $display("PASS: Soft reset recovery confirmed");

        $display("PASS: all control_fsm tests completed successfully");
        $finish;
    end

endmodule : tb_control_fsm
