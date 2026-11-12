// tau_tgy_ent_probe_top.sv -- timing probe of the entropy decoder alone on ONE clock (tools/block_probe.py): the inputs and outputs are one register away from virtual pins. MIT licence.
module tau_tgy_ent_probe_top (
    input  wire clk,
    input  wire [55:0] vi,      // dec_tile, tile_len[19:0], in_valid, in_word[31:0], dec_blk, dec_comp[1:0], dec_bx[3:0] (the rest tied low below), dec_left, dec_above
    output wire [34:0] vo       // in_ready, c_we, c_zz[5:0], c_val[15:0], blk_done, dec_err ... (padded)
);
    reg [55:0] i = 0; reg [34:0] o = 0; always @(posedge clk) i <= vi;
    wire in_ready, c_we, blk_done, dec_err; wire [5:0] c_zz; wire signed [15:0] c_val;
    tau_tgy_ent u (.clk(clk), .rst(1'b0), .dec_tile(i[55]), .tile_len(i[54:35]), .in_valid(i[34]), .in_word(i[33:2]), .in_ready(in_ready), .dec_blk(i[1]), .dec_comp(2'd0), .dec_bx(i[0] ? 4'd5 : 4'd0),
        .dec_left(i[0]), .dec_above(i[1]), .dec_abort(1'b0), .c_we(c_we), .c_zz(c_zz), .c_val(c_val), .blk_done(blk_done), .dec_err(dec_err));
    always @(posedge clk) o <= {in_ready, c_we, c_zz, c_val, blk_done, dec_err, 9'd0};
    assign vo = o;
endmodule
