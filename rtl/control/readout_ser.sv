//=============================================================================
// readout_ser.sv -- Lane A
//
// Serializes response payloads into the byte stream consumed by packet_tx:
//   - CMD_IDENTIFY:    16-byte fixed identity and error counters (protocol §4.5)
//   - CMD_GET_CONFIG:  4-byte register value (protocol §4.3)
//   - CMD_READ_ARGMAX: 10-byte argmax summary (result-buffer-spec §6.1)
//   - CMD_READ_RESULT: count * 4 bytes from result buffer (protocol §4.4)
//
// Spec: result-buffer-spec.md §6, protocol.md §4
//=============================================================================

module readout_ser
    import fecim_pkg::*;
(
    input  logic                clk,
    input  logic                rst_n,

    // Response handshake from packet_parser / fecim_top
    input  logic                resp_valid,
    input  logic [7:0]          resp_cmd,
    input  status_e             resp_status,
    input  logic [15:0]         resp_len,
    input  logic                resp_ready,

    // Config data (for CMD_GET_CONFIG)
    input  logic [31:0]         cfg_rdata,

    // Error counters (for CMD_IDENTIFY)
    input  logic [15:0]         framing_err_cnt,
    input  logic [15:0]         crc_err_cnt,
    input  logic [15:0]         timeout_cnt,

    // Result buffer readout controls (from packet_parser)
    input  logic                rd_go,
    input  logic                rd_argmax,
    input  logic [COL_AW-1:0]   rd_start,
    input  logic [COL_AW:0]     rd_count,

    // Result buffer interface (B side)
    output logic [COL_AW-1:0]   result_raddr,
    input  logic [ACC_W-1:0]    result_rdata,

    // Argmax unit interface (B side)
    input  logic [COL_AW-1:0]   argmax_idx,
    input  logic [ACC_W-1:0]    argmax_val,
    input  logic [ACC_W-1:0]    argmax_second,

    // Payload byte stream -> packet_tx
    output logic [7:0]          rd_data,
    output logic                rd_valid,
    input  logic                rd_next
);

    // Suppress unused signal warnings for rd_go/rd_argmax which are superseded by resp_cmd
    logic unused_inputs;
    assign unused_inputs = &{1'b0, rd_go, rd_argmax, resp_len, 1'b0};

    typedef enum logic [1:0] {
        RO_IDLE,
        RO_BUFFERED,
        RO_RESULT
    } ro_state_e;

    ro_state_e state;

    logic [7:0] buf_data [0:15];
    logic [4:0] buf_len;
    logic [4:0] buf_idx;

    logic [COL_AW-1:0] cur_col;
    logic [COL_AW:0]   words_left;
    logic [1:0]        byte_in_word;

    logic [15:0] argmax_idx_16;
    assign argmax_idx_16 = 16'(argmax_idx);

    assign result_raddr = cur_col;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state        <= RO_IDLE;
            buf_len      <= 5'd0;
            buf_idx      <= 5'd0;
            cur_col      <= '0;
            words_left   <= '0;
            byte_in_word <= 2'd0;
            rd_data      <= 8'd0;
            rd_valid     <= 1'b0;
            for (int i = 0; i < 16; i++) buf_data[i] <= 8'd0;
        end else begin
            rd_valid <= 1'b0;

            // Start of a new response transmission
            if (resp_valid && resp_ready && resp_status == ST_OK) begin
                case (resp_cmd)
                    CMD_IDENTIFY: begin
                        buf_data[0]  <= MAGIC[7:0];
                        buf_data[1]  <= MAGIC[15:8];
                        buf_data[2]  <= PROTO_VER;
                        buf_data[3]  <= BUILD_ID;
                        buf_data[4]  <= 8'(NUM_LANES);
                        buf_data[5]  <= 8'(NUM_LANES >> 8);
                        buf_data[6]  <= 8'(TILE_ROWS);
                        buf_data[7]  <= 8'(TILE_ROWS >> 8);
                        buf_data[8]  <= 8'(TILE_COLS);
                        buf_data[9]  <= 8'(TILE_COLS >> 8);
                        buf_data[10] <= framing_err_cnt[7:0];
                        buf_data[11] <= framing_err_cnt[15:8];
                        buf_data[12] <= crc_err_cnt[7:0];
                        buf_data[13] <= crc_err_cnt[15:8];
                        buf_data[14] <= timeout_cnt[7:0];
                        buf_data[15] <= timeout_cnt[15:8];
                        buf_len      <= 5'd16;
                        buf_idx      <= 5'd0;
                        state        <= RO_BUFFERED;
                    end

                    CMD_GET_CONFIG: begin
                        buf_data[0]  <= cfg_rdata[7:0];
                        buf_data[1]  <= cfg_rdata[15:8];
                        buf_data[2]  <= cfg_rdata[23:16];
                        buf_data[3]  <= cfg_rdata[31:24];
                        buf_len      <= 5'd4;
                        buf_idx      <= 5'd0;
                        state        <= RO_BUFFERED;
                    end

                    CMD_READ_ARGMAX: begin
                        buf_data[0]  <= argmax_idx_16[7:0];
                        buf_data[1]  <= argmax_idx_16[15:8];
                        buf_data[2]  <= argmax_val[7:0];
                        buf_data[3]  <= argmax_val[15:8];
                        buf_data[4]  <= argmax_val[23:16];
                        buf_data[5]  <= argmax_val[31:24];
                        buf_data[6]  <= argmax_second[7:0];
                        buf_data[7]  <= argmax_second[15:8];
                        buf_data[8]  <= argmax_second[23:16];
                        buf_data[9]  <= argmax_second[31:24];
                        buf_len      <= 5'd10;
                        buf_idx      <= 5'd0;
                        state        <= RO_BUFFERED;
                    end

                    CMD_READ_RESULT: begin
                        cur_col      <= rd_start;
                        words_left   <= rd_count;
                        byte_in_word <= 2'd0;
                        state        <= RO_RESULT;
                    end

                    default: begin
                        state <= RO_IDLE;
                    end
                endcase
            end

            // Byte streaming in response to packet_tx rd_next pulse
            case (state)
                RO_BUFFERED: begin
                    if (rd_next) begin
                        rd_data  <= buf_data[buf_idx];
                        rd_valid <= 1'b1;
                        buf_idx  <= buf_idx + 5'd1;
                        if (buf_idx + 5'd1 == buf_len) begin
                            state <= RO_IDLE;
                        end
                    end
                end

                RO_RESULT: begin
                    if (rd_next) begin
                        rd_valid <= 1'b1;
                        case (byte_in_word)
                            2'd0: rd_data <= result_rdata[7:0];
                            2'd1: rd_data <= result_rdata[15:8];
                            2'd2: rd_data <= result_rdata[23:16];
                            2'd3: begin
                                rd_data    <= result_rdata[31:24];
                                cur_col    <= cur_col + 1'b1;
                                words_left <= words_left - 1'b1;
                                if (words_left == 1'b1) begin
                                    state <= RO_IDLE;
                                end
                            end
                        endcase
                        byte_in_word <= byte_in_word + 2'd1;
                    end
                end

                default: ;
            endcase
        end
    end

endmodule : readout_ser
