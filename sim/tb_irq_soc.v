// Pack watchdog on the real CPU (docs/features/PACK_WATCHDOG_PLAN.md, W2): tau_soc with WDOG_ENABLE=1 running sim/fw_wdog/main.c (arm, take the timer interrupt, recover at the call site).
// The firmware prints WDOG ok/FAIL lines and WDOG DONE; any FAIL, a missing DONE or a timeout fails the bench. MUT_RTL = 1 cuts the interrupt line: the firmware hangs and the bench must FAIL. +ROM=<file>.
`timescale 1ns/1ps
`default_nettype none
module tb_irq_soc;
    parameter SWAP = 0;
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
    reg tgt_done_r = 0, land_idle = 0; reg [16:0] vbl = 17'd0; reg [15:0] vsw = 16'd0;
    tau_soc #(.IRQ_ENABLE(1), .LAND_ENABLE(1), .VIEW_ENABLE(1), .VBLANK_ENABLE(1)) u_soc (
        .clk(clk), .rst(rst), .clk_74a(clk74),
        .ld_wr(ld_wr), .ld_addr(ld_addr), .ld_data(ld_data),
        .cont_key(32'd0), .in_menu(1'b0),
        .dataslot_update(1'b0), .dataslot_update_id(16'd0), .dataslot_update_size(32'd0),
        .dataslot_allcomplete(1'b0),
        .tgt_busy(1'b0), .tgt_done(tgt_done_r), .tgt_seq(8'd0), .tgt_err(3'd0),
        .fb_cmd_full(full), .fb_gfx_idle(idle), .fb_gfx_fence(SWAP ? {16'd0, uf} : fence), .fb_gfx_free(free_s), .fb_gfx_ovf(ovf),
        .fb_gfx_underflow(SWAP ? fence[15:0] : uf), .dt_q(32'd0), .vblank_rd(vbl), .fb_land_idle(land_idle), .fb_land_words(32'd0), .fb_land_ovf(32'd0), .fb_vid_cur(1'b0), .fb_vid_pend(1'b0), .fb_vid_switches(vsw), .fb_view_req(), .fb_view_base(), .fb_view_stride(), .fb_land_base(), .fb_land_clear(),
        .set_idx(set_idx), .set_wr(set_wr), .set_wdata(set_wdata), .set_rdata(32'd0),
        .sdram_busy(1'b0), .sdram_done(1'b0), .sdram_rdata(32'd0),
        .sdram_wb_accept(1'b0), .sdram_wb_done(1'b0), .sdram_wb_rdata(32'd0),
        .xm_rdata(32'd0),
        .psram_done(1'b0), .psram_rdata(32'd0), .psram_guard(1'b0),
        .audio_l(audio_l), .audio_r(audio_r)
    );
    reg [8*80-1:0] line; reg [31:0] last4 = 0; reg done = 0, ready = 0, p1 = 0, p2 = 0, p3 = 0, p4 = 0, p5 = 0;
    always @(posedge clk) if (u_soc.con_wr) begin
        last4 = {last4[23:0], u_soc.con_char};
        if (last4 == "FAIL") errors = errors + 1;
        if (u_soc.con_char == 8'd10) begin
            $display("fw: %0s", line);
            if (line[8*4-1:0] == "DONE") done = 1;
            if (line[8*2-1:0] == "P1") p1 = 1;
            if (line[8*2-1:0] == "P2") p2 = 1;
            if (line[8*2-1:0] == "P3") p3 = 1;
            if (line[8*2-1:0] == "P4") p4 = 1;
            if (line[8*2-1:0] == "P5") p5 = 1;
            line = 0;
        end else line = {line[8*79-1:0], u_soc.con_char};
    end
    initial begin
        if (!$value$plusargs("ROM=%s", romfile)) romfile = "build/rtl/fw_irq.bin";
        fd = $fopen(romfile, "rb");
        if (fd == 0) begin $display("FAIL: cannot open ROM"); $finish; end
        rom_n = $fread(rom, fd);
        $fclose(fd);
        repeat (4) @(posedge clk);
        for (i = 0; i < rom_n; i = i + 1) begin ld_addr <= i; ld_data <= rom[i]; ld_wr <= 1; @(posedge clk); end
        ld_wr <= 0; repeat (4) @(posedge clk);
        rst <= 0;
        // the sources, driven after the firmware's markers
        wait (p1); repeat (2000) @(posedge clk); tgt_done_r <= 1; repeat (8) @(posedge clk); tgt_done_r <= 0;                  // P1: bit 0, polled
        wait (p2);                                                                                                           // P2: three pulses while mix() runs
        repeat (10000) @(posedge clk); tgt_done_r <= 1; repeat (8) @(posedge clk); tgt_done_r <= 0;
        repeat (15000) @(posedge clk); tgt_done_r <= 1; repeat (8) @(posedge clk); tgt_done_r <= 0;
        repeat (15000) @(posedge clk); tgt_done_r <= 1; repeat (8) @(posedge clk); tgt_done_r <= 0;
        wait (p3); repeat (400) @(posedge clk); tgt_done_r <= 1; repeat (8) @(posedge clk); tgt_done_r <= 0;              // P3: bit 0 while masked
        wait (p4); repeat (400) @(posedge clk); land_idle <= 1;                                                             // P4: bit 2 rising, not unmasked
        wait (p5); repeat (400) @(posedge clk); land_idle <= 0; repeat (100) @(posedge clk); land_idle <= 1;               // P5: a new edge, unmasked
        wait (done); repeat (50) @(posedge clk);
        if (errors == 0 && done) $display("PASSED: tb_irq_soc"); else $display("FAILED: %0d errors", errors);
        $finish;
    end
    initial begin #60_000_000; $display("FAIL: timeout"); $finish; end
endmodule
