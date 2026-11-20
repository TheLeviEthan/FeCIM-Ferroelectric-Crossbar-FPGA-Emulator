//=============================================================================
// write_path.sv -- programming-time non-idealities on weight writes.
//
// B1: quantization + one-hot lane-select decode. D2D (S4) and stuck-at (S5)
// are pass-through slots now, so adding them later does not change latency.
//
//   S1 capture/filter -> S2 level = u*N -> S3 quantized value -> S4 [D2D] -> S5 [stuck] + lane decode
//
// Latency WR_LAT = 5. addr/data/we travel together, so the exact value is
// invisible to the lanes; only `busy` matters to the control FSM.
//
// Deviation from write-path-spec.md §4.1: the second shift rounds
// ((level*mult + 128) >> 8) so the top level always reaches +127.
// model_exact must match.
//=============================================================================
module write_path
    import fecim_pkg::*;
(
    input  logic                       clk,
    input  logic                       rst_n,
    // bulk stream from packet_parser
    input  logic                       bulk_we,
    input  bulk_target_e               bulk_target,
    input  logic [15:0]                bulk_addr,     // col*TILE_ROWS + row
    input  logic [7:0]                 bulk_data,     // int8 weight
    // config
    input  noise_en_t                  noise_en,
    input  logic [7:0]                 quant_levels,  // N, 2..255
    input  logic [15:0]                quant_mult,    // round(255*256/(N-1)), Q8.8
    // to lane array
    output logic [NUM_LANES-1:0]       wt_we,         // one-hot lane select
    output logic [ROW_AW+COL_AW-1:0]   wt_addr,       // global cell address
    output logic signed [WEIGHT_W-1:0] wt_data,
    output logic                       busy           // writes still in flight
);
    localparam int CELL_AW = ROW_AW + COL_AW;
    localparam int NCELLS  = TILE_ROWS * TILE_COLS;

    //--- S1: accept weights only, in range; latch per-write enables --------
    logic                v1, q1;
    logic [CELL_AW-1:0]  a1;
    logic [7:0]          w1;

    always_ff @(posedge clk or negedge rst_n)
        if (!rst_n) v1 <= 1'b0;
        else        v1 <= bulk_we && (bulk_target == TGT_WEIGHTS)
                                  && ({1'b0, bulk_addr} < 17'(NCELLS));

    always_ff @(posedge clk) begin
        a1 <= bulk_addr[CELL_AW-1:0];
        w1 <= bulk_data;
        q1 <= noise_en.quant;
    end

    //--- S2: level = (u * N) >> 8, u = w + 128 -----------------------------
    // For int8, adding 128 is the same as flipping the MSB (offset binary).
    // Both operands zero-extended to signed-positive: 9x9 signed (pkg header).
    logic signed [8:0]  u9, n9;
    logic signed [17:0] lvl_full;
    assign u9       = $signed({1'b0, w1 ^ 8'h80});
    assign n9       = $signed({1'b0, quant_levels});
    assign lvl_full = u9 * n9;                         // <= 255*255 = 65,025

    logic               v2, q2;
    logic [CELL_AW-1:0] a2;
    logic [7:0]         w2, lvl2;

    always_ff @(posedge clk or negedge rst_n)
        if (!rst_n) v2 <= 1'b0;
        else        v2 <= v1;

    always_ff @(posedge clk) begin
        a2   <= a1;  w2 <= w1;  q2 <= q1;
        lvl2 <= lvl_full[15:8];                        // 0..N-1
    end

    //--- S3: u_q = (level * QUANT_MULT + 128) >> 8; bypass mux --------------
    logic signed [25:0] sc_full;
    logic [7:0]         wq;
    assign sc_full = $signed({1'b0, lvl2}) * $signed({1'b0, quant_mult})
                   + 26'sd128;                         // <= 65,535, see header
    assign wq      = sc_full[15:8] ^ 8'h80;            // back to int8

    logic               v3;
    logic [CELL_AW-1:0] a3;
    logic [7:0]         w3;

    always_ff @(posedge clk or negedge rst_n)
        if (!rst_n) v3 <= 1'b0;
        else        v3 <= v2;

    always_ff @(posedge clk) begin
        a3 <= a2;
        w3 <= q2 ? wq : w2;                            // bypass is latency-matched
    end

    //--- S4: D2D slot (B2) -- pass-through ----------------------------------
    logic               v4;
    logic [CELL_AW-1:0] a4;
    logic [7:0]         w4;

    always_ff @(posedge clk or negedge rst_n)
        if (!rst_n) v4 <= 1'b0;
        else        v4 <= v3;

    always_ff @(posedge clk) begin
        a4 <= a3;
        w4 <= w3;
    end

    //--- S5: stuck-at slot (B3) -- pass-through; lane-select decode ---------
    // Lane l owns columns {l, l+NUM_LANES, ...}: lane = col mod NUM_LANES,
    // which is a wire slice because NUM_LANES is a power of two.
    logic [LANE_AW-1:0] lane4;
    assign lane4 = a4[ROW_AW +: LANE_AW];

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) wt_we <= '0;
        else begin
            wt_we <= '0;
            if (v4) wt_we[lane4] <= 1'b1;
        end
    end

    always_ff @(posedge clk) begin
        wt_addr <= a4;
        wt_data <= w4;
    end

    assign busy = v1 | v2 | v3 | v4 | (|wt_we);


endmodule : write_path
