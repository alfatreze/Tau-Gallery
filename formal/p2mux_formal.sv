// Formal wrapper for src/fpga/core/tau_sdram_p2mux.sv (see formal/README.md). Unbounded proof (k-induction): all inputs are free, constrained only by the scheduler contract the mux relies on:
// s_done only while a request is outstanding, never more than 8 outstanding (the mux's c_out is 4 bits), and in-order completion (a single `done` per request, which the mux's routing needs: while the
// secondary client is in flight nothing else is, so the next done is its).
// Proved: (1) while the secondary request is in flight no second request reaches the port and C is not ready; (2) the secondary is granted only with C offering nothing, nothing outstanding and nothing in flight;
// (3) every done goes to exactly one client, the one that owns the oldest request; (4) the port's address / length / direction / write data / byte enables come from the client that owns the request, and
// next / index / read data go to that client only; (5) C's outstanding counter equals the true count (no overflow or underflow); (6) L writes only: the port sees a write with both byte enables for L, and L gets no read data (the module has none).
module tau_sdram_p2mux_formal #(parameter integer BUG = 0) ();
    (* gclk *) wire gclk;
    reg rst_q = 1'b1; always @(posedge gclk) rst_q <= 1'b0;                    // reset on the first step only, like the real reset
    wire clk = gclk, rst = rst_q;
    (* anyseq *) wire c_valid, c_we; (* anyseq *) wire [24:0] c_addr; (* anyseq *) wire [9:0] c_len; (* anyseq *) wire [15:0] c_wdata; (* anyseq *) wire [1:0] c_be;
    (* anyseq *) wire x_valid, x_we; (* anyseq *) wire [24:0] x_addr; (* anyseq *) wire [9:0] x_len; (* anyseq *) wire [15:0] x_wdata; (* anyseq *) wire [1:0] x_be;
    (* anyseq *) wire s_ready, s_next, s_r_valid, s_r_last, s_done; (* anyseq *) wire [9:0] s_idx; (* anyseq *) wire [15:0] s_r_data;
    wire c_ready, c_next, c_r_valid, c_r_last, c_done, x_ready, x_next, x_r_valid, x_r_last, x_done, s_valid, s_we; wire [9:0] c_idx, x_idx, s_len; wire [15:0] c_r_data, x_r_data, s_wdata; wire [24:0] s_addr; wire [1:0] s_be;
    tau_sdram_p2mux #(.BUG(BUG)) dut (.clk(clk), .rst(rst), .c_valid(c_valid), .c_ready(c_ready), .c_we(c_we), .c_addr(c_addr), .c_len(c_len), .c_next(c_next), .c_idx(c_idx), .c_wdata(c_wdata), .c_be(c_be), .c_r_valid(c_r_valid), .c_r_data(c_r_data), .c_r_last(c_r_last), .c_done(c_done),
        .l_valid(x_valid), .l_ready(x_ready), .l_addr(x_addr), .l_len(x_len), .l_next(x_next), .l_idx(x_idx), .l_wdata(x_wdata), .l_done(x_done),
        .s_valid(s_valid), .s_ready(s_ready), .s_we(s_we), .s_addr(s_addr), .s_len(s_len), .s_next(s_next), .s_idx(s_idx), .s_wdata(s_wdata), .s_be(s_be), .s_r_valid(s_r_valid), .s_r_data(s_r_data), .s_r_last(s_r_last), .s_done(s_done));
endmodule
