// tau_tgy_recon.sv -- TGY pixel stage: chroma upsampling (TGY profile, tile local) and YCbCr to RGB, bit for bit libjpeg (docs/TGY_RECONSTRUCTION_V0.md sections 4 and 5).
// Two modules. tau_tgy_ycc: the integer colour conversion, 2 clocks. tau_tgy_recon: holds the sample planes of ONE tile (Y 128 x 128, Cb and Cr 64 x 64, or 128 x 128 when C444 = 1), written 8 samples at a
// time in the IDCT unit's row format, and streams the tile's RGB pixels in raster order, one pixel per clock, upsampling the chroma of a 4:2:0 tile with libjpeg's h2v2 fancy filter applied to the tile alone.
// Design (docs/TGY_HW_IDCT.md section 6): engine A builds, one output row ahead, the VERTICAL part s[x] = 3 * p[row] + p[other row] of the filter into a line buffer (two banks, ping-pong); engine B runs the HORIZONTAL part
// out = (3 * s[x] + s[x -/+ 1] + 8 or 7) >> 4 with the tile edge replicated (this is exactly libjpeg's first/last column formulae), reads Y, and feeds the colour conversion. A grey tile is a 4:4:4 tile with
// Cb = Cr = 128, which the colour tables turn into R = G = B = Y. A chroma plane no wider than 2 samples uses the box filter (libjpeg's rule, kept by the spec).
// Interface rules: write all planes (we, wcomp 0 = Y, 1 = Cb, 2 = Cr, wx = sample column of the 8 samples (multiple of 8), wy = row, wdata byte k = column wx + k) BEFORE start. start with fmt (0 grey,
// 1 4:4:4, 2 4:2:0), tw, th = the tile's size in luma samples (1 to 128). Output px_valid / px_x / px_y / px_r,g,b / px_last, no back pressure; `hold` is looked at between output ROWS only (a row once begun
// is delivered in 128 or fewer consecutive clocks), so the consumer stops the stream at a row boundary. 4:4:4 needs C444 = 1 (twice the chroma memory); without it fmt 1 raises err. Not synthesised yet:
// the memories are written so that Quartus infers simple dual port RAM (16 M10K for Y, 8 + 8 per chroma plane duplicated for the two read ports: an estimate, see the document).
// BUG (test only): 0 = correct; 1..8 mutate tau_tgy_ycc, 11-18 and 20 mutate tau_tgy_recon (19 unused); each is killed by sim/tb_tau_tgy_recon.v.
module tau_tgy_ycc #(parameter integer BUG = 0) (
    input  wire       clk,
    input  wire [7:0] y, cb, cr,
    output reg  [7:0] r, g, b
);
    localparam signed [31:0] K_CR_R = (BUG == 1) ? 32'sd91800 : 32'sd91881;
    localparam signed [31:0] K_CB_B = (BUG == 2) ? 32'sd116100 : 32'sd116130;
    localparam signed [31:0] K_CR_G = (BUG == 3) ? 32'sd46801 : 32'sd46802;
    localparam signed [31:0] K_CB_G = (BUG == 4) ? 32'sd22555 : 32'sd22554;
    localparam signed [31:0] RND_RB = (BUG == 5) ? 32'sd30000 : 32'sd32768;
    localparam signed [31:0] RND_G  = (BUG == 6) ? 32'sd0 : 32'sd32768;
    wire signed [31:0] xr = $signed({24'd0, cr}) - 32'sd128;
    wire signed [31:0] xb = $signed({24'd0, cb}) - 32'sd128;
    reg [7:0] y1; reg signed [31:0] rr, bb, gg;
    always @(posedge clk) begin
        y1 <= y;
        rr <= (K_CR_R * xr + RND_RB) >>> 16;
        bb <= (K_CB_B * xb + RND_RB) >>> 16;
        gg <= ((RND_G - K_CB_G * xb) + (0 - K_CR_G * xr)) >>> 16;
    end
    wire signed [31:0] sr = $signed({24'd0, y1}) + rr, sg = $signed({24'd0, y1}) + gg, sb = $signed({24'd0, y1}) + bb;
    always @(posedge clk) begin
        r <= (sr < 0) ? 8'd0 : (sr > 255 && BUG != 7) ? 8'd255 : sr[7:0];
        g <= (sg < 0 && BUG != 8) ? 8'd0 : (sg > 255) ? 8'd255 : sg[7:0];
        b <= (sb < 0) ? 8'd0 : (sb > 255) ? 8'd255 : sb[7:0];
    end
endmodule

module tau_tgy_recon #(parameter integer C444 = 0, parameter integer BUG = 0) (
    input  wire        clk, rst,
    input  wire        we,
    input  wire [1:0]  wcomp,
    input  wire [6:0]  wx, wy,
    input  wire [63:0] wdata,
    input  wire        start,
    input  wire [1:0]  fmt,
    input  wire [7:0]  tw, th,
    input  wire        hold,
    output wire        busy,
    output reg         done, err,
    output reg         px_valid,
    output reg  [6:0]  px_x, px_y,
    output wire [7:0]  px_r, px_g, px_b,
    output reg         px_last
);
    localparam integer CB = C444 ? 7 : 6;
    // ---- plane memories: 64-bit words, address {row, column[..:3]}
    reg [63:0] ymem [0:2047];
    reg [63:0] cbmem [0:(1 << (2 * CB - 3)) - 1];
    reg [63:0] crmem [0:(1 << (2 * CB - 3)) - 1];
    function [2 * CB - 4:0] caddr(input [6:0] row, input [6:0] col);
        caddr = {row[CB - 1:0], col[CB - 1:3]};
    endfunction
    always @(posedge clk) if (we && wcomp == 2'd0) ymem[{wy, wx[6:3]}] <= wdata;
    always @(posedge clk) if (we && wcomp == 2'd1) cbmem[caddr(wy, wx)] <= wdata;
    always @(posedge clk) if (we && wcomp == 2'd2) crmem[caddr(wy, wx)] <= wdata;

    // ---- job registers
    reg run; reg [1:0] f; reg [7:0] w, h; reg [6:0] cw, chh;
    assign busy = run;
    wire fmt_ok = (fmt == 2'd0 || fmt == 2'd2 || (fmt == 2'd1 && C444 != 0)) && tw != 8'd0 && tw <= 8'd128 && th != 8'd0 && th <= 8'd128;
    wire box = (BUG == 15) ? 1'b0 : (cw <= 7'd2);

    // ---- engine A: vertical part, one output row ahead
    reg a_act; reg [7:0] a_row; reg [6:0] a_x; reg [1:0] full;
    reg ad_v, ad_last, ad_bank; reg [6:0] ad_x;
    wire [6:0] a_r = a_row[7:1];
    wire [6:0] a_dn = (BUG == 17) ? a_r + 7'd1 : ((a_r + 7'd1 >= chh) ? a_r : a_r + 7'd1);
    wire [6:0] a_up = (a_r == 7'd0) ? 7'd0 : a_r - 7'd1;
    wire [6:0] a_oth = ((BUG == 14) ? !a_row[0] : a_row[0]) ? a_dn : a_up;
    // ---- engine B: horizontal part, Y, colour
    reg b_act; reg [7:0] b_row; reg [6:0] b_x;
    wire [6:0] b_y = b_row[6:0];
    wire b_lastpx = (b_x == w[6:0] - 7'd1) || (w == 8'd128 && b_x == 7'd127);
    wire b_lastrow = (b_row == h - 8'd1);

    // memory read ports (registered)
    reg [63:0] yq, cbq, crq, cbo, cro;
    wire [6:0] cur_row = (f == 2'd2) ? a_r : b_y;
    wire [6:0] cur_col = (f == 2'd2) ? a_x : b_x;
    always @(posedge clk) begin
        yq  <= ymem[{b_y, b_x[6:3]}];
        cbq <= cbmem[caddr(cur_row, cur_col)];
        crq <= crmem[caddr(cur_row, cur_col)];
        cbo <= cbmem[caddr(a_oth, a_x)];
        cro <= crmem[caddr(a_oth, a_x)];
    end

    // line buffers: s = 3 * p + other (box: 4 * p), two banks of 64
    reg [9:0] sCb [0:127];
    reg [9:0] sCr [0:127];
    wire [7:0] cbcur = cbq[8 * ad_x[2:0] +: 8], crcur = crq[8 * ad_x[2:0] +: 8];
    wire [7:0] cboth = cbo[8 * ad_x[2:0] +: 8], croth = cro[8 * ad_x[2:0] +: 8];
    wire [9:0] s_cb = box ? {cbcur, 2'b00} : (3 * {2'b00, cbcur} + {2'b00, cboth});
    wire [9:0] s_cr = box ? {crcur, 2'b00} : (3 * {2'b00, crcur} + {2'b00, croth});
    always @(posedge clk) if (ad_v) begin sCb[{ad_bank, ad_x[5:0]}] <= s_cb; sCr[{ad_bank, ad_x[5:0]}] <= s_cr; end

    // horizontal filter at the issue stage of B
    wire        bank = b_row[0];
    wire [5:0]  cx = b_x[6:1];
    wire [5:0]  i0 = cx;
    wire [5:0]  im = (cx == 6'd0) ? ((BUG == 13 && cw > 7'd1) ? 6'd1 : 6'd0) : cx - 6'd1;
    wire [5:0]  ip = (cx + 7'd1 >= cw) ? ((BUG == 16 && cx != 6'd0) ? cx - 6'd1 : cx) : cx + 6'd1;
    wire odd = b_x[0];
    wire [9:0] cb0 = sCb[{bank, i0}], cbm = sCb[{bank, im}], cbp = sCb[{bank, ip}];
    wire [9:0] cr0 = sCr[{bank, i0}], crm = sCr[{bank, im}], crp = sCr[{bank, ip}];
    wire [3:0] rnd = odd ? ((BUG == 11) ? 4'd8 : 4'd7) : ((BUG == 12) ? 4'd7 : 4'd8);
    wire [13:0] cbsum = 3 * {4'd0, cb0} + {4'd0, odd ? cbp : cbm} + {10'd0, rnd};
    wire [13:0] crsum = 3 * {4'd0, cr0} + {4'd0, odd ? crp : crm} + {10'd0, rnd};
    wire [7:0] cbh = box ? cb0[9:2] : cbsum[11:4];
    wire [7:0] crh = box ? cr0[9:2] : crsum[11:4];

    // stage 1 registers (aligned with the memory read data)
    reg v1, l1; reg [6:0] x1, y1r; reg [7:0] cbh1, crh1;
    always @(posedge clk) begin
        v1 <= b_act; l1 <= b_act && b_lastpx && b_lastrow; x1 <= b_x; y1r <= b_y; cbh1 <= cbh; crh1 <= crh;
    end
    wire [7:0] ybyte = yq[8 * x1[2:0] +: 8];
    wire [7:0] c1b = cbq[8 * x1[2:0] +: 8], c1r = crq[8 * x1[2:0] +: 8];
    wire [7:0] gray = (BUG == 18) ? 8'd127 : 8'd128;
    wire [7:0] ucb = (f == 2'd2) ? cbh1 : (f == 2'd1) ? ((BUG == 20) ? c1r : c1b) : gray;
    wire [7:0] ucr = (f == 2'd2) ? crh1 : (f == 2'd1) ? ((BUG == 20) ? c1b : c1r) : gray;
    tau_tgy_ycc #(.BUG(BUG < 10 ? BUG : 0)) ycc (.clk(clk), .y(ybyte), .cb(ucb), .cr(ucr), .r(px_r), .g(px_g), .b(px_b));
    reg v2, l2, v3; reg [6:0] x2, y2r;
    always @(posedge clk) begin
        v2 <= v1; l2 <= l1; x2 <= x1; y2r <= y1r;
        px_valid <= v2 && run; px_last <= l2; px_x <= x2; px_y <= y2r;
    end

    // ---- control
    always @(posedge clk) begin
        done <= 1'b0; err <= 1'b0;
        if (rst) begin run <= 1'b0; a_act <= 1'b0; b_act <= 1'b0; ad_v <= 1'b0; full <= 2'b00; end
        else begin
            ad_v <= 1'b0;
            if (start && !run) begin
                if (!fmt_ok) err <= 1'b1;
                else begin
                    run <= 1'b1; f <= fmt; w <= tw; h <= th; cw <= (tw[7:0] + 8'd1) >> 1; chh <= (th[7:0] + 8'd1) >> 1;
                    a_act <= 1'b0; b_act <= 1'b0; a_row <= 8'd0; b_row <= 8'd0; full <= 2'b00;
                end
            end else if (run) begin
                // engine A
                if (a_act) begin
                    ad_v <= 1'b1; ad_x <= a_x; ad_bank <= a_row[0]; ad_last <= (a_x == cw - 7'd1);
                    if (a_x == cw - 7'd1) begin a_act <= 1'b0; a_row <= a_row + 8'd1; end
                    else a_x <= a_x + 7'd1;
                end else if (f == 2'd2 && a_row < h && !full[a_row[0]] && !(ad_v && ad_bank == a_row[0])) begin
                    a_act <= 1'b1; a_x <= 7'd0;
                end
                if (ad_v && ad_last) full[ad_bank] <= 1'b1;
                // engine B
                if (b_act) begin
                    if (b_lastpx) begin
                        b_act <= 1'b0; b_row <= b_row + 8'd1;
                        if (f == 2'd2) full[b_row[0]] <= 1'b0;
                    end else b_x <= b_x + 7'd1;
                end else if (b_row < h && !hold && (f != 2'd2 || full[b_row[0]])) begin
                    b_act <= 1'b1; b_x <= 7'd0;
                end
                if (px_valid && px_last) begin run <= 1'b0; done <= 1'b1; end
            end
        end
    end
endmodule
