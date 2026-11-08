// Tau Gallery viewer registers in tau_soc.v (VID_MODE, VIEW_BASE, VIEW_STRIDE, VIEW_CAPS) with the real CPU (firmware sim/fw_view/main.c). The bench drives the sequencer status
// inputs, changes them after READY and watches the request / plane outputs. SWAP = 1 swaps the cur and pend inputs (the firmware must see it); ENABLE = 0 builds the SoC without the viewer.
// (Based on tb_gfx_status_soc.v.) The Talos 2 T3 status registers in tau_soc.v (R_FB_GO bit 1, R_GFX_FENCE/STATUS/COUNTS) with the real CPU reading them (firmware sim/fw_gfxstat/main.c).
// The bench drives the tau_gfx status inputs with known values, changes them after the firmware prints READY, and fails on any firmware "FAIL" verdict.
// SWAP = 1 (the mutant) swaps the fence and underflow inputs: the firmware must see the difference. +ROM=<file>.
`timescale 1ns/1ps
`default_nettype none
module tb_view_soc;
    parameter SWAP = 0, ENABLE = 1;
    reg clk = 0, clk74 = 0, rst = 1;
    always #8.333 clk = ~clk;
    always #6.734 clk74 = ~clk74;
    reg         ld_wr = 0;
    reg  [31:0] ld_addr = 0;
    reg  [7:0]  ld_data = 0;
    reg  [7:0]  rom [0:65535];
    integer     rom_n, fd, i, errors = 0;
    reg  [1023:0] romfile;
    wire [4:0]  set_idx;  wire set_wr;  wire [31:0] set_wdata;
    wire [15:0] audio_l, audio_r;
    reg         full = 0, idle = 1;
    reg  [31:0] fence = 32'h00C0FFEE;
    reg  [8:0]  free_s = 9'd123;
    reg  [15:0] ovf = 16'd45, uf = 16'd7;
    reg vcur = 0, vpend = 0; reg [15:0] vsw = 16'd5; wire vreq; wire [24:0] vbase; wire [15:0] vstride;
    reg lidle = 1; reg [31:0] lwords = 32'd1234, lovf = 32'd0; wire [24:0] lbase; wire lclear; integer clear_pulses = 0, clear_hi = 0;
    tau_soc #(.VIEW_ENABLE(ENABLE), .LAND_ENABLE(ENABLE)) u_soc (
        .clk(clk), .rst(rst), .clk_74a(clk74),
        .ld_wr(ld_wr), .ld_addr(ld_addr), .ld_data(ld_data),
        .cont_key(32'd0), .in_menu(1'b0),
        .dataslot_update(1'b0), .dataslot_update_id(16'd0), .dataslot_update_size(32'd0),
        .dataslot_allcomplete(1'b0),
        .tgt_busy(1'b0), .tgt_done(1'b0), .tgt_seq(8'd0), .tgt_err(3'd0),
        .fb_cmd_full(full), .fb_gfx_idle(idle), .fb_gfx_fence(SWAP ? {16'd0, uf} : fence), .fb_gfx_free(free_s), .fb_gfx_ovf(ovf),
        .fb_gfx_underflow(SWAP ? fence[15:0] : uf), .dt_q(32'd0),
        .fb_view_req(vreq), .fb_view_base(vbase), .fb_view_stride(vstride), .fb_vid_cur(SWAP ? vpend : vcur), .fb_vid_pend(SWAP ? vcur : vpend), .fb_vid_switches(vsw), .fb_land_base(lbase), .fb_land_clear(lclear), .fb_land_idle(lidle), .fb_land_words(lwords), .fb_land_ovf(lovf),
        .set_idx(set_idx), .set_wr(set_wr), .set_wdata(set_wdata), .set_rdata(32'd0),
        .sdram_busy(1'b0), .sdram_done(1'b0), .sdram_rdata(32'd0),
        .sdram_wb_accept(1'b0), .sdram_wb_done(1'b0), .sdram_wb_rdata(32'd0),
        .xm_rdata(32'd0),
        .psram_done(1'b0), .psram_rdata(32'd0), .psram_guard(1'b0),
        .audio_l(audio_l), .audio_r(audio_r)
    );
    reg [8*80-1:0] line; reg [31:0] last4 = 0; reg done = 0, ready = 0, ready2 = 0, ready3 = 0;
    always @(posedge clk) if (u_soc.con_wr) begin
        last4 = {last4[23:0], u_soc.con_char};
        if (last4 == "FAIL") errors = errors + 1;
        if (u_soc.con_char == 8'd10) begin
            $display("fw: %0s", line);
            if (line[8*5-1:0] == "READY") ready = 1;
            if (line[8*6-1:0] == "READY2") ready2 = 1;
            if (line[8*6-1:0] == "READY3") ready3 = 1;
            if (line[8*4-1:0] == "DONE") done = 1;
            line = 0;
        end else line = {line[8*79-1:0], u_soc.con_char};
    end
    reg clear_q = 0;
    always @(posedge clk) begin if (lclear) clear_hi = clear_hi + 1; if (lclear && !clear_q) clear_pulses = clear_pulses + 1; clear_q <= lclear; end
    initial begin
        if (!$value$plusargs("ROM=%s", romfile)) romfile = "build/rtl/fw_view.bin";
        fd = $fopen(romfile, "rb");
        if (fd == 0) begin $display("FAIL: cannot open ROM"); $finish; end
        rom_n = $fread(rom, fd);
        $fclose(fd);
        repeat (4) @(posedge clk);
        for (i = 0; i < rom_n; i = i + 1) begin ld_addr <= i; ld_data <= rom[i]; ld_wr <= 1; @(posedge clk); end
        ld_wr <= 0; repeat (4) @(posedge clk);
        rst <= 0;
        wait (ready);
        if (ENABLE != 0) begin
            if (vreq !== 1'b1 || vbase !== 25'h1234567 || vstride !== 16'd1208) begin errors = errors + 1; $display("FAIL: outputs after the firmware's writes: req=%b base=%h stride=%0d", vreq, vbase, vstride); end
        end else if (vreq !== 1'b0 || vbase !== 25'd0 || vstride !== 16'd0) begin errors = errors + 1; $display("FAIL: outputs not constant without the viewer"); end
        @(negedge clk); vcur <= 1; vpend <= 1; vsw <= 16'd6;
        if (ENABLE != 0) begin wait (ready2); @(negedge clk); vcur <= 0; vpend <= 1; end
        if (ENABLE != 0) begin wait (ready3); @(negedge clk); lovf <= 32'd5; lidle <= 0; end
        wait (done); repeat (50) @(posedge clk);
        if (ENABLE != 0 && (clear_pulses != 1 || clear_hi != 1)) begin errors = errors + 1; $display("FAIL: LAND_CTRL gave %0d pulses of %0d clocks, want one of one", clear_pulses, clear_hi); end
        if (ENABLE == 0 && clear_pulses != 0) begin errors = errors + 1; $display("FAIL: a clear pulse without the landing path"); end
        if (ENABLE != 0 && lbase !== 25'h1FFFFFF) begin errors = errors + 1; $display("FAIL: landing base output %h", lbase); end
        if (ENABLE != 0 && vreq !== 1'b0) begin errors = errors + 1; $display("FAIL: the request did not clear"); end
        if (errors == 0) $display("PASSED: tb_view_soc"); else $display("FAILED: %0d errors", errors);
        $finish;
    end
    initial begin #300_000_000; $display("FAIL: timeout"); $finish; end
endmodule
