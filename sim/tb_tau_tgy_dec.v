// End to end testbench of src/fpga/core/tau_tgy_dec.sv: a TGY tile STREAM (the frozen symbol layer vectors, real photo tiles included) goes in as 32-bit words with random gaps, the RGB pixel stream is compared with the
// Python reference (libjpeg IDCT, tile-local fancy upsampling, libjpeg colour). Half of the tiles run with `hold` toggling at random. Reports the clocks per tile.
// Run: iverilog -g2012 -P tb_tau_tgy_dec.C444=c ...; vvp ... +VEC=file.hex +N=count
`timescale 1ns/1ps
module tb_tau_tgy_dec;
    parameter integer C444 = 1;
    localparam integer STREAM_WORDS = 3300, OFF_STREAM = 34, OFF_EXP = 34 + STREAM_WORDS, SLOT = OFF_EXP + 3 * 2048;
    reg clk = 0, rst = 1; always #5 clk = ~clk;
    reg qwe = 0, qsel = 0; reg [5:0] qidx = 0; reg [15:0] qval = 0; reg start = 0; reg [1:0] fmt = 0; reg [7:0] tw = 0, th = 0; reg [19:0] tile_len = 0;
    reg in_valid = 0; reg [31:0] in_word = 0; wire in_ready; reg hold = 0; wire busy, done, err, px_valid, px_last; wire [6:0] px_x, px_y; wire [7:0] px_r, px_g, px_b;
    tau_tgy_dec #(.C444(C444)) dut (.clk(clk), .rst(rst), .clk_q(clk), .qwe(qwe), .qsel(qsel), .qidx(qidx), .qval(qval), .start(start), .fmt(fmt), .tw(tw), .th(th), .tile_len(tile_len), .in_valid(in_valid), .in_word(in_word), .in_ready(in_ready),
        .hold(hold), .busy(busy), .done(done), .err(err), .px_valid(px_valid), .px_x(px_x), .px_y(px_y), .px_r(px_r), .px_g(px_g), .px_b(px_b), .px_last(px_last));
    reg [63:0] mem [0:SLOT * 12];
    reg [8*200-1:0] vecfile; integer nt, t, errors = 0, base, f, w, h, len, npx, nlast, cyc, bad_order, errs_seen, dones_seen, nw, wi, exp_x, exp_y, skipped = 0, ran = 0, j; reg [31:0] lfsr = 32'h13579BDF; reg hold_en, feeding = 0; time t0;
    always @(posedge clk) lfsr <= {lfsr[30:0], lfsr[31] ^ lfsr[21] ^ lfsr[1] ^ lfsr[0]};
    always @(posedge clk) if (hold_en) hold <= lfsr[3] & lfsr[7]; else hold <= 0;
    always @(posedge clk) begin
        if (feeding) begin
            if (in_valid && in_ready) begin in_valid <= 0; wi = wi + 1; end
            else if (!in_valid && wi < nw && lfsr[2:0] != 3'd0) begin in_valid <= 1; in_word <= (wi % 2 == 0) ? mem[base + OFF_STREAM + wi / 2][31:0] : mem[base + OFF_STREAM + wi / 2][63:32]; end
        end else in_valid <= 0;
    end
    function [7:0] expbyte(input integer plane, input integer y, input integer x);
        reg [63:0] wd; begin wd = mem[base + OFF_EXP + plane * 2048 + y * 16 + x / 8]; expbyte = wd[8 * (x % 8) +: 8]; end
    endfunction
    always @(posedge clk) begin
        if (px_valid) begin
            if (px_x !== exp_x[6:0] || px_y !== exp_y[6:0]) bad_order = bad_order + 1;
            if (px_r !== expbyte(0, px_y, px_x) || px_g !== expbyte(1, px_y, px_x) || px_b !== expbyte(2, px_y, px_x)) begin
                errors = errors + 1; if (errors < 6) $display("test %0d fmt %0d %0dx%0d pixel (%0d,%0d): got %0d %0d %0d want %0d %0d %0d", t, f, w, h, px_x, px_y, px_r, px_g, px_b, expbyte(0, px_y, px_x), expbyte(1, px_y, px_x), expbyte(2, px_y, px_x));
            end
            npx = npx + 1; if (px_last) nlast = nlast + 1;
            if (exp_x == w - 1) begin exp_x = 0; exp_y = exp_y + 1; end else exp_x = exp_x + 1;
        end
        if (err) errs_seen = errs_seen + 1; if (done) dones_seen = dones_seen + 1;
    end
    initial begin
        if (!$value$plusargs("VEC=%s", vecfile)) begin $display("no +VEC"); $finish; end
        if (!$value$plusargs("N=%d", nt)) nt = 10;
        $readmemh(vecfile, mem);
        repeat (4) @(posedge clk); rst <= 0; repeat (2) @(posedge clk);
        for (t = 0; t < nt; t = t + 1) begin
            base = t * SLOT; f = mem[base][17:16]; w = mem[base][15:8]; h = mem[base][7:0]; len = mem[base + 1];
            if (f == 1 && C444 == 0) begin skipped = skipped + 1; end
            else begin
                ran = ran + 1;
                for (j = 0; j < 128; j = j + 1) begin qwe <= 1; qsel <= j / 64; qidx <= j % 64; qval <= mem[base + 2 + j / 4][16 * (j % 4) +: 16]; @(posedge clk); end qwe <= 0;
                hold_en = (t % 2) == 1; npx = 0; nlast = 0; exp_x = 0; exp_y = 0; bad_order = 0; errs_seen = 0; dones_seen = 0; nw = (len + 3) / 4; wi = 0; feeding = 1; t0 = $time;
                fmt <= f; tw <= w; th <= h; tile_len <= len; start <= 1; @(posedge clk); start <= 0; cyc = 0;
                while (!done && !err && cyc < 4000000) begin @(posedge clk); cyc = cyc + 1; end
                repeat (4) @(posedge clk); feeding = 0;
                if (cyc >= 4000000 || npx != w * h || nlast != 1 || bad_order != 0 || errs_seen != 0 || dones_seen != 1 || busy) begin
                    errors = errors + 1; $display("test %0d fmt %0d %0dx%0d: cyc %0d pixels %0d (want %0d) last %0d order %0d err %0d done %0d", t, f, w, h, cyc, npx, w * h, nlast, bad_order, errs_seen, dones_seen);
                end
                if (len > 5000) $display("tile %0d (%0d bytes, %0dx%0d): %0d clocks from start to the last pixel%s", t, len, w, h, ($time - t0) / 10, hold_en ? " (with hold)" : "");
            end
        end
        $display("%0d tiles run, %0d skipped (4:4:4 without C444)", ran, skipped);
        if (errors == 0) $display("DEC PASSED (0 failures)"); else $display("DEC FAILED %0d", errors);
        $finish;
    end
endmodule
