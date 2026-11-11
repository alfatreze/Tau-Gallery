// Testbench of src/fpga/core/tau_tgy_recon.sv (colour conversion + tile-local chroma upsampling) against the Python reference: every vector of sim/gen_tgy_recon_rtl_vectors.py (whole tiles) must stream exactly
// the expected RGB pixels in raster order, one pixel per clock inside a row, with px_last once; half of the tests assert `hold` at random, which may only delay row starts. A mutant (-P BUG=n) must fail.
// Run: iverilog -g2012 -P tb_tau_tgy_recon.BUG=n -P tb_tau_tgy_recon.C444=c ...; vvp ... +VEC=file.hex +N=count
`timescale 1ns/1ps
module tb_tau_tgy_recon;
    parameter integer BUG = 0;
    parameter integer C444 = 0;
    reg clk = 0, rst = 1; always #5 clk = ~clk;
    reg we = 0; reg [1:0] wcomp = 0; reg [6:0] wx = 0, wy = 0; reg [63:0] wdata = 0;
    reg start = 0; reg [1:0] fmt = 0; reg [7:0] tw = 0, th = 0; reg hold = 0;
    wire busy, done, err, px_valid, px_last; wire [6:0] px_x, px_y; wire [7:0] px_r, px_g, px_b;
    tau_tgy_recon #(.C444(C444), .BUG(BUG)) dut (.clk(clk), .rst(rst), .we(we), .wcomp(wcomp), .wx(wx), .wy(wy), .wdata(wdata), .start(start), .fmt(fmt), .tw(tw), .th(th), .hold(hold),
        .busy(busy), .done(done), .err(err), .px_valid(px_valid), .px_x(px_x), .px_y(px_y), .px_r(px_r), .px_g(px_g), .px_b(px_b), .px_last(px_last));
    localparam integer SLOT = 1 + 6 * 2048;
    reg [63:0] mem [0:SLOT * 150];
    reg [8*200-1:0] vecfile; integer nt, t, errors = 0, i, r, c, base, npx, nlast, cyc, f, w, h, nrows, ncw, errs_seen, skipped = 0, ran = 0, bad_order;
    integer exp_x, exp_y; reg [31:0] lfsr = 32'h1234567; reg hold_en;
    always @(posedge clk) lfsr <= {lfsr[30:0], lfsr[31] ^ lfsr[21] ^ lfsr[1] ^ lfsr[0]};
    always @(posedge clk) if (hold_en) hold <= lfsr[3] & lfsr[7]; else hold <= 0;
    function [7:0] expbyte(input integer plane, input integer y, input integer x);   // plane 0 R, 1 G, 2 B
        reg [63:0] wd; begin wd = mem[base + 1 + (3 + plane) * 2048 + y * 16 + x / 8]; expbyte = wd[8 * (x % 8) +: 8]; end
    endfunction
    always @(posedge clk) begin
        if (px_valid) begin
            if (px_x !== exp_x[6:0] || px_y !== exp_y[6:0]) begin bad_order = bad_order + 1; if (errors < 5) $display("test %0d: pixel order got (%0d,%0d) want (%0d,%0d)", t, px_x, px_y, exp_x, exp_y); end
            if (px_r !== expbyte(0, px_y, px_x) || px_g !== expbyte(1, px_y, px_x) || px_b !== expbyte(2, px_y, px_x)) begin
                errors = errors + 1; if (errors < 6) $display("test %0d fmt %0d %0dx%0d pixel (%0d,%0d): got %0d %0d %0d want %0d %0d %0d", t, f, w, h, px_x, px_y, px_r, px_g, px_b, expbyte(0, px_y, px_x), expbyte(1, px_y, px_x), expbyte(2, px_y, px_x));
            end
            npx = npx + 1; if (px_last) nlast = nlast + 1;
            if (exp_x == w - 1) begin exp_x = 0; exp_y = exp_y + 1; end else exp_x = exp_x + 1;
        end
        if (err) errs_seen = errs_seen + 1;
    end
    task wr(input [1:0] comp, input integer row, input integer col8, input [63:0] d); begin
        we <= 1; wcomp <= comp; wy <= row; wx <= col8 * 8; wdata <= d; @(posedge clk); we <= 0;
    end endtask
    initial begin
        if (!$value$plusargs("VEC=%s", vecfile)) begin $display("no +VEC"); $finish; end
        if (!$value$plusargs("N=%d", nt)) nt = 10;
        $readmemh(vecfile, mem);
        repeat (4) @(posedge clk); rst <= 0; repeat (2) @(posedge clk);
        for (t = 0; t < nt; t = t + 1) begin
            base = t * SLOT; f = mem[base][17:16]; w = mem[base][15:8]; h = mem[base][7:0];
            nrows = (f == 1) ? 128 : 64; ncw = (f == 1) ? 16 : 8;
            if (f == 1 && C444 == 0) begin
                // unsupported: err must be raised and nothing streamed
                errs_seen = 0; npx = 0; fmt <= f; tw <= w; th <= h; start <= 1; @(posedge clk); start <= 0; repeat (20) @(posedge clk);
                if (errs_seen != 1 || npx != 0 || busy) begin errors = errors + 1; $display("test %0d: 4:4:4 without C444 not refused", t); end
                skipped = skipped + 1;
            end else begin
                ran = ran + 1;
                for (r = 0; r < 128; r = r + 1) for (c = 0; c < 16; c = c + 1) wr(0, r, c, mem[base + 1 + r * 16 + c]);
                if (f != 0) for (r = 0; r < nrows; r = r + 1) for (c = 0; c < ncw; c = c + 1) begin wr(1, r, c, mem[base + 1 + 2048 + r * 16 + c]); wr(2, r, c, mem[base + 1 + 4096 + r * 16 + c]); end
                hold_en = (t % 2) == 1; npx = 0; nlast = 0; exp_x = 0; exp_y = 0; bad_order = 0; errs_seen = 0;
                fmt <= f; tw <= w; th <= h; start <= 1; @(posedge clk); start <= 0; cyc = 0;
                while (!done && cyc < 80000) begin @(posedge clk); cyc = cyc + 1; end
                repeat (3) @(posedge clk);
                if (cyc >= 80000 || npx != w * h || nlast != 1 || bad_order != 0 || errs_seen != 0 || busy) begin
                    errors = errors + 1; $display("test %0d fmt %0d %0dx%0d: timeout/count: cyc %0d pixels %0d (want %0d) last %0d order errors %0d err %0d", t, f, w, h, cyc, npx, w * h, nlast, bad_order, errs_seen);
                end
                if (!hold_en && t == 0) $display("tile 0: %0d clocks for %0d pixels", cyc, npx);
            end
        end
        $display("%0d tiles run, %0d refused (4:4:4 without C444)", ran, skipped);
        if (errors == 0) $display("RECON PASSED (0 failures)"); else $display("RECON FAILED %0d", errors);
        $finish;
    end
endmodule
