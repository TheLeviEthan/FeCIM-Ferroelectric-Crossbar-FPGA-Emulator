//=============================================================================
// act_buffer.sv -- Lane B
//
// Activation buffer (M9K) plus shared IR-drop attenuation multiply in the broadcast path.
// Spec: activation-buffer-spec.md, mac-array-spec.md §8
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

module act_buffer
    import fecim_pkg::*;
(
    input  logic                clk,
    input  logic                rst_n,

    // bulk stream (TGT_ACT, TGT_ATTEN)
    input  logic                bulk_we,
    input  bulk_target_e        bulk_target,
    input  logic [15:0]         bulk_addr,
    input  logic [7:0]          bulk_data,

    input  noise_en_t           noise_en,        // uses .ir
    input  logic [ROW_AW-1:0]   act_addr,
    output logic [ACT_W-1:0]    act_bcast        // aligned to lane weight read
);

    assign act_bcast = '0;

endmodule : act_buffer

/* verilator lint_on UNUSEDPARAM */
/* verilator lint_on UNUSEDSIGNAL */
