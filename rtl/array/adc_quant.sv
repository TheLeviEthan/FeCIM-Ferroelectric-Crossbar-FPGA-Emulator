//=============================================================================
// adc_quant.sv -- Lane B
//
// Shared ADC model at the drain-chain output: round-to-nearest over ACC_USED_W.
// Spec: mac-array-spec.md §9
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

module adc_quant
    import fecim_pkg::*;
(
    input  noise_en_t           noise_en,        // uses .adc
    input  logic [4:0]          adc_bits,
    input  logic [ACC_W-1:0]    acc_in,
    output logic [ACC_W-1:0]    adc_out
);

    // TODO: shift = ACC_USED_W - adc_bits (NOT ACC_W). Round, do not truncate.
    assign adc_out = acc_in;

endmodule : adc_quant

/* verilator lint_on UNUSEDPARAM */
/* verilator lint_on UNUSEDSIGNAL */
