// ============================================================================
// tb_tau_land.v -- Tau Gallery M0.5: the bridge-to-SDRAM landing path with the CPU-port mux (tau_sdram_p2mux) and a model of a scheduler port (sim/sp_model.v). A bridge model
// (74.25 MHz) writes transfers of 32-bit big-endian words into the window, in bursts with random gaps, with new offsets (a second transfer elsewhere), words outside the window
// that must be ignored, and a random CPU reading and writing the same SDRAM through the mux at the same time. When `idle` rises the memory must equal the golden landing area
// (little-endian halfwords of the byte stream) and the CPU region must be intact; a stalled SDRAM must overflow the FIFO and the overflow count must say how many words were lost.
// Mutants BUG 1..11 must FAIL. SLOW = 1: the overflow scenario.
//   make test-rtl-land
// ============================================================================
`timescale 1ns/1ps
`default_nettype none
module tb_tau_land #(parameter integer BUG = 0, parameter integer SLOW = 0);
    reg clk_74a = 0, clk_sys = 0, clk_mem = 0, rst = 1;
    always #6.734 clk_74a = ~clk_74a;           // 74.25 MHz
    always #7.5   clk_sys = ~clk_sys;           // 66.667 MHz
    always #5     clk_mem = ~clk_mem;           // 100 MHz
    integer seed = 32'h51ab;
    function integer rnd(input integer n); rnd = $unsigned($random(seed)) % n; endfunction

    reg br_wr = 0; reg [31:0] br_addr = 0, br_data = 0;
    reg [24:0] base = 25'h4000; reg clear = 0;
    wire [31:0] words, ovf_count; wire idle;
    wire l_valid, l_ready, l_we, l_next, l_done; wire [24:0] l_addr; wire [9:0] l_len, l_idx; wire [15:0] l_wdata;
    // CPU client
    reg c_valid = 0, c_we = 0; reg [24:0] c_addr = 0; reg [9:0] c_len = 2; reg [15:0] c_wdata = 0; reg [1:0] c_be = 2'b11;
    wire c_ready, c_next, c_r_valid, c_r_last, c_done; wire [9:0] c_idx; wire [15:0] c_r_data;
    wire s_valid, s_ready, s_we, s_next, s_r_valid, s_r_last, s_done; wire [24:0] s_addr; wire [9:0] s_len, s_idx; wire [15:0] s_wdata, s_r_data; wire [1:0] s_be;
    reg stall = 0;

    tau_land #(.BUG(BUG)) land (.clk_74a(clk_74a), .rst_74(rst), .br_wr(br_wr), .br_addr(br_addr), .br_data(br_data),
        .clk_sys(clk_sys), .rst_sys(rst), .base(base), .clear(clear), .words(words), .ovf_count(ovf_count), .idle(idle),
        .clk_mem(clk_mem), .rst_mem(rst), .a_valid(l_valid), .a_ready(l_ready), .a_we(l_we), .a_addr(l_addr), .a_len(l_len),
        .a_w_next(l_next), .a_w_idx(l_idx), .a_w_data(l_wdata), .a_done(l_done));
    tau_sdram_p2mux mux (.clk(clk_mem), .rst(rst),
        .c_valid(c_valid), .c_ready(c_ready), .c_we(c_we), .c_addr(c_addr), .c_len(c_len), .c_next(c_next), .c_idx(c_idx), .c_wdata(c_wdata), .c_be(c_be),
        .c_r_valid(c_r_valid), .c_r_data(c_r_data), .c_r_last(c_r_last), .c_done(c_done),
        .l_valid(l_valid), .l_ready(l_ready), .l_addr(l_addr), .l_len(l_len), .l_next(l_next), .l_idx(l_idx), .l_wdata(l_wdata), .l_done(l_done),
        .s_valid(s_valid), .s_ready(s_ready), .s_we(s_we), .s_addr(s_addr), .s_len(s_len), .s_next(s_next), .s_idx(s_idx), .s_wdata(s_wdata), .s_be(s_be),
        .s_r_valid(s_r_valid), .s_r_data(s_r_data), .s_r_last(s_r_last), .s_done(s_done));
    sp_model #(.SEED(11), .MEMW(16), .RDYPCT(SLOW ? 4 : 45)) port (.clk(clk_mem), .rst(rst), .valid(s_valid), .ready(s_ready), .we(s_we), .addr(s_addr), .len(s_len),
        .w_next(s_next), .w_idx(s_idx), .w_data(s_wdata), .w_be(s_be), .r_valid(s_r_valid), .r_data(s_r_data), .r_last(s_r_last), .done(s_done), .stall(stall));

    // golden memory
    reg [15:0] gold [0:65535];
    integer k; initial for (k = 0; k < 65536; k = k + 1) gold[k] = 16'hE000 ^ k[15:0];
    integer errors = 0;
    task fail(input [255:0] m); begin errors = errors + 1; if (errors < 12) $display("FAIL: %0s (t=%0t)", m, $time); end endtask

    // ---- the bridge: transfers of words at {4'h4, offset}, big-endian words of a byte stream; stray words with another high nibble must be ignored ----------------------------------
    reg [7:0] stream [0:65535];
    integer sent = 0, total_sent = 0;
    task bridge_word(input [31:0] off_bytes, input [31:0] w, input integer gap);
        begin
            @(posedge clk_74a); br_wr <= 1; br_addr <= {4'h4, off_bytes[27:0]}; br_data <= w;
            @(posedge clk_74a); br_wr <= 0; br_addr <= 32'hDEADBEEF; br_data <= 32'hDEADBEEF;
            repeat (gap) @(posedge clk_74a);
        end
    endtask
    // a transfer of `nw` words starting at word offset `ow`: file bytes are pseudo-random; the golden halfword at SDRAM hw (base + 2*word) = {byte1, byte0} (little-endian)
    task transfer(input integer ow, input integer nw, input integer maxgap);
        integer i; reg [31:0] w; reg [7:0] b0, b1, b2, b3;
        begin
            for (i = 0; i < nw; i = i + 1) begin
                b0 = $random(seed); b1 = $random(seed); b2 = $random(seed); b3 = $random(seed);
                w = {b0, b1, b2, b3};                                           // big-endian: the first file byte in bits [31:24]
                gold[base + 2 * (ow + i)]     = {b1, b0};
                gold[base + 2 * (ow + i) + 1] = {b3, b2};
                if (rnd(40) == 0) begin                                         // a stray word outside the window: it must not land anywhere
                    @(posedge clk_74a); br_wr <= 1; br_addr <= {4'h8, 28'h0000100}; br_data <= 32'hA5A5A5A5; @(posedge clk_74a); br_wr <= 0;
                end
                bridge_word((ow + i) * 4, w, rnd(maxgap + 1));
            end
            total_sent = total_sent + nw;
        end
    endtask

    // ---- the CPU in the same SDRAM (region 0x0000-0x1FFF), through the mux ------------------------------------------------------------------
    reg [15:0] cwd [0:3][0:1]; reg [1:0] cwb [0:3]; integer cw_acc = 0, cw_pulled = 0;
    reg [15:0] cr_exp [0:3][0:1]; integer cr_acc = 0, cr_done_n = 0, cr_beat = 0; reg ck_we [0:3]; integer ck_acc = 0, ck_done = 0;
    reg [15:0] c_wdata_q [0:1]; reg [1:0] c_be_q; reg [9:0] ci1 = 0; reg cn1 = 0; integer cgap = 0, cpu_n = 0; reg cpu_on = 0;
    always @(posedge clk_mem) begin
        cn1 <= c_next; ci1 <= c_idx;
        if (cn1 && cw_acc > cw_pulled) begin c_wdata <= cwd[cw_pulled % 4][ci1[0]]; c_be <= cwb[cw_pulled % 4]; if (ci1[0]) cw_pulled = cw_pulled + 1; end
        else begin c_wdata <= $random(seed); c_be <= $random(seed); end
        if (rst) c_valid <= 0;
        else begin
            if (c_valid && c_ready) begin
                if (c_we) begin
                    cwd[cw_acc % 4][0] = c_wdata_q[0]; cwd[cw_acc % 4][1] = c_wdata_q[1]; cwb[cw_acc % 4] = c_be_q; cw_acc = cw_acc + 1;
                    if (c_be_q[0]) begin gold[c_addr][7:0] = c_wdata_q[0][7:0]; gold[c_addr + 1][7:0] = c_wdata_q[1][7:0]; end
                    if (c_be_q[1]) begin gold[c_addr][15:8] = c_wdata_q[0][15:8]; gold[c_addr + 1][15:8] = c_wdata_q[1][15:8]; end
                end else begin cr_exp[cr_acc % 4][0] = gold[c_addr]; cr_exp[cr_acc % 4][1] = gold[c_addr + 1]; cr_acc = cr_acc + 1; end
                ck_we[ck_acc % 4] = c_we; ck_acc = ck_acc + 1; cpu_n = cpu_n + 1;
                c_valid <= 0; cgap = rnd(40);
            end else if (!c_valid && cpu_on) begin
                if (cgap > 0) cgap = cgap - 1;
                else begin
                    c_valid <= 1; c_we <= (rnd(100) < 50); c_addr <= rnd(4000) * 2; c_len <= 10'd2;
                    c_wdata_q[0] = $random(seed); c_wdata_q[1] = $random(seed); c_be_q = 1 + rnd(3);
                end
            end
        end
        if (c_r_valid) begin
            if (c_r_data !== cr_exp[cr_done_n % 4][cr_beat]) fail("CPU read data wrong");
            cr_beat = cr_beat + 1; if (cr_beat == 2) begin cr_beat = 0; cr_done_n = cr_done_n + 1; end
        end
        if (c_done) ck_done = ck_done + 1;
    end

    // ---- the run ---------------------------------------------------------------------------------------------------------------------------------------------------------------
    integer j, idle_checks = 0;
    initial begin
        repeat (8) @(posedge clk_sys); rst = 0; repeat (8) @(posedge clk_sys);
        cpu_on = 1;
        if (!SLOW) begin
            // three transfers: a long one, one that continues after a gap in offsets, and a short one that ends with a partial burst (flushed by the idle timer)
            transfer(0, 600, 6);
            transfer(2000, 123, 2);
            transfer(5000, 5, 0);
            wait (idle === 1'b1); @(posedge clk_sys);
            // the moment idle is seen the whole landing area must already be written (this is what idle means)
            for (j = base; j < base + 2 * 5005 + 2; j = j + 1) if (port.mem[j] !== gold[j]) begin errors = errors + 1; if (errors < 12) $display("FAIL: idle seen but mem[%0h] = %h, want %h", j, port.mem[j], gold[j]); end
            repeat (4) @(posedge clk_sys);
            cpu_on = 0; repeat (200) @(posedge clk_sys);                       // the CPU's last requests finish; idle must still mean "landed"
            for (j = 0; j < 65536; j = j + 1) if (port.mem[j] !== gold[j]) begin errors = errors + 1; if (errors < 12) $display("FAIL: mem[%0h] = %h, want %h", j, port.mem[j], gold[j]); end
            if (words != total_sent) fail("the word count is not the number of words sent");
            if (ovf_count != 0) fail("an overflow was counted with a draining FIFO");
            // clear restarts the counts
            @(posedge clk_sys); clear <= 1; @(posedge clk_sys); clear <= 0; repeat (6) @(posedge clk_sys);
            if (words != 0) fail("clear did not restart the word count");
        end else begin
            // a nearly stalled SDRAM and a fast bridge: the FIFO overflows; the count of dropped words plus the words accepted equals the words sent
            cpu_on = 0;
            transfer(0, 1500, 0);
            repeat (3000) @(posedge clk_sys);
            if (ovf_count == 0) fail("the FIFO did not overflow with a stalled SDRAM");
            if (words + ovf_count != total_sent) fail("accepted + dropped is not the number sent");
            // the words that were accepted must all drain, and idle must then mean exactly that (a FIFO that overwrote instead of dropping writes more words than it counted)
            begin : drain integer n; n = 0; while (idle !== 1'b1 && n < 60000) begin @(posedge clk_sys); n = n + 1; end end
            if (idle !== 1'b1) fail("idle never rose after the overflow: more words written than accepted");
        end
        $display("%s  BUG=%0d SLOW=%0d: sent=%0d words=%0d ovf=%0d cpu=%0d errors=%0d", errors == 0 ? "PASS" : "FAILED", BUG, SLOW, total_sent, words, ovf_count, cpu_n, errors);
        $finish;
    end
    initial begin #60_000_000; $display("FAILED TIMEOUT"); $finish; end
endmodule
