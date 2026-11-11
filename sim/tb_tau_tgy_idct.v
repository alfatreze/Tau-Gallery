// Testbench of src/fpga/core/tau_tgy_idct.sv against the Python reference: every vector of sim/gen_tgy_idct_vectors.py (conformance cases, every impulse, random, extremes, real photo blocks) must produce the 64 samples
// the reference gives; a mutant (+BUG) must make it fail. Run: iverilog -g2012 -P tb_tau_tgy_idct.BUG=n ...; vvp ... +VEC=file.hex +N=count
`timescale 1ns/1ps
module tb_tau_tgy_idct;
    parameter integer BUG = 0;
    reg clk = 0, rst = 1; always #5 clk = ~clk;
    reg clear = 0, we = 0, start = 0; reg [5:0] nat = 0; reg signed [17:0] val = 0;
    wire busy, done, px_valid; wire [2:0] px_row; wire [63:0] px_data;
    tau_tgy_idct #(.BUG(BUG)) dut (.clk(clk), .rst(rst), .clear(clear), .we(we), .nat(nat), .val(val), .start(start), .busy(busy), .done(done), .px_valid(px_valid), .px_row(px_row), .px_data(px_data));
    reg [31:0] mem [0:2000000];
    reg [7:0] got [0:63]; integer errors = 0, t, i, n, k, cyc, rows_seen;
    reg [8*200-1:0] vecfile; integer nt; integer lat_first = 0;
    always @(posedge clk) if (px_valid) begin
        for (k = 0; k < 8; k = k + 1) got[px_row * 8 + k] <= px_data[8 * k +: 8];
        rows_seen <= rows_seen + 1;
    end
    initial begin
        if (!$value$plusargs("VEC=%s", vecfile)) begin $display("no +VEC"); $finish; end
        if (!$value$plusargs("N=%d", nt)) nt = 100;
        $readmemh(vecfile, mem);
        repeat (4) @(posedge clk); rst <= 0; repeat (2) @(posedge clk);
        for (t = 0; t < nt; t = t + 1) begin
            clear <= 1; @(posedge clk); clear <= 0;
            for (i = 0; i < 64; i = i + 1) if (mem[t * 128 + i][17:0] != 18'd0) begin we <= 1; nat <= i; val <= mem[t * 128 + i][17:0]; @(posedge clk); end
            we <= 0; rows_seen = 0; start <= 1; @(posedge clk); start <= 0; cyc = 0;
            while (!done && cyc < 200) begin @(posedge clk); cyc = cyc + 1; end
            repeat (2) @(posedge clk);
            if (t == 0) lat_first = cyc;
            if (cyc >= 200 || rows_seen != 8) begin errors = errors + 1; if (errors < 5) $display("test %0d: timeout or %0d rows", t, rows_seen); end
            else for (i = 0; i < 64; i = i + 1) if (got[i] !== mem[t * 128 + 64 + i][7:0]) begin errors = errors + 1; if (errors < 5) $display("test %0d sample %0d: got %0d want %0d", t, i, got[i], mem[t * 128 + 64 + i][7:0]); i = 64; end
        end
        $display("block latency %0d clocks after start (first block)", lat_first);
        if (errors == 0) $display("IDCT PASSED (0 failures)"); else $display("IDCT FAILED %0d", errors);
        $finish;
    end
endmodule
