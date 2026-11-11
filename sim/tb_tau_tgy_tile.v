// Testbench of src/fpga/core/tau_tgy_tile.sv: a behavioural entropy decoder (this file) answers the controller's block requests from the vector file (coefficients in zigzag order, the DC first, nonzero ACs after, random gaps),
// CHECKING each request (component, block column, left/upper exist) against the expected block order; the controller's RGB stream must equal the Python reference for every tile (sim/gen_tgy_tile_vectors.py).
// Extra scenarios after the vectors: a decoder that never answers (the watchdog must raise err, no hang) and a decoder error mid-tile (err, back to idle, next tile still correct).
// Run: iverilog -g2012 -P tb_tau_tgy_tile.BUG=n -P tb_tau_tgy_tile.C444=c ...; vvp ... +VEC=file.hex +N=count
`timescale 1ns/1ps
module tb_tau_tgy_tile;
    parameter integer BUG = 0;
    parameter integer C444 = 0;
    localparam integer OFF_EXP = 2 + 32 + 768 * 17, SLOT = OFF_EXP + 3 * 2048;
    reg clk = 0, rst = 1; always #5 clk = ~clk;
    reg qwe = 0, qsel = 0; reg [5:0] qidx = 0; reg [15:0] qval = 0;
    reg start = 0; reg [1:0] fmt = 0; reg [7:0] tw = 0, th = 0; reg hold = 0;
    wire busy, done, err, dec_tile, dec_blk, dec_abort, dec_left, dec_above; wire [1:0] dec_comp; wire [3:0] dec_bx;
    reg c_we = 0; reg [5:0] c_zz = 0; reg signed [15:0] c_val = 0; reg blk_done = 0, dec_err = 0;
    wire px_valid, px_last; wire [6:0] px_x, px_y; wire [7:0] px_r, px_g, px_b;
    tau_tgy_tile #(.C444(C444), .BUG(BUG)) dut (.clk(clk), .rst(rst), .qwe(qwe), .qsel(qsel), .qidx(qidx), .qval(qval), .start(start), .fmt(fmt), .tw(tw), .th(th), .busy(busy), .done(done), .err(err),
        .dec_tile(dec_tile), .dec_blk(dec_blk), .dec_abort(dec_abort), .dec_comp(dec_comp), .dec_bx(dec_bx), .dec_left(dec_left), .dec_above(dec_above),
        .c_we(c_we), .c_zz(c_zz), .c_val(c_val), .blk_done(blk_done), .dec_err(dec_err), .hold(hold), .px_valid(px_valid), .px_x(px_x), .px_y(px_y), .px_r(px_r), .px_g(px_g), .px_b(px_b), .px_last(px_last));
    reg [63:0] mem [0:SLOT * 40];
    reg [8*200-1:0] vecfile; integer nt, t, errors = 0, i, base, npx, nlast, cyc, f, w, h, bad_order, errs_seen, dones_seen, skipped = 0, ran = 0, nblk, bi, hdr_bad;
    integer exp_x, exp_y; reg [31:0] lfsr = 32'h7654321; reg hold_en;
    reg mode_stall = 0, mode_err = 0;
    always @(posedge clk) lfsr <= {lfsr[30:0], lfsr[31] ^ lfsr[21] ^ lfsr[1] ^ lfsr[0]};
    always @(posedge clk) if (hold_en) hold <= lfsr[3] & lfsr[7]; else hold <= 0;
    function [7:0] expbyte(input integer plane, input integer y, input integer x);
        reg [63:0] wd; begin wd = mem[base + OFF_EXP + plane * 2048 + y * 16 + x / 8]; expbyte = wd[8 * (x % 8) +: 8]; end
    endfunction
    function signed [15:0] coef(input integer blk, input integer k);
        reg [63:0] wd; begin wd = mem[base + 34 + blk * 17 + 1 + k / 4]; coef = wd[16 * (k % 4) +: 16]; end
    endfunction
    // ---- behavioural entropy decoder
    reg emitting = 0; integer ci, gap, tail, cur_blk, la; integer sent_in_blk; reg tile_seen = 0;
    always @(posedge clk) begin
        c_we <= 0; blk_done <= 0; dec_err <= 0;
        if (dec_tile) begin bi = 0; tile_seen <= 1; end
        if (dec_blk) begin
            reg [63:0] hd; hd = mem[base + 34 + bi * 17];
            if (bi >= nblk || dec_comp !== hd[17:16] || dec_bx !== hd[11:8] || dec_left !== hd[1] || dec_above !== hd[0]) begin
                hdr_bad = hdr_bad + 1; if (hdr_bad < 4) $display("test %0d block %0d: request comp %0d bx %0d left %0d above %0d, expected comp %0d bx %0d left %0d above %0d", t, bi, dec_comp, dec_bx, dec_left, dec_above, hd[17:16], hd[11:8], hd[1], hd[0]);
            end
            cur_blk = bi; bi = bi + 1; emitting <= !mode_stall; ci = 0; gap = lfsr[1:0]; tail = 0; sent_in_blk = 0;
        end else if (emitting) begin
            if (gap > 0) gap = gap - 1;
            else if (ci < 64) begin
                while (ci < 64 && ci != 0 && coef(cur_blk, ci) == 0) ci = ci + 1;
                if (ci < 64) begin
                    c_we <= 1; c_zz <= ci; c_val <= coef(cur_blk, ci); ci = ci + 1; gap = lfsr[2:1]; sent_in_blk = sent_in_blk + 1;
                    if (mode_err && cur_blk == 3 && sent_in_blk == 2) begin dec_err <= 1; emitting <= 0; end
                    la = ci; while (la < 64 && coef(cur_blk, la) == 0) la = la + 1;
                    if (la >= 64 && lfsr[4] && !mode_err) begin blk_done <= 1; emitting <= 0; tail = 0; end      // blk_done in the same clock as the last coefficient
                end else begin emitting <= 0; tail = 1 + lfsr[3:2]; end
            end else begin emitting <= 0; tail = 1 + lfsr[3:2]; end
        end
        if (!emitting && tail > 0) begin tail = tail - 1; if (tail == 0) blk_done <= 1; end
    end
    always @(posedge clk) begin
        if (px_valid) begin
            if (px_x !== exp_x[6:0] || px_y !== exp_y[6:0]) begin bad_order = bad_order + 1; if (bad_order < 3) $display("test %0d: pixel order got (%0d,%0d) want (%0d,%0d)", t, px_x, px_y, exp_x, exp_y); end
            if (px_r !== expbyte(0, px_y, px_x) || px_g !== expbyte(1, px_y, px_x) || px_b !== expbyte(2, px_y, px_x)) begin
                errors = errors + 1; if (errors < 6) $display("test %0d fmt %0d %0dx%0d pixel (%0d,%0d): got %0d %0d %0d want %0d %0d %0d", t, f, w, h, px_x, px_y, px_r, px_g, px_b, expbyte(0, px_y, px_x), expbyte(1, px_y, px_x), expbyte(2, px_y, px_x));
            end
            npx = npx + 1; if (px_last) nlast = nlast + 1;
            if (exp_x == w - 1) begin exp_x = 0; exp_y = exp_y + 1; end else exp_x = exp_x + 1;
        end
        if (err) errs_seen = errs_seen + 1;
        if (done) dones_seen = dones_seen + 1;
    end
    task load_tables; integer j; reg [63:0] wd; begin
        for (j = 0; j < 128; j = j + 1) begin wd = mem[base + 2 + j / 4]; qwe <= 1; qsel <= j / 64; qidx <= j % 64; qval <= wd[16 * (j % 4) +: 16]; @(posedge clk); end
        qwe <= 0;
    end endtask
    task run_tile(input integer maxcyc); begin
        npx = 0; nlast = 0; exp_x = 0; exp_y = 0; bad_order = 0; errs_seen = 0; dones_seen = 0; hdr_bad = 0;
        fmt <= f; tw <= w; th <= h; start <= 1; @(posedge clk); start <= 0; cyc = 0;
        while (!done && !err && cyc < maxcyc) begin @(posedge clk); cyc = cyc + 1; end
        repeat (4) @(posedge clk);
    end endtask
    initial begin
        if (!$value$plusargs("VEC=%s", vecfile)) begin $display("no +VEC"); $finish; end
        if (!$value$plusargs("N=%d", nt)) nt = 10;
        $readmemh(vecfile, mem);
        repeat (4) @(posedge clk); rst <= 0; repeat (2) @(posedge clk);
        for (t = 0; t < nt; t = t + 1) begin
            base = t * SLOT; f = mem[base][17:16]; w = mem[base][15:8]; h = mem[base][7:0]; nblk = mem[base + 1];
            if (f == 1 && C444 == 0) begin
                npx = 0; errs_seen = 0; fmt <= f; tw <= w; th <= h; start <= 1; @(posedge clk); start <= 0; repeat (20) @(posedge clk);
                if (errs_seen != 1 || busy) begin errors = errors + 1; $display("test %0d: 4:4:4 without C444 not refused", t); end
                skipped = skipped + 1;
            end else begin
                ran = ran + 1; load_tables; hold_en = (t % 2) == 1;
                run_tile(3000000);
                if (cyc >= 3000000 || npx != w * h || nlast != 1 || bad_order != 0 || errs_seen != 0 || dones_seen != 1 || hdr_bad != 0 || busy) begin
                    errors = errors + 1; $display("test %0d fmt %0d %0dx%0d: cyc %0d pixels %0d (want %0d) last %0d order %0d err %0d done %0d request errors %0d", t, f, w, h, cyc, npx, w * h, nlast, bad_order, errs_seen, dones_seen, hdr_bad);
                end
                if (t == 0) $display("tile 0 (%0d blocks): %0d clocks to the end of the pixel stream (hold off)", nblk, cyc);
            end
        end
        // watchdog: the decoder never answers
        base = 0; f = mem[0][17:16]; w = mem[0][15:8]; h = mem[0][7:0]; nblk = mem[1]; hold_en = 0; load_tables; mode_stall = 1;
        run_tile(400000);
        if (errs_seen != 1 || dones_seen != 0 || busy || npx != 0) begin errors = errors + 1; $display("watchdog: err %0d done %0d busy %0d pixels %0d (cyc %0d)", errs_seen, dones_seen, busy, npx, cyc); end else $display("watchdog raised err after %0d clocks", cyc);
        mode_stall = 0;
        // decoder error inside block 3, then a good tile
        mode_err = 1; run_tile(400000); mode_err = 0;
        if (errs_seen != 1 || dones_seen != 0 || busy) begin errors = errors + 1; $display("decoder error: err %0d done %0d busy %0d", errs_seen, dones_seen, busy); end else $display("decoder error: err raised, idle again");
        run_tile(3000000);
        if (npx != w * h || errs_seen != 0 || dones_seen != 1 || bad_order != 0) begin errors = errors + 1; $display("tile after an error is wrong (pixels %0d)", npx); end
        if (errors == 0) $display("TILE PASSED (0 failures)"); else $display("TILE FAILED %0d", errors);
        $finish;
    end
endmodule
