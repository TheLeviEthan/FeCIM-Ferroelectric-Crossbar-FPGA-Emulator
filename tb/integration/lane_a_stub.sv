`timescale 1ns/1ps
// Test-only Lane A seam driver. Not synthesizable and not a replacement for control_fsm.
module lane_a_stub
    import fecim_pkg::*;
(
    input logic clk,
    input logic rst_n,
    output logic bulk_we,
    output bulk_target_e bulk_target,
    output logic [15:0] bulk_addr,
    output logic [7:0] bulk_data,
    output logic [SEQ_AW-1:0] seq_cnt,
    output logic [ROW_AW-1:0] act_addr,
    output logic acc_clear,
    output logic acc_en,
    output logic shift_en,
    output logic drain_we,
    output logic [COL_AW-1:0] drain_addr,
    output noise_en_t noise_en,
    output logic [7:0] quant_levels,
    output logic [15:0] quant_mult,
    output logic [SIGMA_W-1:0] d2d_sigma,
    output logic [SIGMA_W-1:0] read_sigma,
    output logic [31:0] noise_seed,
    output logic [4:0] adc_bits,
    output logic [16:0] stuck_rate,
    output logic reseed,
    output logic [15:0] active_rows,
    output logic [15:0] active_cols,
    output logic clear_results
);
    // Drive at falling edges so Lane B can sample at rising edges.
    task automatic idle_controls;
        @(negedge clk);
        bulk_we = 0;
        acc_clear = 0;
        acc_en = 0;
        shift_en = 0;
        drain_we = 0;
        reseed = 0;
        clear_results = 0;
    endtask

    task automatic write_byte(input bulk_target_e target,
                              input logic [15:0] address,
                              input logic [7:0] value);
        @(negedge clk);
        bulk_target = target;
        bulk_addr = address;
        bulk_data = value;
        bulk_we = 1;
        @(negedge clk);
        bulk_we = 0;
    endtask

    task automatic clear_accumulators;
        @(negedge clk);
        acc_clear = 1;
        @(negedge clk);
        acc_clear = 0;
    endtask

    // Issue each row address, then allow LANE_PIPE_DEPTH extra cycles
    // for the final row to propagate through the MAC pipeline.
    task automatic issue_rows(input int unsigned pass_number,
                              input int unsigned rows);
        for (int unsigned r = 0; r < rows; r++) begin
            @(negedge clk);
            seq_cnt = SEQ_AW'(pass_number * TILE_ROWS + r);
            act_addr = ROW_AW'(r);
            acc_en = 1;
        end
        @(negedge clk);
        acc_en = 0;
        repeat (LANE_PIPE_DEPTH) @(negedge clk);
    endtask

    task automatic drain_columns(input int unsigned pass_number,
                                 input int unsigned count);
        for (int unsigned c = 0; c < count; c++) begin
            @(negedge clk);
            shift_en = 1;
            drain_we = 1;
            drain_addr = COL_AW'(pass_number * NUM_LANES + c);
        end
        @(negedge clk);
        shift_en = 0;
        drain_we = 0;
    endtask

    initial begin
        bulk_we = 0;
        bulk_target = TGT_WEIGHTS;
        bulk_addr = 0;
        bulk_data = 0;
        seq_cnt = 0;
        act_addr = 0;
        acc_clear = 0;
        acc_en = 0;
        shift_en = 0;
        drain_we = 0;
        drain_addr = 0;
        noise_en = NOISE_NONE;
        quant_levels = 8'd255;
        quant_mult = 16'd257;
        d2d_sigma = '0;
        read_sigma = '0;
        noise_seed = 0;
        adc_bits = 5'(ACC_USED_W);
        stuck_rate = 0;
        reseed = 0;
        active_rows = 16'(TILE_ROWS);
        active_cols = 16'(TILE_COLS);
        clear_results = 0;
    end
endmodule
