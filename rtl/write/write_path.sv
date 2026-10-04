//=============================================================================
// write_path.sv -- Lane B
//
// Write-path transform: quantize, D2D variation (triangular, n=2), stuck-at faults; lane-select decode.
// Spec: write-path-spec.md
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

module write_path
    import fecim_pkg::*;
(
    input  logic                clk,
    input  logic                rst_n,

    // bulk stream from packet_parser
    input  logic                bulk_we,
    input  bulk_target_e        bulk_target,
    input  logic [15:0]         bulk_addr,
    input  logic [7:0]          bulk_data,

    // config
    input  noise_en_t           noise_en,
    input  logic [7:0]          quant_levels,
    input  logic [15:0]         quant_mult,
    input  logic [15:0]         d2d_sigma,
    input  logic [16:0]         stuck_rate,

    // to lane weight memories (port A)
    output logic                lane_we,
    output logic [LANE_AW-1:0]  lane_sel,
    output logic [SEQ_AW-1:0]   lane_waddr,
    output logic [WEIGHT_W-1:0] lane_wdata
);

    // TODO: D2D/stuck use fecim_pkg::cell_hash16() on the GLOBAL cell address.
    assign lane_we    = 1'b0;
    assign lane_sel   = '0;
    assign lane_waddr = '0;
    assign lane_wdata = '0;

endmodule : write_path

/* verilator lint_on UNUSEDPARAM */
/* verilator lint_on UNUSEDSIGNAL */
