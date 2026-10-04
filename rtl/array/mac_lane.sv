//=============================================================================
// mac_lane.sv -- Lane B
//
// One MAC lane: private weight M9K, read noise, 9x9 signed multiply, 32-bit accumulator, drain shift.
// Spec: mac-array-spec.md
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

module mac_lane
    import fecim_pkg::*;
#(
    parameter int LANE_ID = 0
)(
    input  logic                clk,
    input  logic                rst_n,

    // weight write (port A)
    input  logic                lane_we,
    input  logic [LANE_AW-1:0]  lane_sel,
    input  logic [SEQ_AW-1:0]   lane_waddr,
    input  logic [WEIGHT_W-1:0] lane_wdata,

    // compute
    input  logic [SEQ_AW-1:0]   w_addr,          // = seq_cnt
    input  logic [ACT_W-1:0]    act,
    input  noise_en_t           noise_en,
    input  logic [SIGMA_W-1:0]  read_sigma,
    input  logic [31:0]         noise_seed,
    input  logic                reseed,
    input  logic                acc_clear,
    input  logic                acc_en,
    input  logic                shift_en,
    input  logic [ACC_W-1:0]    shift_in,        // from lane l+1
    output logic [ACC_W-1:0]    acc_out
);

    logic [NOISE_W-1:0] noise_q;

    read_noise #(.LANE_ID(LANE_ID)) u_read_noise (
        .clk        (clk),
        .rst_n      (rst_n),
        .noise_seed (noise_seed),
        .reseed     (reseed),
        .advance    (acc_en),
        .sigma      (read_sigma),
        .noise_q    (noise_q)
    );

    // TODO: keep the multiply 9x9 SIGNED x SIGNED so two lanes pack per block.
    assign acc_out = '0;

endmodule : mac_lane

/* verilator lint_on UNUSEDPARAM */
/* verilator lint_on UNUSEDSIGNAL */
