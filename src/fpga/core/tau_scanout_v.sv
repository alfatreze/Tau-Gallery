// =============================================================================
// tau_scanout_v.sv -- Tau Gallery viewer scan-out: 800 x 720 packed RGB888 at a 40 MHz pixel clock (M0, docs/M0_DESIGN.md).
//
// Timing (physical, no doubling): 900 x 740 total = 666,000 clocks = 60.06 Hz at 40 MHz. These are the exact positions of the 800 x 720
// mode that Tau V's `O2` build ran on a Pocket (tau_tvc_video.sv, logical 450 x 370 with every pixel and line doubled): the active window starts at
// hc 16 / vc 8, horizontal sync is hc 840..879 (40 clocks), vertical sync is vc 734..737 (4 lines).
//
// Plane format (frozen interface, docs/M0_DESIGN.md section 3): a line is 800 pixels of 3 bytes in memory order R, G, B, no padding = 2,400 bytes =
// 1,200 16-bit words; word i = {byte(2i+1), byte(2i)}. The line buffer holds two lines (the one shown and the one being filled).
//
// The fill protocol is the one of tau_scanout (so tau_talos's fill engine can serve it): at hc 0 of every line the module asks, with a toggle, for the next
// line (fill_line_req = the plane line number, 0..719); the filler writes 1,200 words with lb_we / lb_waddr / lb_wdata on clk_sdram, waddr = {line parity,
// word index 0..1199}, and reports the finished line in fill_done_line (fill_seen sticky). A line that is not complete when its fetch starts is counted.
//
// Pixel path (clk_vid), every stage registered, no more than one 4:1 mux of 24 bits between registers:
//   fetch   hc 12..811: three 32-bit words per group of four pixels (the line buffer is two 16-bit banks read at the same address), one word per clock;
//   group   hc 15 + 4g: the 96-bit group register is loaded {P2, P1, P0};
//   pixel   hc 16 + 4g + k: stage 1 picks pixel k = hc[1:0] of the group register and registers the flags; stage 2 registers the four video outputs.
//
// BUG is a mutation hook for the testbench (0 in the build): each value breaks one rule and the bench must fail for it.
// =============================================================================
`default_nettype none

module tau_scanout_v #(
    parameter integer BUG = 0
) (
    input  wire        reset,
    input  wire        clk_sys,
    input  wire        clk_sdram,
    input  wire        clk_vid,

    input  wire        enable,            // clk_sys level: show the picture (black until it is set)
    input  wire        hold,              // clk_vid: clear the counters (the module is not the picture)
    input  wire [23:0] slot_word,         // the scaler-slot word, sent in the RGB word right after DE falls on every line

    input  wire        lb_we,
    input  wire [11:0] lb_waddr,          // {line parity, word 0..1199}
    input  wire [15:0] lb_wdata,

    // pixel effects (tau_pixfx: the colour tables and the ordered dither); mode 0, lut_on 0 leaves the picture exactly as the plane holds it
    input  wire [1:0]  fx_mode,           // clk_sys: 0 off, 1 ordered dither, 2 ordered + temporal
    input  wire        fx_lut_on,         // clk_sys
    input  wire        fx_lut_we,         // clk_sys: one-cycle strobe for a table store
    input  wire [17:0] fx_lut_word,       // clk_sys: {channel, index, value}
    output wire        fx_lut_busy,       // clk_sys

    input  wire [9:0]  fill_done_line,    // clk_sdram: the plane line of the last finished fill
    input  wire        fill_seen,         // clk_sdram, sticky: at least one fill has finished

    output wire [15:0] underflow_cnt,     // lines whose fill was not complete when the fetch started (Gray count into clk_sys)

    output reg         fill_req_tgl  = 1'b0,
    output reg  [9:0]  fill_line_req = 10'd0,
    output reg         vblank_tgl    = 1'b0,                                           // the vertical-blank toggle, delayed by the pixel-effects latency so it stays one clock ahead of vs

    output reg  [23:0] video_rgb     = 24'd0,
    output reg         video_de      = 1'b0,
    output reg         video_hs      = 1'b0,
    output reg         video_vs      = 1'b0,
    output wire [9:0]  scan_vc,
    output wire        frame_end          // the last clock of a frame
);

    localparam [9:0] H_TOT = 10'd900, V_TOT = 10'd740;
    localparam [9:0] X0 = (BUG == 1) ? 10'd17 : 10'd16, Y0 = 10'd8;                      // first active clock / line
    localparam [9:0] HS_ST = (BUG == 2) ? 10'd841 : 10'd840;                              // 40 clocks
    localparam [9:0] VS_ST = (BUG == 3) ? 10'd735 : 10'd734;                              // 4 lines
    localparam [9:0] VB_LINE = (BUG == 4) ? 10'd733 : 10'd734;                            // vblank toggle line

    // ---- counters ----------------------------------------------------------
    reg [9:0] hc = 10'd0;
    reg [9:0] vc = 10'd0;
    wire h_last = (hc == H_TOT - 10'd1);
    wire v_last = (vc == V_TOT - 10'd1);

    always @(posedge clk_vid) begin
        if (reset || hold) begin
            hc <= 10'd0;
            vc <= 10'd0;
        end else begin
            hc <= h_last ? 10'd0 : hc + 10'd1;
            if (h_last) vc <= v_last ? 10'd0 : vc + 10'd1;
        end
    end

    assign scan_vc = vc;
    assign frame_end = h_last && v_last;

    reg vb_raw = 1'b0, vb1 = 1'b0;
    // ---- fill request and vertical-blank toggle ----------------------------
    wire [9:0] vc_next = v_last ? 10'd0 : vc + 10'd1;
    wire [9:0] vc_req  = (BUG == 5) ? (vc_next + 10'd1) : vc_next;                          // BUG 5: two lines ahead
    wire       req_act = (vc_req >= Y0) && (vc_req <= Y0 + 10'd719);
    wire       next_act = (vc_next >= Y0) && (vc_next <= Y0 + 10'd719);

    always @(posedge clk_vid) begin
        if (hc == 10'd0) begin
            if (req_act) begin
                fill_line_req <= vc_req - Y0;
                fill_req_tgl  <= ~fill_req_tgl;
            end
            if (vc == VB_LINE) vb_raw <= ~vb_raw;
        end
    end
    // the video outputs are 2 clocks later than before the pixel effects were added (their pipeline): the toggle follows by the same 2 extra clocks
    always @(posedge clk_vid) begin vb1 <= vb_raw; vblank_tgl <= vb1; end

    // ---- enable (clk_sys level -> three-flop synchroniser) -----------------
    reg [2:0] en_sync = 3'b000;
    always @(posedge clk_vid) en_sync <= {en_sync[1:0], enable};
    wire en_vid = (BUG == 6) ? 1'b1 : en_sync[2];                                           // BUG 6: not gated by enable

    // ---- line buffer: two 16-bit banks (even / odd word), read as one 32-bit word ----
    (* ramstyle = "M10K" *) reg [15:0] bank_e [0:2047];
    (* ramstyle = "M10K" *) reg [15:0] bank_o [0:2047];

    wire [10:0] wr_pair = {lb_waddr[11], lb_waddr[10:1]};
    always @(posedge clk_sdram) if (lb_we) begin
        if (!lb_waddr[0]) bank_e[wr_pair] <= lb_wdata;
        else              bank_o[wr_pair] <= lb_wdata;
    end

    // read pointer: pair index within the line, restarted at hc 11, one per clock in the first three of every four clocks
    reg [9:0] pa = 10'd0;
    wire      fetch_win = (hc >= 10'd12) && (hc < 10'd812);
    wire [1:0] ph = hc[1:0];                                                               // fetch phase: 12 is a multiple of 4
    always @(posedge clk_vid) begin
        if (hc == 10'd11) pa <= 10'd0;
        else if (fetch_win && ph != 2'd3) pa <= pa + 10'd1;
    end

    wire        half   = (BUG == 7) ? ~vc[0] : vc[0];                                       // BUG 7: wrong line half
    wire [10:0] rd_addr = {half, pa};
    reg  [15:0] rd_e = 16'd0, rd_o = 16'd0;
    always @(posedge clk_vid) begin
        rd_e <= bank_e[rd_addr];
        rd_o <= bank_o[rd_addr];
    end
    wire [31:0] rd_w = {rd_o, rd_e};

    reg [31:0] n0 = 32'd0, n1 = 32'd0;
    reg [95:0] grp = 96'd0;
    always @(posedge clk_vid) begin
        if (ph == 2'd1) n0 <= rd_w;
        if (ph == 2'd2) n1 <= (BUG == 8) ? n0 : rd_w;                                        // BUG 8: second word taken as the first
        if (ph == 2'd3) grp <= {rd_w, n1, n0};
    end

    // ---- stage 1: pixel pick and flags -------------------------------------
    wire act_h = (hc >= X0) && (hc <= X0 + 10'd799);
    wire act_v = (vc >= Y0) && (vc <= Y0 + 10'd719);
    wire hs_w  = (hc >= HS_ST) && (hc <= HS_ST + 10'd39);
    wire vs_w  = (vc >= VS_ST) && (vc <= VS_ST + 10'd3);

    reg [23:0] s1_px = 24'd0;
    reg        s1_de = 1'b0, s1_hs = 1'b0, s1_vs = 1'b0;
    reg [2:0]  s1_x = 3'd0, s1_y = 3'd0;                                                  // the pixel's column and line in the picture (low 3 bits: X0 and Y0 are multiples of 8 less the BUG 1 offset), for the dither matrix
    reg [1:0]  fph = 2'd0;                                                                // frame phase for the temporal dither
    always @(posedge clk_vid) begin
        if (reset || hold) fph <= 2'd0; else if (frame_end) fph <= fph + 2'd1;
        s1_x <= hc[2:0] - X0[2:0];
        s1_y <= vc[2:0] - Y0[2:0];
    end
    always @(posedge clk_vid) begin
        case (hc[1:0])
            2'd0: s1_px <= grp[23:0];
            2'd1: s1_px <= grp[47:24];
            2'd2: s1_px <= grp[71:48];
            2'd3: s1_px <= grp[95:72];
        endcase
        s1_de <= act_h && act_v;
        s1_hs <= hs_w;
        s1_vs <= vs_w;
    end

    // ---- pixel effects, 2 clocks (grp holds {B,G,R} per pixel, the effects and the bus want {R,G,B}) ----
    wire [23:0] rgb_x = (BUG == 9) ? s1_px : {s1_px[7:0], s1_px[15:8], s1_px[23:16]};        // BUG 9: byte order not swapped
    wire [23:0] fx_px; wire fx_de, fx_hs, fx_vs;
    tau_pixfx u_fx (.clk_sys(clk_sys), .clk_vid(clk_vid), .reset(reset), .mode(fx_mode), .lut_on(fx_lut_on), .lut_we(fx_lut_we), .lut_word(fx_lut_word), .lut_busy(fx_lut_busy),
        .px_in(rgb_x), .de_in(s1_de), .hs_in(s1_hs), .vs_in(s1_vs), .x_in(s1_x), .y_in(s1_y), .f_in(fph),
        .px_out(fx_px), .de_out(fx_de), .hs_out(fx_hs), .vs_out(fx_vs));

    // ---- stage 2: the video outputs ----
    always @(posedge clk_vid) begin
        video_rgb <= (fx_de && en_vid) ? fx_px : ((video_de && !fx_de && BUG != 11) ? slot_word : 24'd0);     // BUG 11: no slot word
        video_de  <= fx_de;
        video_hs  <= fx_hs;
        video_vs  <= fx_vs;
    end

    // ---- late-line counter ---------------------------------------------------
    wire [9:0] done_line_vid;
    tau_cdc_gray_bus #(.W(10), .STAGES(3)) u_done_line (
        .clk_src(clk_sdram), .d_src(fill_done_line),
        .clk_dst(clk_vid),   .q_dst(done_line_vid)
    );

    reg [2:0] seen_sync = 3'b000;
    always @(posedge clk_vid) seen_sync <= {seen_sync[1:0], fill_seen};

    wire [9:0] chk_hc = 10'd12;                                                              // the fetch of the line starts at hc 12
    wire [9:0] n_line = vc - Y0;
    wire       chk    = (hc == chk_hc) && act_v && seen_sync[2];
    wire       ok_now = (done_line_vid == n_line);
    wire       ok_nxt = next_act && (done_line_vid == n_line + 10'd1);

    reg late_pulse = 1'b0;
    always @(posedge clk_vid) late_pulse <= (BUG == 10) ? 1'b0 : (chk && !(ok_now || ok_nxt));                             // BUG 10: a late line is never counted

    tau_cdc_gray_ctr #(.WIDTH(16)) u_late_cnt (
        .clk_src(clk_vid), .rst_src(reset), .inc(late_pulse),
        .clk_dst(clk_sys), .rst_dst(reset), .count_dst(underflow_cnt)
    );

endmodule

`default_nettype wire
