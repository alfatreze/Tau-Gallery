// Testbench of src/fpga/core/tau_tgy_ent.sv (the entropy decoder) against the frozen symbol layer vectors: this file plays the tile controller. For every vector it starts the tile (dec_tile with the stream length),
// feeds the stream as 32-bit words with random gaps in in_valid, requests the blocks in coding order (dec_blk with component, block column, left/upper exist), collects the c_we events (the DC first, then the nonzero ACs in strictly
// increasing zigzag order) and compares every block with the expected 64 coefficients. Extra scenarios: a stream of 0xFF bytes (the Exp-Golomb exponent overflows: dec_err), an abort in mid-tile followed by a good tile.
// Run: iverilog -g2012 -P tb_tau_tgy_ent.BUG=n ...; vvp ... +VEC=file.hex +N=count
`timescale 1ns/1ps
module tb_tau_tgy_ent;
    parameter integer BUG = 0;
    localparam integer STREAM_WORDS = 3300, SLOT = 6 + STREAM_WORDS + 384 * 17;
    reg clk = 0, rst = 1; always #5 clk = ~clk;
    reg dec_tile = 0; reg [19:0] tile_len = 0; reg in_valid = 0; reg [31:0] in_word = 0; wire in_ready;
    reg dec_blk = 0; reg [1:0] dec_comp = 0; reg [3:0] dec_bx = 0; reg dec_left = 0, dec_above = 0, dec_abort = 0;
    wire c_we; wire [5:0] c_zz; wire signed [15:0] c_val; wire blk_done, dec_err;
    tau_tgy_ent #(.BUG(BUG)) dut (.clk(clk), .rst(rst), .dec_tile(dec_tile), .tile_len(tile_len), .in_valid(in_valid), .in_word(in_word), .in_ready(in_ready), .dec_blk(dec_blk), .dec_comp(dec_comp), .dec_bx(dec_bx),
        .dec_left(dec_left), .dec_above(dec_above), .dec_abort(dec_abort), .c_we(c_we), .c_zz(c_zz), .c_val(c_val), .blk_done(blk_done), .dec_err(dec_err));
    reg [63:0] mem [0:SLOT * 20];
    reg [8*200-1:0] vecfile; integer nt, t, errors = 0, i, base, blk, nblk, nw, wi, len, cyc, order_bad, zz_prev, nevents;
    reg signed [15:0] got [0:63]; reg [31:0] lfsr = 32'hCAFE1234; reg feeding = 0, stream_ff = 0; integer errs_seen;
    always @(posedge clk) lfsr <= {lfsr[30:0], lfsr[31] ^ lfsr[21] ^ lfsr[1] ^ lfsr[0]};
    // stream feeder
    always @(posedge clk) begin
        if (feeding) begin
            if (in_valid && in_ready) begin in_valid <= 0; wi = wi + 1; end
            else if (!in_valid && wi < nw && lfsr[2:0] != 3'd0) begin
                in_valid <= 1;
                in_word <= stream_ff ? 32'hFFFFFFFF : ((wi % 2 == 0) ? mem[base + 6 + wi / 2][31:0] : mem[base + 6 + wi / 2][63:32]);
            end
        end else in_valid <= 0;
        if (dec_err) errs_seen = errs_seen + 1;
    end
    // block collector
    always @(posedge clk) if (c_we) begin
        if (c_zz != 0 && c_zz <= zz_prev) order_bad = order_bad + 1;
        if (nevents == 0 && c_zz != 0) order_bad = order_bad + 1;
        zz_prev = c_zz; got[c_zz] = c_val; nevents = nevents + 1;
    end
    task start_tile(input integer slen); begin
        nw = (slen + 3) / 4; wi = 0; feeding = 1; dec_tile <= 1; tile_len <= slen; @(posedge clk); dec_tile <= 0;
    end endtask
    task request(input [1:0] comp, input [3:0] bx, input l, input a); begin
        for (i = 0; i < 64; i = i + 1) got[i] = 0; zz_prev = 0; nevents = 0; order_bad = 0;
        dec_blk <= 1; dec_comp <= comp; dec_bx <= bx; dec_left <= l; dec_above <= a; @(posedge clk); dec_blk <= 0; cyc = 0;
        while (!blk_done && !dec_err && cyc < 400000) begin @(posedge clk); cyc = cyc + 1; end
    end endtask
    reg [63:0] hd, wd; integer bi_i, k2; time t_start;
    initial begin
        if (!$value$plusargs("VEC=%s", vecfile)) begin $display("no +VEC"); $finish; end
        if (!$value$plusargs("N=%d", nt)) nt = 10;
        $readmemh(vecfile, mem);
        repeat (4) @(posedge clk); rst <= 0; repeat (2) @(posedge clk);
        for (t = 0; t < nt; t = t + 1) begin
            base = t * SLOT; len = mem[base]; nblk = mem[base + 1]; errs_seen = 0;
            t_start = $time; start_tile(len);
            for (blk = 0; blk < nblk; blk = blk + 1) begin
                hd = mem[base + 6 + STREAM_WORDS + blk * 17];
                request(hd[17:16], hd[11:8], hd[1], hd[0]);
                if (cyc >= 400000 || dec_err) begin errors = errors + 1; $display("test %0d block %0d: timeout or error", t, blk); blk = nblk; end
                else begin
                    if (order_bad != 0) begin errors = errors + 1; if (errors < 6) $display("test %0d block %0d: coefficient events out of order", t, blk); end
                    for (k2 = 0; k2 < 64; k2 = k2 + 1) begin
                        wd = mem[base + 6 + STREAM_WORDS + blk * 17 + 1 + k2 / 4];
                        if (got[k2] !== $signed(wd[16 * (k2 % 4) +: 16])) begin errors = errors + 1; if (errors < 6) $display("test %0d block %0d coef %0d: got %0d want %0d", t, blk, k2, got[k2], $signed(wd[16 * (k2 % 4) +: 16])); k2 = 64; end
                    end
                end
            end
            feeding = 0; repeat (4) @(posedge clk);
            if (t == 7 || t == 8) $display("real tile (vector %0d): %0d bytes, %0d blocks, %0d clocks (%0d clocks per stream byte)", t, len, nblk, ($time - t_start) / 10, ($time - t_start) / 10 / len);
        end
        // exponent overflow: a stream of 0xFF bytes
        errs_seen = 0; stream_ff = 1; start_tile(64); request(0, 0, 0, 0); feeding = 0; stream_ff = 0; repeat (4) @(posedge clk);
        if (errs_seen != 1) begin errors = errors + 1; $display("exponent overflow not reported (err pulses %0d)", errs_seen); end else $display("exponent overflow reported");
        // abort in mid-tile, then a good tile (test 2: all zero 8x8)
        base = 2 * SLOT; len = mem[base]; nblk = mem[base + 1]; start_tile(len); hd = mem[base + 6 + STREAM_WORDS]; request(hd[17:16], hd[11:8], hd[1], hd[0]);
        dec_abort <= 1; @(posedge clk); dec_abort <= 0; feeding = 0; repeat (4) @(posedge clk);
        errs_seen = 0; start_tile(len);
        for (blk = 0; blk < nblk; blk = blk + 1) begin
            hd = mem[base + 6 + STREAM_WORDS + blk * 17]; request(hd[17:16], hd[11:8], hd[1], hd[0]);
            if (cyc >= 400000 || dec_err || got[0] !== 0) begin errors = errors + 1; $display("tile after an abort wrong at block %0d", blk); blk = nblk; end
        end
        feeding = 0;
        if (errors == 0) $display("ENT PASSED (0 failures)"); else $display("ENT FAILED %0d", errors);
        $finish;
    end
endmodule
