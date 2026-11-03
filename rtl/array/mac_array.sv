//=============================================================================
// mac_array.sv -- Lane B
//
// Lane array: NUM_LANES x mac_lane with an adjacent-lane drain shift chain
// toward lane 0.
// Spec: mac-array-spec.md, rtl-conventions §6
//
// External port list is still provisional -- confirm against the frozen A/B
// seam (top-level-spec §7.1). It is deliberately unchanged here so fecim_top
// needs no edits. Write-port mapping: see "Weight write address" below.
//=============================================================================
module mac_array
    import fecim_pkg::*;
(
    input  logic                clk,
    input  logic                rst_n,

    // Weight write: lane select + lane-local address {pass, row}
    input  logic                lane_we,
    input  logic [LANE_AW-1:0]  lane_sel,
    input  logic [SEQ_AW-1:0]   lane_waddr,
    input  logic [WEIGHT_W-1:0] lane_wdata,

    // Compute
    input  logic [SEQ_AW-1:0]   seq_cnt,
    input  logic [ACT_W-1:0]    act_bcast,
    input  noise_en_t           noise_en,
    /* verilator lint_off UNUSEDSIGNAL */       // week 5: read noise in mac_lane
    input  logic [SIGMA_W-1:0]  read_sigma,
    input  logic [31:0]         noise_seed,
    input  logic                reseed,
    /* verilator lint_on UNUSEDSIGNAL */
    input  logic                acc_clear,
    input  logic                acc_en,
    input  logic                shift_en,

    output logic [ACC_W-1:0]    chain_out       // lane 0 accumulator
);

    //-------------------------------------------------------------------------
    // Weight write address. mac_lane decodes its own columns from the GLOBAL
    // cell address col*TILE_ROWS + row, with col = {pass, lane}. Rebuild it
    // from the lane-local form: {pass, row} + lane_sel -> {pass, lane, row}.
    //-------------------------------------------------------------------------
    logic [ROW_AW+COL_AW-1:0] wt_addr;

    assign wt_addr = {lane_waddr[SEQ_AW-1:ROW_AW], lane_sel, lane_waddr[ROW_AW-1:0]};

    //-------------------------------------------------------------------------
    // Lane array + drain chain (lane l takes from l+1, tail tied to zero)
    //-------------------------------------------------------------------------
    logic [ACC_W-1:0] lane_acc [NUM_LANES+1];

    for (genvar l = 0; l < NUM_LANES; l++) begin : g_lane
        mac_lane #(.LANE_ID(l)) u_lane (
            .clk        (clk),
            .rst_n      (rst_n),
            .wt_we      (lane_we),
            .wt_addr    (wt_addr),
            .wt_data    ($signed(lane_wdata)),
            .w_addr     (seq_cnt),
            .act_bcast  (act_bcast),
            .noise_en   (noise_en),
            .acc_clear  (acc_clear),
            .acc_en     (acc_en),
            .shift_en   (shift_en),
            .shift_in   (lane_acc[l+1]),
            .acc_out    (lane_acc[l])
        );
    end

    assign lane_acc[NUM_LANES] = '0;
    assign chain_out           = lane_acc[0];

endmodule : mac_array