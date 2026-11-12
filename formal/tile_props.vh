// Proof-only properties of tau_tgy_tile's control FSM (included by the RTL under `ifdef FORMAL; formal/tile.sby, unbounded k-induction). Not synthesised. The IDCT and the recon unit are replaced by handshake stand-ins
// (formal/tile_stubs.sv): their arithmetic stays with the bit-exact simulations. Environment contract (assumed): the entropy decoder only talks (c_we, blk_done, dec_err) while a block is requested (state S_DEC);
// the stand-ins answer only after their start. Proved for all input sequences:
//  (1) geometry: the stored column/row counts match the tile size and format, and the walk position stays inside them;   (2) the walk visits every block exactly once, Y raster, then Cb, then Cr (ghost counter =
//  position), and the pixel stage starts only after exactly all blocks;   (3) each block request carries the right component, column, left/above flags;   (4) the IDCT is cleared before every block and started only with
//  no coefficient in the pipeline;   (5) the watchdog leaves S_DEC after 2**17 clocks;   (6) done and err never together; (7) the quantiser table select, zigzag map, saturation and the plane write address/component are
//  the specified ones.
    reg [9:0] f_g = 10'd0;                                                     // ghost: blocks completed in this tile
    wire [9:0] f_ny = {5'd0, ycols} * {5'd0, yrows};
    wire [9:0] f_nc = {5'd0, ccols} * {5'd0, crows};
    wire [9:0] f_off = (comp == 2'd0) ? 10'd0 : (comp == 2'd1) ? f_ny : f_ny + f_nc;
    wire [9:0] f_pos = f_off + {5'd0, by} * {5'd0, cols} + {6'd0, bx};
    wire [9:0] f_total = f_ny + ((ncomp == 2'd3) ? (f_nc << 1) : 10'd0);
    wire f_walk = (st == S_NEXT) || (st == S_DEC1) || (st == S_DEC) || (st == S_FLUSH) || (st == S_IDCT);
    always @(posedge clk) begin
        if (rst) f_g <= 10'd0;
        else if (st == S_IDLE && start) f_g <= 10'd0;
        else if (st == S_IDCT && ix_done) f_g <= f_g + 10'd1;
    end
    // environment
    always @(*) if (!rst) begin
        assume (st == S_DEC || (!c_we && !blk_done && !dec_err));
    end
    reg [5:0] f_zz1d = 6'd0; reg signed [32:0] f_prod_d = 33'sd0; reg f_pm = 1'b0;
    always @(posedge clk) begin f_zz1d <= zz1; f_prod_d <= prod; f_pm <= !rst && st == S_DEC && wd == 17'h1FFFF; end
    wire f_busy_st = (st != S_IDLE);
    wire [4:0] f_c16 = (f == 2'd2) ? ((tw_r + 8'd15) >> 4) : 5'd0;
    always @(*) if (!rst) begin
        // state-space helpers for the induction (hold whenever a tile is running)
        if (f_busy_st) begin
            assert (tw_r != 8'd0 && tw_r <= 8'd128 && th_r != 8'd0 && th_r <= 8'd128);
            assert (f == 2'd0 || f == 2'd2 || (f == 2'd1 && C444 != 0));
            assert (ncomp == ((f == 2'd0) ? 2'd1 : 2'd3));
            if (f == 2'd2) begin assert (ycols == (((tw_r + 8'd15) >> 4) << 1)); assert (yrows == (((th_r + 8'd15) >> 4) << 1)); assert (ccols == ((tw_r + 8'd15) >> 4)); assert (crows == ((th_r + 8'd15) >> 4)); end   // (1)
            else begin assert (ycols == ((tw_r + 8'd7) >> 3)); assert (yrows == ((th_r + 8'd7) >> 3)); assert (ccols == ycols); assert (crows == yrows); end
            assert (comp < ncomp);
        end
        if (f_walk) begin
            assert (bx < cols); assert (by < rows);                                                // (1)
            assert (f_g == f_pos);                                                                  // (2) the walk is the ordered, once-each enumeration
        end
        if (st == S_RECON || st == S_OUT) assert (f_g == f_total);                                  // (2) the pixel stage starts after exactly all blocks
        if (st == S_DEC1) assert (ix_clear);                                                        // (4)
        if (dec_blk) begin assert (dec_comp == comp); assert (dec_bx == bx); assert (dec_left == (bx != 4'd0)); assert (dec_above == (by != 4'd0)); end   // (3)
        if (ix_start) assert (!pend);                                                               // (4)
        assert (!f_pm || st != S_DEC);                                                              // (5)
        assert (!(done && err));                                                                    // (6)
        assert (qs == (comp != 2'd0));                                                              // (7)
        assert (nat2 == zzn(f_zz1d));
        assert (ix_val == ((f_prod_d > 33'sd131071) ? 18'sd131071 : (f_prod_d < -33'sd131071) ? -18'sd131071 : f_prod_d[17:0]));
        assert (wcomp == comp); assert (wx == {bx, 3'b000}); assert (wy == {by, ix_row});
    end
