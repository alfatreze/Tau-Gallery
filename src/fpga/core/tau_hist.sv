// =============================================================================
// tau_hist.sv -- Tau Gallery histogram unit (K4): four 256-bin histograms (luma, red, green, blue) of the pixels the TGY decoder writes, counted on the fly.
//
// Why: the details page shows histograms of the picture. Built from the thumbnail they are a guess (a thumbnail of a letterboxed picture, 96 x 96 pixels); counted here from
// the pixel stream of the FIT level's decode (the decoder writes it anyway when a picture opens, about 40 tiles) they are the picture's own, exact for what was decoded, at no
// CPU cost. The same unit counts the tiles of a zoomed view if the firmware asks.
//
// The tap is the decoder's pixel stage: px_valid with the pixel's position in its 128 x 128 tile (px_x, px_y) and R, G, B (8 bits). Clock: the decoder's (clk).
// Window: a pixel is counted only when its CANVAS position (org_x + px_x, org_y + px_y; org = the canvas position of the tile's pixel (0, 0), set before the tile starts) lies in
// [rect_x0, rect_x1) x [rect_y0, rect_y1), so the black bars of a letterboxed fit canvas are not counted. en = 0 counts nothing.
// Luma = (77 R + 150 G + 29 B) >> 8 (0..255). Counts are 24 bits; n_counted is the number of pixels counted.
// Clear: a toggle on clr_req_tgl sweeps all 4 x 256 bins to 0 (256 clocks, clr_busy high); nothing is counted while it runs.
// Read: rd_addr = {histogram 0 luma / 1 red / 2 green / 3 blue, bin}; rd_data follows 2 clocks later.
// Rate: one pixel a clock. A read-modify-write pipeline of 3 stages with forwarding, so consecutive pixels of the same bin count correctly.
//
// BUG is a mutation hook for the testbench (0 in the build): each value breaks one rule and the bench must fail for it.
// =============================================================================
`default_nettype none

module tau_hist #(
    parameter integer BUG = 0
) (
    input  wire        clk,
    input  wire        rst,

    input  wire        px_valid,
    input  wire [6:0]  px_x,
    input  wire [6:0]  px_y,
    input  wire [7:0]  px_r,
    input  wire [7:0]  px_g,
    input  wire [7:0]  px_b,

    input  wire        en,
    input  wire [9:0]  org_x,
    input  wire [9:0]  org_y,
    input  wire [9:0]  rect_x0,
    input  wire [9:0]  rect_y0,
    input  wire [9:0]  rect_x1,
    input  wire [9:0]  rect_y1,

    input  wire        clr_req_tgl,
    output reg         clr_busy   = 1'b0,

    input  wire [9:0]  rd_addr,
    output reg  [23:0] rd_data    = 24'd0,
    output reg  [23:0] n_counted  = 24'd0
);

    (* ramstyle = "M10K" *) reg [23:0] ram_y [0:255];
    (* ramstyle = "M10K" *) reg [23:0] ram_r [0:255];
    (* ramstyle = "M10K" *) reg [23:0] ram_g [0:255];
    (* ramstyle = "M10K" *) reg [23:0] ram_b [0:255];
    integer ii;
    initial for (ii = 0; ii < 256; ii = ii + 1) begin ram_y[ii] = 24'd0; ram_r[ii] = 24'd0; ram_g[ii] = 24'd0; ram_b[ii] = 24'd0; end

    // ---- clear sweep -------------------------------------------------------------
    reg        clr_seen = 1'b0;
    reg [8:0]  sweep = 9'd0;                                                      // 0..255 running, 256 = idle
    wire       sweeping = clr_busy;
    always @(posedge clk) begin
        clr_seen <= clr_req_tgl;
        if (rst) begin clr_busy <= 1'b0; sweep <= 9'd0; end
        else if (clr_req_tgl != clr_seen) begin clr_busy <= 1'b1; sweep <= 9'd0; end
        else if (clr_busy) begin
            if (sweep == ((BUG == 6) ? 9'd127 : 9'd255)) clr_busy <= 1'b0;                  // BUG 6: only half of the bins are cleared
            sweep <= sweep + 9'd1;
        end
    end

    // ---- stage 0: window test and bin numbers ----------------------------------------
    wire [10:0] cx = {1'b0, (BUG == 5) ? 10'd0 : org_x} + {4'd0, px_x};                    // BUG 5: the tile origin is not added
    wire [10:0] cy = {1'b0, org_y} + {4'd0, px_y};
    wire        in_win = (cx >= {1'b0, rect_x0}) && ((BUG == 3) ? (cx <= {1'b0, rect_x1}) : (cx < {1'b0, rect_x1})) && (cy >= {1'b0, rect_y0}) && (cy < {1'b0, rect_y1});     // BUG 3: the right edge is inside
    wire        take   = ((BUG == 4) ? 1'b1 : en) && px_valid && in_win && !sweeping;                                                                                            // BUG 4: en ignored
    wire [15:0] lsum = (BUG == 2) ? ({8'd0, px_r} + {8'd0, px_g} + {8'd0, px_b}) / 16'd3 * 16'd256 : (16'd77 * px_r + 16'd150 * px_g + 16'd29 * px_b);                      // BUG 2: a plain average
    wire [7:0]  luma = lsum[15:8];

    reg        v0 = 1'b0;
    reg [7:0]  a0y = 8'd0, a0r = 8'd0, a0g = 8'd0, a0b = 8'd0;
    always @(posedge clk) begin
        v0  <= take;
        a0y <= luma;
        a0r <= (BUG == 7) ? px_b : px_r;                                          // BUG 7: red and blue swapped
        a0g <= px_g;
        a0b <= (BUG == 7) ? px_r : px_b;
    end

    // ---- stage 1: read the four bins --------------------------------------------------
    reg        v1 = 1'b0;
    reg [7:0]  a1y = 8'd0, a1r = 8'd0, a1g = 8'd0, a1b = 8'd0;
    reg [23:0] d1y = 24'd0, d1r = 24'd0, d1g = 24'd0, d1b = 24'd0;
    always @(posedge clk) begin
        v1  <= v0;
        a1y <= a0y; a1r <= a0r; a1g <= a0g; a1b <= a0b;
        d1y <= ram_y[a0y]; d1r <= ram_r[a0r]; d1g <= ram_g[a0g]; d1b <= ram_b[a0b];
    end

    // ---- stage 2: add one (with the value just written when the same bin comes twice in a row) and write ------------------------------------
    reg        vp = 1'b0;
    reg [7:0]  pay = 8'd0, par = 8'd0, pag = 8'd0, pab = 8'd0;
    reg [23:0] pvy = 24'd0, pvr = 24'd0, pvg = 24'd0, pvb = 24'd0;
    wire [23:0] by = (BUG != 1 && vp && pay == a1y) ? pvy : d1y;                  // BUG 1: no forwarding
    wire [23:0] br = (BUG != 1 && vp && par == a1r) ? pvr : d1r;
    wire [23:0] bg = (BUG != 1 && vp && pag == a1g) ? pvg : d1g;
    wire [23:0] bb = (BUG != 1 && vp && pab == a1b) ? pvb : d1b;
    always @(posedge clk) begin
        if (sweeping) begin
            ram_y[sweep[7:0]] <= 24'd0; ram_r[sweep[7:0]] <= 24'd0; ram_g[sweep[7:0]] <= 24'd0; ram_b[sweep[7:0]] <= 24'd0;
            vp <= 1'b0;
            n_counted <= (BUG == 8) ? n_counted : 24'd0;                           // BUG 8: the count is not cleared
        end else begin
            vp <= v1;
            if (v1) begin
                ram_y[a1y] <= by + 24'd1; ram_r[a1r] <= br + 24'd1; ram_g[a1g] <= bg + 24'd1; ram_b[a1b] <= bb + 24'd1;
                pay <= a1y; par <= a1r; pag <= a1g; pab <= a1b;
                pvy <= by + 24'd1; pvr <= br + 24'd1; pvg <= bg + 24'd1; pvb <= bb + 24'd1;
                n_counted <= n_counted + 24'd1;
            end
        end
    end

    // ---- read port: 2 clocks ---------------------------------------------------------------
    reg [23:0] q_y = 24'd0, q_r = 24'd0, q_g = 24'd0, q_b = 24'd0;
    reg [1:0]  rsel = 2'd0;
    always @(posedge clk) begin
        q_y <= ram_y[rd_addr[7:0]]; q_r <= ram_r[rd_addr[7:0]]; q_g <= ram_g[rd_addr[7:0]]; q_b <= ram_b[rd_addr[7:0]];
        rsel <= rd_addr[9:8];
        rd_data <= (rsel == 2'd0) ? q_y : (rsel == 2'd1) ? q_r : (rsel == 2'd2) ? q_g : q_b;
    end

endmodule

`default_nettype wire
