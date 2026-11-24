`timescale 1ns/1ps

/* verilator lint_off INITIALDLY */

module tb_argmax;
    import fecim_pkg::*;

    logic clk = 0;
    always #10 clk = ~clk; // 50 MHz

    logic rst_n = 0;
    logic clear = 0;
    logic [15:0] active_cols = 16'd64;
    logic in_valid = 0;
    logic [COL_AW-1:0] in_idx = '0;
    logic [ACC_W-1:0] in_val = '0;

    logic [COL_AW-1:0] argmax_idx;
    logic [ACC_W-1:0] argmax_val;
    logic [ACC_W-1:0] argmax_second;

    argmax_unit dut (
        .clk           (clk),
        .rst_n         (rst_n),
        .clear         (clear),
        .active_cols   (active_cols),
        .in_valid      (in_valid),
        .in_idx        (in_idx),
        .in_val        (in_val),
        .argmax_idx    (argmax_idx),
        .argmax_val    (argmax_val),
        .argmax_second (argmax_second)
    );

    // Stream a single column input on negedge clk
    task automatic send_column(input logic [COL_AW-1:0] col, input logic [ACC_W-1:0] val);
        @(negedge clk);
        in_valid = 1;
        in_idx   = col;
        in_val   = val;
        @(negedge clk);
        in_valid = 0;
    endtask

    // Pulse clear on negedge clk
    task automatic do_clear();
        @(negedge clk);
        clear = 1;
        @(negedge clk);
        clear = 0;
        @(negedge clk);
    endtask

    // Check outputs
    task automatic check_result(
        input string name,
        input int exp_idx,
        input int exp_val,
        input int exp_second
    );
        if (int'(argmax_idx) != exp_idx) begin
            $fatal(1, "FAIL [%s]: argmax_idx mismatch: got %0d, expected %0d",
                   name, argmax_idx, exp_idx);
        end
        if (int'(argmax_val) != exp_val) begin
            $fatal(1, "FAIL [%s]: argmax_val mismatch: got %0d, expected %0d",
                   name, int'(argmax_val), exp_val);
        end
        if (int'(argmax_second) != exp_second) begin
            $fatal(1, "FAIL [%s]: argmax_second mismatch: got %0d, expected %0d",
                   name, int'(argmax_second), exp_second);
        end
        $display("  -> [%s] PASS: idx=%0d, max=%0d, second=%0d",
                 name, argmax_idx, int'(argmax_val), int'(argmax_second));
    endtask

    initial begin
        $display("=======================================================");
        $display("  FeCIM Argmax Unit Verification (Task A4)");
        $display("=======================================================");

        rst_n = 0;
        clear = 0;
        active_cols = 16'd64;
        in_valid = 0;
        in_idx = 0;
        in_val = 0;

        repeat (5) @(posedge clk);
        rst_n = 1;
        repeat (5) @(posedge clk);

        //=====================================================================
        // Test 1: Masking (columns >= active_cols ignored, e.g. 64 -> 10 digits)
        //=====================================================================
        $display("\n[Test 1] Masking columns >= active_cols (64 -> 10 digit layer)...");
        active_cols = 16'd10;
        do_clear();

        // Feed full 64-column group (columns 0..63)
        // Columns 0..9 are valid digit scores
        // Columns 10..63 are garbage columns containing large positive values
        for (int c = 0; c < 64; c++) begin
            logic [31:0] val;
            if (c < 10) begin
                case (c)
                    3: val = 32'sd720;  // Runner-up among active columns
                    7: val = 32'sd850;  // True winner among active columns
                    default: val = 32'sd100 + 32'(c * 10);
                endcase
            end else begin
                // Garbage values beyond active_cols (would falsely win if not masked!)
                case (c)
                    10: val = 32'sd999999;
                    35: val = 32'sd2000000000;
                    default: val = 32'sd500000;
                endcase
            end
            send_column(COL_AW'(c), val);
        end

        // Must ignore cols 10..63 and correctly pick col 7 and col 3
        check_result("Active cols mask (10 cols)", 7, 850, 720);

        //=====================================================================
        // Test 2: Deliberately tied values (Lowest index must win)
        //=====================================================================
        $display("\n[Test 2] Deliberately tied values (numpy.argmax tie-breaking)...");

        // Case 2a: Two equal maximums at column 2 and column 8
        active_cols = 16'd10;
        do_clear();
        for (int c = 0; c < 10; c++) begin
            logic [31:0] val;
            case (c)
                2: val = 32'sd5000; // First occurrence of max
                8: val = 32'sd5000; // Tied max
                default: val = 32'sd1000 + 32'(c);
            endcase
            send_column(COL_AW'(c), val);
        end
        // Lowest index 2 wins, runner-up is the tied 5000
        check_result("Tie between col 2 and col 8", 2, 5000, 5000);

        // Case 2b: Three equal maximums at columns 1, 4, 7
        do_clear();
        for (int c = 0; c < 10; c++) begin
            logic [31:0] val;
            case (c)
                1: val = 32'sd9000;
                4: val = 32'sd9000;
                7: val = 32'sd9000;
                default: val = 32'sd500;
            endcase
            send_column(COL_AW'(c), val);
        end
        // Lowest index 1 wins, runner-up is 9000
        check_result("Triple tie at cols 1, 4, 7", 1, 9000, 9000);

        // Case 2c: Tied runner-up (unique max, tied second place)
        do_clear();
        for (int c = 0; c < 10; c++) begin
            logic [31:0] val;
            case (c)
                0: val = 32'sd12000; // Unique max
                3: val = 32'sd6000;  // First second-place
                6: val = 32'sd6000;  // Tied second-place
                default: val = 32'sd2000;
            endcase
            send_column(COL_AW'(c), val);
        end
        check_result("Tied runner-up at cols 3, 6", 0, 12000, 6000);

        // Case 2d: All columns identical
        do_clear();
        for (int c = 0; c < 10; c++) begin
            send_column(COL_AW'(c), 32'sd300);
        end
        check_result("All columns identical", 0, 300, 300);

        //=====================================================================
        // Test 3: Runner-up when max is in Column 0
        //=====================================================================
        $display("\n[Test 3] Runner-up when max is in Column 0...");
        active_cols = 16'd10;
        do_clear();
        for (int c = 0; c < 10; c++) begin
            logic [31:0] val;
            case (c)
                0: val = 32'sd15000; // Max in column 0
                4: val = 32'sd9500;  // True runner-up
                default: val = 32'sd1000 + 32'(c * 100);
            endcase
            send_column(COL_AW'(c), val);
        end
        check_result("Max in column 0", 0, 15000, 9500);

        //=====================================================================
        // Test 4: Runner-up when max is in the Last Active Column
        //=====================================================================
        $display("\n[Test 4] Runner-up when max is in the last active column...");
        active_cols = 16'd10; // Last active column is 9
        do_clear();
        for (int c = 0; c < 10; c++) begin
            logic [31:0] val;
            case (c)
                3: val = 32'sd8000;  // Leader until col 9
                6: val = 32'sd6000;  // Third place
                9: val = 32'sd18000; // Max in last active column
                default: val = 32'sd1000 + 32'(c * 50);
            endcase
            send_column(COL_AW'(c), val);
        end
        check_result("Max in last active col 9", 9, 18000, 8000);

        //=====================================================================
        // Test 5: All-negative values (signed arithmetic)
        //=====================================================================
        $display("\n[Test 5] All-negative values (signed comparison)...");
        active_cols = 16'd5;
        do_clear();
        send_column(COL_AW'(0), -32'sd5000);
        send_column(COL_AW'(1), -32'sd100);   // Max
        send_column(COL_AW'(2), -32'sd2500);
        send_column(COL_AW'(3), -32'sd400);   // Runner-up
        send_column(COL_AW'(4), -32'sd8000);
        check_result("All negative numbers", 1, -100, -400);

        // All negative with max in column 0
        do_clear();
        send_column(COL_AW'(0), -32'sd50);    // Max
        send_column(COL_AW'(1), -32'sd200);   // Runner-up
        send_column(COL_AW'(2), -32'sd600);
        check_result("All negative with max in col 0", 0, -50, -200);

        //=====================================================================
        // Test 6: active_cols == 1
        //=====================================================================
        $display("\n[Test 6] active_cols == 1 (second_val undefined)...");
        active_cols = 16'd1;
        do_clear();
        send_column(COL_AW'(0), 32'sd4242);
        // Col 1..3 garbage
        send_column(COL_AW'(1), 32'sd99999);
        send_column(COL_AW'(2), 32'sd88888);
        if (int'(argmax_idx) != 0 || int'(argmax_val) != 4242) begin
            $fatal(1, "FAIL: active_cols == 1 mismatch: got (idx=%0d, val=%0d)",
                   argmax_idx, int'(argmax_val));
        end
        $display("  -> [active_cols == 1] PASS: idx=0, max=4242 (second_val undefined)");

        //=====================================================================
        // Test 7: Randomized vectors against golden NumPy reference model
        //=====================================================================
        $display("\n[Test 7] Randomized test vectors against golden reference...");
        for (int rep = 0; rep < 50; rep++) begin
            int test_cols;
            int gold_max_idx;
            int gold_max_val;
            int gold_second_val;
            logic [31:0] test_vec [0:127];

            // Random active_cols between 2 and 64
            test_cols = 2 + ($urandom % 63);
            active_cols = 16'(test_cols);
            do_clear();

            // Populate active columns with signed random values
            for (int i = 0; i < test_cols; i++) begin
                // Mix of positive and negative signed values
                test_vec[i] = $urandom - 32'sd1000000000;
            end

            // Compute golden winner and runner-up matching numpy.argmax (strict >)
            gold_max_val = int'(test_vec[0]);
            gold_max_idx = 0;
            gold_second_val = -2147483648;

            for (int i = 1; i < test_cols; i++) begin
                int cur_v;
                cur_v = int'(test_vec[i]);
                if (cur_v > gold_max_val) begin
                    gold_second_val = gold_max_val;
                    gold_max_val = cur_v;
                    gold_max_idx = i;
                end else if (cur_v > gold_second_val) begin
                    gold_second_val = cur_v;
                end
            end

            // If rep is a tie-injection test, force duplicate max
            if (rep % 5 == 0 && test_cols > 2) begin
                int tie_idx;
                tie_idx = 1 + ($urandom % (test_cols - 1));
                if (tie_idx == gold_max_idx) tie_idx = (tie_idx + 1) % test_cols;
                test_vec[tie_idx] = test_vec[gold_max_idx];

                // Re-evaluate golden with tie
                gold_max_val = int'(test_vec[0]);
                gold_max_idx = 0;
                gold_second_val = -2147483648;
                for (int i = 1; i < test_cols; i++) begin
                    int cur_v;
                    cur_v = int'(test_vec[i]);
                    if (cur_v > gold_max_val) begin
                        gold_second_val = gold_max_val;
                        gold_max_val = cur_v;
                        gold_max_idx = i;
                    end else if (cur_v > gold_second_val) begin
                        gold_second_val = cur_v;
                    end
                end
            end

            // Stream all columns including up to 64 (some garbage beyond active_cols)
            for (int i = 0; i < 64; i++) begin
                logic [31:0] v;
                if (i < test_cols) v = test_vec[i];
                else v = 32'sd2147483000; // Garbage that must be masked
                send_column(COL_AW'(i), v);
            end

            if (int'(argmax_idx) != gold_max_idx ||
                int'(argmax_val) != gold_max_val ||
                int'(argmax_second) != gold_second_val) begin
                $fatal(1, "FAIL [Random rep %0d]: cols=%0d, got (idx=%0d, val=%0d, sec=%0d), expected (idx=%0d, val=%0d, sec=%0d)",
                       rep, test_cols, argmax_idx, int'(argmax_val), int'(argmax_second),
                       gold_max_idx, gold_max_val, gold_second_val);
            end
        end
        $display("  -> 50 randomized trials (with masking & injected ties) matched golden model [OK]");

        //=====================================================================
        // Test 8: Clear Strobe
        //=====================================================================
        $display("\n[Test 8] Clear strobe resets state...");
        do_clear();
        if (argmax_idx != 0 || argmax_val != 32'sh8000_0000 || argmax_second != 32'sh8000_0000) begin
            $fatal(1, "FAIL: Clear strobe did not reset outputs");
        end
        $display("  -> Clear strobe reset confirmed [OK]");

        $display("\n=======================================================");
        $display("PASS: all argmax_unit tests completed successfully");
        $display("=======================================================");
        $finish;
    end

endmodule : tb_argmax
