// ============================================================================
// tb_tau_gfx_view.v -- Tau Gallery M0.2: the real graphics path (tau_gfx = tau_talos + tau_scanout + tau_scanout_v, VIEW_ENABLE = 1) shows an 800 x 720 packed RGB888 plane
// that sits in an SDRAM model with a base and a stride of its own. The UI scan-out runs first; view_en is set, the mode is latched at a vblank edge, and the video outputs
// switch to the viewer. Every active pixel of two whole frames after the switch is compared with the plane, the late-line counter must not move, and the frame is 666,000
// clocks. BUG_VIEW (the Talos mutation hook) must make it FAIL for every value 1..5; SLOW=1 makes the SDRAM model slow (the late counter must count).
//   make test-rtl-gfx-view          make test-rtl-gfx-view-mutation
// ============================================================================
`timescale 1ns/1ps
`default_nettype none

module vw_sdram_stub #(parameter integer BASE = 0, STRIDE = 0, SLOWRD = 0) (
    input  wire        clk,
    input  wire [24:0] p0_addr, input wire [15:0] p0_data, input wire [10:0] p0_wr_len,
    input  wire        p0_wr_stream, p0_wr_req, p0_rd_req, p0_end_burst_req,
    output reg  [15:0] p0_q = 0, output reg p0_available = 0, p0_ready = 0, p0_data_available = 0,
    output reg  [10:0] wsrc_addr = 0, input wire [15:0] wsrc_q
);
    reg [15:0] mem [0:(1<<20)-1];
    function [23:0] pix(input integer x, input integer y);
        reg [31:0] h;
        begin
            h = x * 32'd2654435761 + y * 32'd40503 + 32'd12345;
            h = h ^ (h >> 13); h = h * 32'd1274126177; h = h ^ (h >> 16);
            pix = h[23:0];
        end
    endfunction
    function [7:0] pbyte(input integer y, input integer k);
        reg [23:0] p;
        begin p = pix(k / 3, y); pbyte = (k % 3 == 0) ? p[23:16] : (k % 3 == 1) ? p[15:8] : p[7:0]; end
    endfunction
    integer ln, w;
    initial begin
        for (ln = 0; ln < (1<<20); ln = ln + 1) mem[ln] = 16'hDEAD;                     // a marker: a wrong address shows as a wrong pixel
        for (ln = 0; ln < 720; ln = ln + 1)
            for (w = 0; w < 1200; w = w + 1)
                mem[BASE + ln * STRIDE + w] = {pbyte(ln, 2 * w + 1), pbyte(ln, 2 * w)};
    end
    localparam IDLE = 0, DONE = 3, READ = 4;
    integer st = IDLE, wait_n = 0;
    reg [24:0] a;
    always @(posedge clk) begin
        p0_ready <= 1'b0; p0_data_available <= 1'b0;
        case (st)
            IDLE: begin
                p0_available <= 1'b1;
                if (p0_rd_req) begin p0_available <= 1'b0; a <= p0_addr; st <= READ; wait_n = 0; end
                else if (p0_wr_req) begin p0_available <= 1'b0; st <= DONE; end
            end
            READ: begin
                if (wait_n < SLOWRD) wait_n = wait_n + 1;
                else begin p0_data_available <= 1'b1; p0_q <= mem[a & 25'hFFFFF]; a <= a + 25'd1; wait_n = 0; end
                if (p0_end_burst_req) st <= IDLE;
            end
            DONE: begin p0_ready <= 1'b1; st <= IDLE; end
        endcase
    end
endmodule

module tb_tau_gfx_view #(parameter integer BUG_VIEW = 0, parameter integer SLOW = 0);
    localparam integer BASE = 4096, STRIDE = 1208;
    reg clk_sdram = 0, clk_sys = 0, clk_vid = 0, reset = 1;
    always #5    clk_sdram = ~clk_sdram;     // 100 MHz
    always #7.5  clk_sys   = ~clk_sys;       // 66.667 MHz
    always #12.5 clk_vid   = ~clk_vid;       // 40 MHz
    reg view_en = 0;
    wire [24:0] p0_addr; wire [15:0] p0_data; wire [10:0] p0_len; wire p0_str, p0_wr, p0_rd, p0_eb;
    wire [15:0] p0_q; wire p0_av, p0_rdy, p0_da; wire [10:0] wsa; wire [15:0] wsq;
    wire [23:0] rgb; wire de, hs, vs; wire [15:0] uf; wire [8:0] svc;
    wire full;

    tau_gfx #(.BLIT_BLEND_ENABLE(0), .VIEW_ENABLE(1), .BUG_VIEW(BUG_VIEW)) dut (
        .reset(reset), .clk_sys(clk_sys), .clk_sdram(clk_sdram), .clk_vid(clk_vid),
        .cmd_push(1'b0), .cmd_op(4'd0), .cmd_addr(19'd0), .cmd_w(9'd0), .cmd_h(9'd0), .cmd_fg(16'd0), .cmd_bg(16'd0),
        .cmd_glyph(7'd0), .cmd_sx(2'd0), .cmd_sy(2'd0), .cmd_glyph_hi(2'd0), .font_we(1'b0), .font_waddr(13'd0), .font_wdata(32'd0),
        .cmd_full(full), .text_light(1'b0),
        .blt_src_base(25'd0), .blt_src_stride(10'd512), .blt_dst_base(25'd0), .blt_dst_stride(10'd512), .blt_key_en(1'b0), .blt_key(16'd0),
        .blt_blend_en(1'b0), .blt_blend_mode(3'd0), .blt_blend_alpha(8'd0), .rc_cut_lut(80'd0), .blt_reindex(8'd0), .clut_wr(1'b0), .clut_waddr(8'd0), .clut_wdata(16'd0),
        .dbuf_cpu_buf(1'b0), .dbuf_flip_req_tgl(1'b0), .dbuf_disp_buf(), .dbuf_flip_pending(),
        .sdram_init_complete(1'b1), .p0_addr(p0_addr), .p0_data(p0_data), .p0_byte_en(), .p0_wr_len(p0_len), .p0_wr_stream(p0_str), .p0_q(p0_q),
        .p0_wr_req(p0_wr), .p0_rd_req(p0_rd), .p0_end_burst_req(p0_eb), .p0_available(p0_av), .p0_ready(p0_rdy), .p0_data_available(p0_da), .wsrc_addr(wsa), .wsrc_q(wsq),
        .video_rgb(rgb), .video_de(de), .video_hs(hs), .video_vs(vs), .scan_vc(svc),
        .gfx_idle(), .gfx_fence(), .gfx_free(), .gfx_ovf(), .gfx_underflow(uf),
        .view_en(view_en), .view_base(BASE[24:0]), .view_stride(STRIDE[15:0]));
    vw_sdram_stub #(.BASE(BASE), .STRIDE(STRIDE), .SLOWRD(SLOW)) mem (.clk(clk_sdram), .p0_addr(p0_addr), .p0_data(p0_data), .p0_wr_len(p0_len), .p0_wr_stream(p0_str),
        .p0_wr_req(p0_wr), .p0_rd_req(p0_rd), .p0_end_burst_req(p0_eb), .p0_q(p0_q), .p0_available(p0_av), .p0_ready(p0_rdy), .p0_data_available(p0_da), .wsrc_addr(wsa), .wsrc_q(wsq));

    function [23:0] pix(input integer x, input integer y);
        reg [31:0] h;
        begin
            h = x * 32'd2654435761 + y * 32'd40503 + 32'd12345;
            h = h ^ (h >> 13); h = h * 32'd1274126177; h = h ^ (h >> 16);
            pix = h[23:0];
        end
    endfunction

    // the viewer is on screen when the muxed video shows 900 x 740 timing: wait for the switch, two frames to settle, then check two
    integer cyc = 0, errors = 0, frames_seen = 0, x = 0, y = -1, pix_bad = 0, de_lines = 0, last_vs = 0, frame_clks = 0, uf0 = 0;
    reg checking = 0, de_q = 0, vs_q = 0;
    always @(posedge clk_vid) begin
        cyc <= cyc + 1; de_q <= de; vs_q <= vs;
        if (checking) begin
            if (de) begin
                if (!de_q) begin y = y + 1; x = 0; de_lines = de_lines + 1; end
                if (!SLOW && frames_seen >= 1 && rgb !== pix(x, y)) begin pix_bad = pix_bad + 1; if (pix_bad < 4) $display("DIFF f=%0d x=%0d y=%0d got %h want %h", frames_seen, x, y, rgb, pix(x, y)); end
                x = x + 1;
            end
            if (vs && !vs_q) begin
                if (last_vs != 0 && cyc - last_vs != 666000) begin errors = errors + 1; $display("FAIL: frame is %0d clocks", cyc - last_vs); end
                last_vs = cyc; y = -1;
                if (frames_seen > 0 && de_lines != 720) begin errors = errors + 1; $display("FAIL: %0d active lines", de_lines); end
                de_lines = 0; frames_seen = frames_seen + 1;
            end
        end
    end
    integer k;
    initial begin
        repeat (8) @(posedge clk_sys); reset = 0;
        repeat (2 * 500 * 400) @(posedge clk_vid);               // UI frames run first
        view_en = 1;
        wait (dut.g_view.sel == 1'b1);
        // two frames of the viewer to settle (its own counters ran all the time; the first lines after the switch had no fill)
        repeat (666000 + 5000) @(posedge clk_vid);
        uf0 = uf; checking = 1;
        wait (frames_seen == 2);
        checking = 0;
        if (SLOW) begin if (uf == uf0) begin errors = errors + 1; $display("FAIL: slow memory but no late line counted"); end end
        else if (uf != uf0) begin errors = errors + 1; $display("FAIL: %0d late lines in the checked frames", uf - uf0); end
        if (!SLOW && pix_bad != 0) errors = errors + 1;
        $display("%s  BUG_VIEW=%0d SLOW=%0d: pix_bad=%0d late_delta=%0d errors=%0d", errors == 0 ? "PASSED" : "FAILED", BUG_VIEW, SLOW, pix_bad, uf - uf0, errors);
        $finish;
    end
    initial begin #900000000; $display("FAILED TIMEOUT"); $finish; end
endmodule
