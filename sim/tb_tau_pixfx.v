// tb_tau_pixfx.v -- tau_pixfx against the vectors of sim/test_rtl_pixfx.py (an independent Python model).
// Command file, one line per clk_vid cycle: "r g b x y f flags ev a b c"
//   r g b = the input pixel, x y = its position (low 3 bits), f = the frame phase, flags = {vs, hs, de};
//   ev 0 = nothing, 1 = a colour table store (a channel, b index, c value) from clk_sys, 2 = set the mode a and lut_on b.
// Events are issued from the clk_sys side and run on their own while the stream goes on (the model only places them in blanking, spaced apart).
// Expected file, one line per cycle: "r g b flags" of the output that belongs to that cycle's input (the pipeline latency of 2 is added here).
// The flags are compared on EVERY cycle, the colour only where de is set. Prints PASS or FAILED with BUG and the error count; -Ptb_tau_pixfx.BUG=n for the mutants.
`timescale 1ns/10ps
module tb_tau_pixfx #(parameter BUG = 0, parameter CMD = "build/rtl/pixfx_cmd.txt", parameter EXP = "build/rtl/pixfx_exp.txt");
    reg clk_sys = 0, clk_vid = 0, reset = 1;
    always #7.5 clk_sys = ~clk_sys;
    always #12.5 clk_vid = ~clk_vid;

    reg [1:0] mode = 0; reg lut_on = 0; reg lut_we = 0; reg [17:0] lut_word = 0; wire lut_busy;
    reg [23:0] px_in = 0; reg de_in = 0, hs_in = 0, vs_in = 0; reg [2:0] x_in = 0, y_in = 0; reg [1:0] f_in = 0;
    wire [23:0] px_out; wire de_out, hs_out, vs_out;

    tau_pixfx #(.BUG(BUG)) dut (.clk_sys(clk_sys), .clk_vid(clk_vid), .reset(reset), .mode(mode), .lut_on(lut_on), .lut_we(lut_we), .lut_word(lut_word), .lut_busy(lut_busy),
        .px_in(px_in), .de_in(de_in), .hs_in(hs_in), .vs_in(vs_in), .x_in(x_in), .y_in(y_in), .f_in(f_in), .px_out(px_out), .de_out(de_out), .hs_out(hs_out), .vs_out(vs_out));

    integer fc, fe, errors = 0, n = 0, nde = 0, stores = 0, rr, r, g, b, x, y, f, fl, ev, ea, eb, ec;
    integer er, eg, ebb, ef;
    reg [31:0] expq [0:3];                  // the expected {flags, r, g, b} of the last four inputs
    reg done = 0;

    task fail(input [255:0] msg); begin if (errors < 10) $display("FAIL: %0s (cycle %0d)", msg, n); errors = errors + 1; end endtask

    task do_store(input integer ch, input integer idx, input integer val); integer guard; begin
        @(posedge clk_sys); lut_word <= {ch[1:0], idx[7:0], val[7:0]}; lut_we <= 1; @(posedge clk_sys); lut_we <= 0; stores = stores + 1;
        guard = 0; @(posedge clk_sys); while (lut_busy && guard < 400) begin @(posedge clk_sys); guard = guard + 1; end
        if (guard >= 400) fail("lut_busy never fell");
    end endtask
    task do_mode(input integer m, input integer l); begin @(posedge clk_sys); mode <= m[1:0]; lut_on <= l[0]; end endtask

    reg [31:0] q;
    always @(negedge clk_vid) if (!reset && !done) begin
        // the output now belongs to the input driven two cycles ago
        if (n >= 2) begin
            q = expq[(n - 2) % 4];
            if ({de_out, hs_out, vs_out} !== {q[24], q[25], q[26]}) fail("a flag (de, hs, vs) is not the input delayed by two cycles");
            else if (q[24] && px_out !== q[23:0]) begin fail("a pixel differs from the model"); if (errors < 6) $display("   got %06x expected %06x", px_out, q[23:0]); end
            if (q[24]) nde = nde + 1;
        end
        rr = $fscanf(fc, "%d %d %d %d %d %d %d %d %d %d %d\n", r, g, b, x, y, f, fl, ev, ea, eb, ec);
        if (rr != 11) begin done = 1; end
        else begin
            rr = $fscanf(fe, "%d %d %d %d\n", er, eg, ebb, ef);
            expq[n % 4] = {5'd0, ef[2:0], er[7:0], eg[7:0], ebb[7:0]};
            px_in <= {r[7:0], g[7:0], b[7:0]}; x_in <= x[2:0]; y_in <= y[2:0]; f_in <= f[1:0]; de_in <= fl[0]; hs_in <= fl[1]; vs_in <= fl[2];
            if (ev == 1) fork do_store(ea, eb, ec); join_none
            if (ev == 2) fork do_mode(ea, eb); join_none
            n = n + 1;
        end
    end

    initial begin
        fc = $fopen(CMD, "r"); fe = $fopen(EXP, "r");
        if (fc == 0 || fe == 0) begin $display("FAILED  BUG=%0d: cannot open the vector files", BUG); $finish; end
        repeat (6) @(posedge clk_sys); reset <= 0;
        wait (done); repeat (6) @(negedge clk_vid);
        $display("%s  BUG=%0d: %0d cycles, %0d pixels checked, %0d table stores, %0d errors", (errors == 0 && n > 5000 && nde > 3000) ? "PASS" : "FAILED", BUG, n, nde, stores, errors);
        $finish;
    end
    initial begin #200000000; $display("TIMEOUT  BUG=%0d", BUG); $finish; end
endmodule
