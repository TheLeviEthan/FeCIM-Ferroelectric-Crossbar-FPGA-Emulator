
`timescale 1ns/1ps

module config_regs
    import fecim_pkg::*;
(
    input  logic                  clk,
    input  logic                  rst_n,

    input  logic                  cfg_we,
    input  logic [7:0]            cfg_addr,
    input  logic [31:0]           cfg_wdata,
    output logic [31:0]           cfg_rdata,

    input  ctrl_state_e           ctrl_state,
    input  logic                  compute_busy,
    input  logic                  last_err,
    input  logic                  results_valid,
    input  logic [31:0]           cycle_cnt,

    output logic                  soft_reset,
    output logic                  clear_results,
    output logic                  reseed,

    output logic [15:0]           active_rows,
    output logic [15:0]           active_cols,
    output logic [7:0]            quant_levels,
    output logic [15:0]           quant_mult,
    output logic [SIGMA_W-1:0]    d2d_sigma,
    output logic [SIGMA_W-1:0]    read_sigma,
    output logic [31:0]           noise_seed,
    output noise_en_t             noise_en,
    output logic [4:0]            adc_bits,
    output logic [16:0]           stuck_rate,
    output logic [BAUD_ACC_W-1:0] baud_inc
);

    //=====================================================================
    // Register writes and reset defaults
    //=====================================================================

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            soft_reset    <= 1'b0;
            clear_results <= 1'b0;
            reseed        <= 1'b0;

            active_rows   <= 16'(TILE_ROWS);
            active_cols   <= 16'(TILE_COLS);
            quant_levels  <= 8'(QUANT_LEVELS_MAX);
            quant_mult    <= 16'd257;

            d2d_sigma     <= '0;
            read_sigma    <= '0;
            noise_seed    <= 32'd0;
            noise_en      <= NOISE_NONE;
            adc_bits      <= 5'(ACC_USED_W);
            stuck_rate    <= 17'd0;
            baud_inc      <= BAUD_INC_115200;

        end else begin

            // Control strobes are one clock wide.
            soft_reset    <= 1'b0;
            clear_results <= 1'b0;
            reseed        <= 1'b0;

            if (cfg_we) begin
                case (cfg_addr)

                    REG_CTRL: begin
                        soft_reset    <= cfg_wdata[0];
                        clear_results <= cfg_wdata[1];
                        reseed        <= cfg_wdata[2];
                    end

                    REG_TILE_CFG: begin
                        active_rows <= cfg_wdata[15:0];
                        active_cols <= cfg_wdata[31:16];
                    end

                    REG_QUANT_LEVELS: begin
                        if (cfg_wdata >= 32'd2 &&
                            cfg_wdata <= 32'd255)
                            quant_levels <= cfg_wdata[7:0];
                    end

                    REG_D2D_SIGMA: begin
                        d2d_sigma <=
                            SIGMA_W'({1'b0, cfg_wdata[7:0]});
                    end

                    REG_READ_SIGMA: begin
                        read_sigma <=
                            SIGMA_W'({1'b0, cfg_wdata[7:0]});
                    end

                    REG_NOISE_SEED: begin
                        noise_seed <= cfg_wdata;
                    end

                    REG_NOISE_EN: begin
                        noise_en <= noise_en_t'(cfg_wdata[5:0]);
                    end

                    REG_ADC_BITS: begin
                        if (cfg_wdata >= 32'd4 &&
                            cfg_wdata <= 32'(ACC_USED_W))
                            adc_bits <= cfg_wdata[4:0];
                    end

                    REG_STUCK_RATE: begin
                        stuck_rate <= cfg_wdata[16:0];
                    end

                    REG_BAUD_INC: begin
                        baud_inc <= BAUD_ACC_W'(cfg_wdata);
                    end

                    REG_QUANT_MULT: begin
                        quant_mult <= cfg_wdata[15:0];
                    end

                    // STATUS and CYCLE_CNT are read-only.
                    // Reserved addresses have no write effect.
                    default: begin
                    end

                endcase
            end
        end
    end

    //=====================================================================
    // Combinational register reads
    //=====================================================================

    always_comb begin
        cfg_rdata = 32'd0;

        case (cfg_addr)

            // Write-only CTRL
            REG_CTRL: begin
                cfg_rdata = 32'd0;
            end

            REG_TILE_CFG: begin
                cfg_rdata = {active_cols, active_rows};
            end

            REG_QUANT_LEVELS: begin
                cfg_rdata = {24'd0, quant_levels};
            end

            REG_D2D_SIGMA: begin
                cfg_rdata = {24'd0, d2d_sigma[7:0]};
            end

            REG_READ_SIGMA: begin
                cfg_rdata = {24'd0, read_sigma[7:0]};
            end

            REG_NOISE_SEED: begin
                cfg_rdata = noise_seed;
            end

            REG_NOISE_EN: begin
                cfg_rdata = 32'(noise_en);
            end

            REG_ADC_BITS: begin
                cfg_rdata = {27'd0, adc_bits};
            end

            REG_STUCK_RATE: begin
                cfg_rdata = {15'd0, stuck_rate};
            end

            REG_BAUD_INC: begin
                cfg_rdata = 32'(baud_inc);
            end

            REG_STATUS: begin
                cfg_rdata = {
                    16'd0,
                    8'd0, // result_seq: not present in current port list
                    1'b0,
                    results_valid,
                    last_err,
                    compute_busy,
                    4'(ctrl_state)
                };
            end

            REG_CYCLE_CNT: begin
                cfg_rdata = cycle_cnt;
            end

            REG_QUANT_MULT: begin
                cfg_rdata = {16'd0, quant_mult};
            end

            default: begin
                cfg_rdata = 32'd0;
            end

        endcase
    end

endmodule : config_regs
