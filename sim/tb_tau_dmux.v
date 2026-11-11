// ============================================================================
// tb_tau_dmux.v -- the CPU-port mux for a read AND write secondary client (tau_sdram_dmux). A random CPU (32-bit reads and writes with random byte enables, its own region) and a random DMA client D
// (reads of 1..24 halfwords, writes of 1..24 halfwords with random byte enables, another region) share one model of a scheduler port (sim/sp_model.v). Every read beat of either client is checked against
// a golden memory, every write must land with its byte enables, `done` must reach the right client exactly once, and at the end the whole memory equals the golden one. Mutants BUG 1..10 must FAIL.
//   make test-rtl-dmux
// ============================================================================
`timescale 1ns/1ps
`default_nettype none
module tb_tau_dmux #(parameter integer BUG = 0);
    reg clk = 0, rst = 1; always #5 clk = ~clk;
    integer seed = 32'h5eed1234;
    function integer rnd(input integer n); rnd = $unsigned($random(seed)) % n; endfunction
    reg        c_valid = 0, c_we = 0; reg [24:0] c_addr = 0; reg [9:0] c_len = 0; reg [15:0] c_wdata = 0; reg [1:0] c_be = 2'b11;
    wire       c_ready, c_next, c_r_valid, c_r_last, c_done; wire [9:0] c_idx; wire [15:0] c_r_data;
    reg        d_valid = 0, d_we = 0; reg [24:0] d_addr = 0; reg [9:0] d_len = 0; reg [15:0] d_wdata = 0; reg [1:0] d_be = 2'b11;
    wire       d_ready, d_next, d_r_valid, d_r_last, d_done; wire [9:0] d_idx; wire [15:0] d_r_data;
    wire s_valid, s_ready, s_we, s_next, s_r_valid, s_r_last, s_done; wire [24:0] s_addr; wire [9:0] s_len, s_idx; wire [15:0] s_wdata, s_r_data; wire [1:0] s_be;
    reg stall = 0;
    tau_sdram_dmux #(.BUG(BUG)) mux (.clk(clk), .rst(rst),
        .c_valid(c_valid), .c_ready(c_ready), .c_we(c_we), .c_addr(c_addr), .c_len(c_len), .c_next(c_next), .c_idx(c_idx), .c_wdata(c_wdata), .c_be(c_be), .c_r_valid(c_r_valid), .c_r_data(c_r_data), .c_r_last(c_r_last), .c_done(c_done),
        .d_valid(d_valid), .d_ready(d_ready), .d_we(d_we), .d_addr(d_addr), .d_len(d_len), .d_next(d_next), .d_idx(d_idx), .d_wdata(d_wdata), .d_be(d_be), .d_r_valid(d_r_valid), .d_r_data(d_r_data), .d_r_last(d_r_last), .d_done(d_done),
        .s_valid(s_valid), .s_ready(s_ready), .s_we(s_we), .s_addr(s_addr), .s_len(s_len), .s_next(s_next), .s_idx(s_idx), .s_wdata(s_wdata), .s_be(s_be), .s_r_valid(s_r_valid), .s_r_data(s_r_data), .s_r_last(s_r_last), .s_done(s_done));
    sp_model #(.SEED(9), .MEMW(16), .RDYPCT(55)) port (.clk(clk), .rst(rst), .valid(s_valid), .ready(s_ready), .we(s_we), .addr(s_addr), .len(s_len),
        .w_next(s_next), .w_idx(s_idx), .w_data(s_wdata), .w_be(s_be), .r_valid(s_r_valid), .r_data(s_r_data), .r_last(s_r_last), .done(s_done), .stall(stall));
    reg [15:0] gold [0:65535];
    integer k; initial for (k = 0; k < 65536; k = k + 1) gold[k] = 16'hE000 ^ k[15:0];
    integer errors = 0;
    task fail(input [255:0] m); begin errors = errors + 1; if (errors < 12) $display("FAIL: %0s (t=%0t)", m, $time); end endtask
    // ---- C: 2-halfword reads and writes in 0..16000 (as in tb_tau_p2mux) ----
    reg [15:0] cw_data [0:3][0:1]; reg [1:0] cw_be [0:3]; integer cw_acc = 0, cw_pulled = 0;
    reg [15:0] cr_exp [0:3][0:1]; integer cr_h = 0, cr_t = 0, cr_n = 0, cr_beat = 0;
    integer c_acc = 0, c_don = 0, ck_n = 0;
    reg [9:0] ci1 = 0; reg cn1 = 0;
    always @(posedge clk) begin
        cn1 <= c_next; ci1 <= c_idx;
        if (cn1 && cw_acc > cw_pulled) begin c_wdata <= cw_data[cw_pulled % 4][ci1[0]]; c_be <= cw_be[cw_pulled % 4]; if (ci1[0]) cw_pulled = cw_pulled + 1; end
        else begin c_wdata <= $random(seed); c_be <= $random(seed); end
    end
    reg [15:0] c_wq [0:1]; reg [1:0] c_beq; integer cgap = 0;
    always @(posedge clk) begin
        if (rst) c_valid <= 0;
        else begin
            if (c_valid && c_ready) begin
                if (c_we) begin
                    cw_data[cw_acc % 4][0] = c_wq[0]; cw_data[cw_acc % 4][1] = c_wq[1]; cw_be[cw_acc % 4] = c_beq; cw_acc = cw_acc + 1;
                    if (c_beq[0]) begin gold[c_addr][7:0] = c_wq[0][7:0]; gold[c_addr + 1][7:0] = c_wq[1][7:0]; end
                    if (c_beq[1]) begin gold[c_addr][15:8] = c_wq[0][15:8]; gold[c_addr + 1][15:8] = c_wq[1][15:8]; end
                end else begin cr_exp[cr_t][0] = gold[c_addr]; cr_exp[cr_t][1] = gold[c_addr + 1]; cr_t = (cr_t + 1) % 4; cr_n = cr_n + 1; end
                ck_n = ck_n + 1; c_acc = c_acc + 1; c_valid <= 0; cgap = rnd(6);
            end else if (!c_valid) begin
                if (cgap > 0) cgap = cgap - 1;
                else if (c_acc < 300) begin c_valid <= 1; c_we <= (rnd(100) < 55); c_addr <= rnd(8000) * 2; c_len <= 10'd2; c_wq[0] = $random(seed); c_wq[1] = $random(seed); c_beq = 1 + rnd(3); end
            end
        end
        if (c_r_valid) begin
            if (cr_n == 0) fail("C read data with no C read outstanding");
            else begin
                if (c_r_data !== cr_exp[cr_h][cr_beat]) fail("C read data wrong");
                cr_beat = cr_beat + 1; if (c_r_last != (cr_beat == 2)) fail("C r_last wrong");
                if (cr_beat == 2) begin cr_beat = 0; cr_h = (cr_h + 1) % 4; cr_n = cr_n - 1; end
            end
        end
        if (c_done) begin if (ck_n == 0) fail("C done with nothing outstanding"); else begin ck_n = ck_n - 1; c_don = c_don + 1; end end
    end
    // ---- D: reads and writes of 1..24 halfwords in 0x8000.. with random byte enables ----
    reg [15:0] dw_data [0:31]; reg [1:0] dw_be [0:31]; reg [15:0] dr_exp [0:31]; integer d_acc = 0, d_out = 0, d_dones = 0, dgap = 0, d_rbeat = 0; reg d_kind_we = 0; integer d_len_i = 0;
    reg [9:0] di1 = 0; reg dn1 = 0;
    always @(posedge clk) begin
        dn1 <= d_next; di1 <= d_idx;
        if (dn1) begin d_wdata <= dw_data[di1[4:0]]; d_be <= dw_be[di1[4:0]]; end else begin d_wdata <= $random(seed); d_be <= $random(seed); end
        if (rst) d_valid <= 0;
        else begin
            if (d_valid && d_ready) begin
                if (d_we) begin
                    for (k = 0; k < d_len; k = k + 1) begin
                        if (dw_be[k][0]) gold[d_addr + k][7:0] = dw_data[k][7:0];
                        if (dw_be[k][1]) gold[d_addr + k][15:8] = dw_data[k][15:8];
                    end
                end else begin for (k = 0; k < d_len; k = k + 1) dr_exp[k] = gold[d_addr + k]; d_rbeat = 0; end
                d_kind_we = d_we; d_len_i = d_len; d_valid <= 0; d_out = d_out + 1; dgap = rnd(25); d_acc = d_acc + 1;
            end else if (!d_valid && d_out == 0) begin
                if (dgap > 0) dgap = dgap - 1;
                else if (d_acc < 150) begin
                    d_we <= (rnd(100) < 50); d_len <= 1 + rnd(24); d_addr <= 25'h8000 + rnd(2000);
                    for (k = 0; k < 32; k = k + 1) begin dw_data[k] = $random(seed); dw_be[k] = 1 + rnd(3); end
                    d_valid <= 1;
                end
            end
            if (d_r_valid) begin
                if (d_out == 0 || d_kind_we) fail("D read data with no D read outstanding");
                else begin
                    if (d_r_data !== dr_exp[d_rbeat]) fail("D read data wrong");
                    if (d_r_last != (d_rbeat == d_len_i - 1)) fail("D r_last wrong");
                    d_rbeat = d_rbeat + 1;
                end
            end
            if (d_done) begin if (d_out == 0) fail("D done with nothing outstanding"); else begin d_out = d_out - 1; d_dones = d_dones + 1; end end
        end
    end
    integer c_in_d = 0, d_after_c = 0;
    always @(posedge clk) begin if (c_valid && c_ready && mux.sub_busy) c_in_d = c_in_d + 1; end
    integer n, j;
    initial begin
        repeat (6) @(posedge clk); rst = 0;
        fork
            begin : stalls forever begin repeat (200 + rnd(400)) @(posedge clk); stall = 1; repeat (20 + rnd(60)) @(posedge clk); stall = 0; end end
            begin : wait_done n = 0; while ((c_don < 300 || d_dones < 150) && n < 400000) begin @(posedge clk); n = n + 1; end disable stalls; end
        join
        repeat (60) @(posedge clk);
        if (c_don != 300) fail("not every C request completed");
        if (d_dones != 150) fail("not every D request completed");
        if (c_in_d != 0) fail("C accepted while D was in flight");
        for (j = 0; j < 65536; j = j + 1) if (port.mem[j] !== gold[j]) begin errors = errors + 1; if (errors < 12) $display("FAIL: mem[%0h] = %h, want %h", j, port.mem[j], gold[j]); end
        $display("%s  BUG=%0d: C %0d requests, D %0d requests, errors=%0d", errors == 0 ? "PASS" : "FAILED", BUG, c_don, d_dones, errors);
        $finish;
    end
endmodule
