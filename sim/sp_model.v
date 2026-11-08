// ============================================================================
// sp_model.v -- a model of one scheduler client port (the contract of tau_sdram_sched_a, section 4): accepts requests with a random ready, keeps them in a small in-order queue,
// executes them one at a time on a halfword memory, pulls write data with w_next / w_idx and takes it two clocks later, returns read data with r_valid / r_data / r_last, and
// raises `done` once per request. `stall_n` (0..) makes the port refuse everything for that many clocks at a time (a busy SDRAM). Used by tb_tau_p2mux.v and tb_tau_land.v.
// ============================================================================
`timescale 1ns/1ps
`default_nettype none
module sp_model #(parameter integer SEED = 1, MEMW = 16, RDYPCT = 60) (
    input  wire        clk,
    input  wire        rst,
    input  wire        valid,
    output wire        ready,
    input  wire        we,
    input  wire [24:0] addr,
    input  wire [9:0]  len,
    output reg         w_next = 1'b0,
    output reg  [9:0]  w_idx = 10'd0,
    input  wire [15:0] w_data,
    input  wire [1:0]  w_be,
    output reg         r_valid = 1'b0,
    output reg  [15:0] r_data = 16'd0,
    output reg         r_last = 1'b0,
    output reg         done = 1'b0,
    input  wire        stall                 // refuse every request while 1
);
    reg [15:0] mem [0:(1<<MEMW)-1];
    integer seed = SEED, k;
    initial for (k = 0; k < (1<<MEMW); k = k + 1) mem[k] = 16'hE000 ^ k[15:0];
    // request queue (depth 2)
    reg        q_we [0:1]; reg [24:0] q_a [0:1]; reg [9:0] q_l [0:1];
    integer    qn = 0, qh = 0, qt = 0;
    reg        rdy_r = 1'b0;
    assign ready = rdy_r && (qn < 2) && !stall;
    always @(posedge clk) rdy_r <= (($unsigned($random(seed)) % 100) < RDYPCT);
    // execution. Writes are PIPELINED: the next write's data pull starts as soon as the last word of the previous one has been pulled, while the previous request's `done`
    // (a fixed few clocks later) is still to come, as in a real scheduler. A read waits until every pending `done` has been given (done stays in order).
    reg        busy = 1'b0, cur_we; reg [24:0] cur_a; reg [9:0] cur_l; integer pos = 0, rpos = 0;
    reg [24:0] w_base = 0, wb1 = 0, wb2 = 0; reg [9:0] idx_d1 = 0, idx_d2 = 0; reg nx_d1 = 0, nx_d2 = 0;
    integer cyc_m = 0, dq_h = 0, dq_t = 0, dq_n = 0, last_t = 0; integer dq [0:15];     // the clocks at which the `done` of each write in flight is due (in order)
    wire   tails = (dq_n != 0);
    task sched_done; begin dq[dq_t] = (cyc_m + 5 > last_t + 1) ? cyc_m + 5 : last_t + 1; last_t = dq[dq_t]; dq_t = (dq_t + 1) % 16; dq_n = dq_n + 1; end endtask
    always @(posedge clk) begin
        w_next <= 1'b0; r_valid <= 1'b0; r_last <= 1'b0; done <= 1'b0;
        nx_d1 <= w_next; nx_d2 <= nx_d1; idx_d1 <= w_idx; idx_d2 <= idx_d1; wb1 <= w_base; wb2 <= wb1;
        cyc_m = cyc_m + 1;
        if (rst) begin qn = 0; qh = 0; qt = 0; busy <= 1'b0; dq_h = 0; dq_t = 0; dq_n = 0; end
        else begin
            // the oldest write's done when it is due
            if (dq_n != 0 && cyc_m >= dq[dq_h]) begin done <= 1'b1; dq_h = (dq_h + 1) % 16; dq_n = dq_n - 1; end
            if (valid && ready) begin q_we[qt] = we; q_a[qt] = addr; q_l[qt] = len; qt = (qt + 1) % 2; qn = qn + 1; end
            // data pulled two clocks ago is taken now
            if (nx_d2) begin
                if (w_be[0]) mem[(wb2 + idx_d2) & ((1<<MEMW)-1)][7:0]  <= w_data[7:0];
                if (w_be[1]) mem[(wb2 + idx_d2) & ((1<<MEMW)-1)][15:8] <= w_data[15:8];
            end
            if (!busy) begin
                if (qn > 0 && (q_we[qh] || !tails)) begin
                    cur_we <= q_we[qh]; cur_a <= q_a[qh]; cur_l <= q_l[qh]; qh = (qh + 1) % 2; qn = qn - 1;
                    busy <= 1'b1; pos = 0; rpos = 0;
                end
            end else if (cur_we) begin
                if (pos < cur_l) begin w_next <= 1'b1; w_idx <= pos[9:0]; w_base <= cur_a; pos = pos + 1; if (pos == cur_l) begin sched_done; busy <= 1'b0; end end
            end else begin
                if (rpos < cur_l) begin
                    r_valid <= 1'b1; r_data <= mem[(cur_a + rpos) & ((1<<MEMW)-1)]; r_last <= (rpos == cur_l - 1); rpos = rpos + 1;
                end else begin done <= 1'b1; busy <= 1'b0; end
            end
        end
    end
endmodule
`default_nettype wire
