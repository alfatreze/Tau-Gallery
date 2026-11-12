// tau_tgy_unit_probe_top.sv -- timing probe of the hardware TGY decoder unit alone (tools/block_probe.py, docs/FULLCORE_SIM.md): the unit with its three real clocks (66.7 / 100 / 50 MHz) and every input and output one
// register away from a virtual pin, so the timing of the unit's own logic is measured without the rest of the core. A fit of this takes minutes; the full core takes about 1h45. MIT licence.
module tau_tgy_unit_probe_top (
    input  wire clk_sys, clk_mem, clk_dec,
    input  wire [42:0] vi_sys,     // reg_addr[9:0], reg_we, reg_wdata[31:0]
    input  wire [31:0] vi_mem,     // p_ready, p_next, p_idx[9:0], p_r_valid, p_r_data[15:0], p_r_last, p_done
    output wire [33:0] vo_sys,     // reg_hit, reg_rdata[31:0], irq
    output wire [54:0] vo_mem      // p_valid, p_we, p_addr[24:0], p_len[9:0], p_wdata[15:0], p_be[1:0]
);
    reg [42:0] is = 0; reg [31:0] im = 0; reg [33:0] os = 0; reg [54:0] om = 0;
    always @(posedge clk_sys) is <= vi_sys;
    always @(posedge clk_mem) im <= vi_mem;
    wire hit, irq, p_valid, p_we; wire [31:0] rdata; wire [24:0] p_addr; wire [9:0] p_len; wire [15:0] p_wdata; wire [1:0] p_be;
    reg rst_sys = 1'b1, rst_mem = 1'b1; reg [3:0] rc = 4'd0;
    always @(posedge clk_mem) begin if (rc != 4'hF) rc <= rc + 4'd1; rst_mem <= (rc != 4'hF); end
    always @(posedge clk_sys) rst_sys <= rst_mem;
    tau_tgy_unit #(.C444(0)) u (.clk_sys(clk_sys), .rst_sys(rst_sys), .reg_addr(is[42:33]), .reg_we(is[32]), .reg_wdata(is[31:0]), .reg_hit(hit), .reg_rdata(rdata), .irq(irq),
        .clk_mem(clk_mem), .rst_mem(rst_mem), .clk_dec(clk_dec), .p_valid(p_valid), .p_ready(im[31]), .p_we(p_we), .p_addr(p_addr), .p_len(p_len), .p_next(im[30]), .p_idx(im[29:20]), .p_wdata(p_wdata), .p_be(p_be),
        .p_r_valid(im[19]), .p_r_data(im[18:3]), .p_r_last(im[2]), .p_done(im[1]));
    always @(posedge clk_sys) os <= {hit, rdata, irq};
    always @(posedge clk_mem) om <= {p_valid, p_we, p_addr, p_len, p_wdata, p_be};
    assign vo_sys = os; assign vo_mem = om;
endmodule
