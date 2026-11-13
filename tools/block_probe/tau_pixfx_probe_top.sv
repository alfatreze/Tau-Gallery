// tau_pixfx_probe_top.sv -- timing probe of the pixel effects (dither + colour table) alone with its two real clocks (clk_sys 66.7 MHz, clk_vid 40 MHz): every input and output is one register away from a virtual pin. MIT licence.
module tau_pixfx_probe_top (
    input  wire clk_sys, clk_vid,
    input  wire [21:0] vi_sys,     // mode[1:0], lut_on, lut_we, lut_word[17:0]
    input  wire [34:0] vi_vid,     // px_in[23:0], de, hs, vs, x[2:0], y[2:0], f[1:0] -> 24+3+3+3+2 = 35
    output wire [0:0]  vo_sys,     // lut_busy
    output wire [26:0] vo_vid      // px_out[23:0], de, hs, vs
);
    reg [21:0] is = 0; reg [34:0] iv = 0; reg [0:0] os = 0; reg [26:0] ov = 0;
    always @(posedge clk_sys) is <= vi_sys;
    always @(posedge clk_vid) iv <= vi_vid;
    reg rs = 1'b1; reg [3:0] rc = 4'd0; always @(posedge clk_sys) begin if (rc != 4'hF) rc <= rc + 4'd1; rs <= (rc != 4'hF); end
    wire busy, de, hs, vs; wire [23:0] po;
    tau_pixfx u (.clk_sys(clk_sys), .clk_vid(clk_vid), .reset(rs), .mode(is[21:20]), .lut_on(is[19]), .lut_we(is[18]), .lut_word(is[17:0]), .lut_busy(busy),
        .px_in(iv[34:11]), .de_in(iv[10]), .hs_in(iv[9]), .vs_in(iv[8]), .x_in(iv[7:5]), .y_in(iv[4:2]), .f_in(iv[1:0]), .px_out(po), .de_out(de), .hs_out(hs), .vs_out(vs));
    always @(posedge clk_sys) os <= busy;
    always @(posedge clk_vid) ov <= {po, de, hs, vs};
    assign vo_sys = os; assign vo_vid = ov;
endmodule
