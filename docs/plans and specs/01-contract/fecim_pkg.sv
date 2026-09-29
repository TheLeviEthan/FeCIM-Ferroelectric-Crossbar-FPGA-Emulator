//=============================================================================
// fecim_pkg.sv
//
// Shared parameters, types, and protocol encodings for the FeCIM crossbar
// emulator. Every RTL module and every testbench imports this package.
//
// Rev 3 -- adds TGT_ATTEN, CMD_WRITE_ATTEN, ACC_USED_W. See docs/consistency-audit.md.
// Rev 2 -- corrected against DE10-Lite User Manual v1.6 and the MAX 10
//          Embedded Multipliers User Guide. See docs/hardware-verification.md.
//
// CRITICAL WIDTH CONSTRAINT
//   The MAX 10 has 144 embedded multiplier blocks. Each block is EITHER one
//   18x18 multiplier OR two 9x9 multipliers. Any operand wider than 9 bits
//   consumes a whole block.
//
//   Every multiply in this design is therefore kept to 9x9 signed so two fit
//   per block. Widening any operand past 9 bits doubles that multiply's block
//   cost. At 64 lanes that is the difference between 50% and 100% utilization.
//
//   A block has ONE signa/signb pair shared by both of its 9x9 multipliers,
//   so paired multiplies must agree on signedness. Everything here is
//   signed x signed -- unsigned operands are zero-extended to signed-positive.
//   Do NOT "optimize" any multiply to unsigned; it prevents pairing.
//=============================================================================

package fecim_pkg;

    //-------------------------------------------------------------------------
    // Board constants -- verified against DE10-Lite User Manual v1.6
    //-------------------------------------------------------------------------
    parameter int CLK_HZ          = 50_000_000;   // MAX10_CLK1_50, PIN_P11

    // Device capacity, for assertions and resource reporting
    parameter int DEV_LE          = 49_760;
    parameter int DEV_M9K         = 182;          // 1,638 Kbit / 9 Kbit
    parameter int DEV_MULT_BLOCKS = 144;          // 18x18, or 288 in 9x9 mode

    //-------------------------------------------------------------------------
    // Array geometry
    //
    // TILE_ROWS and NUM_LANES MUST be powers of two -- the sequencer decodes
    // addresses with wire slices instead of arithmetic. See control-fsm §3.1.
    //-------------------------------------------------------------------------
    parameter int TILE_ROWS       = 128;
    parameter int TILE_COLS       = 128;
    parameter int NUM_LANES       = 64;
    parameter int PASSES          = TILE_COLS / NUM_LANES;

    parameter int ROW_AW          = $clog2(TILE_ROWS);   // 7
    parameter int COL_AW          = $clog2(TILE_COLS);   // 7
    parameter int LANE_AW         = $clog2(NUM_LANES);   // 6
    parameter int SEQ_AW          = ROW_AW + $clog2(PASSES);

    //-------------------------------------------------------------------------
    // Datapath widths -- every one of these is load-bearing, see header note
    //-------------------------------------------------------------------------
    parameter int WEIGHT_W        = 8;    // stored weight, signed, +/-127
    parameter int ACT_W           = 8;    // activation, UNSIGNED 0..255
    parameter int NOISE_W         = 8;    // read noise, signed, clamped +/-127
    parameter int W_N_W           = 9;    // weight + noise, signed, +/-254
    parameter int SIGMA_W         = 9;    // Q1.8, signed-positive
    parameter int PROD_W          = 18;   // 9x9 signed product
    parameter int ACC_W           = 32;   // accumulator

    // Read-noise clamp. Sized so W_N_W stays at 9 bits:
    //   +/-127 (weight) + /-127 (noise) = +/-254, which is 9 bits signed.
    // Raising this to +/-255 pushes w_n to 10 bits and doubles MAC block cost.
    parameter int NOISE_CLAMP     = 127;

    // Capped at 255, not 256, so (u * QUANT_LEVELS) stays a 9x9 multiply.
    parameter int QUANT_LEVELS_MAX = 255;

    // Worst-case accumulator magnitude:
    //   254 (max |w_n|) x 255 (max act) x 128 (rows) = 8,290,560 -> 24 bits.
    // That fits 24-bit signed by 1.2%, which is too thin to rely on, and it
    // fails outright at 256 rows. ACC_W stays 32; the result buffer M9K runs
    // natively in 256x32 mode, so the extra width is free there.
    parameter int ACC_MAX_MAG     = 254 * 255 * TILE_ROWS;

    // Bits the accumulator actually USES, as distinct from ACC_W which is the
    // container width. The ADC must quantize over this range -- using ACC_W
    // would place full scale at +/-2^31, a range the signal never approaches,
    // collapsing every result into one or two codes.
    //   adc_shift = ACC_USED_W - adc_bits
    parameter int ACC_USED_W      = $clog2(ACC_MAX_MAG) + 1;   // 24 at 128 rows

    //-------------------------------------------------------------------------
    // Lane pipeline depth: address issue -> accumulator final.
    //
    //   N   : seq_cnt -> M9K address (weight and activation)
    //   N+1 : M9K out -> alignment register; noise scaled and clamped
    //   N+2 : w_n = w + noise, then 9x9 multiply (shared cycle)
    //   N+3 : accumulate
    //
    // The control FSM holds in ACC for this many cycles after the last row.
    // If the N+2 path fails timing, register between the add and the multiply
    // and change this to 4 -- the FSM follows automatically.
    //-------------------------------------------------------------------------
    parameter int LANE_PIPE_DEPTH = 3;

    //-------------------------------------------------------------------------
    // Host interface
    //-------------------------------------------------------------------------
    parameter int  MAX_PAYLOAD    = 512;
    parameter int  BAUD_ACC_W     = 16;
    parameter int  TIMEOUT_CYCLES = 5_000_000;          // 100 ms @ 50 MHz

    parameter logic [7:0] SYNC_H2F = 8'hA5;             // host -> fpga
    parameter logic [7:0] SYNC_F2H = 8'h5A;             // fpga -> host
    parameter logic [7:0] CRC_POLY = 8'h07;             // CRC-8/ATM
    parameter logic [7:0] CRC_INIT = 8'h00;

    // baud_inc = round(2^16 * 16 * baud / CLK_HZ)
    parameter logic [BAUD_ACC_W-1:0] BAUD_INC_115200 = 16'd2416;
    parameter logic [BAUD_ACC_W-1:0] BAUD_INC_230400 = 16'd4832;
    parameter logic [BAUD_ACC_W-1:0] BAUD_INC_460800 = 16'd9664;
    parameter logic [BAUD_ACC_W-1:0] BAUD_INC_921600 = 16'd19327;

    //-------------------------------------------------------------------------
    // Identify block
    //-------------------------------------------------------------------------
    parameter logic [15:0] MAGIC     = 16'hFEC1;
    parameter logic [7:0]  PROTO_VER = 8'h02;           // rev 2
    parameter logic [7:0]  BUILD_ID  = 8'h01;           // bump every release

    //-------------------------------------------------------------------------
    // Protocol enums
    //-------------------------------------------------------------------------
    typedef enum logic [7:0] {
        CMD_WRITE_WEIGHTS = 8'h01,
        CMD_WRITE_ACT     = 8'h02,
        CMD_SET_CONFIG    = 8'h03,
        CMD_COMPUTE       = 8'h04,
        CMD_READ_RESULT   = 8'h05,
        CMD_IDENTIFY      = 8'h06,
        CMD_GET_CONFIG    = 8'h07,
        CMD_READ_ARGMAX   = 8'h08,
        CMD_WRITE_ATTEN   = 8'h09
    } cmd_e;

    typedef enum logic [7:0] {
        ST_OK          = 8'h00,
        ST_CRC_ERR     = 8'h01,
        ST_BAD_LEN     = 8'h02,
        ST_UNKNOWN_CMD = 8'h03,
        ST_BUSY        = 8'h04,
        ST_ADDR_RANGE  = 8'h05
    } status_e;

    typedef enum logic [1:0] {
        TGT_WEIGHTS = 2'd0,
        TGT_ACT     = 2'd1,
        TGT_ATTEN   = 2'd2      // IR-drop coefficients, Q0.8, one per row
    } bulk_target_e;

    //-------------------------------------------------------------------------
    // State enums -- encoded explicitly so STATUS and the HEX display show
    // documented values rather than synthesizer-chosen ones.
    //-------------------------------------------------------------------------
    typedef enum logic [2:0] {
        CTRL_IDLE  = 3'd0,
        CTRL_CLEAR = 3'd1,
        CTRL_ACC   = 3'd2,
        CTRL_DRAIN = 3'd3,
        CTRL_DONE  = 3'd4
    } ctrl_state_e;

    typedef enum logic [2:0] {
        PKT_HUNT     = 3'd0,
        PKT_CMD      = 3'd1,
        PKT_LEN0     = 3'd2,
        PKT_LEN1     = 3'd3,
        PKT_PAYLOAD  = 3'd4,
        PKT_CRC      = 3'd5,
        PKT_DISPATCH = 3'd6,
        PKT_RESPOND  = 3'd7
    } pkt_state_e;

    typedef enum logic [1:0] {
        RX_IDLE  = 2'd0,
        RX_START = 2'd1,
        RX_DATA  = 2'd2,
        RX_STOP  = 2'd3
    } uart_rx_state_e;

    //-------------------------------------------------------------------------
    // Config register map -- see control-fsm §6
    //-------------------------------------------------------------------------
    typedef enum logic [7:0] {
        REG_CTRL         = 8'h00,
        REG_TILE_CFG     = 8'h01,
        REG_QUANT_LEVELS = 8'h02,
        REG_D2D_SIGMA    = 8'h03,
        REG_READ_SIGMA   = 8'h04,   // Q1.8, 9 bits used
        REG_NOISE_SEED   = 8'h05,
        REG_NOISE_EN     = 8'h06,
        REG_ADC_BITS     = 8'h07,
        REG_STUCK_RATE   = 8'h08,   // [15:0] rate, [16] mode
        REG_BAUD_INC     = 8'h09,
        REG_STATUS       = 8'h0A,
        REG_CYCLE_CNT    = 8'h0B,
        REG_QUANT_MULT   = 8'h0C    // Q8.8: round(255*256/(N-1)), 16 bits
        // 0x10-0x1F reserved for semester 2 -- see docs/protocol.md section 6.0
    } cfg_addr_e;

    //-------------------------------------------------------------------------
    // Non-ideality enables. Packed so bit order is declared once and cannot
    // drift between RTL, reference model, and driver. quant is bit 0.
    //-------------------------------------------------------------------------
    typedef struct packed {
        logic adc;      // bit 5 -- output truncation
        logic ir;       // bit 4 -- IR-drop attenuation
        logic stuck;    // bit 3 -- stuck-at faults
        logic read;     // bit 2 -- cycle-to-cycle read noise
        logic d2d;      // bit 1 -- device-to-device variation
        logic quant;    // bit 0 -- conductance quantization
    } noise_en_t;

    parameter noise_en_t NOISE_NONE = '0;   // reset default: ideal crossbar

    //-------------------------------------------------------------------------
    // Cell hash -- 16-bit rounds.
    //
    // A 32x32 multiply costs FOUR 18x18 blocks; two rounds across two hash
    // instances would have been 16 blocks. 16x16 rounds cost one block each,
    // so both hashes together cost four.
    //
    // Consequence: 16 bits of output per hash, so write-path D2D variation
    // uses Irwin-Hall n=2 (triangular), not n=4. Document it as triangular;
    // do not claim Gaussian.
    //-------------------------------------------------------------------------
    parameter logic [15:0] HASH_C0 = 16'h9E37;
    parameter logic [15:0] HASH_C1 = 16'h85EB;

    parameter logic [15:0] SEED_D2D   = 16'hD2D0;
    parameter logic [15:0] SEED_STUCK = 16'h57C0;

    function automatic logic [15:0] cell_hash16(
        input logic [15:0] addr,
        input logic [15:0] seed
    );
        logic [15:0] h;
        h = addr ^ seed;
        h = h * HASH_C0;
        h = h ^ (h >> 7);
        h = h * HASH_C1;
        h = h ^ (h >> 9);
        return h;
    endfunction

    //-------------------------------------------------------------------------
    // Read-noise LFSR: 32-bit Galois, x^32 + x^22 + x^2 + x + 1.
    //
    // Advance ONLY on acc_en, never free-running -- otherwise the noise
    // sequence depends on host timing and reproducibility is lost, which
    // breaks the RTL-vs-model equivalence tests.
    //-------------------------------------------------------------------------
    parameter logic [31:0] LFSR_TAPS   = 32'h8020_0003;
    parameter logic [31:0] LANE_SALT_C = 32'h9E37_79B1;   // x lane_id

    //-------------------------------------------------------------------------
    // CRC-8 (poly 0x07, init 0x00, no reflection, no final XOR).
    // Unrolls to a combinational XOR tree, roughly 30 LEs.
    //-------------------------------------------------------------------------
    function automatic logic [7:0] crc8_byte(
        input logic [7:0] crc_in,
        input logic [7:0] data
    );
        logic [7:0] c;
        c = crc_in ^ data;
        for (int i = 0; i < 8; i++)
            c = c[7] ? ((c << 1) ^ CRC_POLY) : (c << 1);
        return c;
    endfunction

    //-------------------------------------------------------------------------
    // Elaboration-time assertions -- catch the failure modes above at compile
    // time rather than at fitting time or, worse, in silicon.
    //-------------------------------------------------------------------------
    // synthesis translate_off
    initial begin
        assert (2**$clog2(TILE_ROWS) == TILE_ROWS)
            else $fatal(1, "TILE_ROWS must be a power of two");
        assert (2**$clog2(NUM_LANES) == NUM_LANES)
            else $fatal(1, "NUM_LANES must be a power of two");
        assert (W_N_W <= 9)
            else $fatal(1, "w_n > 9 bits: MAC multiply no longer packs 2 per block");
        assert (SIGMA_W <= 9)
            else $fatal(1, "sigma > 9 bits: noise multiply no longer packs 2 per block");
        assert (ACC_MAX_MAG < 2**(ACC_W-1))
            else $fatal(1, "accumulator can overflow at this geometry");
        assert (NUM_LANES <= DEV_M9K - 2)
            else $fatal(1, "not enough M9K for one per lane plus act and result buffers");
        assert (ACC_USED_W <= ACC_W)
            else $fatal(1, "ACC_USED_W exceeds container width ACC_W");
        assert (QUANT_LEVELS_MAX <= 255)
            else $fatal(1, "QUANT_LEVELS > 255 pushes the quantize multiply past 9 bits");
    end
    // synthesis translate_on

endpackage : fecim_pkg
