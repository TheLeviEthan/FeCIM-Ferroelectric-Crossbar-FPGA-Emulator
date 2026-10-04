//=============================================================================
// packet_parser.sv -- Lane A
//
// Packet parser: sync hunt, length, payload, CRC-8 check, command dispatch.
// Spec: packet-parser-spec.md §7.1
//
// SKELETON. Port list is provisional -- confirm against the spec and the
// frozen A/B seam (top-level-spec §7.1) before implementing. Outputs are
// tied to safe idle values so the top level elaborates and lints cleanly.
//
// TODO(lane a): implement, then DELETE the lint waivers below. They
// exist only because a skeleton's inputs are unused; leaving them in place
// after implementation hides real -Wall findings.
//=============================================================================

/* verilator lint_off UNUSEDSIGNAL */
/* verilator lint_off UNUSEDPARAM */

module packet_parser
    import fecim_pkg::*;
(
    input  logic                clk,
    input  logic                rst_n,

    // from uart_rx
    input  logic [7:0]          rx_data,
    input  logic                rx_valid,

    // bulk stream (weights / activations / attenuation)
    output logic                bulk_we,
    output bulk_target_e        bulk_target,
    output logic [15:0]         bulk_addr,
    output logic [7:0]          bulk_data,

    // config register port
    output logic                cfg_we,
    output logic [7:0]          cfg_addr,
    output logic [31:0]         cfg_wdata,
    input  logic [31:0]         cfg_rdata,

    // compute control
    output logic                start_compute,
    input  logic                compute_busy,
    input  logic                compute_done,

    // readout request -> readout_ser
    output logic                rd_go,
    output logic                rd_argmax,     // 1 = READ_ARGMAX, 0 = READ_RESULT
    output logic [COL_AW-1:0]   rd_start,
    output logic [COL_AW:0]     rd_count,

    // response header -> packet_tx
    output logic                resp_valid,
    output logic [7:0]          resp_cmd,
    output status_e             resp_status,
    output logic [15:0]         resp_len,
    input  logic                resp_ready,

    // diagnostics
    output logic [15:0]         rx_byte_cnt,
    output logic [15:0]         err_cnt,
    output logic                err_latched
);

    // TODO: CRC via fecim_pkg::crc8_byte(); timeout = TIMEOUT_CYCLES.
    assign bulk_we       = 1'b0;
    assign bulk_target   = TGT_WEIGHTS;
    assign bulk_addr     = '0;
    assign bulk_data     = '0;
    assign cfg_we        = 1'b0;
    assign cfg_addr      = '0;
    assign cfg_wdata     = '0;
    assign start_compute = 1'b0;
    assign rd_go         = 1'b0;
    assign rd_argmax     = 1'b0;
    assign rd_start      = '0;
    assign rd_count      = '0;
    assign resp_valid    = 1'b0;
    assign resp_cmd      = '0;
    assign resp_status   = ST_OK;
    assign resp_len      = '0;
    assign rx_byte_cnt   = '0;
    assign err_cnt       = '0;
    assign err_latched   = 1'b0;

endmodule : packet_parser

/* verilator lint_on UNUSEDPARAM */
/* verilator lint_on UNUSEDSIGNAL */
