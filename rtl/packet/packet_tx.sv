// Lane A response packet transmitter.
// Wire format: [5A][STATUS][LEN_LO][LEN_HI][PAYLOAD...][CRC8]
// CRC covers STATUS, LEN_LO, LEN_HI, and PAYLOAD (not sync).
module packet_tx import fecim_pkg::*; (
    input logic clk,
    input logic rst_n,
    input logic resp_valid,
    input logic [7:0] resp_cmd,
    input status_e resp_status,
    input logic [15:0] resp_len,
    output logic resp_ready,
    input logic [7:0] pl_data,
    input logic pl_valid,
    output logic pl_next,
    output logic [7:0] tx_data,
    output logic tx_start,
    input logic tx_busy
);
    typedef enum logic [3:0] {
        S_IDLE, S_SYNC, S_STATUS, S_LEN_LO, S_LEN_HI,
        S_PAYLOAD_REQ, S_PAYLOAD_WAIT, S_CRC, S_WAIT_BUSY, S_WAIT_DONE
    } state_t;
    state_t state, return_state;
    logic [7:0] crc;
    logic [15:0] length_latched, payload_sent;
    logic [7:0] status_latched;
    logic [7:0] payload_byte;
    logic payload_buffer_valid;

    // A request is accepted when resp_valid and resp_ready are both high.
    // The parser holds resp_valid until this handshake occurs.
    assign resp_ready = (state == S_IDLE);
    // One-cycle request; producer must respond with pl_valid after the request.
    assign pl_next = (state == S_PAYLOAD_REQ);

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state <= S_IDLE;
            return_state <= S_IDLE;
            crc <= CRC_INIT;
            length_latched <= 0;
            payload_sent <= 0;
            status_latched <= 0;
            tx_data <= 0;
            tx_start <= 0;
            payload_byte <= 0;
            payload_buffer_valid <= 0;
        end else begin
            tx_start <= 0;
            case (state)
                S_IDLE: if (resp_valid) begin
                    length_latched <= resp_len;
                    status_latched <= 8'(resp_status);
                    payload_sent <= 0;
                    crc <= CRC_INIT;
                    state <= S_SYNC;
                end
                S_SYNC: if (!tx_busy) begin
                    tx_data <= SYNC_F2H;
                    tx_start <= 1;
                    return_state <= S_STATUS;
                            state <= S_WAIT_BUSY;
                end
                S_STATUS: if (!tx_busy) begin
                    tx_data <= status_latched;
                    tx_start <= 1;
                    crc <= crc8_byte(crc, status_latched);
                    return_state <= S_LEN_LO;
                            state <= S_WAIT_BUSY;
                end
                S_LEN_LO: if (!tx_busy) begin
                    tx_data <= length_latched[7:0];
                    tx_start <= 1;
                    crc <= crc8_byte(crc, length_latched[7:0]);
                    return_state <= S_LEN_HI;
                            state <= S_WAIT_BUSY;
                end
                S_LEN_HI: if (!tx_busy) begin
                    tx_data <= length_latched[15:8];
                    tx_start <= 1;
                    crc <= crc8_byte(crc, length_latched[15:8]);
                    return_state <= (length_latched == 0) ? S_CRC : S_PAYLOAD_REQ;
                            state <= S_WAIT_BUSY;
                end
                S_PAYLOAD_REQ: begin
                    state <= S_PAYLOAD_WAIT;
                end
                S_PAYLOAD_WAIT: begin
                    if (pl_valid) begin
                        payload_byte <= pl_data;
                        payload_buffer_valid <= 1;
                    end
                    if (payload_buffer_valid && !tx_busy) begin
                        tx_data <= payload_byte;
                        tx_start <= 1;
                        crc <= crc8_byte(crc, payload_byte);
                        payload_sent <= payload_sent + 1'b1;
                        payload_buffer_valid <= 0;
                        return_state <= ((payload_sent + 1'b1) == length_latched) ? S_CRC : S_PAYLOAD_REQ;
                                    state <= S_WAIT_BUSY;
                    end
                end
                S_CRC: if (!tx_busy) begin
                    tx_data <= crc;
                    tx_start <= 1;
                    return_state <= S_IDLE;
                            state <= S_WAIT_BUSY;
                end
                // Wait until UART acknowledges start by raising busy.
                S_WAIT_BUSY: if (tx_busy) begin
                    state <= S_WAIT_DONE;
                end
                // Then wait for UART to finish the byte.
                S_WAIT_DONE: if (!tx_busy) state <= return_state;
                default: state <= S_IDLE;
            endcase
        end
    end
endmodule : packet_tx
