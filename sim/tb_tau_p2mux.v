// ============================================================================
// tb_tau_p2mux.v -- Tau Gallery M0.4: the CPU-port write mux. A random CPU (32-bit reads and writes with random byte enables, in its own memory region) and a random landing
// master (bursts of 2..16 halfwords in another region) share one model of a scheduler port. Every CPU read beat is checked against a golden memory, every CPU / landing write must
// land at its address with its data, `done` must reach the right client exactly once, and when both are finished the whole memory equals the golden one. Mutants BUG 1..7 must FAIL.
//   make test-rtl-p2mux
// ============================================================================
`timescale 1ns/1ps
`default_nettype none
module tb_tau_p2mux #(parameter integer BUG = 0);
    reg clk = 0, rst = 1;
    always #5 clk = ~clk;
    integer seed = 32'h2c0ffee;
    function integer rnd(input integer n); rnd = $unsigned($random(seed)) % n; endfunction

    // ---- CPU client C ------------------------------------------------------------
    reg        c_valid = 0, c_we = 0; reg [24:0] c_addr = 0; reg [9:0] c_len = 0; reg [15:0] c_wdata = 0; reg [1:0] c_be = 2'b11;
    wire       c_ready, c_next, c_r_valid, c_r_last, c_done; wire [9:0] c_idx; wire [15:0] c_r_data;
    // ---- landing client L ---------------------------------------------------------
    reg        l_valid = 0; reg [24:0] l_addr = 0; reg [9:0] l_len = 0; reg [15:0] l_wdata = 0;
    wire       l_ready, l_next, l_done; wire [9:0] l_idx;
    // ---- port ------------------------------------------------------------------------
    wire s_valid, s_ready, s_we, s_next, s_r_valid, s_r_last, s_done; wire [24:0] s_addr; wire [9:0] s_len, s_idx; wire [15:0] s_wdata, s_r_data; wire [1:0] s_be;
    reg stall = 0;

    tau_sdram_p2mux #(.BUG(BUG)) mux (.clk(clk), .rst(rst),
        .c_valid(c_valid), .c_ready(c_ready), .c_we(c_we), .c_addr(c_addr), .c_len(c_len), .c_next(c_next), .c_idx(c_idx), .c_wdata(c_wdata), .c_be(c_be),
        .c_r_valid(c_r_valid), .c_r_data(c_r_data), .c_r_last(c_r_last), .c_done(c_done),
        .l_valid(l_valid), .l_ready(l_ready), .l_addr(l_addr), .l_len(l_len), .l_next(l_next), .l_idx(l_idx), .l_wdata(l_wdata), .l_done(l_done),
        .s_valid(s_valid), .s_ready(s_ready), .s_we(s_we), .s_addr(s_addr), .s_len(s_len), .s_next(s_next), .s_idx(s_idx), .s_wdata(s_wdata), .s_be(s_be),
        .s_r_valid(s_r_valid), .s_r_data(s_r_data), .s_r_last(s_r_last), .s_done(s_done));
    sp_model #(.SEED(7), .MEMW(16), .RDYPCT(55)) port (.clk(clk), .rst(rst), .valid(s_valid), .ready(s_ready), .we(s_we), .addr(s_addr), .len(s_len),
        .w_next(s_next), .w_idx(s_idx), .w_data(s_wdata), .w_be(s_be), .r_valid(s_r_valid), .r_data(s_r_data), .r_last(s_r_last), .done(s_done), .stall(stall));

    // ---- golden memory (same initial pattern as the model) and per-client bookkeeping ------------------------------------------------
    reg [15:0] gold [0:65535];
    integer k; initial for (k = 0; k < 65536; k = k + 1) gold[k] = 16'hE000 ^ k[15:0];
    integer errors = 0;
    task fail(input [255:0] m); begin errors = errors + 1; if (errors < 12) $display("FAIL: %0s (t=%0t)", m, $time); end endtask

    // CPU: write data for an accepted write is kept in a small in-order queue; reads remember the expected beats
    reg [15:0] cw_data [0:3][0:1]; reg [1:0] cw_be [0:3]; integer cw_acc = 0, cw_pulled = 0;   // written by the generator / by the presenter only
    reg [15:0] cr_exp [0:3][0:1]; integer cr_h = 0, cr_t = 0, cr_n = 0, cr_beat = 0;
    reg        ck_we [0:3]; integer ck_h = 0, ck_t = 0, ck_n = 0;           // kinds of accepted C requests in order, popped at done
    integer c_acc = 0, c_don = 0;
    // C data presenter: the word for c_idx, two clocks after c_next
    reg [9:0] ci1 = 0, ci2 = 0; reg cn1 = 0, cn2 = 0;
    always @(posedge clk) begin
        cn1 <= c_next; cn2 <= cn1; ci1 <= c_idx; ci2 <= ci1;
        if (cn1 && cw_acc > cw_pulled) begin c_wdata <= cw_data[cw_pulled % 4][ci1[0]]; c_be <= cw_be[cw_pulled % 4]; if (ci1[0]) cw_pulled = cw_pulled + 1; end
        else begin c_wdata <= $random(seed); c_be <= $random(seed); end          // garbage and odd byte enables while idle: a mux that lets them through is caught
    end
    reg [15:0] c_wdata_q [0:1]; reg [1:0] c_be_q;
    // C request generator
    integer cgap = 0;
    always @(posedge clk) begin
        if (rst) begin c_valid <= 0; end
        else begin
            if (c_valid && c_ready) begin
                // accepted: update golden / expectations in order
                if (c_we) begin
                    cw_data[cw_acc % 4][0] = c_wdata_q[0]; cw_data[cw_acc % 4][1] = c_wdata_q[1]; cw_be[cw_acc % 4] = c_be_q; cw_acc = cw_acc + 1;
                    if (c_be_q[0]) begin gold[c_addr][7:0] = c_wdata_q[0][7:0]; gold[c_addr + 1][7:0] = c_wdata_q[1][7:0]; end
                    if (c_be_q[1]) begin gold[c_addr][15:8] = c_wdata_q[0][15:8]; gold[c_addr + 1][15:8] = c_wdata_q[1][15:8]; end
                end else begin
                    cr_exp[cr_t][0] = gold[c_addr]; cr_exp[cr_t][1] = gold[c_addr + 1]; cr_t = (cr_t + 1) % 4; cr_n = cr_n + 1;
                end
                ck_we[ck_t] = c_we; ck_t = (ck_t + 1) % 4; ck_n = ck_n + 1; c_acc = c_acc + 1;
                c_valid <= 0; cgap = rnd(6);
            end else if (!c_valid) begin
                if (cgap > 0) cgap = cgap - 1;
                else if (c_acc < 400) begin
                    c_valid <= 1; c_we <= (rnd(100) < 55); c_addr <= rnd(8000) * 2; c_len <= 10'd2;
                    c_wdata_q[0] = $random(seed); c_wdata_q[1] = $random(seed); c_be_q = 1 + rnd(3);
                end
            end
        end
    end
    // C reads and done
    always @(posedge clk) begin
        if (c_r_valid) begin
            if (cr_n == 0) fail("C read data with no read outstanding");
            else begin
                if (c_r_data !== cr_exp[cr_h][cr_beat]) fail("C read data wrong");
                cr_beat = cr_beat + 1;
                if (c_r_last != (cr_beat == 2)) fail("C r_last wrong");
                if (cr_beat == 2) begin cr_beat = 0; cr_h = (cr_h + 1) % 4; cr_n = cr_n - 1; end
            end
        end
        if (c_done) begin
            if (ck_n == 0) fail("C done with nothing outstanding");
            else begin
                ck_h = (ck_h + 1) % 4; ck_n = ck_n - 1; c_don = c_don + 1;
            end
        end
    end

    // ---- landing master L: bursts of 2..16 halfwords in 0x8000.. , data from the golden value to be written ------------------------------------------------------------------
    reg [15:0] lw_data [0:15]; integer l_acc = 0, l_out = 0, l_dones = 0, lgap = 0; reg [9:0] li1 = 0, li2 = 0; reg ln1 = 0, ln2 = 0;
    reg [24:0] l_next_addr = 25'h8000;
    always @(posedge clk) begin
        ln1 <= l_next; ln2 <= ln1; li1 <= l_idx; li2 <= li1;
        if (ln1) l_wdata <= lw_data[li1[3:0]]; else l_wdata <= $random(seed);
        if (rst) l_valid <= 0;
        else begin
            if (l_valid && l_ready) begin
                for (k = 0; k < l_len; k = k + 1) gold[l_addr + k] = lw_data[k];
                l_valid <= 0; l_out = l_out + 1; lgap = rnd(30); l_acc = l_acc + 1;
            end else if (!l_valid && l_out == 0) begin
                if (lgap > 0) lgap = lgap - 1;
                else if (l_acc < 120) begin
                    l_len <= 2 + 2 * rnd(8);                                         // halfwords, even: whole 32-bit words
                    l_addr <= l_next_addr;
                    for (k = 0; k < 16; k = k + 1) lw_data[k] = $random(seed);
                    l_next_addr = l_next_addr + 16 + 2 * rnd(4);                     // sometimes a gap: a new address
                    l_valid <= 1;
                end
            end
            if (l_done) begin
                if (l_out == 0) fail("L done with nothing outstanding");
                else begin l_out = l_out - 1; l_dones = l_dones + 1; end
            end
        end
    end

    integer n, j;
    initial begin
        repeat (6) @(posedge clk); rst = 0;
        // a busy SDRAM now and then
        fork
            begin : stalls
                forever begin repeat (200 + rnd(400)) @(posedge clk); stall = 1; repeat (20 + rnd(60)) @(posedge clk); stall = 0; end
            end
            begin : wait_done
                n = 0;
                while ((c_don < 400 || l_dones < 120) && n < 400000) begin @(posedge clk); n = n + 1; end
                disable stalls;
            end
        join
        repeat (50) @(posedge clk);
        if (c_don != 400) fail("not every C request completed");
        if (l_dones != 120) fail("not every landing burst completed");
        // the whole memory
        for (j = 0; j < 65536; j = j + 1) if (port.mem[j] !== gold[j]) begin errors = errors + 1; if (errors < 12) $display("FAIL: mem[%0h] = %h, want %h", j, port.mem[j], gold[j]); end
        $display("%s  BUG=%0d: C %0d requests, L %0d bursts, errors=%0d", errors == 0 ? "PASS" : "FAILED", BUG, c_don, l_dones, errors);
        $finish;
    end
endmodule
