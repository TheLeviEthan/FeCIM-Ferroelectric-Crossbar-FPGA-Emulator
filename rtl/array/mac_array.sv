//=============================================================================
// mac_array.sv -- Lane B
//
// Lane array: NUM_LANES x mac_lane with an adjacent-lane drain shift chain toward lane 0.
// Spec: mac-array-spec.md, rtl-conventions §6
//
// SKELETON. Port list is provisional -- confirm against the spec and the
// frozen A/B seam (top-level-spec §7.1) before implementing. Outputs are
// tied to safe idle values so the top level elaborates and lints cleanly.
//
// TODO(lane b): implement, then DELETE the lint waivers below. They
// exist only because a skeleton's inputs are unused; leaving them in place
// after implementation hides real -Wall findings.
//=============================================================================

/* verilator lint_off UNUSEDSIGNAL */
/* verilator lint_off UNUSEDPARAM */

module mac_array
    import fecim_pkg::*;
(
    input  logic                clk,
    input  logic                rst_n,

    input  logic                lane_we,
    input  logic [LANE_AW-1:0]  lane_sel,
    input  logic [SEQ_AW-1:0]   lane_waddr,
    input  logic [WEIGHT_W-1:0] lane_wdata,

    input  logic [SEQ_AW-1:0]   seq_cnt,
    input  logic [ACT_W-1:0]    act_bcast,
    input  noise_en_t           noise_en,
    input  logic [SIGMA_W-1:0]  read_sigma,
    input  logic [31:0]         noise_seed,
    input  logic                reseed,
    input  logic                acc_clear,
    input  logic                acc_en,
    input  logic                shift_en,

    output logic [ACC_W-1:0]    chain_out        // lane 0 accumulator
);

    logic [ACC_W-1:0] lane_acc [NUM_LANES+1];

    for (genvar l = 0; l < NUM_LANES; l++) begin : g_lane
        mac_lane #(.LANE_ID(l)) u_lane (
            .clk        (clk),
            .rst_n      (rst_n),
            .lane_we    (lane_we),
            .lane_sel   (lane_sel),
            .lane_waddr (lane_waddr),
            .lane_wdata (lane_wdata),
            .w_addr     (seq_cnt),
            .act        (act_bcast),
            .noise_en   (noise_en),
            .read_sigma (read_sigma),
            .noise_seed (noise_seed),
            .reseed     (reseed),
            .acc_clear  (acc_clear),
            .acc_en     (acc_en),
            .shift_en   (shift_en),
            .shift_in   (lane_acc[l+1]),   // drain chain: l takes from l+1
            .acc_out    (lane_acc[l])
        );
    end

    assign lane_acc[NUM_LANES] = '0;       // chain tail
    assign chain_out           = lane_acc[0];

endmodule : mac_array

/* verilator lint_on UNUSEDPARAM */
/* verilator lint_on UNUSEDSIGNAL */
