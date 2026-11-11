// tau_tgy_dec.sv -- the TGY tile decoder: entropy decoder (tau_tgy_ent) + tile controller (tau_tgy_tile, which holds the IDCT and the pixel stage). One tile stream in as 32-bit words, RGB pixels out in raster order.
// Usage: write the two quantisation tables (qwe, qsel 0 = Y / 1 = Cb and Cr, qidx = zigzag index, qval) ONCE per picture; per tile pulse start with fmt (0 grey, 1 4:4:4, 2 4:2:0), tw, th (the tile's size in luma
// samples, 1 to 128) and tile_len (the stream's length in bytes), then feed the stream through in_valid / in_word / in_ready (little-endian words; the bytes past tile_len are ignored and read as 0); read the pixels
// (px_valid, px_x, px_y, px_r/g/b, px_last; `hold` pauses between rows). done after the last pixel; err (pulse) if the decoder or the watchdog gave up (the unit is idle again). docs/TGY_HW_DECODER.md.
module tau_tgy_dec #(parameter integer C444 = 0) (
    input  wire        clk, rst,
    input  wire        qwe, qsel,
    input  wire [5:0]  qidx,
    input  wire [15:0] qval,
    input  wire        start,
    input  wire [1:0]  fmt,
    input  wire [7:0]  tw, th,
    input  wire [19:0] tile_len,
    input  wire        in_valid,
    input  wire [31:0] in_word,
    output wire        in_ready,
    input  wire        hold,
    output wire        busy, done, err,
    output wire        px_valid,
    output wire [6:0]  px_x, px_y,
    output wire [7:0]  px_r, px_g, px_b,
    output wire        px_last
);
    wire dec_tile, dec_blk, dec_abort, dec_left, dec_above, c_we, blk_done, dec_err; wire [1:0] dec_comp; wire [3:0] dec_bx; wire [5:0] c_zz; wire signed [15:0] c_val;
    reg [19:0] len_r; always @(posedge clk) if (start) len_r <= tile_len;
    wire tile_err;
    tau_tgy_tile #(.C444(C444)) tile (.clk(clk), .rst(rst), .qwe(qwe), .qsel(qsel), .qidx(qidx), .qval(qval), .start(start), .fmt(fmt), .tw(tw), .th(th), .busy(busy), .done(done), .err(tile_err),
        .dec_tile(dec_tile), .dec_blk(dec_blk), .dec_abort(dec_abort), .dec_comp(dec_comp), .dec_bx(dec_bx), .dec_left(dec_left), .dec_above(dec_above), .c_we(c_we), .c_zz(c_zz), .c_val(c_val),
        .blk_done(blk_done), .dec_err(dec_err), .hold(hold), .px_valid(px_valid), .px_x(px_x), .px_y(px_y), .px_r(px_r), .px_g(px_g), .px_b(px_b), .px_last(px_last));
    assign err = tile_err;
    tau_tgy_ent ent (.clk(clk), .rst(rst), .dec_tile(dec_tile), .tile_len(len_r), .in_valid(in_valid), .in_word(in_word), .in_ready(in_ready), .dec_blk(dec_blk), .dec_comp(dec_comp), .dec_bx(dec_bx),
        .dec_left(dec_left), .dec_above(dec_above), .dec_abort(dec_abort), .c_we(c_we), .c_zz(c_zz), .c_val(c_val), .blk_done(blk_done), .dec_err(dec_err));
endmodule
