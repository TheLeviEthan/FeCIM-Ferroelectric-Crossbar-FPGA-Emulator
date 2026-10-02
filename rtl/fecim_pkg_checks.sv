//=============================================================================
// fecim_pkg_checks.sv
//
// Elaboration-time checks on fecim_pkg parameters. These lived in an initial
// block inside the package (rev 3), which is not legal SystemVerilog.
// As generate-if blocks they fire at ELABORATION, so `verilator --lint-only`
// in CI fails the build the moment someone edits the package into a
// configuration that will not fit or will overflow -- no simulation needed.
//
// Instantiated once in fecim_top. Has no ports and synthesizes to nothing.
//=============================================================================

module fecim_pkg_checks
    import fecim_pkg::*;
();

    // synthesis translate_off
    if (2**$clog2(TILE_ROWS) != TILE_ROWS) begin : g_chk_rows_pow2
        $fatal(1, "TILE_ROWS must be a power of two");
    end
    if (2**$clog2(NUM_LANES) != NUM_LANES) begin : g_chk_lanes_pow2
        $fatal(1, "NUM_LANES must be a power of two");
    end
    if (W_N_W > 9) begin : g_chk_wn
        $fatal(1, "w_n > 9 bits: MAC multiply no longer packs 2 per block");
    end
    if (SIGMA_W > 9) begin : g_chk_sigma
        $fatal(1, "sigma > 9 bits: noise multiply no longer packs 2 per block");
    end
    // 64-bit shift, not 2**(ACC_W-1): at ACC_W = 32 that int expression
    // overflows to a negative number and the check fires unconditionally.
    if (64'(ACC_MAX_MAG) >= (64'd1 << (ACC_W-1))) begin : g_chk_acc
        $fatal(1, "accumulator can overflow at this geometry");
    end
    if (NUM_LANES > DEV_M9K - 2) begin : g_chk_m9k
        $fatal(1, "not enough M9K for one per lane plus act and result buffers");
    end
    if (ACC_USED_W > ACC_W) begin : g_chk_acc_used
        $fatal(1, "ACC_USED_W exceeds container width ACC_W");
    end
    if (QUANT_LEVELS_MAX > 255) begin : g_chk_quant
        $fatal(1, "QUANT_LEVELS > 255 pushes the quantize multiply past 9 bits");
    end
    // synthesis translate_on

endmodule : fecim_pkg_checks
