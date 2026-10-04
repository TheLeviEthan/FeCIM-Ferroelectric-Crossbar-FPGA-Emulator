//=============================================================================
// read_noise.sv -- Lane B
//
// Per-lane read-noise generator: 32-bit Galois LFSR, bell-shaped sum, Q1.8 scale, +/-127 clamp.
// Spec: mac-array-spec.md §5
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

module read_noise
    import fecim_pkg::*;
#(
    parameter int LANE_ID = 0
)(
    input  logic                clk,
    input  logic                rst_n,
    input  logic [31:0]         noise_seed,
    input  logic                reseed,
    input  logic                advance,         // acc_en ONLY -- never free-run
    input  logic [SIGMA_W-1:0]  sigma,           // Q1.8
    output logic [NOISE_W-1:0]  noise_q          // signed, clamped to +/-NOISE_CLAMP
);

    assign noise_q = '0;

endmodule : read_noise

/* verilator lint_on UNUSEDPARAM */
/* verilator lint_on UNUSEDSIGNAL */
