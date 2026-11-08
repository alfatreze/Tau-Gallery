// =============================================================================
// tau_land.sv -- Tau Gallery M0.5: the bridge-to-SDRAM landing path (docs/M0_DESIGN.md section 7).
//
// Why: a data-slot read lands in the 4 KiB tag zone of the CPU RAM through the loader, one command per 4 KB (about 850 KB/s, 3-4 ms of fixed overhead per command: the SD probe of
// 12.8 in the plan). A read whose bridge address is in this module's window (bridge_addr[31:28] == WIN) is written to SDRAM instead, in one command of any length, with no CPU
// copy. The bridge cannot be stalled (core_bridge_cmd strobes bridge_wr for every word), so the words go through a FIFO; an overflow is counted, never silent.
//
//   capture (clk_74a): a word at bridge_addr {WIN, offset} is stored as {word offset [25:0], data byte-reversed}. The bridge is big-endian (core_top.v: bridge_endian_little = 0), so
//                      the first file byte is in bits [31:24]; reversing the bytes makes the low halfword the first two file bytes, which is the little-endian halfword order of the
//                      plane format (word i = {byte(2i+1), byte(2i)}, M0_DESIGN.md section 3).
//   FIFO:    256 entries of 58 bits, written on clk_74a, read on clk_mem (the SDRAM clock); the pointers cross as Gray code (tau_cdc_gray_bus); a word is dropped and counted when full.
//   master (clk_mem): stages up to 8 words, writes them as one burst of 16 halfwords at base + 2 * word offset through one scheduler client port (pull writes, data two clocks after
//                      w_next), a new offset (a gap or a jump) ends the burst; a partial burst is written after FLUSH_CYC idle clocks. `base` (clk_sys, quasi-static, set before the
//                      read is started) is the SDRAM halfword address of word offset 0.
//   status (clk_sys): words accepted, overflow count, idle (every accepted word has been written; stable for 8 clocks).
//
// The structure follows Tau V's `tau_tvc_subw` (a posted write FIFO and a burst master for one scheduler client port); here the FIFO is written by the bridge (clk_74a) and carries
// an offset per word, and the master adds a base.
// BUG is a mutation hook for the testbench (0 in the build): 1 the burst address is not reloaded from the next word after a burst, 2 halfwords swapped, 3 a new offset does not end the burst, 4 the idle flush
// is disabled, 5 the eighth word of a burst is not stored, 6 base ignored, 7 an overflow is not counted, 8 idle ignores the staged words, 9 bytes not reversed, 10 the window is not
// checked, 11 a full FIFO overwrites instead of dropping the new word.
// =============================================================================
`default_nettype none

module tau_land #(
    parameter integer BUG = 0,
    parameter integer FLUSH_CYC = 16,
    parameter [3:0]   WIN = 4'h4
) (
    // bridge (clk_74a)
    input  wire        clk_74a,
    input  wire        rst_74,
    input  wire        br_wr,
    input  wire [31:0] br_addr,
    input  wire [31:0] br_data,
    // control and status (clk_sys)
    input  wire        clk_sys,
    input  wire        rst_sys,
    input  wire [24:0] base,                 // SDRAM halfword address of word offset 0
    input  wire        clear,                // one clk_sys pulse: restart the word and overflow counts
    output wire [31:0] words,                // words accepted since the last clear
    output wire [31:0] ovf_count,            // words dropped because the FIFO was full, since the last clear
    output wire        idle,                 // every accepted word has been written to SDRAM
    // scheduler client port (clk_mem = the SDRAM clock)
    input  wire        clk_mem,
    input  wire        rst_mem,
    output reg         a_valid,
    input  wire        a_ready,
    output wire        a_we,
    output reg  [24:0] a_addr,
    output reg  [9:0]  a_len,
    input  wire        a_w_next,
    input  wire [9:0]  a_w_idx,
    output reg  [15:0] a_w_data,
    input  wire        a_done
);
    assign a_we = 1'b1;

    // ---- capture and FIFO write (clk_74a) -------------------------------------------------------
    (* ramstyle = "M10K, no_rw_check" *) reg [57:0] mem [0:255];
    reg  [8:0] wptr = 9'd0;
    wire [8:0] rptr_w;                                          // the read pointer, in clk_74a
    wire       in_win  = (BUG == 10) ? 1'b1 : (br_addr[31:28] == WIN);
    wire       full    = ((wptr - rptr_w) >= 9'd256);
    wire       take    = br_wr && in_win;
    wire       accept  = take && !full;
    wire [31:0] data_le = (BUG == 9) ? br_data : {br_data[7:0], br_data[15:8], br_data[23:16], br_data[31:24]};
    always @(posedge clk_74a) begin
        if (rst_74) wptr <= 9'd0;
        else if (accept || (BUG == 11 && take)) begin
            mem[wptr[7:0]] <= {br_addr[27:2], data_le};
            wptr <= wptr + 9'd1;
        end
    end
    wire drop = take && full && (BUG != 7);

    // counters across to clk_sys (Gray, one step at a time)
    wire [31:0] in_cnt, drop_cnt, out_cnt;
    tau_cdc_gray_ctr #(.WIDTH(32)) u_in   (.clk_src(clk_74a), .rst_src(rst_74), .inc(accept), .clk_dst(clk_sys), .rst_dst(rst_sys), .count_dst(in_cnt));
    tau_cdc_gray_ctr #(.WIDTH(32)) u_drop (.clk_src(clk_74a), .rst_src(rst_74), .inc(drop),   .clk_dst(clk_sys), .rst_dst(rst_sys), .count_dst(drop_cnt));
    reg  [31:0] in_snap = 32'd0, drop_snap = 32'd0;
    always @(posedge clk_sys) if (clear) begin in_snap <= in_cnt; drop_snap <= drop_cnt; end
    assign words     = in_cnt - in_snap;
    assign ovf_count = drop_cnt - drop_snap;

    // ---- the read pointer and the write pointer across ---------------------------------------------
    wire [8:0] wptr_m;
    tau_cdc_gray_bus #(.W(9), .STAGES(3)) u_w2m (.clk_src(clk_74a), .d_src(wptr), .clk_dst(clk_mem), .q_dst(wptr_m));
    reg  [8:0] rptr = 9'd0;
    tau_cdc_gray_bus #(.W(9), .STAGES(3)) u_r2w (.clk_src(clk_mem), .d_src(rptr), .clk_dst(clk_74a), .q_dst(rptr_w));

    // ---- base (clk_sys, quasi-static) into clk_mem ---------------------------------------------------
    reg [24:0] base_s1 = 25'd0, base_s2 = 25'd0;
    always @(posedge clk_mem) begin base_s1 <= base; base_s2 <= base_s1; end
    wire [24:0] base_m = (BUG == 6) ? 25'd0 : base_s2;

    // ---- the master (clk_mem) -------------------------------------------------------------------------
    wire        pop;
    wire [8:0]  rd_addr = pop ? (rptr + 9'd1) : rptr;
    reg  [57:0] head = 58'd0;                                   // mem[rptr], read every clock (registered, so the RAM is an M10K); valid whenever the FIFO is not empty
    always @(posedge clk_mem) head <= mem[rd_addr[7:0]];
    wire        empty = (rptr == wptr_m);
    wire [25:0] h_off  = head[57:32];
    wire [31:0] h_data = head[31:0];

    reg [31:0] stg [0:7];
    reg [3:0]  n;                                               // staged words
    reg [25:0] start_off;                                       // word offset of stg[0]
    reg [1:0]  st;                                              // 0 fill, 1 request, 2 waiting for done
    reg [7:0]  tmo;
    wire       contiguous = (n == 4'd0) || (h_off == start_off + {22'd0, n});
    wire       burst_full = (n == 4'd8);
    wire [24:0] burst_addr = base_m + {start_off[23:0], 1'b0};
    assign pop = (st == 2'd0) && !empty && !burst_full && (contiguous || BUG == 3);
    reg [3:0]  dn;                                              // words to report as written after a done (one per clock)
    wire       out_inc = (dn != 4'd0);
    always @(posedge clk_mem) begin
        if (rst_mem) begin rptr <= 9'd0; n <= 4'd0; start_off <= 26'd0; st <= 2'd0; tmo <= 8'd0; a_valid <= 1'b0; a_addr <= 25'd0; a_len <= 10'd0; dn <= 4'd0; end
        else begin
            if (dn != 4'd0) dn <= dn - 4'd1;
            case (st)
                2'd0: begin
                    if (burst_full) begin st <= 2'd1; a_valid <= 1'b1; a_addr <= burst_addr; a_len <= {n, 1'b0}; end
                    else if (!empty) begin
                        tmo <= 8'd0;
                        if (contiguous || BUG == 3) begin
                            if (n == 4'd0 && BUG != 1) start_off <= h_off;
                            if (!(BUG == 5 && n == 4'd7)) stg[n[2:0]] <= h_data;
                            n <= n + 4'd1; rptr <= rptr + 9'd1;
                        end else begin st <= 2'd1; a_valid <= 1'b1; a_addr <= burst_addr; a_len <= {n, 1'b0}; end    // a new offset: the burst before it first
                    end else if (n != 4'd0 && BUG != 4) begin
                        if (tmo == FLUSH_CYC) begin st <= 2'd1; a_valid <= 1'b1; a_addr <= burst_addr; a_len <= {n, 1'b0}; end
                        else tmo <= tmo + 8'd1;
                    end
                end
                2'd1: if (a_ready) begin a_valid <= 1'b0; st <= 2'd2; end
                2'd2: if (a_done) begin
                    dn <= n; n <= 4'd0; st <= 2'd0; tmo <= 8'd0;
                end
                default: st <= 2'd0;
            endcase
        end
    end
    // after a burst the next word may continue at start_off + n: start_off is reloaded from the next word (n == 0 -> contiguous), so a gap and a continuation are the same case

    // write data: w_next at t with w_idx, the halfword is presented at t + 2 (a registered read of the staging array at t + 1)
    reg [9:0] wi1;
    always @(posedge clk_mem) begin
        wi1 <= a_w_idx;
        a_w_data <= (((BUG == 2) ? ~wi1[0] : wi1[0]) ? stg[wi1[3:1]][31:16] : stg[wi1[3:1]][15:0]);
    end

    // ---- written-word count back to clk_sys, and idle ---------------------------------------------------
    tau_cdc_gray_ctr #(.WIDTH(32)) u_out (.clk_src(clk_mem), .rst_src(rst_mem), .inc(out_inc), .clk_dst(clk_sys), .rst_dst(rst_sys), .count_dst(out_cnt));
    // idle: the words written equal the words accepted (both observed in clk_sys), stable for 8 clocks (the crossings lag by a few clocks); BUG 8 ignores the staged words
    reg [3:0] eq_run = 4'd0;
    wire      eq_now = (in_cnt == out_cnt);
    always @(posedge clk_sys) begin
        if (rst_sys) eq_run <= 4'd0;
        else if (eq_now) begin if (eq_run != 4'd15) eq_run <= eq_run + 4'd1; end
        else eq_run <= 4'd0;
    end
    assign idle = (eq_run >= 4'd8) || (BUG == 8 && (in_cnt - out_cnt) < 32'd8);
endmodule

`default_nettype wire
