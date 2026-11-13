// tb_tau_hist.v -- tau_hist against the vectors of sim/test_rtl_hist.py (an independent numpy model).
// Command file, one line per clk cycle: "valid x y r g b ev p0 p1 p2 p3 p4 p5 p6"
//   ev 0 nothing; ev 1 = set the configuration (p0 en, p1 org_x, p2 org_y, p3 rect_x0, p4 rect_y0, p5 rect_x1, p6 rect_y1);
//   ev 2 = toggle the clear request and wait for the sweep to end; ev 3 = dump: read all 4 x 256 bins and the pixel count through the read port into the output file.
// Prints PASS / FAILED with BUG and the dump count; the Python script compares the dumps with its own counts (the verdict of a run is the script's).
`timescale 1ns/1ps
module tb_tau_hist #(parameter BUG = 0, parameter CMD = "build/rtl/hist_cmd.txt", parameter OUT = "build/rtl/hist_out.txt");
    reg clk = 0, rst = 1; always #10 clk = ~clk;
    reg px_valid = 0; reg [6:0] px_x = 0, px_y = 0; reg [7:0] px_r = 0, px_g = 0, px_b = 0;
    reg en = 0; reg [9:0] org_x = 0, org_y = 0, rx0 = 0, ry0 = 0, rx1 = 0, ry1 = 0;
    reg clr_req_tgl = 0; wire clr_busy; reg [9:0] rd_addr = 0; wire [23:0] rd_data, n_counted;
    tau_hist #(.BUG(BUG)) dut (.clk(clk), .rst(rst), .px_valid(px_valid), .px_x(px_x), .px_y(px_y), .px_r(px_r), .px_g(px_g), .px_b(px_b),
        .en(en), .org_x(org_x), .org_y(org_y), .rect_x0(rx0), .rect_y0(ry0), .rect_x1(rx1), .rect_y1(ry1),
        .clr_req_tgl(clr_req_tgl), .clr_busy(clr_busy), .rd_addr(rd_addr), .rd_data(rd_data), .n_counted(n_counted));
    integer fc, fo, rr, v, x, y, r, g, b, ev, p0, p1, p2, p3, p4, p5, p6, n = 0, dumps = 0, i, guard;
    initial begin
        fc = $fopen(CMD, "r"); fo = $fopen(OUT, "w");
        if (fc == 0 || fo == 0) begin $display("FAILED  BUG=%0d: cannot open files", BUG); $finish; end
        repeat (4) @(negedge clk); rst <= 0; repeat (2) @(negedge clk);
        rr = 14;
        while (rr == 14) begin
            rr = $fscanf(fc, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d\n", v, x, y, r, g, b, ev, p0, p1, p2, p3, p4, p5, p6);
            if (rr == 14) begin
                @(negedge clk);
                px_valid <= v[0]; px_x <= x[6:0]; px_y <= y[6:0]; px_r <= r[7:0]; px_g <= g[7:0]; px_b <= b[7:0];
                n = n + 1;
                if (ev == 1) begin en <= p0[0]; org_x <= p1[9:0]; org_y <= p2[9:0]; rx0 <= p3[9:0]; ry0 <= p4[9:0]; rx1 <= p5[9:0]; ry1 <= p6[9:0]; end
                if (ev == 2) begin
                    @(negedge clk); px_valid <= 0; clr_req_tgl <= ~clr_req_tgl; repeat (3) @(negedge clk);
                    guard = 0; while (clr_busy && guard < 1000) begin @(negedge clk); guard = guard + 1; end
                    if (guard >= 1000) $display("FAIL: clear never ended");
                end
                if (ev == 3) begin
                    px_valid <= 0; repeat (6) @(negedge clk);                                                  // let the pipeline drain
                    $fwrite(fo, "DUMP %0d\n", dumps);
                    for (i = 0; i < 1024; i = i + 1) begin rd_addr <= i[9:0]; repeat (4) @(negedge clk); $fwrite(fo, "%0d\n", rd_data); end
                    $fwrite(fo, "N %0d\n", n_counted); dumps = dumps + 1;
                end
            end
        end
        repeat (8) @(negedge clk); $fclose(fo);
        $display("%s  BUG=%0d: %0d cycles, %0d dumps", dumps > 0 ? "PASS" : "FAILED", BUG, n, dumps);
        $finish;
    end
    initial begin #400000000; $display("TIMEOUT  BUG=%0d", BUG); $finish; end
endmodule
