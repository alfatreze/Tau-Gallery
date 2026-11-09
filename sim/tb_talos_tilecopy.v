// ============================================================================
// tb_talos_tilecopy.v -- M2.0 (docs/M2_DESIGN.md section 7): what Talos's OP_BLIT can do for a tile copy into the
// viewer plane, read from the real RTL, not assumed. Derived from tb_mp3_fb.v (same SDRAM stub, widened). The
// questions are the plan's A1 (do the sticky bases reach the whole 64 MB?) and A2 (row width, stride width). The old
// header follows. -- validate the rev 7 draw engine BEFORE spending a hardware
// cycle on it. Renders glyphs to the console as ASCII art, so a wrong EPX
// rule, a wrong scale, or an off-by-one in the row loop is visible directly
// rather than inferred from "the Pocket looks wrong".
//
// The SDRAM path (sched_a + PHY today; the retired tau_sdram_ctrl.sv in the past, whose unpacked SV structs were past
// iverilog) is not elaborated here: it is stubbed to the contract the graphics client actually
// depends on (the client protocol the scheduler and the old controller both implement):
//   * p0_available high only when idle
//   * a streaming write pulls beat N from wsrc_q, with wsrc_addr driven ahead
//     of each beat (BRAM read latency 1)
//   * p0_ready pulses once when the burst has fully retired
//
//   iverilog -g2012 -o tb.vvp sim/tb_mp3_fb.v src/fpga/core/tau_{gfx,talos,scanout}.sv \
//            src/fpga/core/font_rom.v && vvp tb.vvp
// ============================================================================
`timescale 1ns/1ps
`default_nettype none

module tb_talos_tilecopy;
    parameter BUG_IGNORE_BLIT_STRIDE = 0;   // mutation hook: 1 must fail this bench (make test-rtl-fb-mutation)
    parameter BUG_IGNORE_KEY = 0;           // mutation hook: 1 must fail this bench (make test-rtl-fb-mutation)
    parameter BUG_SBLIT_NO_SCALE = 0;       // mutation hook: 1 must fail this bench (make test-rtl-fb-mutation)
    parameter BUG_BLEND_ALWAYS_SRC = 0;     // mutation hook: 1 must fail this bench (make test-rtl-fb-mutation)
    parameter BUG_IGNORE_RC_CUT = 0;        // mutation hook: 1 must fail this bench (make test-rtl-fb-mutation)
    parameter BUG_IGNORE_BAR_HI = 0;        // mutation hook: 1 must fail this bench (make test-rtl-fb-mutation)

    reg clk_sdram = 0, clk_sys = 0, clk_vid = 0, reset = 1;
    always #5    clk_sdram = ~clk_sdram;   // 100 MHz
    always #10   clk_sys   = ~clk_sys;     //  50 MHz
    always #41.7 clk_vid   = ~clk_vid;     //  12 MHz

    reg         cmd_push = 0;
    reg  [3:0]  cmd_op = 0;
    reg  [18:0] cmd_addr = 0;
    reg  [8:0]  cmd_w = 0, cmd_h = 0;
    reg  [15:0] cmd_fg = 16'hFFFF, cmd_bg = 16'h0000;
    reg  [6:0]  cmd_glyph = 0;
    reg  [1:0]  cmd_sx = 0, cmd_sy = 0;
    reg  [1:0]  cmd_glyph_hi = 0;   // B12 (B-454): BAR lit-count high bits
    wire        cmd_full;

    // Phase F B1: sticky blit addressing state. Left at the power-up defaults
    // (base 0, stride 512) makes OP_BLIT degenerate to exactly OP_COPY's own
    // addressing -- the equivalence test below relies on that.
    reg  [24:0] blt_src_base = 25'd0, blt_dst_base = 25'd0;
    reg  [9:0]  blt_src_stride = 10'd512, blt_dst_stride = 10'd512;
    // Phase F B2: colour-key transparency, left disabled by default.
    reg         blt_key_en = 1'b0;
    reg  [15:0] blt_key = 16'd0;
    // Phase F B5: alpha blend, left disabled by default.
    reg         blt_blend_en = 1'b0;
    reg  [2:0]  blt_blend_mode = 3'd0;
    reg  [7:0]  blt_blend_alpha = 8'd0;
    // Phase F B9: palette re-index offset, left at 0 (no-op) by default.
    reg  [7:0]  blt_reindex = 8'd0;
    reg         text_light = 1'b0;   // theme/gamma: the dark-polarity weight table (default)
    // Phase F B8: CLUT load, idle unless a test drives it.
    reg         clut_wr = 1'b0;
    reg  [7:0]  clut_waddr = 8'd0;
    reg  [15:0] clut_wdata = 16'd0;
    // B11: corner-cut LUT, all zero (every "row" cuts nothing) unless a test loads it.
    reg  [79:0] rc_cut_lut = 80'd0;

    wire [24:0] p0_addr;
    wire [15:0] p0_data;
    wire [1:0]  p0_byte_en;
    wire [10:0] p0_wr_len;
    wire        p0_wr_stream, p0_wr_req, p0_rd_req, p0_end_burst_req;
    reg  [15:0] p0_q = 0;
    reg         p0_available = 0, p0_ready = 0, p0_data_available = 0;
    reg  [10:0] wsrc_addr = 0;
    wire [15:0] wsrc_q;

    tau_gfx #(.BUG_IGNORE_BLIT_STRIDE(BUG_IGNORE_BLIT_STRIDE), .BUG_IGNORE_KEY(BUG_IGNORE_KEY),
             .BUG_SBLIT_NO_SCALE(BUG_SBLIT_NO_SCALE), .BLIT_BLEND_ENABLE(1),
             .BUG_BLEND_ALWAYS_SRC(BUG_BLEND_ALWAYS_SRC),
             .BUG_IGNORE_RC_CUT(BUG_IGNORE_RC_CUT),
             .BUG_IGNORE_BAR_HI(BUG_IGNORE_BAR_HI)) dut (
        .reset(reset), .clk_sys(clk_sys), .clk_sdram(clk_sdram), .clk_vid(clk_vid),
        .cmd_push(cmd_push), .cmd_op(cmd_op), .cmd_addr(cmd_addr),
        .cmd_w(cmd_w), .cmd_h(cmd_h), .cmd_fg(cmd_fg), .cmd_bg(cmd_bg),
        .cmd_glyph(cmd_glyph), .cmd_sx(cmd_sx), .cmd_sy(cmd_sy), .cmd_glyph_hi(cmd_glyph_hi), .cmd_full(cmd_full),
        .blt_src_base(blt_src_base), .blt_src_stride(blt_src_stride),
        .blt_dst_base(blt_dst_base), .blt_dst_stride(blt_dst_stride),
        .blt_key_en(blt_key_en), .blt_key(blt_key),
        .blt_blend_en(blt_blend_en), .blt_blend_mode(blt_blend_mode), .blt_blend_alpha(blt_blend_alpha),
        .blt_reindex(blt_reindex),
        .text_light(text_light),
        .clut_wr(clut_wr), .clut_waddr(clut_waddr), .clut_wdata(clut_wdata),
        .rc_cut_lut(rc_cut_lut),
        .sdram_init_complete(1'b1),
        .p0_addr(p0_addr), .p0_data(p0_data), .p0_byte_en(p0_byte_en),
        .p0_wr_len(p0_wr_len), .p0_wr_stream(p0_wr_stream), .p0_q(p0_q),
        .p0_wr_req(p0_wr_req), .p0_rd_req(p0_rd_req),
        .p0_end_burst_req(p0_end_burst_req),
        .p0_available(p0_available), .p0_ready(p0_ready),
        .p0_data_available(p0_data_available),
        .wsrc_addr(wsrc_addr), .wsrc_q(wsrc_q),
        .video_rgb(), .video_de(), .video_hs(), .video_vs()
    );

    // ---- SDRAM controller stub ---------------------------------------------
    localparam S_IDLE = 0, S_STREAM = 1, S_CONST = 2, S_DONE = 3;
    integer     st = S_IDLE;
    integer     beats, i;
    reg [15:0]  captured [0:255];      // one streamed row
    reg [24:0]  last_addr;
    reg [10:0]  last_len;
    reg         last_stream;
    integer     rows_written = 0;
    reg [24:0]  row_addr [0:511];
    reg [15:0]  row_pix  [0:511][0:255];
    integer     row_len  [0:511];

    // Simple readable "memory": word N reads back as N+1, so a copy's output
    // is checkable against its source address without modelling real storage.
    localparam S_READ = 4;
    reg [24:0] rd_addr;

    always @(posedge clk_sdram) begin
        p0_ready <= 1'b0;
        p0_data_available <= 1'b0;
        case (st)
            S_IDLE: begin
                p0_available <= 1'b1;
                if (p0_rd_req) begin
                    p0_available <= 1'b0;
                    rd_addr <= p0_addr;
                    st      <= S_READ;
                end else if (p0_wr_req) begin
                    p0_available <= 1'b0;
                    last_addr   <= p0_addr;
                    last_len    <= p0_wr_len;
                    last_stream <= p0_wr_stream;
                    wsrc_addr   <= 11'd0;
                    beats        = 0;
                    st          <= p0_wr_stream ? S_STREAM : S_CONST;
                end
            end
            // Mirrors tau_sdram_ctrl: wsrc_addr leads by one cycle, so the word
            // valid on wsrc_q now is the one requested last cycle.
            S_STREAM: begin
                wsrc_addr <= wsrc_addr + 11'd1;
                if (wsrc_addr > 0) begin
                    captured[beats] = wsrc_q;
                    beats = beats + 1;
                end
                if (beats == last_len) st <= S_DONE;
            end
            S_READ: begin
                p0_data_available <= 1'b1;
                p0_q    <= rd_addr[15:0] + 16'd1;
                rd_addr <= rd_addr + 25'd1;
                if (p0_end_burst_req) st <= S_IDLE;
            end
            S_CONST: st <= S_DONE;
            S_DONE: begin
                row_addr[rows_written] = last_addr;
                row_len[rows_written]  = last_len;
                for (i = 0; i < 256; i = i + 1)
                    row_pix[rows_written][i] = last_stream ? captured[i] : p0_data;
                rows_written = rows_written + 1;
                p0_ready <= 1'b1;
                st       <= S_IDLE;
            end
        endcase
    end

    // ---- helpers ------------------------------------------------------------
    task push(input [3:0] op, input [18:0] a, input [8:0] w, input [8:0] h,
              input [6:0] g, input [1:0] sx, input [1:0] sy);
        begin
            @(posedge clk_sys);
            cmd_op <= op; cmd_addr <= a; cmd_w <= w; cmd_h <= h;
            cmd_glyph <= g; cmd_sx <= sx; cmd_sy <= sy; cmd_push <= 1'b1;
            @(posedge clk_sys);
            cmd_push <= 1'b0;
        end
    endtask

    // B12 (B-454): a dedicated task rather than widening push()'s own signature -- every existing
    // push() call site (BAR included, lit<=127) stays byte-for-byte unchanged and cmd_glyph_hi
    // defaults to 0 for all of them, exactly as it should. `lit9` is the FULL 9-bit lit-row count;
    // this splits it across cmd_glyph's low 7 bits and cmd_glyph_hi's 2 bits the same way
    // fw/player.c's fb_bar() will.
    task push_bar(input [18:0] a, input [8:0] w, input [8:0] h, input [8:0] lit9);
        begin
            @(posedge clk_sys);
            cmd_op <= 4'd5; cmd_addr <= a; cmd_w <= w; cmd_h <= h;
            cmd_glyph <= lit9[6:0]; cmd_glyph_hi <= lit9[8:7]; cmd_sx <= 2'd0; cmd_sy <= 2'd0; cmd_push <= 1'b1;
            @(posedge clk_sys);
            cmd_push <= 1'b0;
        end
    endtask

    reg [15:0] mid_expect;
    task show(input [127:0] label);
        integer r, c;
        reg [15:0] p;
        integer lvl;
        begin
            $display("\n=== %0s : %0d rows ===", label, rows_written);
            for (r = 0; r < rows_written; r = r + 1) begin
                $write("  ");
                for (c = 0; c < row_len[r]; c = c + 1) begin
                    p = row_pix[r][c];
                    // fg=white bg=black, so the green channel tracks coverage.
                    lvl = ((p >> 5) & 6'h3F);
                    if      (lvl < 6)  $write(".");
                    else if (lvl < 16) $write(":");
                    else if (lvl < 26) $write("-");
                    else if (lvl < 36) $write("=");
                    else if (lvl < 46) $write("*");
                    else if (lvl < 56) $write("#");
                    else               $write("@");
                end
                $display("   @%0d", row_addr[r]);
            end
        end
    endtask

    integer errors = 0;
    task check(input cond, input [1023:0] what);
        begin
            if (!cond) begin $display("FAIL: %0s", what); errors = errors + 1; end
            else         $display("ok:   %0s", what);
        end
    endtask
    integer k, ok;
    reg [24:0] sa;

    initial begin
        repeat (10) @(posedge clk_sdram);
        reset = 0;
        repeat (10) @(posedge clk_sdram);

        // ---- A1: the sticky bases reach the whole 64 MB (25-bit word addresses) ------------------
        // src base 0x1234500 (about 37.7 MB), offset 0x67 in {fg[2:0], bg}; dst base 0x1C00000 (56 MB).
        rows_written = 0;
        blt_src_base = 25'h1234500; blt_dst_base = 25'h1C00000; blt_src_stride = 10'd192; blt_dst_stride = 10'd1000;
        cmd_fg <= 16'd0; cmd_bg <= 16'h0067;
        push(3'd4, 19'h00010, 9'd96, 9'd1, 7'd0, 2'd0, 2'd0);
        wait (rows_written == 1); repeat (30) @(posedge clk_sdram);
        check(row_addr[0] == 25'h1C00010, "A1: the destination is DST_BASE + offset, 56 MB up, above bit 20");
        check(row_len[0] == 96, "A2: a 96-word row (a 64-pixel half of a tile row) is one command");
        check(row_pix[0][0] == 16'h4568 && row_pix[0][95] == 16'h45C7, "A1: the source is SRC_BASE + offset, 37 MB up: first and last word right");

        // ---- A2: the row buffer: 127 words is the most, wider is cut silently -------------------
        rows_written = 0;
        push(3'd4, 19'h00000, 9'd127, 9'd1, 7'd0, 2'd0, 2'd0);
        wait (rows_written == 1); repeat (30) @(posedge clk_sdram);
        check(row_len[0] == 127, "A2: 127 words is the widest row");
        rows_written = 0;
        push(3'd4, 19'h00000, 9'd192, 9'd1, 7'd0, 2'd0, 2'd0);       // a 128-pixel tile row is 192 words
        wait (rows_written == 1); repeat (30) @(posedge clk_sdram);
        check(row_len[0] == 64, "A2: a 192-word tile row is cut to 64 words (the width field is used mod 128): it must be two commands");

        // ---- A2: the stride registers are 10 bits: the viewer plane's 1,200 words do not fit -----
        rows_written = 0;
        blt_dst_stride = 1200;                                       // the port is [9:0]: this is 1200 mod 1024
        push(3'd4, 19'h00000, 9'd96, 9'd3, 7'd0, 2'd0, 2'd0);
        wait (rows_written == 3); repeat (30) @(posedge clk_sdram);
        check(row_addr[1] - row_addr[0] == 176 && row_addr[2] - row_addr[1] == 176, "A2: a destination stride of 1,200 words becomes 176: a multi-row BLIT cannot step a plane line");
        blt_dst_stride = 10'd1000;
        rows_written = 0;
        push(3'd4, 19'h00000, 9'd96, 9'd3, 7'd0, 2'd0, 2'd0);
        wait (rows_written == 3); repeat (30) @(posedge clk_sdram);
        check(row_addr[1] - row_addr[0] == 1000, "A2: 1,000 words (the 10-bit limit is 1,023) steps correctly");

        // ---- the way that works: one command per row piece, each with its own destination offset ----
        // A whole 128 x 128 tile (192 words a row) into a plane of 1,200-word lines: 2 pieces x 128 rows = 256 commands.
        rows_written = 0;
        blt_src_base = 25'h1000000; blt_dst_base = 25'h1800000;
        for (k = 0; k < 128; k = k + 1) begin
            cmd_fg <= 16'd0; cmd_bg <= k * 192;                      // source: the staged tile, 192 words a row
            push(3'd4, k * 1200 + 100, 9'd96, 9'd1, 7'd0, 2'd0, 2'd0);
            cmd_bg <= k * 192 + 96;
            push(3'd4, k * 1200 + 100 + 96, 9'd96, 9'd1, 7'd0, 2'd0, 2'd0);
        end
        wait (rows_written == 256); repeat (60) @(posedge clk_sdram);
        ok = 1;
        for (k = 0; k < 128; k = k + 1) begin
            if (row_addr[2*k]   != 25'h1800000 + k * 1200 + 100)        ok = 0;
            if (row_addr[2*k+1] != 25'h1800000 + k * 1200 + 100 + 96)   ok = 0;
            if (row_len[2*k] != 96 || row_len[2*k+1] != 96)             ok = 0;
            sa = 25'h1000000 + k * 192;
            if (row_pix[2*k][0]   != sa[15:0] + 16'd1)                  ok = 0;
            if (row_pix[2*k+1][0] != sa[15:0] + 16'd97)                 ok = 0;
        end
        check(rows_written == 256 && ok, "a 128 x 128 tile lands in a 1,200-word-line plane with 256 row-piece commands, every address and source right");

        // ---- the destination offset is 19 bits (524,288 words): about 437 plane lines, then the base must be rewritten ----
        rows_written = 0;
        blt_dst_base = 25'h1800000;
        push(3'd4, 19'h7FFFF, 9'd8, 9'd1, 7'd0, 2'd0, 2'd0);
        wait (rows_written == 1); repeat (30) @(posedge clk_sdram);
        check(row_addr[0] == 25'h1800000 + 25'h7FFFF, "the offset reaches 524,287 words past the base (437 lines of 1,200 words)");

        if (errors == 0) $display("PASSED"); else $display("FAILED %0d", errors);
        $finish;
    end

    initial begin
        #5000000;
        $display("TIMEOUT -- engine stalled");
        $finish;
    end

endmodule

`default_nettype wire
