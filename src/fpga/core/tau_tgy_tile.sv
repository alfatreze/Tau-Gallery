// tau_tgy_tile.sv -- TGY tile controller: runs ONE tile from the entropy decoder's coefficients to the pixel stream (docs/TGY_HW_DECODER.md).
// It owns the quantisation tables (two: Y, and Cb/Cr shared), walks the tile's blocks in the symbol layer's order (docs/TGY_SYMBOL_LAYER_V0.md section 2: all Y blocks in raster order, then all Cb, then all Cr),
// asks the entropy decoder for one block at a time (dec_blk with the component, the block column and whether the left and upper blocks exist), dequantises each coefficient the decoder delivers
// (zigzag index to natural index, times the table entry, saturated to 18 bits signed for the IDCT), runs the IDCT unit, writes its 8 rows into the plane memories of tau_tgy_recon, and when the last block of
// the last component is in, starts the pixel stage and passes its stream through. A watchdog (2^17 clocks per block, docs/M3_STREAMING_NOTE.md D7) turns a decoder that never finishes into `err`.
// Block geometry (docs/TGY_CONTAINER_V1.md section 4): grey and 4:4:4 tiles have ceil(w / 8) x ceil(h / 8) blocks per component, 4:2:0 tiles have Y blocks padded to MCUs, 2 * ceil(w / 16) x 2 * ceil(h / 16),
// and chroma ceil(w / 16) x ceil(h / 16). The IDCT and the entropy decode do not overlap in this version (the IDCT takes 30 clocks of each block's time; overlapping is a later optimisation).
// Entropy decoder interface: dec_tile (pulse) starts a tile (reset contexts, begin the stream); dec_blk (pulse) with dec_comp / dec_bx / dec_left / dec_above starts a block; the decoder answers with c_we pulses
// (c_zz, c_val: the DC value, then any nonzero AC coefficients, one per pulse, at least one clock apart) and blk_done after the last; dec_err aborts; dec_abort tells the decoder this unit gave up.
// BUG (test only): 0 = correct; 1..13 = one deliberate fault each, killed by sim/tb_tau_tgy_tile.v.
module tau_tgy_tile #(parameter integer C444 = 0, parameter integer BUG = 0) (
    input  wire        clk, rst,
    input  wire        qwe, qsel,
    input  wire [5:0]  qidx,
    input  wire [15:0] qval,
    input  wire        start,
    input  wire [1:0]  fmt,
    input  wire [7:0]  tw, th,
    output wire        busy,
    output reg         done, err,
    output reg         dec_tile, dec_blk, dec_abort,
    output reg  [1:0]  dec_comp,
    output reg  [3:0]  dec_bx,
    output reg         dec_left, dec_above,
    input  wire        c_we,
    input  wire [5:0]  c_zz,
    input  wire signed [15:0] c_val,
    input  wire        blk_done, dec_err,
    input  wire        hold,
    output wire        px_valid,
    output wire [6:0]  px_x, px_y,
    output wire [7:0]  px_r, px_g, px_b,
    output wire        px_last
);
    function [5:0] zzn(input [5:0] k);
        case (k)
            6'd0: zzn = 6'd0;
            6'd1: zzn = 6'd1;
            6'd2: zzn = 6'd8;
            6'd3: zzn = 6'd16;
            6'd4: zzn = 6'd9;
            6'd5: zzn = 6'd2;
            6'd6: zzn = 6'd3;
            6'd7: zzn = 6'd10;
            6'd8: zzn = 6'd17;
            6'd9: zzn = 6'd24;
            6'd10: zzn = 6'd32;
            6'd11: zzn = 6'd25;
            6'd12: zzn = 6'd18;
            6'd13: zzn = 6'd11;
            6'd14: zzn = 6'd4;
            6'd15: zzn = 6'd5;
            6'd16: zzn = 6'd12;
            6'd17: zzn = 6'd19;
            6'd18: zzn = 6'd26;
            6'd19: zzn = 6'd33;
            6'd20: zzn = 6'd40;
            6'd21: zzn = 6'd48;
            6'd22: zzn = 6'd41;
            6'd23: zzn = 6'd34;
            6'd24: zzn = 6'd27;
            6'd25: zzn = 6'd20;
            6'd26: zzn = 6'd13;
            6'd27: zzn = 6'd6;
            6'd28: zzn = 6'd7;
            6'd29: zzn = 6'd14;
            6'd30: zzn = 6'd21;
            6'd31: zzn = 6'd28;
            6'd32: zzn = 6'd35;
            6'd33: zzn = 6'd42;
            6'd34: zzn = 6'd49;
            6'd35: zzn = 6'd56;
            6'd36: zzn = 6'd57;
            6'd37: zzn = 6'd50;
            6'd38: zzn = 6'd43;
            6'd39: zzn = 6'd36;
            6'd40: zzn = 6'd29;
            6'd41: zzn = 6'd22;
            6'd42: zzn = 6'd15;
            6'd43: zzn = 6'd23;
            6'd44: zzn = 6'd30;
            6'd45: zzn = 6'd37;
            6'd46: zzn = 6'd44;
            6'd47: zzn = 6'd51;
            6'd48: zzn = 6'd58;
            6'd49: zzn = 6'd59;
            6'd50: zzn = 6'd52;
            6'd51: zzn = 6'd45;
            6'd52: zzn = 6'd38;
            6'd53: zzn = 6'd31;
            6'd54: zzn = 6'd39;
            6'd55: zzn = 6'd46;
            6'd56: zzn = 6'd53;
            6'd57: zzn = 6'd60;
            6'd58: zzn = 6'd61;
            6'd59: zzn = 6'd54;
            6'd60: zzn = 6'd47;
            6'd61: zzn = 6'd55;
            6'd62: zzn = 6'd62;
            6'd63: zzn = 6'd63;
            default: zzn = 6'd0;
        endcase
    endfunction

    // ---- quantisation tables: [0..63] Y, [64..127] Cb/Cr, zigzag order
    reg [15:0] qmem [0:127];
    always @(posedge clk) if (qwe) qmem[{qsel, qidx}] <= qval;

    // ---- state
    localparam [3:0] S_IDLE = 0, S_NEXT = 1, S_DEC1 = 2, S_DEC = 3, S_FLUSH = 4, S_IDCT = 5, S_RECON = 6, S_OUT = 7;
    reg [3:0] st; reg [1:0] f; reg [1:0] comp; reg [3:0] bx, by; reg [4:0] ycols, yrows, ccols, crows; reg [16:0] wd; reg [1:0] ncomp;
    assign busy = (st != S_IDLE);
    wire [4:0] cols = (comp == 2'd0) ? ycols : ccols;
    wire [4:0] rows = (comp == 2'd0) ? yrows : crows;
    wire fmt_ok = (fmt == 2'd0 || fmt == 2'd2 || (fmt == 2'd1 && C444 != 0)) && tw != 8'd0 && tw <= 8'd128 && th != 8'd0 && th <= 8'd128;

    // ---- coefficient pipeline: quantiser read, multiply, saturate, write into the IDCT
    reg v1, v2, ix_we; reg [5:0] zz1, nat2, ix_nat; reg signed [15:0] val1; reg [15:0] rq; reg signed [32:0] prod; reg signed [17:0] ix_val;
    wire qs = (BUG == 1) ? 1'b0 : (comp != 2'd0);
    always @(posedge clk) begin
        rq <= qmem[{qs, c_zz}]; v1 <= c_we; zz1 <= c_zz; val1 <= c_val;
        v2 <= v1; nat2 <= (BUG == 2 && zz1 == 6'd5) ? 6'd3 : zzn(zz1); prod <= val1 * $signed({1'b0, rq});
        ix_we <= v2; ix_nat <= nat2;
        ix_val <= (BUG == 5) ? prod[17:0] : (prod > 33'sd131071) ? 18'sd131071 : (prod < -33'sd131071) ? -18'sd131071 : prod[17:0];
    end
    wire pend = v1 | v2 | ix_we;

    // ---- IDCT and pixel stage
    reg ix_clear, ix_start; wire ix_busy, ix_done, ix_pv; wire [2:0] ix_row; wire [63:0] ix_data;
    tau_tgy_idct idct (.clk(clk), .rst(rst), .clear(ix_clear), .we(ix_we), .nat(ix_nat), .val(ix_val), .start(ix_start), .busy(ix_busy), .done(ix_done), .px_valid(ix_pv), .px_row(ix_row), .px_data(ix_data));
    reg [7:0] tw_r, th_r;
    reg rc_start; wire rc_busy, rc_done, rc_err;
    wire [1:0] wcomp = (BUG == 7) ? ((comp == 2'd1) ? 2'd2 : (comp == 2'd2) ? 2'd1 : comp) : comp;
    wire [6:0] wx = (BUG == 3) ? {1'b0, bx[2:0], 3'b000} : {bx, 3'b000};
    wire [6:0] wy = (BUG == 4) ? {by, ~ix_row} : {by, ix_row};
    tau_tgy_recon #(.C444(C444)) recon (.clk(clk), .rst(rst), .we(ix_pv), .wcomp(wcomp), .wx(wx), .wy(wy), .wdata(ix_data), .start(rc_start), .fmt(f), .tw(tw_r), .th(th_r), .hold(hold),
        .busy(rc_busy), .done(rc_done), .err(rc_err), .px_valid(px_valid), .px_x(px_x), .px_y(px_y), .px_r(px_r), .px_g(px_g), .px_b(px_b), .px_last(px_last));

    always @(posedge clk) begin
        done <= 1'b0; err <= 1'b0; dec_tile <= 1'b0; dec_blk <= 1'b0; dec_abort <= 1'b0; ix_clear <= 1'b0; ix_start <= 1'b0; rc_start <= 1'b0;
        if (rst) st <= S_IDLE;
        else case (st)
            S_IDLE: if (start) begin
                if (!fmt_ok) err <= 1'b1;
                else begin
                    f <= fmt; tw_r <= tw; th_r <= th; comp <= 2'd0; bx <= 4'd0; by <= 4'd0; dec_tile <= 1'b1; st <= S_NEXT;
                    ncomp <= (fmt == 2'd0) ? 2'd1 : 2'd3;
                    if (fmt == 2'd2) begin
                        ycols <= ((tw + 8'd15) >> 4) << 1; yrows <= ((th + 8'd15) >> 4) << 1;
                        ccols <= (BUG == 6) ? ((tw + 8'd14) >> 4) : ((tw + 8'd15) >> 4); crows <= (th + 8'd15) >> 4;
                    end else begin
                        ycols <= (tw + 8'd7) >> 3; yrows <= (th + 8'd7) >> 3; ccols <= (tw + 8'd7) >> 3; crows <= (th + 8'd7) >> 3;
                    end
                end
            end
            S_NEXT: begin if (BUG != 9) ix_clear <= 1'b1; st <= S_DEC1; end
            S_DEC1: begin
                dec_blk <= 1'b1; dec_comp <= comp; dec_bx <= (BUG == 13) ? bx + 4'd1 : bx; dec_left <= (BUG == 10) ? 1'b1 : (bx != 4'd0); dec_above <= (BUG == 11) ? (bx != 4'd0) : (by != 4'd0);
                wd <= 17'd0; st <= S_DEC;
            end
            S_DEC: begin
                wd <= wd + 17'd1;
                if (dec_err) begin err <= 1'b1; dec_abort <= 1'b1; st <= S_IDLE; end
                else if (blk_done) st <= S_FLUSH;
                else if (wd == 17'h1FFFF && BUG != 12) begin err <= 1'b1; dec_abort <= 1'b1; st <= S_IDLE; end
            end
            S_FLUSH: if (!pend || BUG == 8) begin ix_start <= 1'b1; st <= S_IDCT; end
            S_IDCT: if (ix_done) begin
                if (bx == cols - 5'd1) begin
                    bx <= 4'd0;
                    if (by == rows - 5'd1) begin
                        by <= 4'd0;
                        if (comp == ncomp - 2'd1) st <= S_RECON; else begin comp <= comp + 2'd1; st <= S_NEXT; end
                    end else begin by <= by + 4'd1; st <= S_NEXT; end
                end else begin bx <= bx + 4'd1; st <= S_NEXT; end
            end
            S_RECON: begin rc_start <= 1'b1; st <= S_OUT; end
            S_OUT: begin
                if (rc_err) begin err <= 1'b1; st <= S_IDLE; end
                else if (rc_done) begin done <= 1'b1; st <= S_IDLE; end
            end
            default: st <= S_IDLE;
        endcase
    end
endmodule
