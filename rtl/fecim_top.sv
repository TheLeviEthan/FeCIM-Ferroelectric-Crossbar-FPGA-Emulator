//=============================================================================
// fecim_top.sv -- Lane A
//
// Top level: the only module that touches device pins. Instantiates the full
// hierarchy from top-level-spec §7 and wires the frozen A/B seam (§7.1).
//
// Single 50 MHz clock domain, no PLL. KEY[0] = reset, KEY[1] = manual compute.
// ARDUINO_IO0 = rxd, ARDUINO_IO1 = txd.
//=============================================================================

module fecim_top
    import fecim_pkg::*;
(
    input  logic        MAX10_CLK1_50,
    input  logic [1:0]  KEY,
    input  logic [9:0]  SW,
    output logic [9:0]  LEDR,
    output logic [7:0]  HEX0, HEX1, HEX2, HEX3, HEX4, HEX5,
    input  logic        ARDUINO_IO0,     // rxd
    output logic        ARDUINO_IO1      // txd
);

    logic clk;
    logic rst_n;

    assign clk = MAX10_CLK1_50;

    // Elaboration-time parameter checks (no hardware).
    fecim_pkg_checks u_pkg_checks ();

    //-------------------------------------------------------------------------
    // Reset and slow asynchronous inputs
    //-------------------------------------------------------------------------
    reset_sync u_reset_sync (
        .clk    (clk),
        .arst_n (KEY[0]),
        .rst_n  (rst_n)
    );

    // KEY and SW: two-flop synchronizers (top-level-spec §3.3). KEY is
    // active low and hardware-debounced, so a falling-edge detect suffices.
    logic [2:0] key1_sync;
    logic [9:0] sw_meta, sw_sync;
    logic       manual_compute;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            key1_sync <= '1;
            sw_meta   <= '0;
            sw_sync   <= '0;
        end else begin
            key1_sync <= {key1_sync[1:0], KEY[1]};
            sw_meta   <= SW;
            sw_sync   <= sw_meta;
        end
    end

    assign manual_compute = key1_sync[2] & ~key1_sync[1];   // press = high->low

    //-------------------------------------------------------------------------
    // Interconnect
    //-------------------------------------------------------------------------
    // UART
    logic                  tick_16x;
    logic [7:0]            rx_data;
    logic                  rx_valid;
    logic                  rx_frame_err;
    logic [7:0]            tx_data;
    logic                  tx_start;
    logic                  tx_busy;

    // Bulk stream (A -> B)
    logic                  bulk_we;
    bulk_target_e          bulk_target;
    logic [15:0]           bulk_addr;
    logic [7:0]            bulk_data;

    // Config port and register values
    logic                  cfg_we;
    logic [7:0]            cfg_addr;
    logic [31:0]           cfg_wdata;
    logic [31:0]           cfg_rdata;
    logic                  soft_reset, clear_results, reseed;
    logic [15:0]           active_rows, active_cols;
    logic [7:0]            quant_levels;
    logic [15:0]           quant_mult;
    logic [15:0]           d2d_sigma;
    logic [SIGMA_W-1:0]    read_sigma;
    logic [31:0]           noise_seed;
    noise_en_t             noise_en;
    logic [4:0]            adc_bits;
    logic [16:0]           stuck_rate;
    logic [BAUD_ACC_W-1:0] baud_inc;

    // Control / sequencing (A -> B)
    logic                  start_compute;
    logic [SEQ_AW-1:0]     seq_cnt;
    logic [ROW_AW-1:0]     act_addr;
    logic                  acc_clear, acc_en, shift_en;
    logic                  drain_we;
    logic [COL_AW-1:0]     drain_addr;
    ctrl_state_e           ctrl_state;
    logic                  compute_busy, compute_done, run_start;
    logic [31:0]           cycle_cnt;

    // Response path
    logic                  rd_go, rd_argmax;
    logic [COL_AW-1:0]     rd_start;
    logic [COL_AW:0]       rd_count;
    logic                  resp_valid, resp_ready;
    logic [7:0]            resp_cmd;
    status_e               resp_status;
    logic [15:0]           resp_len;
    logic [7:0]            pl_data;
    logic                  pl_valid, pl_next;

    // Diagnostics
    logic [15:0]           rx_byte_cnt, err_cnt;
    logic                  err_latched;

    // Array / results (B -> A)
    logic                  lane_we;
    logic [LANE_AW-1:0]    lane_sel;
    logic [SEQ_AW-1:0]     lane_waddr;
    logic [WEIGHT_W-1:0]   lane_wdata;
    logic [ACT_W-1:0]      act_bcast;
    logic [ACC_W-1:0]      chain_out, adc_out;
    logic [COL_AW-1:0]     result_raddr;
    logic [ACC_W-1:0]      result_rdata;
    logic                  results_valid;
    logic [COL_AW-1:0]     argmax_idx;
    logic [ACC_W-1:0]      argmax_val, argmax_second;

    //=========================================================================
    // Lane A
    //=========================================================================
    baud_gen u_baud_gen (
        .clk      (clk),
        .rst_n    (rst_n),
        .baud_inc (baud_inc),
        .tick_16x (tick_16x)
    );

    uart_rx u_uart_rx (
        .clk          (clk),
        .rst_n        (rst_n),
        .tick_16x     (tick_16x),
        .rxd          (ARDUINO_IO0),
        .rx_data      (rx_data),
        .rx_valid     (rx_valid),
        .rx_frame_err (rx_frame_err)
    );

    uart_tx u_uart_tx (
        .clk      (clk),
        .rst_n    (rst_n),
        .tick_16x (tick_16x),
        .tx_data  (tx_data),
        .tx_start (tx_start),
        .tx_busy  (tx_busy),
        .txd      (ARDUINO_IO1)
    );

    packet_parser u_packet_parser (
        .clk           (clk),
        .rst_n         (rst_n),
        .rx_data       (rx_data),
        .rx_valid      (rx_valid),
        .bulk_we       (bulk_we),
        .bulk_target   (bulk_target),
        .bulk_addr     (bulk_addr),
        .bulk_data     (bulk_data),
        .cfg_we        (cfg_we),
        .cfg_addr      (cfg_addr),
        .cfg_wdata     (cfg_wdata),
        .cfg_rdata     (cfg_rdata),
        .start_compute (start_compute),
        .compute_busy  (compute_busy),
        .compute_done  (compute_done),
        .rd_go         (rd_go),
        .rd_argmax     (rd_argmax),
        .rd_start      (rd_start),
        .rd_count      (rd_count),
        .resp_valid    (resp_valid),
        .resp_cmd      (resp_cmd),
        .resp_status   (resp_status),
        .resp_len      (resp_len),
        .resp_ready    (resp_ready),
        .rx_byte_cnt   (rx_byte_cnt),
        .err_cnt       (err_cnt),
        .err_latched   (err_latched)
    );

    packet_tx u_packet_tx (
        .clk         (clk),
        .rst_n       (rst_n),
        .resp_valid  (resp_valid),
        .resp_cmd    (resp_cmd),
        .resp_status (resp_status),
        .resp_len    (resp_len),
        .resp_ready  (resp_ready),
        .pl_data     (pl_data),
        .pl_valid    (pl_valid),
        .pl_next     (pl_next),
        .tx_data     (tx_data),
        .tx_start    (tx_start),
        .tx_busy     (tx_busy)
    );

    config_regs u_config_regs (
        .clk           (clk),
        .rst_n         (rst_n),
        .cfg_we        (cfg_we),
        .cfg_addr      (cfg_addr),
        .cfg_wdata     (cfg_wdata),
        .cfg_rdata     (cfg_rdata),
        .ctrl_state    (ctrl_state),
        .compute_busy  (compute_busy),
        .last_err      (err_latched | rx_frame_err),
        .results_valid (results_valid),
        .cycle_cnt     (cycle_cnt),
        .soft_reset    (soft_reset),
        .clear_results (clear_results),
        .reseed        (reseed),
        .active_rows   (active_rows),
        .active_cols   (active_cols),
        .quant_levels  (quant_levels),
        .quant_mult    (quant_mult),
        .d2d_sigma     (d2d_sigma),
        .read_sigma    (read_sigma),
        .noise_seed    (noise_seed),
        .noise_en      (noise_en),
        .adc_bits      (adc_bits),
        .stuck_rate    (stuck_rate),
        .baud_inc      (baud_inc)
    );

    control_fsm u_control_fsm (
        .clk            (clk),
        .rst_n          (rst_n),
        .soft_reset     (soft_reset),
        .start_compute  (start_compute),
        .manual_compute (manual_compute),
        .active_rows    (active_rows),
        .active_cols    (active_cols),
        .seq_cnt        (seq_cnt),
        .act_addr       (act_addr),
        .acc_clear      (acc_clear),
        .acc_en         (acc_en),
        .shift_en       (shift_en),
        .drain_we       (drain_we),
        .drain_addr     (drain_addr),
        .state          (ctrl_state),
        .compute_busy   (compute_busy),
        .compute_done   (compute_done),
        .run_start      (run_start),
        .cycle_cnt      (cycle_cnt)
    );

    readout_ser u_readout_ser (
        .clk           (clk),
        .rst_n         (rst_n),
        .rd_go         (rd_go),
        .rd_argmax     (rd_argmax),
        .rd_start      (rd_start),
        .rd_count      (rd_count),
        .result_raddr  (result_raddr),
        .result_rdata  (result_rdata),
        .argmax_idx    (argmax_idx),
        .argmax_val    (argmax_val),
        .argmax_second (argmax_second),
        .rd_data       (pl_data),
        .rd_valid      (pl_valid),
        .rd_next       (pl_next)
    );

    display_driver u_display_driver (
        .clk          (clk),
        .rst_n        (rst_n),
        .sw           (sw_sync),
        .ctrl_state   (ctrl_state),
        .compute_busy (compute_busy),
        .err_latched  (err_latched),
        .rx_activity  (rx_valid),
        .seq_cnt      (seq_cnt),
        .rx_byte_cnt  (rx_byte_cnt),
        .err_cnt      (err_cnt),
        .result_word  (result_rdata),
        .argmax_idx   (argmax_idx),
        .hex0         (HEX0),
        .hex1         (HEX1),
        .hex2         (HEX2),
        .hex3         (HEX3),
        .hex4         (HEX4),
        .hex5         (HEX5),
        .ledr         (LEDR)
    );

    //=========================================================================
    // ---------------------------- A/B seam ---------------------------------
    // Lane B
    //=========================================================================
    write_path u_write_path (
        .clk          (clk),
        .rst_n        (rst_n),
        .bulk_we      (bulk_we),
        .bulk_target  (bulk_target),
        .bulk_addr    (bulk_addr),
        .bulk_data    (bulk_data),
        .noise_en     (noise_en),
        .quant_levels (quant_levels),
        .quant_mult   (quant_mult),
        .d2d_sigma    (d2d_sigma),
        .stuck_rate   (stuck_rate),
        .lane_we      (lane_we),
        .lane_sel     (lane_sel),
        .lane_waddr   (lane_waddr),
        .lane_wdata   (lane_wdata)
    );

    act_buffer u_act_buffer (
        .clk         (clk),
        .rst_n       (rst_n),
        .bulk_we     (bulk_we),
        .bulk_target (bulk_target),
        .bulk_addr   (bulk_addr),
        .bulk_data   (bulk_data),
        .noise_en    (noise_en),
        .act_addr    (act_addr),
        .act_bcast   (act_bcast)
    );

    mac_array u_mac_array (
        .clk        (clk),
        .rst_n      (rst_n),
        .lane_we    (lane_we),
        .lane_sel   (lane_sel),
        .lane_waddr (lane_waddr),
        .lane_wdata (lane_wdata),
        .seq_cnt    (seq_cnt),
        .act_bcast  (act_bcast),
        .noise_en   (noise_en),
        .read_sigma (read_sigma),
        .noise_seed (noise_seed),
        .reseed     (reseed),
        .acc_clear  (acc_clear),
        .acc_en     (acc_en),
        .shift_en   (shift_en),
        .chain_out  (chain_out)
    );

    adc_quant u_adc_quant (
        .noise_en (noise_en),
        .adc_bits (adc_bits),
        .acc_in   (chain_out),
        .adc_out  (adc_out)
    );

    result_buffer u_result_buffer (
        .clk           (clk),
        .rst_n         (rst_n),
        .clear_results (clear_results),
        .run_start     (run_start),
        .compute_done  (compute_done),
        .we            (drain_we),
        .waddr         (drain_addr),
        .wdata         (adc_out),
        .raddr         (result_raddr),
        .rdata         (result_rdata),
        .results_valid (results_valid)
    );

    argmax_unit u_argmax_unit (
        .clk           (clk),
        .rst_n         (rst_n),
        .clear         (run_start),
        .in_valid      (drain_we),
        .in_idx        (drain_addr),
        .in_val        (adc_out),
        .argmax_idx    (argmax_idx),
        .argmax_val    (argmax_val),
        .argmax_second (argmax_second)
    );

endmodule : fecim_top
