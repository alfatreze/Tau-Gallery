// Formal wrapper for src/fpga/core/tau_tgy_wfifo.sv (see formal/README.md). Two clocks derived from one free-running step counter: clk_w = bit KW of the counter, clk_r = bit KR of (counter + OFF), OFF a free
// constant, so every relative phase of a fixed ratio is covered (ratios are swept by the .sby tasks). The writer side is the DMA's own clear handshake (tau_tgy_dma.sv tph 0..2: request, wait ack, +9, release, wait
// release seen, +9) with a free choice of WHEN to clear and when to push in the idle state. Ghost counters (no hierarchy peeking) count accepted pushes since the clear (w domain) and pops (r domain), and one free
// word (index N, value V) is followed through the FIFO.
// Proved: (1) the word at index N is read back as V (nothing lost, repeated, reordered, overwritten or taken from before a clear); (2) the FIFO never holds more than 2**AW + 1 words (mem + the read stage) i.e.
// `full` is never overrun; (3) rvalid only when a word was written and not yet popped (never reads empty/stale); (4) rvalid is low during a clear; (5) the handshake cannot get stuck (clear_ack follows within bound).
module wfifo_formal #(parameter integer AW = 3, parameter integer KW = 0, parameter integer KR = 1, parameter integer BUG = 0) ();
    (* gclk *) wire gclk;
    reg [4:0] step = 5'd0; always @(posedge gclk) step <= step + 5'd1;
    (* anyconst *) wire [4:0] off;
    wire clk_w = step[KW];
    wire [4:0] stepr = step + off; wire clk_r = stepr[KR];
    reg [3:0] wcnt = 4'd0, rcnt = 4'd0;   // saturating positions (stop at 15): which word is the tracked one
    reg [3:0] wm = 4'd0, rm = 4'd0;       // modular counts: occupancy = wm - rm (never above 2**AW + 2, so mod 16 is exact)
    // ---- writer environment: the DMA's handshake
    localparam [1:0] IDLE = 2'd0, CLR = 2'd1, H1 = 2'd2, H2 = 2'd3;
    reg [1:0] st = CLR; reg [3:0] dly = 4'd0; reg clear_req = 1'b1;       // like the DMA after reset/start: a clear first
    wire clear_ack, full, rvalid; wire [31:0] rdata; (* anyseq *) wire go_clear; (* anyseq *) wire push_want; (* anyseq *) wire pop_want;
    (* anyconst *) wire [3:0] N; wire [31:0] V = 32'd1;   // the tracked word is a 1 in bit 0; every other pushed word is 0 (keeps the state small: bit 0 is the one that carries the proof)
    initial assume (N < 4'd14);
    wire pop = pop_want && rvalid;
    wire push = (st == IDLE) && push_want;
    wire [31:0] wdata = (wcnt == N) ? V : 32'd0;                       // the tracked word has the value V, the rest is arbitrary
    always @(posedge clk_w) case (st)
        IDLE: if (go_clear) begin st <= CLR; clear_req <= 1'b1; end
        CLR:  if (clear_ack) begin dly <= 4'd8; st <= H1; end
        H1:   if (dly == 0) begin clear_req <= 1'b0; dly <= 4'd8; st <= H2; end else dly <= dly - 4'd1;
        H2:   if (!clear_ack) begin if (dly == 0) st <= IDLE; else dly <= dly - 4'd1; end
    endcase
    // ---- the unit under proof
    tau_tgy_wfifo #(.AW(AW), .BUG(BUG)) dut (.clk_w(clk_w), .clk_r(clk_r), .clear_req(clear_req), .clear_ack(clear_ack), .push(push), .wdata(wdata), .full(full), .rvalid(rvalid), .rdata(rdata), .pop(pop));
    // pop only when valid (the decoder's contract)
    // ---- ghosts
    always @(posedge clk_w) if (clear_req) begin wcnt <= 4'd0; wm <= 4'd0; end else if (push && !full) begin if (wcnt != 4'd15) wcnt <= wcnt + 4'd1; wm <= wm + 4'd1; end
    always @(posedge clk_r) if (clear_req_r) begin rcnt <= 4'd0; rm <= 4'd0; end else if (pop) begin if (rcnt != 4'd15) rcnt <= rcnt + 4'd1; rm <= rm + 4'd1; end
    reg clear_req_r = 1'b1; reg crr1 = 1'b1, crr2 = 1'b1;                  // the request seen on the read clock, 3 stages like the design's, for the ghost's own reset
    always @(posedge clk_r) begin crr1 <= clear_req; crr2 <= crr1; clear_req_r <= crr2; end
    wire quiet = (st == IDLE);                                            // outside the clear handshake: the counters are meaningful
    initial assume (step == 0);
    always @(posedge gclk) if (st != IDLE || 1'b1) begin
        // (1) data integrity
        if (quiet && rvalid && rcnt == N) assert (rdata[0]);
        // (2) occupancy
        if (quiet) assert (wm - rm <= (4'd1 << AW) + 4'd1);
        // (3) valid only for written words
        if (quiet && rvalid) assert (wm != rm);
        // (4) no valid output while the clear is acknowledged
        if (clear_req && clear_ack) assert (!rvalid);
    end
endmodule
