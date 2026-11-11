// tau_tgy_ent.sv -- TGY entropy decoder: the frozen symbol layer v0 (docs/TGY_SYMBOL_LAYER_V0.md) in hardware, held to docs/conformance/tgy_v0_vectors.json (docs/TGY_HW_DECODER.md).
// One tile stream in (32-bit words through in_valid / in_word / in_ready, tile_len bytes; bytes past the end read as 0, as the range coder requires), quantised coefficients out one block at a time on the request of the tile
// controller (dec_blk with component, block column, left / upper block exist): the DC value first, then every nonzero AC coefficient in zigzag order (c_we, c_zz, c_val), then blk_done.
// Design: one range-coder engine shared by a symbol state machine. A context bin takes 3 clocks (R: context RAM read, C: the multiplications bound = (range >> 15) * p and the two candidate probability updates, F: decide, update
// range/code, renormalise (up to 2 bytes), write the context back, choose the next symbol state), a bypass bin 1 clock. Contexts: 1,250 x (15-bit probability, 5-bit count) in one RAM, reset in 1,250 clocks at every tile start.
// The 64 magnitudes of the block being decoded are kept saturated to 5 (3 bits each: the local class only needs min(sum, 5)), the DC values of the previous block row in 16 registers. No coefficient line buffers.
// Errors: an Exp-Golomb exponent above 15 raises (the C decoder's TGY_MAX_EXP; valid streams stay at or below 11) dec_err (and the decoder waits for the next dec_tile). BUG (test only): 0 = correct; 1..15 = one deliberate fault each, killed by sim/tb_tau_tgy_ent.v.
module tau_tgy_ent #(parameter integer BUG = 0) (
    input  wire        clk, rst,
    input  wire        dec_tile,
    input  wire [19:0] tile_len,
    input  wire        in_valid,
    input  wire [31:0] in_word,
    output wire        in_ready,
    input  wire        dec_blk,
    input  wire [1:0]  dec_comp,
    input  wire [3:0]  dec_bx,
    input  wire        dec_left, dec_above,
    input  wire        dec_abort,
    output reg         c_we,
    output reg  [5:0]  c_zz,
    output reg  signed [15:0] c_val,
    output reg         blk_done,
    output reg         dec_err
);
    // ---- constant tables (generated from the frozen vectors file)
    function [3:0] kgrom(input [5:0] k);
        case (k)
            6'd0: kgrom = 4'd0;
            6'd1: kgrom = 4'd0;
            6'd2: kgrom = 4'd1;
            6'd3: kgrom = 4'd2;
            6'd4: kgrom = 4'd3;
            6'd5: kgrom = 4'd4;
            6'd6: kgrom = 4'd4;
            6'd7: kgrom = 4'd5;
            6'd8: kgrom = 4'd5;
            6'd9: kgrom = 4'd6;
            6'd10: kgrom = 4'd6;
            6'd11: kgrom = 4'd6;
            6'd12: kgrom = 4'd7;
            6'd13: kgrom = 4'd7;
            6'd14: kgrom = 4'd7;
            6'd15: kgrom = 4'd7;
            6'd16: kgrom = 4'd8;
            6'd17: kgrom = 4'd8;
            6'd18: kgrom = 4'd8;
            6'd19: kgrom = 4'd8;
            6'd20: kgrom = 4'd8;
            6'd21: kgrom = 4'd9;
            6'd22: kgrom = 4'd9;
            6'd23: kgrom = 4'd9;
            6'd24: kgrom = 4'd9;
            6'd25: kgrom = 4'd9;
            6'd26: kgrom = 4'd9;
            6'd27: kgrom = 4'd9;
            6'd28: kgrom = 4'd10;
            6'd29: kgrom = 4'd10;
            6'd30: kgrom = 4'd10;
            6'd31: kgrom = 4'd10;
            6'd32: kgrom = 4'd10;
            6'd33: kgrom = 4'd10;
            6'd34: kgrom = 4'd10;
            6'd35: kgrom = 4'd10;
            6'd36: kgrom = 4'd10;
            6'd37: kgrom = 4'd11;
            6'd38: kgrom = 4'd11;
            6'd39: kgrom = 4'd11;
            6'd40: kgrom = 4'd11;
            6'd41: kgrom = 4'd11;
            6'd42: kgrom = 4'd11;
            6'd43: kgrom = 4'd11;
            6'd44: kgrom = 4'd11;
            6'd45: kgrom = 4'd11;
            6'd46: kgrom = 4'd11;
            6'd47: kgrom = 4'd11;
            6'd48: kgrom = 4'd11;
            6'd49: kgrom = 4'd12;
            6'd50: kgrom = 4'd12;
            6'd51: kgrom = 4'd12;
            6'd52: kgrom = 4'd12;
            6'd53: kgrom = 4'd12;
            6'd54: kgrom = 4'd12;
            6'd55: kgrom = 4'd12;
            6'd56: kgrom = 4'd12;
            6'd57: kgrom = 4'd12;
            6'd58: kgrom = 4'd12;
            6'd59: kgrom = 4'd12;
            6'd60: kgrom = 4'd12;
            6'd61: kgrom = 4'd12;
            6'd62: kgrom = 4'd12;
            6'd63: kgrom = 4'd12;
            default: kgrom = 4'd0;
        endcase
    endfunction
    function [13:0] lnrom(input [5:0] k);   // {left valid, left index, upper valid, upper index}
        case (k)
            6'd0: lnrom = {1'b0, 6'd0, 1'b0, 6'd0};
            6'd1: lnrom = {1'b1, 6'd0, 1'b0, 6'd0};
            6'd2: lnrom = {1'b0, 6'd0, 1'b1, 6'd0};
            6'd3: lnrom = {1'b0, 6'd0, 1'b1, 6'd2};
            6'd4: lnrom = {1'b1, 6'd2, 1'b1, 6'd1};
            6'd5: lnrom = {1'b1, 6'd1, 1'b0, 6'd0};
            6'd6: lnrom = {1'b1, 6'd5, 1'b0, 6'd0};
            6'd7: lnrom = {1'b1, 6'd4, 1'b1, 6'd5};
            6'd8: lnrom = {1'b1, 6'd3, 1'b1, 6'd4};
            6'd9: lnrom = {1'b0, 6'd0, 1'b1, 6'd3};
            6'd10: lnrom = {1'b0, 6'd0, 1'b1, 6'd9};
            6'd11: lnrom = {1'b1, 6'd9, 1'b1, 6'd8};
            6'd12: lnrom = {1'b1, 6'd8, 1'b1, 6'd7};
            6'd13: lnrom = {1'b1, 6'd7, 1'b1, 6'd6};
            6'd14: lnrom = {1'b1, 6'd6, 1'b0, 6'd0};
            6'd15: lnrom = {1'b1, 6'd14, 1'b0, 6'd0};
            6'd16: lnrom = {1'b1, 6'd13, 1'b1, 6'd14};
            6'd17: lnrom = {1'b1, 6'd12, 1'b1, 6'd13};
            6'd18: lnrom = {1'b1, 6'd11, 1'b1, 6'd12};
            6'd19: lnrom = {1'b1, 6'd10, 1'b1, 6'd11};
            6'd20: lnrom = {1'b0, 6'd0, 1'b1, 6'd10};
            6'd21: lnrom = {1'b0, 6'd0, 1'b1, 6'd20};
            6'd22: lnrom = {1'b1, 6'd20, 1'b1, 6'd19};
            6'd23: lnrom = {1'b1, 6'd19, 1'b1, 6'd18};
            6'd24: lnrom = {1'b1, 6'd18, 1'b1, 6'd17};
            6'd25: lnrom = {1'b1, 6'd17, 1'b1, 6'd16};
            6'd26: lnrom = {1'b1, 6'd16, 1'b1, 6'd15};
            6'd27: lnrom = {1'b1, 6'd15, 1'b0, 6'd0};
            6'd28: lnrom = {1'b1, 6'd27, 1'b0, 6'd0};
            6'd29: lnrom = {1'b1, 6'd26, 1'b1, 6'd27};
            6'd30: lnrom = {1'b1, 6'd25, 1'b1, 6'd26};
            6'd31: lnrom = {1'b1, 6'd24, 1'b1, 6'd25};
            6'd32: lnrom = {1'b1, 6'd23, 1'b1, 6'd24};
            6'd33: lnrom = {1'b1, 6'd22, 1'b1, 6'd23};
            6'd34: lnrom = {1'b1, 6'd21, 1'b1, 6'd22};
            6'd35: lnrom = {1'b0, 6'd0, 1'b1, 6'd21};
            6'd36: lnrom = {1'b1, 6'd35, 1'b1, 6'd34};
            6'd37: lnrom = {1'b1, 6'd34, 1'b1, 6'd33};
            6'd38: lnrom = {1'b1, 6'd33, 1'b1, 6'd32};
            6'd39: lnrom = {1'b1, 6'd32, 1'b1, 6'd31};
            6'd40: lnrom = {1'b1, 6'd31, 1'b1, 6'd30};
            6'd41: lnrom = {1'b1, 6'd30, 1'b1, 6'd29};
            6'd42: lnrom = {1'b1, 6'd29, 1'b1, 6'd28};
            6'd43: lnrom = {1'b1, 6'd41, 1'b1, 6'd42};
            6'd44: lnrom = {1'b1, 6'd40, 1'b1, 6'd41};
            6'd45: lnrom = {1'b1, 6'd39, 1'b1, 6'd40};
            6'd46: lnrom = {1'b1, 6'd38, 1'b1, 6'd39};
            6'd47: lnrom = {1'b1, 6'd37, 1'b1, 6'd38};
            6'd48: lnrom = {1'b1, 6'd36, 1'b1, 6'd37};
            6'd49: lnrom = {1'b1, 6'd48, 1'b1, 6'd47};
            6'd50: lnrom = {1'b1, 6'd47, 1'b1, 6'd46};
            6'd51: lnrom = {1'b1, 6'd46, 1'b1, 6'd45};
            6'd52: lnrom = {1'b1, 6'd45, 1'b1, 6'd44};
            6'd53: lnrom = {1'b1, 6'd44, 1'b1, 6'd43};
            6'd54: lnrom = {1'b1, 6'd52, 1'b1, 6'd53};
            6'd55: lnrom = {1'b1, 6'd51, 1'b1, 6'd52};
            6'd56: lnrom = {1'b1, 6'd50, 1'b1, 6'd51};
            6'd57: lnrom = {1'b1, 6'd49, 1'b1, 6'd50};
            6'd58: lnrom = {1'b1, 6'd57, 1'b1, 6'd56};
            6'd59: lnrom = {1'b1, 6'd56, 1'b1, 6'd55};
            6'd60: lnrom = {1'b1, 6'd55, 1'b1, 6'd54};
            6'd61: lnrom = {1'b1, 6'd59, 1'b1, 6'd60};
            6'd62: lnrom = {1'b1, 6'd58, 1'b1, 6'd59};
            6'd63: lnrom = {1'b1, 6'd62, 1'b1, 6'd61};
            default: lnrom = 14'd0;
        endcase
    endfunction
    function [15:0] recrom(input [4:0] n);
        case (n)
            5'd0: recrom = 16'd32768;
            5'd1: recrom = 16'd26214;
            5'd2: recrom = 16'd18724;
            5'd3: recrom = 16'd14563;
            5'd4: recrom = 16'd11915;
            5'd5: recrom = 16'd10082;
            5'd6: recrom = 16'd8738;
            5'd7: recrom = 16'd7710;
            5'd8: recrom = 16'd6898;
            5'd9: recrom = 16'd6241;
            5'd10: recrom = 16'd5698;
            5'd11: recrom = 16'd5242;
            5'd12: recrom = 16'd4854;
            5'd13: recrom = 16'd4519;
            5'd14: recrom = 16'd4228;
            5'd15: recrom = 16'd3971;
            5'd16: recrom = 16'd3744;
            5'd17: recrom = 16'd3542;
            5'd18: recrom = 16'd3360;
            5'd19: recrom = 16'd3196;
            5'd20: recrom = 16'd3048;
            5'd21: recrom = 16'd2912;
            5'd22: recrom = 16'd2788;
            5'd23: recrom = 16'd2674;
            5'd24: recrom = 16'd2570;
            5'd25: recrom = 16'd2473;
            5'd26: recrom = 16'd2383;
            5'd27: recrom = 16'd2299;
            5'd28: recrom = 16'd2221;
            5'd29: recrom = 16'd2148;
            5'd30: recrom = 16'd2080;
            default: recrom = 16'd2080;
        endcase
    endfunction

    // ---- context offsets
    localparam [10:0] O_D0 = 11'd0, O_DS = 11'd2, O_DX = 11'd4, O_DM = 11'd24, O_LT = 11'd44, O_SG = 11'd170, O_G1 = 11'd300, O_MX = 11'd430, O_MM = 11'd1230;

    // ---- input byte FIFO (16 bytes), filled 4 bytes at a time
    reg [7:0] fb [0:15]; reg [3:0] wp, rp; reg [4:0] cnt; reg [19:0] rem; reg active;
    assign in_ready = active && (rem != 20'd0) && (cnt <= 5'd12);
    wire [7:0] b0 = (cnt >= 5'd1) ? fb[rp] : 8'd0;
    wire [7:0] b1 = (cnt >= 5'd2) ? fb[rp + 4'd1] : 8'd0;
    wire pop_ok = (cnt >= 5'd2) || (rem == 20'd0);

    // ---- range coder state
    reg [31:0] range, code;
    // ---- context RAM
    reg [19:0] cmem [0:2047]; reg [19:0] cq; reg [10:0] cx;
    // ---- symbol state
    localparam [4:0] S_DEAD = 0, S_INIT = 1, S_PRIME = 2, S_IDLE = 3, S_DZ = 4, S_DS = 5, S_EGX = 6, S_EGM = 7, S_EGB = 8, S_EGEND = 9, S_DCEMIT = 10, S_LT = 11, S_CSET = 12, S_SG = 13, S_G1 = 14, S_SIGN = 15, S_BDONE = 16;
    reg [4:0] st; reg [1:0] ph; reg [10:0] ic; reg [2:0] pc;
    reg cc; reg [3:0] bxr; reg blk_pend; reg pend_cc; reg [3:0] pend_bx; reg pend_left, pend_above;
    reg signed [15:0] pred, left_dc; reg signed [15:0] dc_row [0:15];
    reg neg; reg [4:0] e, j; reg [25:0] v, m; reg egret; reg [10:0] xb, mb; reg signed [15:0] dcv;
    reg [5:0] node, k, last; reg [2:0] ni; reg [9:0] cb; reg [10:0] mxb; reg [14:0] a_sat;
    reg [2:0] mag [0:63];
    reg [31:0] bnd; reg [30:0] pm, qm;

    function [2:0] cap5(input [14:0] a); cap5 = (a > 15'd5) ? 3'd5 : a[2:0]; endfunction
    function [2:0] lbk(input [3:0] s); lbk = (s == 4'd0) ? 3'd0 : (s == 4'd1) ? 3'd1 : (s == 4'd2) ? 3'd2 : (s <= 4'd4) ? 3'd3 : 3'd4; endfunction
    function signed [15:0] sat16(input signed [31:0] x); sat16 = (x > 32'sd32767) ? 16'sd32767 : (x < -32'sd32767) ? -16'sd32767 : x[15:0]; endfunction

    // context index of the current state
    wire [3:0] e9 = (e > 5'd9) ? 4'd9 : e[3:0];
    reg [10:0] ctx_now;
    always @* begin
        case (st)
            S_DZ:  ctx_now = O_D0 + {10'd0, cc};
            S_DS:  ctx_now = O_DS + {10'd0, cc};
            S_EGX: ctx_now = xb + {7'd0, e9};
            S_EGM: ctx_now = mb + {7'd0, e9};
            S_LT:  ctx_now = O_LT + (cc ? ((BUG == 11) ? 11'd62 : 11'd63) : 11'd0) + {5'd0, node} - 11'd1;
            S_SG:  ctx_now = O_SG + {1'b0, cb};
            S_G1:  ctx_now = O_G1 + {1'b0, cb};
            default: ctx_now = 11'd0;
        endcase
    end
    wire is_ctx_st = (st == S_DZ || st == S_DS || st == S_EGX || st == S_EGM || st == S_LT || st == S_SG || st == S_G1);
    wire is_byp_st = (st == S_EGB || st == S_SIGN);

    // ---- range coder arithmetic of the F phase (context bin) and of a bypass bin
    wire        bit_c = (BUG == 1) ? (code > bnd) : (code >= bnd);
    wire [31:0] rA = bit_c ? range - bnd : bnd;
    wire [31:0] cA = bit_c ? code - bnd : code;
    wire        n1 = rA < 32'h01000000;
    wire [31:0] r1 = n1 ? {rA[23:0], 8'd0} : rA;
    wire [31:0] c1 = n1 ? {cA[23:0], b0} : cA;
    wire        n2 = (BUG == 2) ? 1'b0 : (r1 < 32'h01000000);
    wire [31:0] r2 = n2 ? {r1[23:0], 8'd0} : r1;
    wire [31:0] c2 = n2 ? {c1[23:0], n1 ? b1 : b0} : c1;
    wire [1:0]  pop_c = {1'b0, n1} + {1'b0, n2};
    wire [31:0] rB = range >> 1;
    wire        bit_b = (BUG == 3) ? (code > rB) : (code >= rB);
    wire [31:0] cB = bit_b ? code - rB : code;
    wire        nB = (BUG == 12) ? 1'b0 : (rB < 32'h01000000);
    wire [31:0] r_b = nB ? {rB[23:0], 8'd0} : rB;
    wire [31:0] c_b = nB ? {cB[23:0], b0} : cB;
    // probability update (F phase)
    wire [14:0] p_old = cq[14:0]; wire [4:0] n_old = cq[19:15];
    wire [14:0] p_dn = p_old - pm[30:16];                  // bit = 1
    wire [15:0] p_up = {1'b0, p_old} + {1'b0, qm[30:16]};  // bit = 0
    wire [14:0] p_raw = bit_c ? p_dn : p_up[14:0];
    wire [14:0] p_new = (BUG == 4) ? p_raw : (p_raw < 15'd32) ? 15'd32 : (p_raw > 15'd32736) ? 15'd32736 : p_raw;
    wire [4:0]  n_new = (n_old < ((BUG == 5) ? 5'd29 : 5'd30)) ? n_old + 5'd1 : n_old;
    wire [15:0] rate = recrom(n_old);

    // ---- neighbours and local class for the CSET state
    wire [13:0] ln = lnrom(k);
    wire [3:0]  sum = (ln[13] ? {1'b0, mag[ln[12:7]]} : 4'd0) + (ln[6] ? {1'b0, mag[ln[5:0]]} : 4'd0);
    wire [2:0]  lc = (BUG == 6) ? lbk(sum + 4'd1) : lbk(sum);
    wire [3:0]  kg = kgrom(k);
    wire [3:0]  kg7 = (kg > ((BUG == 10) ? 4'd6 : 4'd7)) ? ((BUG == 10) ? 4'd6 : 4'd7) : kg;
    wire [9:0]  cb_new = (({6'd0, kg} + (cc ? 10'd13 : 10'd0)) << 2) + ({6'd0, kg} + (cc ? 10'd13 : 10'd0)) + {7'd0, lc};
    wire [9:0]  g8 = {6'd0, kg7} + (cc ? 10'd8 : 10'd0);
    wire [9:0]  mxi = ((g8 << 2) + g8) + {7'd0, lc};
    wire [10:0] mxb_new = O_MX + ({1'b0, mxi} << 3) + ({1'b0, mxi} << 1);

    reg [3:0] push_n; reg [4:0] ncnt; integer ii;
    wire [19:0] rem_take = (rem >= 20'd4) ? 20'd4 : rem;

    always @(posedge clk) begin
        c_we <= 1'b0; blk_done <= 1'b0; dec_err <= 1'b0;
        if (rst || dec_abort) begin st <= S_DEAD; active <= 1'b0; blk_pend <= 1'b0; ph <= 2'd0; cnt <= 5'd0; wp <= 4'd0; rp <= 4'd0; end
        else begin
            // ---- FIFO push (a word whenever in_ready) and the pop of the current step, combined below
            if (dec_tile) begin
                active <= 1'b1; rem <= tile_len; cnt <= 5'd0; wp <= 4'd0; rp <= 4'd0; ic <= 11'd0; st <= S_INIT; ph <= 2'd0; blk_pend <= 1'b0; range <= 32'hFFFFFFFF; code <= 32'd0; pc <= 3'd0;
            end else begin
                if (dec_blk) begin blk_pend <= 1'b1; pend_cc <= (dec_comp != 2'd0); pend_bx <= dec_bx; pend_left <= dec_left; pend_above <= dec_above; end
                // memory read of the R phase (every clock; only used in R) and the C phase multiplications
                cq <= cmem[ctx_now];
                bnd <= (range >> 15) * cq[14:0];
                pm  <= cq[14:0] * rate;
                qm  <= (16'd32768 - {1'b0, cq[14:0]}) * rate;
                push_n = 4'd0; ncnt = cnt;
                if (in_valid && in_ready) begin
                    push_n = rem_take[3:0];
                    fb[wp] <= (rem >= 20'd1) ? in_word[7:0] : 8'd0; fb[wp + 4'd1] <= (rem >= 20'd2) ? in_word[15:8] : 8'd0;
                    fb[wp + 4'd2] <= (rem >= 20'd3) ? in_word[23:16] : 8'd0; fb[wp + 4'd3] <= (rem >= 20'd4) ? in_word[31:24] : 8'd0;
                    wp <= wp + 4'd4; rem <= rem - rem_take; ncnt = ncnt + 5'd4;
                end
                case (st)
                    S_INIT: begin
                        cmem[ic] <= {5'd0, 15'd16384};
                        if (ic == 11'd1249) begin st <= S_PRIME; pc <= 3'd0; end else ic <= ic + 11'd1;
                    end
                    S_PRIME: begin
                        if (cnt >= 5'd1 || rem == 20'd0) begin
                            code <= {code[23:0], b0};
                            if (cnt >= 5'd1) begin rp <= rp + 4'd1; ncnt = ncnt - 5'd1; end
                            if (pc == 3'd4) st <= S_IDLE; else pc <= pc + 3'd1;
                        end
                    end
                    S_IDLE: if (blk_pend) begin
                        blk_pend <= 1'b0; cc <= pend_cc; bxr <= pend_bx;
                        pred <= (BUG == 7) ? (pend_above ? dc_row[pend_bx] : pend_left ? left_dc : 16'sd0) : (pend_left ? left_dc : pend_above ? dc_row[pend_bx] : 16'sd0);
                        for (ii = 0; ii < 64; ii = ii + 1) mag[ii] <= 3'd0;
                        st <= S_DZ; ph <= 2'd0;
                    end
                    default: begin
                        if (is_ctx_st) begin
                            case (ph)
                                2'd0: begin cx <= ctx_now; ph <= 2'd1; end
                                2'd1: ph <= 2'd2;
                                2'd2: if (pop_ok) begin
                                    range <= r2; code <= c2; cmem[cx] <= {n_new, p_new};
                                    if (pop_c != 2'd0) begin rp <= rp + {2'd0, pop_c}; ncnt = ncnt - ((cnt >= {3'd0, pop_c}) ? {3'd0, pop_c} : cnt); end
                                    ph <= 2'd0;
                                    case (st)
                                        S_DZ: if (bit_c) st <= S_DS; else begin dcv <= (BUG == 13) ? 16'sd0 : sat16($signed({{16{pred[15]}}, pred})); st <= S_DCEMIT; end
                                        S_DS: begin neg <= bit_c; xb <= O_DX + (cc ? 11'd10 : 11'd0); mb <= O_DM + (cc ? 11'd10 : 11'd0); e <= 5'd0; egret <= 1'b0; st <= S_EGX; end
                                        S_EGX: if (bit_c) begin
                                            if (e == 5'd15) begin dec_err <= 1'b1; st <= S_DEAD; end else e <= e + 5'd1;
                                        end else if (e == 5'd0) begin m <= 26'd0; st <= S_EGEND; end
                                        else begin v <= 26'd1; j <= e; st <= S_EGM; end
                                        S_EGM: begin
                                            v <= {v[24:0], bit_c}; j <= j - 5'd1;
                                            if (j == 5'd1) begin m <= {v[24:0], bit_c} - 26'd1; st <= S_EGEND; end else st <= S_EGB;
                                        end
                                        S_LT: begin
                                            node <= {node[4:0], bit_c}; ni <= ni + 3'd1;
                                            if (ni == 3'd5) begin
                                                last <= {node[4:0], bit_c};
                                                if ({node[4:0], bit_c} == 6'd0) st <= S_BDONE; else begin k <= 6'd1; st <= S_CSET; end
                                            end
                                        end
                                        S_SG: if (bit_c) st <= S_G1; else begin k <= k + 6'd1; st <= S_CSET; end
                                        S_G1: if (bit_c) begin xb <= mxb; mb <= O_MM + ((cc && BUG != 9) ? 11'd10 : 11'd0); e <= 5'd0; egret <= 1'b1; st <= S_EGX; end
                                              else begin a_sat <= (BUG == 14) ? 15'd2 : 15'd1; st <= S_SIGN; end
                                        default: ;
                                    endcase
                                end
                                default: ph <= 2'd0;
                            endcase
                        end else if (is_byp_st) begin
                            if (pop_ok) begin
                                range <= r_b; code <= c_b;
                                if (nB) begin rp <= rp + 4'd1; ncnt = ncnt - ((cnt >= 5'd1) ? 5'd1 : 5'd0); end
                                if (st == S_EGB) begin
                                    v <= {v[24:0], bit_b}; j <= j - 5'd1;
                                    if (j == 5'd1) begin m <= {v[24:0], bit_b} - 26'd1; st <= S_EGEND; end
                                end else begin   // S_SIGN
                                    c_we <= 1'b1; c_zz <= k; c_val <= (bit_b ^ (BUG == 8)) ? -$signed({1'b0, a_sat}) : $signed({1'b0, a_sat}); mag[k] <= cap5(a_sat);
                                    if (k == last) st <= S_BDONE; else begin k <= k + 6'd1; st <= S_CSET; end
                                end
                            end
                        end else begin
                            case (st)
                                S_EGEND: begin
                                    if (!egret) begin
                                        dcv <= neg ? sat16(-($signed({6'd0, m}) + 32'sd1) + $signed({{16{pred[15]}}, pred})) : sat16(($signed({6'd0, m}) + 32'sd1) + $signed({{16{pred[15]}}, pred}));
                                        st <= S_DCEMIT;
                                    end else begin
                                        a_sat <= (m + 26'd2 > 26'd32767) ? 15'd32767 : m[14:0] + 15'd2; st <= S_SIGN;
                                    end
                                end
                                S_DCEMIT: begin
                                    c_we <= 1'b1; c_zz <= 6'd0; c_val <= dcv; left_dc <= dcv; dc_row[bxr] <= dcv;
                                    if (BUG != 15) mag[0] <= cap5(dcv[15] ? -dcv : dcv);
                                    node <= 6'd1; ni <= 3'd0; st <= S_LT;
                                end
                                S_CSET: begin
                                    cb <= cb_new; mxb <= mxb_new;
                                    if (k < last) st <= S_SG; else st <= S_G1;
                                end
                                S_BDONE: begin blk_done <= 1'b1; st <= S_IDLE; end
                                default: ;
                            endcase
                        end
                    end
                endcase
                // FIFO occupancy: push (+4 bytes) and pop were folded into ncnt above
                cnt <= ncnt;
            end
        end
    end
endmodule
