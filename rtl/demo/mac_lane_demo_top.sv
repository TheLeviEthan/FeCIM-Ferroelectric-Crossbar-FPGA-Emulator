//=============================================================================
// mac_lane_demo_top.sv -- standalone DE10-Lite bring-up for one mac_lane.
// KEY0 = reset, KEY1 = run, SW[1:0] = pattern. Result (low 24 bits) on HEX5..0,
// LEDR[0] = match, LEDR[1] = mismatch, LEDR[9] = busy.
// Board demo only; not part of fecim_top.
//=============================================================================
module mac_lane_demo_top
    import fecim_pkg::*;
(
    input  logic       MAX10_CLK1_50,
    input  logic [1:0] KEY,             // active low, hardware-debounced
    input  logic [9:0] SW,
    output logic [9:0] LEDR,
    output logic [7:0] HEX0, HEX1, HEX2, HEX3, HEX4, HEX5   // active low
);
    logic clk;
    assign clk = MAX10_CLK1_50;

    //--- Reset: async assert, sync deassert ---------------------------------
    logic [1:0] rst_sync;
    logic       rst_n;
    always_ff @(posedge clk or negedge KEY[0])
        if (!KEY[0]) rst_sync <= '0;
        else         rst_sync <= {rst_sync[0], 1'b1};
    assign rst_n = rst_sync[1];

    //--- KEY1 press detect and SW sync (debounced on board, still async) ----
    logic [2:0] k1_sync;
    logic [1:0] sw_s1, sw_s2;
    logic       start;
    always_ff @(posedge clk or negedge rst_n)
        if (!rst_n) begin k1_sync <= '1; sw_s1 <= '0; sw_s2 <= '0; end
        else begin
            k1_sync <= {k1_sync[1:0], KEY[1]};
            sw_s1   <= SW[1:0];
            sw_s2   <= sw_s1;
        end
    assign start = k1_sync[2] & ~k1_sync[1];     // falling edge = press

    //--- Test patterns ------------------------------------------------------
    function automatic logic signed [WEIGHT_W-1:0] w_of(logic [1:0] pat);
        unique case (pat)
            2'd0:    return  8'sd127;
            2'd1:    return -8'sd128;
            default: return  8'sd100;
        endcase
    endfunction

    function automatic logic [ACT_W-1:0] a_of(logic [1:0] pat, logic [ROW_AW-1:0] r);
        unique case (pat)
            2'd0, 2'd1: return 8'd255;
            2'd2:       return (r == '0)             ? 8'd255 : 8'd0;
            default:    return (r == ROW_AW'(TILE_ROWS-1)) ? 8'd255 : 8'd0;
        endcase
    endfunction

    function automatic logic signed [ACC_W-1:0] exp_of(logic [1:0] pat);
        unique case (pat)
            2'd0:    return  32'sd4145280;     // 127 * 255 * 128
            2'd1:    return -32'sd4177920;     // -128 * 255 * 128
            default: return  32'sd25500;       // 100 * 255, one row
        endcase
    endfunction

    //--- Sequencer ----------------------------------------------------------
    typedef enum logic [2:0] {
        S_IDLE = 3'd0, S_LOAD = 3'd1, S_CLEAR = 3'd2,
        S_RUN  = 3'd3, S_WAIT = 3'd4, S_DONE  = 3'd5
    } state_e;

    state_e                   state;
    logic [ROW_AW-1:0]        row;
    logic [1:0]               pat, wait_cnt;
    logic signed [ACC_W-1:0]  result;
    logic                     have_result;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state <= S_IDLE; row <= '0; pat <= '0; wait_cnt <= '0;
            result <= '0; have_result <= 1'b0;
        end else begin
            unique case (state)
                S_IDLE:  if (start) begin
                             pat <= sw_s2; row <= '0; have_result <= 1'b0;
                             state <= S_LOAD;
                         end
                S_LOAD:  begin
                             row <= row + 1'b1;
                             if (row == ROW_AW'(TILE_ROWS-1)) state <= S_CLEAR;
                         end
                S_CLEAR: begin row <= '0; state <= S_RUN; end
                S_RUN:   begin
                             row <= row + 1'b1;
                             if (row == ROW_AW'(TILE_ROWS-1)) begin
                                 wait_cnt <= '0; state <= S_WAIT;
                             end
                         end
                S_WAIT:  begin
                             wait_cnt <= wait_cnt + 1'b1;
                             if (wait_cnt == 2'(LANE_PIPE_DEPTH-1)) state <= S_DONE;
                         end
                S_DONE:  begin
                             result <= acc_out; have_result <= 1'b1;
                             state  <= S_IDLE;
                         end
                default: state <= S_IDLE;
            endcase
        end
    end

    //--- Lane stimulus (lane 0, pass 0, column 0) ---------------------------
    logic                       wt_we, acc_clear, acc_en;
    logic [ROW_AW+COL_AW-1:0]   wt_addr;
    logic signed [WEIGHT_W-1:0] wt_data;
    logic [SEQ_AW-1:0]          w_addr;
    logic signed [ACC_W-1:0]    acc_out;

    assign wt_we     = (state == S_LOAD);
    assign wt_addr   = (ROW_AW+COL_AW)'(row);      // col 0: addr = row
    assign wt_data   = w_of(pat);
    assign acc_clear = (state == S_CLEAR);
    assign acc_en    = (state == S_RUN);            // issue-aligned
    assign w_addr    = SEQ_AW'(row);                // pass 0

    // Activation-buffer stand-in: two registers, so act is valid at N+2
    logic [ROW_AW-1:0] row_d1;
    logic [ACT_W-1:0]  act_bcast;
    always_ff @(posedge clk) begin
        row_d1    <= w_addr[ROW_AW-1:0];
        act_bcast <= a_of(pat, row_d1);
    end

    mac_lane #(.LANE_ID(0)) u_lane (
        .clk, .rst_n, .wt_we, .wt_addr, .wt_data, .w_addr, .act_bcast,
        .noise_en (NOISE_NONE),
        .acc_clear, .acc_en,
        .shift_en (1'b0),
        .shift_in ('0),
        .acc_out
    );

    //--- Outputs ------------------------------------------------------------
    logic match;
    assign match = have_result && (result == exp_of(pat));

    assign LEDR[0]   = match;
    assign LEDR[1]   = have_result && !match;
    assign LEDR[8:2] = '0;
    assign LEDR[9]   = (state != S_IDLE);

    function automatic logic [7:0] hex7(logic [3:0] n);   // active low, DP off
        logic [6:0] s;                                     // gfedcba
        unique case (n)
            4'h0: s = 7'h3F; 4'h1: s = 7'h06; 4'h2: s = 7'h5B; 4'h3: s = 7'h4F;
            4'h4: s = 7'h66; 4'h5: s = 7'h6D; 4'h6: s = 7'h7D; 4'h7: s = 7'h07;
            4'h8: s = 7'h7F; 4'h9: s = 7'h6F; 4'hA: s = 7'h77; 4'hB: s = 7'h7C;
            4'hC: s = 7'h39; 4'hD: s = 7'h5E; 4'hE: s = 7'h79; default: s = 7'h71;
        endcase
        return ~{1'b0, s};
    endfunction

    assign HEX0 = hex7(result[3:0]);
    assign HEX1 = hex7(result[7:4]);
    assign HEX2 = hex7(result[11:8]);
    assign HEX3 = hex7(result[15:12]);
    assign HEX4 = hex7(result[19:16]);
    assign HEX5 = hex7(result[23:20]);

endmodule : mac_lane_demo_top