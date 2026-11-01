//=============================================================================
// mac_lane.sv -- one crossbar column per pass.
//
// Week 2: clean datapath only. Read noise (week 5) adds noise_q at the w_n add;
// ADC is shared at the drain-chain output, not in the lane.
//
// Pipeline (activation-buffer-spec.md §4):
//   N   : w_addr -> weight M9K address            acc_en high = row issued
//   N+1 : M9K out -> w_align                      v1
//   N+2 : w_n = w_align (+ noise), 9x9 multiply   v2 gates prod
//   N+3 : accumulate                              v3 gates acc
// acc_en is ISSUE-aligned and delayed internally. The FSM holds in ACC for
// LANE_PIPE_DEPTH cycles with acc_en LOW. If LANE_PIPE_DEPTH changes to 4,
// add one datapath register AND one valid stage here.
//=============================================================================
module mac_lane
    import fecim_pkg::*;
#(
    parameter int LANE_ID = 0
)(
    input  logic                       clk,
    input  logic                       rst_n,

    // Weight write port, from write_path (qualified TGT_WEIGHTS, delay-matched)
    input  logic                       wt_we,
    input  logic [ROW_AW+COL_AW-1:0]   wt_addr,     // col*TILE_ROWS + row
    input  logic signed [WEIGHT_W-1:0] wt_data,

    // Compute
    input  logic [SEQ_AW-1:0]          w_addr,      // seq_cnt = {pass, row}
    input  logic [ACT_W-1:0]           act_bcast,   // valid in cycle N+2
    /* verilator lint_off UNUSEDSIGNAL */
    input  noise_en_t                  noise_en,    // used from week 5
    /* verilator lint_on UNUSEDSIGNAL */
    input  logic                       acc_clear,
    input  logic                       acc_en,      // row issued this cycle

    // Drain shift chain
    input  logic                       shift_en,
    input  logic signed [ACC_W-1:0]    shift_in,    // from lane l+1
    output logic signed [ACC_W-1:0]    acc_out
);

    //-------------------------------------------------------------------------
    // Write decode: lane l owns columns {l, l+NUM_LANES, ...}
    // Assumes PASSES >= 2 (true for 64x64/32 and 128x128/64).
    //-------------------------------------------------------------------------
    logic [ROW_AW-1:0] wt_row;
    logic [COL_AW-1:0] wt_col;
    /* verilator lint_off UNUSEDSIGNAL */
    logic [COL_AW-1:0] wt_pass;                     // upper bits always zero
    /* verilator lint_on UNUSEDSIGNAL */
    logic [SEQ_AW-1:0] wt_local;
    logic              wt_mine;

    assign wt_row   = wt_addr[ROW_AW-1:0];
    assign wt_col   = wt_addr[ROW_AW +: COL_AW];
    assign wt_pass  = wt_col >> LANE_AW;
    assign wt_local = {wt_pass[SEQ_AW-ROW_AW-1:0], wt_row};
    assign wt_mine  = wt_we && (wt_col[LANE_AW-1:0] == LANE_ID[LANE_AW-1:0]);

    //-------------------------------------------------------------------------
    // Weight memory: simple dual-port M9K, registered read (cycle N -> N+1)
    //-------------------------------------------------------------------------
    (* ramstyle = "M9K" *) logic [WEIGHT_W-1:0] wmem [0:(1<<SEQ_AW)-1];
    logic [WEIGHT_W-1:0] w_rd;

    always_ff @(posedge clk) begin
        if (wt_mine) wmem[wt_local] <= wt_data;
        w_rd <= wmem[w_addr];
    end

    //-------------------------------------------------------------------------
    // Valid pipeline -- the only reset flops in the datapath
    //-------------------------------------------------------------------------
    logic v1, v2, v3;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) {v1, v2, v3} <= '0;
        else        {v1, v2, v3} <= {acc_en, v1, v2};
    end

    //-------------------------------------------------------------------------
    // N+1: alignment register (matches the activation broadcast register)
    //-------------------------------------------------------------------------
    logic signed [WEIGHT_W-1:0] w_align;

    always_ff @(posedge clk) w_align <= w_rd;

    //-------------------------------------------------------------------------
    // N+2: w_n and 9x9 SIGNED multiply. Both operands signed so two lanes'
    // multipliers can share one block (shared signa/signb). Do not make
    // either operand unsigned.
    //-------------------------------------------------------------------------
    logic signed [W_N_W-1:0]  w_n;
    logic signed [W_N_W-1:0]  act9;
    logic signed [PROD_W-1:0] prod;

    assign w_n  = {w_align[WEIGHT_W-1], w_align};   // sign-extend; + noise_q in week 5
    assign act9 = {1'b0, act_bcast};                // zero-extend to signed-positive

    // No reset, so Quartus can absorb this into the multiplier output register
    always_ff @(posedge clk)
        if (v2) prod <= w_n * act9;

    //-------------------------------------------------------------------------
    // N+3: accumulate. Priority: clear > accumulate > shift.
    //-------------------------------------------------------------------------
    logic signed [ACC_W-1:0] acc;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n)         acc <= '0;
        else if (acc_clear) acc <= '0;
        else if (v3)        acc <= acc + {{(ACC_W-PROD_W){prod[PROD_W-1]}}, prod};
        else if (shift_en)  acc <= shift_in;
    end

    assign acc_out = acc;

endmodule : mac_lane
