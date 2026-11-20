//=============================================================================
// tb_write_path.sv -- B1: bypass, quantization, filtering, lane decode, latency.
// Stimulus and checks on negedge. A scoreboard checks every write in order.
//=============================================================================
module tb_write_path;
    import fecim_pkg::*;

    localparam int WR_LAT = 5;

    logic clk = 1'b0, rst_n = 1'b0;
    always #10 clk = ~clk;

    logic                       bulk_we = 1'b0;
    bulk_target_e               bulk_target = TGT_WEIGHTS;
    logic [15:0]                bulk_addr = '0;
    logic [7:0]                 bulk_data = '0;
    noise_en_t                  noise_en  = NOISE_NONE;
    logic [7:0]                 quant_levels = 8'd255;
    logic [15:0]                quant_mult   = 16'd257;
    logic [NUM_LANES-1:0]       wt_we;
    logic [ROW_AW+COL_AW-1:0]   wt_addr;
    logic signed [WEIGHT_W-1:0] wt_data;
    logic                       busy;

    write_path dut (.*);

    int errors = 0;

    //--- Reference (mirrors RTL arithmetic; structural checks below don't) --
    function automatic int mult_of(int n);
        return (65280 + (n-1)/2) / (n-1);     // round(255*256/(N-1)); never a tie
    endfunction

    function automatic int ref_w(int w, int n, int mult, bit q);
        int lvl;
        if (!q) return w;
        lvl = ((w + 128) * n) >> 8;
        return (((lvl * mult) + 128) >> 8) - 128;
    endfunction

    //--- Scoreboard ---------------------------------------------------------
    int exp_addr_q[$], exp_data_q[$];
    int obs[int];                              // addr -> stored weight

    always @(negedge clk) if (rst_n && |wt_we) begin
        automatic int col  = int'(wt_addr) / TILE_ROWS;
        automatic int lane = col % NUM_LANES;
        if (exp_addr_q.size() == 0) begin
            $display("ERROR unexpected write addr %0d", wt_addr); errors++;
        end else begin
            automatic int ea = exp_addr_q.pop_front();
            automatic int ed = exp_data_q.pop_front();
            if (int'(wt_addr) != ea || int'(wt_data) != ed) begin
                $display("ERROR write: got addr %0d data %0d, exp addr %0d data %0d",
                         wt_addr, wt_data, ea, ed); errors++;
            end
            if (wt_we !== (NUM_LANES'(1) << lane)) begin
                $display("ERROR lane decode: addr %0d col %0d, wt_we %h", wt_addr, col, wt_we);
                errors++;
            end
            obs[int'(wt_addr)] = int'(wt_data);
        end
    end

    //--- Stimulus helpers ---------------------------------------------------
    // Drives one write for one cycle; consecutive calls are back-to-back.
    task automatic write(int addr, int w, bit expect_it = 1);
        bulk_we = 1; bulk_addr = 16'(addr); bulk_data = 8'(w);
        if (expect_it) begin
            exp_addr_q.push_back(addr);
            exp_data_q.push_back(ref_w(w, quant_levels, quant_mult, noise_en.quant));
        end
        @(negedge clk);
    endtask

    task automatic drain();
        bulk_we = 0; bulk_target = TGT_WEIGHTS;
        repeat (WR_LAT + 2) @(negedge clk);
        if (exp_addr_q.size() != 0) begin
            $display("ERROR %0d expected writes never arrived", exp_addr_q.size()); errors++;
            exp_addr_q.delete(); exp_data_q.delete();
        end
        if (busy) begin $display("ERROR busy stuck high"); errors++; end
    endtask

    // Write all 256 int8 values to addresses 0..255 (column 0 and 1)
    task automatic write_all();
        obs.delete();
        for (int i = 0; i < 256; i++) write(i, i - 128);
        drain();
    endtask

    //--- Independent structural checks on a quantized sweep -----------------
    task automatic check_levels(int n);
        int lv[$];
        int dmin = 1 << 30, dmax = 0;
        for (int i = 0; i < 256; i++) begin
            if (!obs.exists(i)) begin $display("ERROR N=%0d: addr %0d missing", n, i); errors++; return; end
            if (i > 0 && obs[i] < obs[i-1]) begin
                $display("ERROR N=%0d: not monotonic at %0d", n, i); errors++;
            end
            if (lv.size() == 0 || obs[i] != lv[$]) lv.push_back(obs[i]);
        end
        for (int k = 1; k < lv.size(); k++) begin
            dmin = (lv[k]-lv[k-1] < dmin) ? lv[k]-lv[k-1] : dmin;
            dmax = (lv[k]-lv[k-1] > dmax) ? lv[k]-lv[k-1] : dmax;
        end
        if (lv.size() != n)  begin $display("ERROR N=%0d: %0d distinct levels", n, lv.size()); errors++; end
        if (lv[0]   != -128) begin $display("ERROR N=%0d: bottom level %0d", n, lv[0]);         errors++; end
        if (lv[$]   !=  127) begin $display("ERROR N=%0d: top level %0d", n, lv[$]);            errors++; end
        if (n > 2 && dmax - dmin > 1) begin
            $display("ERROR N=%0d: uneven spacing %0d..%0d", n, dmin, dmax); errors++;
        end
    endtask

    initial begin
        repeat (3) @(negedge clk);
        rst_n = 1;
        @(negedge clk);

        // 1. Bypass: exact pass-through even with a coarse N configured
        quant_levels = 8'd2; quant_mult = 16'(mult_of(2));
        write_all();
        for (int i = 0; i < 256; i++)
            if (obs[i] != i - 128) begin $display("ERROR bypass at %0d", i); errors++; end

        // 2. Non-write-path enables (read, IR, ADC) must not touch weights
        noise_en = NOISE_NONE; noise_en.read = 1; noise_en.ir = 1; noise_en.adc = 1;
        write_all();
        for (int i = 0; i < 256; i++)
            if (obs[i] != i - 128) begin $display("ERROR non-wp enable at %0d", i); errors++; end

        // 3. Quantization: N = 3 and 5 exercise the reciprocal path;
        //    10 and 255 are where an unrounded shift misses +127
        noise_en = NOISE_NONE; noise_en.quant = 1;
        foreach (int'{2, 3, 5, 6, 8, 10, 16, 255}[k]) ;   // (list below)
        begin
            int ns[8] = '{2, 3, 5, 6, 8, 10, 16, 255};
            foreach (ns[k]) begin
                quant_levels = 8'(ns[k]); quant_mult = 16'(mult_of(ns[k]));
                write_all();
                check_levels(ns[k]);
            end
        end
        noise_en = NOISE_NONE;

        // 4. Only TGT_WEIGHTS is written
        bulk_target = TGT_ACT;   write(10, 5, 0);
        bulk_target = TGT_ATTEN; write(11, 5, 0);
        drain();

        // 5. Out-of-range addresses are dropped
        write(TILE_ROWS*TILE_COLS, 1, 0);
        write(16'hFFFF, 1, 0);
        drain();

        // 6. Lane decode across lane and pass boundaries (scoreboard checks one-hot)
        foreach (int'{0, 1, 63, 64, 65, NUM_LANES*PASSES-1}[k]) ;
        begin
            int cols[6] = '{0, 1, NUM_LANES-1, NUM_LANES, NUM_LANES+1, TILE_COLS-1};
            foreach (cols[k]) write(cols[k]*TILE_ROWS + 5, k);
        end
        drain();

        // 7. Latency is exactly WR_LAT
        begin
            int n = 0;
            exp_addr_q.push_back(3); exp_data_q.push_back(7);
            bulk_we = 1; bulk_addr = 16'd3; bulk_data = 8'd7;
            do begin @(negedge clk); bulk_we = 0; n++; end while (!(|wt_we) && n < 20);
            if (n != WR_LAT) begin $display("ERROR latency %0d, expected %0d", n, WR_LAT); errors++; end
            drain();
        end

        if (errors == 0) begin $display("PASS tb_write_path"); $finish; end
        else $fatal(1, "FAIL tb_write_path: %0d errors", errors);
    end
endmodule : tb_write_path