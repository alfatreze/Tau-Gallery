// Free-input wrapper for src/fpga/core/tau_tgy_tile.sv (properties: tile_props.vh, included by the RTL under `ifdef FORMAL). One reset step, then every input is free within the contract assumed in the props file.
module tau_tgy_tile_formal #(parameter integer C444 = 0, parameter integer BUG = 0) ();
    (* gclk *) wire clk;
    reg rst_q = 1'b1; always @(posedge clk) rst_q <= 1'b0;
    wire rst = rst_q;
    (* anyseq *) wire qwe, qsel, start, c_we, blk_done, dec_err, hold; (* anyseq *) wire [5:0] qidx, c_zz; (* anyseq *) wire [15:0] qval; (* anyseq *) wire [1:0] fmt; (* anyseq *) wire [7:0] tw, th; (* anyseq *) wire signed [15:0] c_val;
    wire busy, done, err, dec_tile, dec_blk, dec_abort, dec_left, dec_above, px_valid, px_last; wire [1:0] dec_comp; wire [3:0] dec_bx; wire [6:0] px_x, px_y; wire [7:0] px_r, px_g, px_b;
    tau_tgy_tile #(.C444(C444), .BUG(BUG)) dut (.clk(clk), .rst(rst), .clk_q(clk), .qwe(qwe), .qsel(qsel), .qidx(qidx), .qval(qval), .start(start), .fmt(fmt), .tw(tw), .th(th), .busy(busy), .done(done), .err(err),
        .dec_tile(dec_tile), .dec_blk(dec_blk), .dec_abort(dec_abort), .dec_comp(dec_comp), .dec_bx(dec_bx), .dec_left(dec_left), .dec_above(dec_above), .c_we(c_we), .c_zz(c_zz), .c_val(c_val), .blk_done(blk_done), .dec_err(dec_err),
        .hold(hold), .px_valid(px_valid), .px_x(px_x), .px_y(px_y), .px_r(px_r), .px_g(px_g), .px_b(px_b), .px_last(px_last));
endmodule
