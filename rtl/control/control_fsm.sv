`timescale 1ns/1ps
//=============================================================================
// control_fsm.sv -- Lane A
//
// MVM sequencer: IDLE -> CLEAR -> ACC -> DRAIN -> DONE, one counter drives the array.
// Spec: control-fsm-spec.md §3-4, rtl-conventions §5
//=============================================================================

module control_fsm
    import fecim_pkg::*;
(
    input  logic                clk,
    input  logic                rst_n,
    input  logic                soft_reset,

    input  logic                start_compute,   // from packet_parser
    input  logic                manual_compute,  // KEY1, synchronized pulse
    input  logic [15:0]         active_rows,
    input  logic [15:0]         active_cols,

    // A -> B seam
    output logic [SEQ_AW-1:0]   seq_cnt,
    output logic [ROW_AW-1:0]   act_addr,
    output logic                acc_clear,
    output logic                acc_en,
    output logic                shift_en,

    // result buffer write (drain)
    output logic                drain_we,
    output logic [COL_AW-1:0]   drain_addr,

    // status
    output ctrl_state_e         state,
    output logic                compute_busy,
    output logic                compute_done,    // 1-cycle pulse in DONE
    output logic                run_start,       // 1-cycle pulse on IDLE -> CLEAR
    output logic [31:0]         cycle_cnt
);

    // Decode active rows and columns
    logic [ROW_AW:0] num_rows;
    assign num_rows = (active_rows == 16'd0 || active_rows > 16'(TILE_ROWS)) ?
                      (ROW_AW+1)'(TILE_ROWS) : active_rows[ROW_AW:0];

    logic [1:0] total_passes;
    assign total_passes = (active_cols == 16'd0 || active_cols > 16'(NUM_LANES)) ?
                          2'd2 : 2'd1;

    // Internal state and counters
    ctrl_state_e        state_q;
    logic               pass_idx_q;
    logic [ROW_AW:0]    row_cnt_q;
    logic [1:0]         pipe_cnt_q;
    logic [LANE_AW-1:0] drain_cnt_q;
    logic [31:0]        run_cycle_cnt_q;

    assign state    = state_q;
    assign act_addr = seq_cnt[ROW_AW-1:0];

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state_q         <= CTRL_IDLE;
            pass_idx_q      <= 1'b0;
            row_cnt_q       <= '0;
            pipe_cnt_q      <= '0;
            drain_cnt_q     <= '0;
            run_cycle_cnt_q <= '0;

            seq_cnt         <= '0;
            acc_clear       <= 1'b0;
            acc_en          <= 1'b0;
            shift_en        <= 1'b0;
            drain_we        <= 1'b0;
            drain_addr      <= '0;

            compute_busy    <= 1'b0;
            compute_done    <= 1'b0;
            run_start       <= 1'b0;
            cycle_cnt       <= '0;
        end else if (soft_reset) begin
            state_q         <= CTRL_IDLE;
            pass_idx_q      <= 1'b0;
            row_cnt_q       <= '0;
            pipe_cnt_q      <= '0;
            drain_cnt_q     <= '0;
            run_cycle_cnt_q <= '0;

            seq_cnt         <= '0;
            acc_clear       <= 1'b0;
            acc_en          <= 1'b0;
            shift_en        <= 1'b0;
            drain_we        <= 1'b0;
            drain_addr      <= '0;

            compute_busy    <= 1'b0;
            compute_done    <= 1'b0;
            run_start       <= 1'b0;
            cycle_cnt       <= '0;
        end else begin
            // Default single-cycle pulses
            run_start    <= 1'b0;
            compute_done <= 1'b0;

            case (state_q)
                CTRL_IDLE: begin
                    acc_clear    <= 1'b0;
                    acc_en       <= 1'b0;
                    shift_en     <= 1'b0;
                    drain_we     <= 1'b0;
                    compute_busy <= 1'b0;

                    if (start_compute || manual_compute) begin
                        state_q         <= CTRL_CLEAR;
                        pass_idx_q      <= 1'b0;
                        run_start       <= 1'b1;
                        compute_busy    <= 1'b1;
                        acc_clear       <= 1'b1;
                        run_cycle_cnt_q <= 32'd0;
                    end
                end

                CTRL_CLEAR: begin
                    acc_clear       <= 1'b0;
                    run_cycle_cnt_q <= run_cycle_cnt_q + 32'd1;

                    state_q         <= CTRL_ACC;
                    acc_en          <= 1'b1;
                    row_cnt_q       <= '0;
                    pipe_cnt_q      <= '0;
                    seq_cnt         <= {pass_idx_q, {ROW_AW{1'b0}}};
                end

                CTRL_ACC: begin
                    run_cycle_cnt_q <= run_cycle_cnt_q + 32'd1;

                    if (row_cnt_q < num_rows - 1'b1) begin
                        row_cnt_q <= row_cnt_q + 1'b1;
                        seq_cnt   <= seq_cnt + 1'b1;
                        acc_en    <= 1'b1;
                    end else if (row_cnt_q == num_rows - 1'b1) begin
                        // Last row issued: deassert acc_en and enter pipeline hold
                        row_cnt_q  <= row_cnt_q + 1'b1;
                        acc_en     <= 1'b0;
                        pipe_cnt_q <= 2'd0;
                    end else begin
                        // Pipeline drain hold: LANE_PIPE_DEPTH (3) cycles
                        acc_en <= 1'b0;
                        if (pipe_cnt_q < 2'(LANE_PIPE_DEPTH - 1)) begin
                            pipe_cnt_q <= pipe_cnt_q + 1'b1;
                        end else begin
                            state_q     <= CTRL_DRAIN;
                            shift_en    <= 1'b1;
                            drain_we    <= 1'b1;
                            drain_cnt_q <= '0;
                            drain_addr  <= {pass_idx_q, {LANE_AW{1'b0}}};
                        end
                    end
                end

                CTRL_DRAIN: begin
                    run_cycle_cnt_q <= run_cycle_cnt_q + 32'd1;
                    shift_en        <= 1'b1;
                    drain_we        <= 1'b1;

                    if (drain_cnt_q < LANE_AW'(NUM_LANES - 1)) begin
                        drain_cnt_q <= drain_cnt_q + 1'b1;
                        drain_addr  <= {pass_idx_q, drain_cnt_q + 1'b1};
                    end else begin
                        shift_en <= 1'b0;
                        drain_we <= 1'b0;

                        if ({1'b0, pass_idx_q} < total_passes - 2'd1) begin
                            pass_idx_q <= pass_idx_q + 1'b1;
                            state_q    <= CTRL_CLEAR;
                            acc_clear  <= 1'b1;
                        end else begin
                            state_q      <= CTRL_DONE;
                            compute_done <= 1'b1;
                            compute_busy <= 1'b0;
                            cycle_cnt    <= run_cycle_cnt_q + 32'd1;
                        end
                    end
                end

                CTRL_DONE: begin
                    compute_busy <= 1'b0;
                    state_q      <= CTRL_IDLE;
                end

                default: state_q <= CTRL_IDLE;
            endcase
        end
    end

endmodule : control_fsm
