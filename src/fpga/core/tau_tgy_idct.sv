// tau_tgy_idct.sv -- TGY inverse DCT unit: libjpeg's jpeg_idct_islow bit for bit (docs/TGY_RECONSTRUCTION_V0.md section 3), one 8 x 8 block of DEQUANTISED coefficients to 64 samples.
// Architecture (docs/TGY_HW_IDCT.md): ONE pipelined 1-D butterfly of 12 multipliers that takes a column or a row vector of 8 values every clock, shared by the two passes. Pass 1 issues the 8 columns of the
// coefficient memory and writes the descaled results into the workspace; pass 2 issues the 8 rows of the workspace and emits 8 samples per row. Latency 6 clocks per vector; a block takes about 28 clocks
// (8 + 6 + 8 + 6), 384 blocks of a tile about 10.8 k clocks. Input range: 18-bit signed dequantised coefficients (|v| < 2^17: covers every valid TGY file, whose coefficients times their quantiser stay below 2^12,
// and the saturating test vectors); inside that range the arithmetic is exact (widths from the linear weight analysis: pass 1 output 34 bits, workspace 23, pass 2 output 39, of which only bits [27:18] matter).
// BUG (test only): 0 = correct; 1..9 = one deliberate fault each, killed by sim/tb_tau_tgy_idct.v.
module tau_tgy_bfly #(parameter integer BUG = 0) (
    input  wire               clk,
    input  wire               valid_in,
    input  wire [3:0]         tag_in,                              // {pass, index[2:0]} travels with the vector
    input  wire signed [23:0] i0, i1, i2, i3, i4, i5, i6, i7,
    output reg                valid_out,
    output reg  [3:0]         tag_out,
    output reg  signed [39:0] o0, o1, o2, o3, o4, o5, o6, o7       // before the descale
);
    localparam signed [15:0] F0298 = 16'sd2446, F0390 = 16'sd3196, F0541 = 16'sd4433, F0765 = 16'sd6270, F0899 = 16'sd7373, F1175 = 16'sd9633,
                             F1501 = 16'sd12299, F1847 = 16'sd15137, F1961 = 16'sd16069, F2053 = 16'sd16819, F2562 = 16'sd20995, F3072 = 16'sd25172;
    localparam signed [15:0] C0541 = (BUG == 1) ? 16'sd4432 : F0541;
    // stage 1: the sums, and the inputs the multipliers of stage 2 need
    reg v1; reg [3:0] g1;
    reg signed [39:0] s26, s04, d04, zA, zB, zC, zD, zS;
    reg signed [39:0] p2, p6, p7, p5, p3, p1;
    always @(posedge clk) begin
        v1 <= valid_in; g1 <= tag_in;
        s26 <= $signed(i2) + $signed(i6); s04 <= $signed(i0) + $signed(i4); d04 <= $signed(i0) - $signed(i4);
        zA <= $signed(i7) + $signed(i1); zB <= $signed(i5) + $signed(i3); zC <= $signed(i7) + $signed(i3); zD <= $signed(i5) + $signed(i1); zS <= $signed(i7) + $signed(i3) + $signed(i5) + $signed(i1);
        p2 <= i2; p6 <= i6; p7 <= i7; p5 <= i5; p3 <= i3; p1 <= i1;
    end
    // stage 2: the 12 multiplications
    reg v2; reg [3:0] g2;
    reg signed [39:0] mz1, mi6, mi2, mz5, ma0, ma1, ma2, ma3, mz1o, mz2o, mz3o, mz4o, t0, t1;
    always @(posedge clk) begin
        v2 <= v1; g2 <= g1;
        mz1 <= s26 * C0541; mi6 <= p6 * -F1847; mi2 <= p2 * F0765; mz5 <= zS * F1175;
        ma0 <= p7 * F0298; ma1 <= p5 * F2053; ma2 <= p3 * F3072; ma3 <= p1 * F1501;
        mz1o <= zA * -F0899; mz2o <= zB * -F2562; mz3o <= zC * -F1961; mz4o <= zD * -F0390;
        t0 <= s04 <<< 13; t1 <= d04 <<< 13;
    end
    // stage 3
    reg v3; reg [3:0] g3;
    reg signed [39:0] t2, t3, z3, z4, q0, q1, q2, q3, r1, r2, u0, u1;
    always @(posedge clk) begin
        v3 <= v2; g3 <= g2;
        t2 <= mz1 + mi6; t3 <= mz1 + mi2; z3 <= mz3o + mz5; z4 <= (BUG == 7) ? mz4o : mz4o + mz5;
        q0 <= ma0; q1 <= ma1; q2 <= ma2; q3 <= ma3; r1 <= mz1o; r2 <= mz2o; u0 <= t0; u1 <= t1;
    end
    // stage 4
    reg v4; reg [3:0] g4;
    reg signed [39:0] t10, t13, t11, t12, a0, a1, a2, a3;
    always @(posedge clk) begin
        v4 <= v3; g4 <= g3;
        t10 <= u0 + t3; t13 <= u0 - t3; t11 <= u1 + t2; t12 <= (BUG == 8) ? u1 + t2 : u1 - t2;
        a0 <= q0 + r1 + z3; a1 <= q1 + r2 + z4; a2 <= q2 + r2 + z3; a3 <= q3 + r1 + z4;
    end
    // stage 5: the eight outputs
    always @(posedge clk) begin
        valid_out <= v4; tag_out <= g4;
        o0 <= t10 + a3; o1 <= t11 + a2; o2 <= t12 + a1; o3 <= (BUG == 2) ? t13 - a0 : t13 + a0; o4 <= (BUG == 2) ? t13 + a0 : t13 - a0; o5 <= t12 - a1; o6 <= t11 - a2; o7 <= t10 - a3;
    end
endmodule

module tau_tgy_idct #(parameter integer BUG = 0) (
    input  wire        clk,
    input  wire        rst,
    input  wire        clear,                 // zero the coefficient memory (idle only)
    input  wire        we,                    // write one dequantised coefficient ...
    input  wire [5:0]  nat,                   // ... at this natural (row-major) index: row = vertical frequency
    input  wire signed [17:0] val,
    input  wire        start,                 // compute the block
    output reg         busy,
    output reg         done,                  // one pulse after the last row has been delivered
    output reg         px_valid,              // a row of 8 samples
    output reg  [2:0]  px_row,
    output reg  [63:0] px_data                // sample k of the row in bits [8k+7:8k]
);
    reg signed [17:0] cf [0:63];
    reg signed [23:0] ws [0:63];
    integer i;
    // control
    localparam S_IDLE = 0, S_P1 = 1, S_W1 = 2, S_P2 = 3, S_W2 = 4;
    reg [2:0] st; reg [2:0] cnt; reg [3:0] wr1; reg [3:0] out2;
    reg bf_valid; reg [3:0] bf_tag;
    reg signed [23:0] b0, b1, b2, b3, b4, b5, b6, b7;
    wire bo_valid; wire [3:0] bo_tag; wire signed [39:0] o0, o1, o2, o3, o4, o5, o6, o7;
    tau_tgy_bfly #(.BUG(BUG)) u_bf (.clk(clk), .valid_in(bf_valid), .tag_in(bf_tag), .i0(b0), .i1(b1), .i2(b2), .i3(b3), .i4(b4), .i5(b5), .i6(b6), .i7(b7),
                                    .valid_out(bo_valid), .tag_out(bo_tag), .o0(o0), .o1(o1), .o2(o2), .o3(o3), .o4(o4), .o5(o5), .o6(o6), .o7(o7));
    wire signed [39:0] ob [0:7];
    assign ob[0] = o0; assign ob[1] = o1; assign ob[2] = o2; assign ob[3] = o3; assign ob[4] = o4; assign ob[5] = o5; assign ob[6] = o6; assign ob[7] = o7;
    function [7:0] limit(input signed [39:0] x);                     // descale by 18, the post-IDCT range limit of libjpeg (the low 10 bits of the descaled value)
        reg signed [39:0] d; reg [9:0] j;
        begin
            d = (x + 40'sd131072) >>> ((BUG == 4) ? 17 : 18); j = d[9:0];
            if (j < 10'd128) limit = j[7:0] + 8'd128; else if (j < ((BUG == 5) ? 10'd511 : 10'd512)) limit = 8'd255; else if (j < 10'd896) limit = 8'd0; else limit = j[7:0] - 8'd128;
        end
    endfunction
    function signed [23:0] desc1(input signed [39:0] x);              // pass 1: descale by 11
        reg signed [39:0] d;
        begin d = (x + 40'sd1024) >>> ((BUG == 3) ? 10 : 11); desc1 = d[23:0]; end
    endfunction
    wire [2:0] bidx = bo_tag[2:0]; wire bpass = bo_tag[3];
    always @(posedge clk) begin
        done <= 1'b0; px_valid <= 1'b0; bf_valid <= 1'b0;
        if (rst) begin st <= S_IDLE; busy <= 1'b0; cnt <= 0; wr1 <= 0; out2 <= 0; end
        else begin
            if (clear && st == S_IDLE) for (i = 0; i < 64; i = i + 1) cf[i] <= 18'sd0;
            else if (we && st == S_IDLE) cf[nat] <= val;
            // pass 1 results go to the workspace, column bidx (BUG 6: transposed)
            if (bo_valid && !bpass) begin
                for (i = 0; i < 8; i = i + 1) ws[(BUG == 6) ? (bidx * 8 + i) : (i * 8 + bidx)] <= desc1(ob[i]);
                wr1 <= wr1 + 1;
            end
            if (bo_valid && bpass) begin
                px_valid <= 1'b1; px_row <= bidx;
                px_data <= {limit(o7), limit(o6), limit(o5), limit(o4), limit(o3), limit(o2), limit(o1), limit(o0)};
                out2 <= out2 + 1;
            end
            case (st)
                S_IDLE: if (start) begin st <= S_P1; busy <= 1'b1; cnt <= 0; wr1 <= 0; out2 <= 0; end
                S_P1: begin                                            // issue column cnt of the coefficient memory
                    bf_valid <= 1'b1; bf_tag <= {1'b0, cnt};
                    b0 <= cf[0 * 8 + cnt]; b1 <= cf[1 * 8 + cnt]; b2 <= cf[2 * 8 + cnt]; b3 <= cf[3 * 8 + cnt]; b4 <= cf[4 * 8 + cnt]; b5 <= cf[5 * 8 + cnt]; b6 <= cf[6 * 8 + cnt]; b7 <= cf[7 * 8 + cnt];
                    cnt <= cnt + 1; if (cnt == 7) st <= S_W1;
                end
                S_W1: if (wr1 == 4'd8 || (bo_valid && !bpass && wr1 == ((BUG == 9) ? 4'd6 : 4'd7))) begin st <= S_P2; cnt <= 0; end    // all eight columns are in the workspace (the last one is being written this clock)
                S_P2: begin                                            // issue row cnt of the workspace
                    bf_valid <= 1'b1; bf_tag <= {1'b1, cnt};
                    b0 <= ws[cnt * 8 + 0]; b1 <= ws[cnt * 8 + 1]; b2 <= ws[cnt * 8 + 2]; b3 <= ws[cnt * 8 + 3]; b4 <= ws[cnt * 8 + 4]; b5 <= ws[cnt * 8 + 5]; b6 <= ws[cnt * 8 + 6]; b7 <= ws[cnt * 8 + 7];
                    cnt <= cnt + 1; if (cnt == 7) st <= S_W2;
                end
                S_W2: if (out2 == 4'd8) begin st <= S_IDLE; busy <= 1'b0; done <= 1'b1; end
                default: st <= S_IDLE;
            endcase
        end
    end
endmodule
