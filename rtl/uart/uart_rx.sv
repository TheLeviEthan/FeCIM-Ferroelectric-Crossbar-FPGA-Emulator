module uart_rx (
    input logic clk, rst_n, tick_16x, rxd,
    output logic [7:0] rx_data,
    output logic rx_valid, rx_frame_err
);
    typedef enum logic [1:0] {IDLE, START, DATA, STOP} state_t;
    state_t state;
    logic [2:0] rx_sync;
    logic [3:0] phase;
    logic [2:0] bit_idx;
    logic [7:0] shift_data;
    logic [1:0] samples;
    logic voted;
    assign voted = (samples[0] & samples[1]) |
                   (samples[0] & rx_sync[2]) |
                   (samples[1] & rx_sync[2]);
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            rx_sync <= 3'b111; state <= IDLE; phase <= 0;
            bit_idx <= 0; shift_data <= 0; samples <= 0;
            rx_data <= 0; rx_valid <= 0; rx_frame_err <= 0;
        end else begin
            rx_sync <= {rx_sync[1:0], rxd};
            rx_valid <= 0; rx_frame_err <= 0;
            if (state == IDLE) begin
                if (!rx_sync[2]) begin state <= START; phase <= 0; end
            end else if (tick_16x) begin
                if (phase == 7) samples[0] <= rx_sync[2];
                if (phase == 8) samples[1] <= rx_sync[2];
                if (phase == 9) begin
                    case (state)
                        START: begin
                            if (!voted) begin state <= DATA; bit_idx <= 0; end
                            else state <= IDLE;
                        end
                        DATA: begin
                            shift_data[bit_idx] <= voted;
                            if (bit_idx == 7) state <= STOP;
                            else bit_idx <= bit_idx + 1'b1;
                        end
                        STOP: begin
                            if (voted) begin rx_data <= shift_data; rx_valid <= 1; end
                            else rx_frame_err <= 1;
                            state <= IDLE;
                        end
                        default: state <= IDLE;
                    endcase
                end
                phase <= phase + 1'b1;
            end
        end
    end
endmodule
