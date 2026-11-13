// =============================================================================
// tau_pixfx.sv -- Tau Gallery pixel effects in the viewer scan-out: a per-channel colour table (K5) and an ordered dither (K1).
//
// Why: the Pocket's handheld panel keeps only 5 / 6 / 5 bits of the 8-bit picture (docs/M0_DESIGN.md section 6: truncation, measured), so skies and gradients band.
// An ordered dither of the low bits before the panel truncates them gives the eye the missing levels. The colour table (brightness, contrast, gamma, tints, invert) is
// three 256 x 8 tables loaded by the CPU.
//
// Pipeline on clk_vid, latency 2 (the flags are delayed to match, so the sync and data-enable alignment of the scan-out does not move):
//   A: table read  (R, G, B each from its own table), the raw channels and the flags registered beside it
//   B: pick table or raw (lut_on), add the dither threshold with saturation, register the outputs
//
// Dither: an 8 x 8 Bayer matrix m(x, y) of 0..63 (x, y = the pixel's position in the picture, low 3 bits). The threshold added is m >> 3 (0..7) to red and blue (5 bits
// kept: step 8) and m >> 4 (0..3) to green (6 bits kept: step 4), saturating at 255; the panel then truncates. mode 0 = off (threshold 0), 1 = ordered, 2 = ordered with
// the matrix position rotated every frame by the frame phase f (a temporal dither: four patterns, 15 Hz each at 60 Hz).
//
// Colour table loads: one store from the CPU (lut_we with lut_word = {channel 2 bits (0 R, 1 G, 2 B), index 8 bits, value 8 bits}) is held, a request toggle crosses to
// clk_vid, the table is written there and an acknowledge toggle comes back; lut_busy is high from the store until the acknowledge, so the CPU polls it before the next
// store. mode and lut_on change rarely (quasi-static) and cross through two flops.
//
// BUG is a mutation hook for the testbench (0 in the build): each value breaks one rule and the bench must fail for it.
// =============================================================================
`default_nettype none

module tau_pixfx #(
    parameter integer BUG = 0
) (
    input  wire        clk_sys,
    input  wire        clk_vid,
    input  wire        reset,

    input  wire [1:0]  mode,          // clk_sys: 0 off, 1 ordered dither, 2 ordered + temporal
    input  wire        lut_on,        // clk_sys: 1 = the colour tables are applied, 0 = the picture passes as it is
    input  wire        lut_we,        // clk_sys: one-cycle strobe, ignored while lut_busy
    input  wire [17:0] lut_word,      // {channel[1:0], index[7:0], value[7:0]}
    output wire        lut_busy,      // clk_sys

    input  wire [23:0] px_in,         // clk_vid: {R, G, B}
    input  wire        de_in,
    input  wire        hs_in,
    input  wire        vs_in,
    input  wire [2:0]  x_in,          // the pixel's column and line in the picture, low 3 bits
    input  wire [2:0]  y_in,
    input  wire [1:0]  f_in,          // frame phase

    output reg  [23:0] px_out        = 24'd0,
    output reg         de_out        = 1'b0,
    output reg         hs_out        = 1'b0,
    output reg         vs_out        = 1'b0
);

    // ---- the three tables, identity at power-up ----------------------------
    (* ramstyle = "M10K" *) reg [7:0] lut_r [0:255];
    (* ramstyle = "M10K" *) reg [7:0] lut_g [0:255];
    (* ramstyle = "M10K" *) reg [7:0] lut_b [0:255];
    integer ii;
    initial for (ii = 0; ii < 256; ii = ii + 1) begin lut_r[ii] = ii[7:0]; lut_g[ii] = ii[7:0]; lut_b[ii] = ii[7:0]; end

    // ---- table load handshake (clk_sys -> clk_vid -> clk_sys) ----------------
    reg [17:0] hold     = 18'd0;
    reg        req_tgl  = 1'b0;
    reg [1:0]  ack_sync = 2'b00;
    reg        ack_tgl  = 1'b0;                                                   // clk_vid
    assign lut_busy = (req_tgl != ack_sync[1]);

    always @(posedge clk_sys) begin
        ack_sync <= {ack_sync[0], ack_tgl};
        if (reset) begin
            req_tgl <= ack_sync[1];                                                  // not busy after a reset
        end else if (lut_we && !lut_busy) begin
            hold    <= lut_word;
            req_tgl <= ~req_tgl;
        end
    end

    reg [2:0] req_sync = 3'b000;
    always @(posedge clk_vid) begin
        req_sync <= {req_sync[1:0], req_tgl};
        if (req_sync[2] != req_sync[1]) begin
            case (hold[17:16])
                2'd0: lut_r[(BUG == 8) ? hold[15:8] + 8'd1 : hold[15:8]] <= hold[7:0];       // BUG 8: written one slot off
                2'd1: if (BUG == 9) lut_b[hold[15:8]] <= hold[7:0]; else lut_g[hold[15:8]] <= hold[7:0];       // BUG 9: a green store goes to the blue table
                2'd2: lut_b[hold[15:8]] <= hold[7:0];
                default: ;
            endcase
            ack_tgl <= ~ack_tgl;
        end
    end

    // ---- quasi-static configuration into clk_vid -------------------------------
    reg [2:0] cfg_a = 3'd0, cfg_b = 3'd0;
    always @(posedge clk_vid) begin cfg_a <= {lut_on, mode}; cfg_b <= cfg_a; end
    wire [1:0] mode_v   = cfg_b[1:0];
    wire       lut_on_v = cfg_b[2];

    // ---- stage A: table read, raw channels, flags -----------------------------
    reg [7:0] ta_r = 8'd0, ta_g = 8'd0, ta_b = 8'd0;
    reg [7:0] ra_r = 8'd0, ra_g = 8'd0, ra_b = 8'd0;
    reg [2:0] xa = 3'd0, ya = 3'd0;
    reg [1:0] fa = 2'd0;
    reg       dea = 1'b0, hsa = 1'b0, vsa = 1'b0;
    always @(posedge clk_vid) begin
        ta_r <= lut_r[(BUG == 5) ? px_in[15:8] : px_in[23:16]];                        // BUG 5: red read from the green index
        ta_g <= lut_g[px_in[15:8]];
        ta_b <= lut_b[px_in[7:0]];
        ra_r <= px_in[23:16]; ra_g <= px_in[15:8]; ra_b <= px_in[7:0];
        xa <= x_in; ya <= y_in; fa <= f_in;
        dea <= de_in; hsa <= hs_in; vsa <= vs_in;
    end

    // ---- the 8 x 8 Bayer matrix ---------------------------------------------
    // m = {x0^y0, y0, x1^y1, y1, x2^y2, y2}: the recursive Bayer ordering (0 .. 63), each of the 64 values once (checked against the textbook recursion in sim/test_rtl_pixfx.py).
    function automatic [5:0] bayer(input [2:0] x, input [2:0] y);
        bayer = (BUG == 1) ? {x[0], 1'b0, x[1], 1'b0, x[2], 1'b0}                          // BUG 1: the line does not take part
                           : {x[0] ^ y[0], y[0], x[1] ^ y[1], y[1], x[2] ^ y[2], y[2]};
    endfunction

    // ---- stage B: pick, dither, saturate ------------------------------------------
    wire [2:0] xs = (mode_v == 2'd2 && BUG != 7) ? (xa ^ {1'b0, fa[1], fa[0]}) : xa;        // BUG 7: the temporal mode ignores the frame phase
    wire [2:0] ys = (mode_v == 2'd2 && BUG != 7) ? (ya ^ {fa[0], 1'b0, fa[1]}) : ya;
    wire [5:0] m  = (BUG == 10) ? bayer(ys, xs) : bayer(xs, ys);                               // BUG 10: the coordinates swapped
    wire       dith = (mode_v != 2'd0);

    wire [7:0] vr = lut_on_v ? ta_r : ra_r;
    wire [7:0] vg = lut_on_v ? ta_g : ra_g;
    wire [7:0] vb = lut_on_v ? ta_b : ra_b;
    wire [2:0] t_rb = dith ? ((BUG == 2) ? m[4:2] : m[5:3]) : 3'd0;                            // BUG 2: the wrong bits of the matrix for red and blue
    wire [1:0] t_g  = dith ? ((BUG == 3) ? m[4:3] : m[5:4]) : 2'd0;                            // BUG 3: the wrong bits for green
    wire [8:0] sr = {1'b0, vr} + {6'd0, t_rb};
    wire [8:0] sg = {1'b0, vg} + {7'd0, t_g};
    wire [8:0] sb = {1'b0, vb} + {6'd0, t_rb};
    wire [7:0] or8 = (BUG == 4) ? sr[7:0] : (sr[8] ? 8'hFF : sr[7:0]);                         // BUG 4: no saturation (wraps)
    wire [7:0] og8 = sg[8] ? 8'hFF : sg[7:0];
    wire [7:0] ob8 = sb[8] ? 8'hFF : sb[7:0];

    always @(posedge clk_vid) begin
        px_out <= {or8, og8, ob8};
        de_out <= dea;
        hs_out <= hsa;
        vs_out <= (BUG == 6) ? vs_in : vsa;                                                    // BUG 6: the vertical sync is not delayed
    end

endmodule

`default_nettype wire
