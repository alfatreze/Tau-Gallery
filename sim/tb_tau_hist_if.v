// tb_tau_hist_if.v -- tau_hist_if (registers on clk_sys, crossings, tau_hist on clk_dec) driven through its register interface, with a pixel stream on an unrelated decoder clock.
// Expected values come from a plain behavioural model in this file (luma = (77R+150G+29B)>>8, half-open window on the canvas position). Prints "HISTIF PASSED" or "HISTIF FAILED".
// BUG selects tau_hist_if mutants (1..3) for the mutation sweep.
`timescale 1ns/10ps
module tb_tau_hist_if #(parameter BUG = 0);
    reg clk_sys = 0, clk_dec = 0, rst = 1;
    always #7.3 clk_sys = ~clk_sys;
    always #10.9 clk_dec = ~clk_dec;
    reg [9:0] a = 0; reg we = 0; reg [31:0] wd = 0; wire hit; wire [31:0] rdat;
    reg pv = 0; reg [6:0] px = 0, py = 0; reg [7:0] pr = 0, pg = 0, pb = 0;
    tau_hist_if #(.BUG(BUG)) dut (.clk_sys(clk_sys), .rst_sys(rst), .reg_addr(a), .reg_we(we), .reg_wdata(wd), .reg_hit(hit), .reg_rdata(rdat),
        .clk_dec(clk_dec), .px_valid(pv), .px_x(px), .px_y(py), .px_r(pr), .px_g(pg), .px_b(pb));

    integer errors = 0, i, j, k, round;
    integer m [0:1023]; integer mn;
    integer en_m, ox, oy, x0, y0, x1, y1;
    reg [31:0] v;

    task wr(input [9:0] ad, input [31:0] d); begin @(posedge clk_sys); #1 a = ad; wd = d; we = 1; @(posedge clk_sys); #1 we = 0; end endtask
    task rd(input [9:0] ad); begin @(posedge clk_sys); #1 a = ad; #2 v = rdat; end endtask
    task fail(input [255:0] msg); begin if (errors < 10) $display("FAIL: %0s", msg); errors = errors + 1; end endtask

    task clear_all; integer g; begin
        wr(10'h260, {30'd0, 1'b1, en_m[0]});
        rd(10'h260); if (v[8] !== 1'b1) fail("busy not high right after a clear");
        g = 0; rd(10'h260); while (v[8] && g < 2000) begin rd(10'h260); g = g + 1; end
        if (g >= 2000) fail("clear never finished");
        for (k = 0; k < 1024; k = k + 1) m[k] = 0; mn = 0;
    end endtask
    task setcfg; begin
        wr(10'h264, {6'd0, y0[9:0], 6'd0, x0[9:0]}); wr(10'h268, {6'd0, y1[9:0], 6'd0, x1[9:0]}); wr(10'h26C, {6'd0, oy[9:0], 6'd0, ox[9:0]});
        wr(10'h260, {30'd0, 1'b0, en_m[0]});
        rd(10'h264); if (v !== {6'd0, y0[9:0], 6'd0, x0[9:0]}) fail("RECT0 readback");
        rd(10'h268); if (v !== {6'd0, y1[9:0], 6'd0, x1[9:0]}) fail("RECT1 readback");
        rd(10'h26C); if (v !== {6'd0, oy[9:0], 6'd0, ox[9:0]}) fail("ORG readback");
        rd(10'h260); if (v[0] !== en_m[0]) fail("CTRL enable readback");
        repeat (12) @(posedge clk_dec);                                    // the quasi-static configuration crosses before the first pixel
    end endtask
    task feed(input integer npix, input integer mode); integer n, cx, cy, l; begin
        for (n = 0; n < npix; n = n + 1) begin
            @(posedge clk_dec); #1;
            px = $urandom % 128; py = $urandom % 128; pr = $urandom; pg = $urandom; pb = $urandom;
            if (mode == 1) begin pr = 8'd77; pg = 8'd77; pb = 8'd77; end
            pv = (mode == 2) ? (($urandom % 4) != 0) : 1'b1;
            if (pv) begin
                cx = ox + px; cy = oy + py;
                if (en_m && cx >= x0 && cx < x1 && cy >= y0 && cy < y1) begin
                    l = (77 * pr + 150 * pg + 29 * pb) >> 8;
                    m[l] = m[l] + 1; m[256 + pr] = m[256 + pr] + 1; m[512 + pg] = m[512 + pg] + 1; m[768 + pb] = m[768 + pb] + 1; mn = mn + 1;
                end
            end
        end
        @(posedge clk_dec); #1 pv = 0; repeat (8) @(posedge clk_dec);
    end endtask
    task check_all; integer g, bad; begin
        bad = 0;
        for (k = 0; k < 1024; k = k + 1) begin
            wr(10'h270, {22'd0, k[9:0]});
            rd(10'h274); if (v[31] !== 1'b1) begin fail("DATA not busy right after an index write"); k = 1024; end
            else begin
                g = 0; rd(10'h274); while (v[31] && g < 400) begin rd(10'h274); g = g + 1; end
                if (g >= 400) begin fail("read never finished"); k = 1024; end
                else if (v[23:0] !== m[k]) begin bad = bad + 1; if (bad < 4) $display("   bin %0d got %0d expected %0d", k, v[23:0], m[k]); end
            end
        end
        if (bad) fail("bins differ from the model");
        rd(10'h278); if (v[23:0] !== mn) begin fail("pixel count"); $display("   N got %0d expected %0d", v[23:0], mn); end
    end endtask

    initial begin
        for (k = 0; k < 1024; k = k + 1) m[k] = 0; mn = 0;
        en_m = 1; ox = 0; oy = 0; x0 = 0; y0 = 0; x1 = 800; y1 = 720;
        repeat (6) @(posedge clk_sys); #1 rst = 0;
        clear_all; setcfg;
        for (round = 0; round < 6; round = round + 1) begin
            ox = 128 * (round % 3); oy = 128 * (round / 3); setcfg;
            feed(2500, (round == 2) ? 1 : (round == 4 ? 2 : 0));
            check_all;
            if (round == 1) begin x0 = 100; y0 = 40; x1 = 300; y1 = 200; setcfg; end         // a window through the tiles
            if (round == 3) begin clear_all; end
            if (round == 4) begin en_m = 0; setcfg; end                                      // enable off: nothing counts
            if (round == 5) begin en_m = 1; setcfg; end
        end
        $display("%s  BUG=%0d: %0d errors", errors == 0 ? "HISTIF PASSED" : "HISTIF FAILED", BUG, errors);
        $finish;
    end
    initial begin #400000000; $display("TIMEOUT HISTIF FAILED BUG=%0d", BUG); $finish; end
endmodule
