// tau_soc.v with TGY_ENABLE=1 and the real VexRiscv running sim/fw_tgy/main.c: the MMIO window 0x220-0x24C reaches tau_tgy_unit, the interrupt source 4 reaches the pending register, CORE_VERSION is rev 29
// (compile with -DTAU_TGY). The unit's scheduler port is the model sim/sp_model.v (no scheduler, no dmux here: those are tb_tau_dmux.v and tb_tau_tgy_*.v). The testbench preloads the model with the tables
// and the stream of tile 4 of the decoder vectors, checks each line the firmware prints, then compares the whole memory with the expected picture. +ROM=<file> +VEC=<file>
`timescale 1ns/1ps
`default_nettype none
module tb_tgy_soc;
    reg clk = 0, clk74 = 0, clkm = 0, clkd = 0, rst = 1;
    always #8.333 clk = ~clk; always #6.734 clk74 = ~clk74; always #5 clkm = ~clkm; initial begin #3.3; forever #10 clkd = ~clkd; end
    reg ld_wr = 0; reg [31:0] ld_addr = 0; reg [7:0] ld_data = 0; reg [7:0] rom [0:65535];
    integer rom_n, fd, i, errors = 0; reg [1023:0] romfile; reg [8*200-1:0] vecfile;
    wire [4:0] set_idx; wire set_wr; wire [31:0] set_wdata; wire [15:0] audio_l, audio_r;
    wire [9:0] tg_addr; wire tg_we, tg_hit, tg_irq; wire [31:0] tg_wdata, tg_rdata;
    tau_soc #(.IRQ_ENABLE(1), .TGY_ENABLE(1)) u_soc (
        .clk(clk), .rst(rst), .clk_74a(clk74), .ld_wr(ld_wr), .ld_addr(ld_addr), .ld_data(ld_data), .cont_key(32'd0), .in_menu(1'b0),
        .dataslot_update(1'b0), .dataslot_update_id(16'd0), .dataslot_update_size(32'd0), .dataslot_allcomplete(1'b0),
        .tgt_busy(1'b0), .tgt_done(1'b0), .tgt_seq(8'd0), .tgt_err(3'd0),
        .fb_cmd_full(1'b0), .fb_gfx_idle(1'b0), .fb_gfx_fence(32'd0), .fb_gfx_free(9'd0), .fb_gfx_ovf(16'd0), .fb_gfx_underflow(16'd0), .dt_q(32'd0),
        .fb_vid_cur(1'b0), .fb_vid_pend(1'b0), .fb_vid_switches(16'd0), .fb_land_idle(1'b0), .fb_land_words(32'd0), .fb_land_ovf(32'd0), .vblank_rd(17'd0),
        .fb_tgy_addr(tg_addr), .fb_tgy_we(tg_we), .fb_tgy_wdata(tg_wdata), .fb_tgy_hit(tg_hit), .fb_tgy_rdata(tg_rdata), .fb_tgy_irq(tg_irq),
        .set_idx(set_idx), .set_wr(set_wr), .set_wdata(set_wdata), .set_rdata(32'd0), .sdram_busy(1'b0), .sdram_done(1'b0), .sdram_rdata(32'd0),
        .sdram_wb_accept(1'b0), .sdram_wb_done(1'b0), .sdram_wb_rdata(32'd0), .xm_rdata(32'd0), .psram_done(1'b0), .psram_rdata(32'd0), .psram_guard(1'b0),
        .audio_l(audio_l), .audio_r(audio_r));
    wire p_valid, p_ready, p_we, p_next, p_r_valid, p_r_last, p_done; wire [24:0] p_addr; wire [9:0] p_len, p_idx; wire [15:0] p_wdata, p_r_data; wire [1:0] p_be;
    tau_tgy_unit #(.C444(0)) u_tgy (.clk_sys(clk), .rst_sys(rst), .reg_addr(tg_addr), .reg_we(tg_we), .reg_wdata(tg_wdata), .reg_hit(tg_hit), .reg_rdata(tg_rdata), .irq(tg_irq),
        .clk_mem(clkm), .rst_mem(rst), .clk_dec(clkd), .p_valid(p_valid), .p_ready(p_ready), .p_we(p_we), .p_addr(p_addr), .p_len(p_len), .p_next(p_next), .p_idx(p_idx), .p_wdata(p_wdata), .p_be(p_be),
        .p_r_valid(p_r_valid), .p_r_data(p_r_data), .p_r_last(p_r_last), .p_done(p_done));
    sp_model #(.SEED(41), .MEMW(18), .RDYPCT(70)) port (.clk(clkm), .rst(rst), .valid(p_valid), .ready(p_ready), .we(p_we), .addr(p_addr), .len(p_len), .w_next(p_next), .w_idx(p_idx), .w_data(p_wdata), .w_be(p_be),
        .r_valid(p_r_valid), .r_data(p_r_data), .r_last(p_r_last), .done(p_done), .stall(1'b0));
    localparam integer STREAM_WORDS = 3300, OFF_STREAM = 34, OFF_EXP = 34 + STREAM_WORDS, SLOT = OFF_EXP + 3 * 2048, MEMB = 2 << 18;
    reg [63:0] mem [0:SLOT * 5]; reg [7:0] gold [0:MEMB - 1];
    integer base, len, w, h, pitch, srcb, dstb, nbad, j, y, x, c; reg [63:0] wd;
    task setbyte(input integer a, input [7:0] v); begin if (a % 2 == 0) port.mem[a / 2][7:0] = v; else port.mem[a / 2][15:8] = v; gold[a] = v; end endtask
    reg [8*80-1:0] line = 0; reg [15:0] seen = 0;
    always @(posedge clk) if (u_soc.con_wr) begin
        if (u_soc.con_char == 8'd10) begin
            $display("fw: %0s", line);
            if (line == "VER 4D50331F") seen[0] = 1;
            if (line == "ST 80000000") seen[1] = 1;
            if (line == "QCHK 0002C91C") seen[2] = 1;
            if (line == "BUSY 80000001") seen[3] = 1;
            if (line == "TBL 80000008") seen[4] = 1;
            if (line == "BUSY2 80000009") seen[5] = 1;
            if (line == "TILE 8000000A") seen[6] = 1;
            if (line == "IRQ 00000010") seen[7] = 1;
            if (line == "IRQ2 00000000") seen[8] = 1;
            if (line == "ST2 80000008") seen[9] = 1;
            if (line == "CYC 00000001") seen[10] = 1;
            if (line == "TGY DONE") seen[11] = 1;
            line = 0;
        end else line = {line[8*79-1:0], u_soc.con_char};
    end
    initial begin
        if (!$value$plusargs("ROM=%s", romfile)) romfile = "build/rtl/fw_tgy.bin";
        if (!$value$plusargs("VEC=%s", vecfile)) vecfile = "build/rtl/dec_vec.hex";
        $readmemh(vecfile, mem);
        fd = $fopen(romfile, "rb"); if (fd == 0) begin $display("FAIL: cannot open ROM"); $finish; end
        rom_n = $fread(rom, fd); $fclose(fd);
        base = 4 * SLOT; len = mem[base + 1]; w = mem[base][15:8]; h = mem[base][7:0]; pitch = 397; srcb = 'h1001; dstb = 'h8001;
        if (mem[base][17:16] != 2 || w != 48 || h != 48 || len != 605) begin $display("FAIL: tile 4 of the vectors is not the 4:2:0 48 x 48 605-byte tile the firmware expects"); $finish; end
        for (j = 0; j < (2 << 18); j = j + 1) gold[j] = (j % 2 == 0) ? port.mem[j / 2][7:0] : port.mem[j / 2][15:8];
        for (j = 0; j < 128; j = j + 1) begin wd = mem[base + 2 + j / 4]; setbyte((j < 64 ? 'h200 : 'h300) + 2 * (j % 64), wd[16 * (j % 4) +: 8]); setbyte((j < 64 ? 'h200 : 'h300) + 2 * (j % 64) + 1, wd[16 * (j % 4) + 8 +: 8]); end
        for (j = 0; j < len; j = j + 1) begin wd = mem[base + OFF_STREAM + j / 8]; setbyte(srcb + j, wd[8 * (j % 8) +: 8]); end
        for (y = 0; y < h; y = y + 1) for (x = 0; x < w; x = x + 1) for (c = 0; c < 3; c = c + 1) begin wd = mem[base + OFF_EXP + c * 2048 + y * 16 + x / 8]; gold[dstb + y * pitch + 3 * x + c] = wd[8 * (x % 8) +: 8]; end
        repeat (4) @(posedge clk);
        for (i = 0; i < rom_n; i = i + 1) begin ld_addr <= i; ld_data <= rom[i]; ld_wr <= 1; @(posedge clk); end
        ld_wr <= 0; repeat (4) @(posedge clk); rst <= 0;
        wait (seen[11] === 1'b1); repeat (50) @(posedge clk);
        for (j = 0; j < MEMB; j = j + 1) if (((j % 2 == 0) ? port.mem[j / 2][7:0] : port.mem[j / 2][15:8]) !== gold[j]) begin nbad = nbad + 1; if (nbad < 4) $display("FAIL: byte %0d = %0h want %0h", j, (j % 2 == 0) ? port.mem[j / 2][7:0] : port.mem[j / 2][15:8], gold[j]); end
        if (nbad != 0) begin errors = errors + 1; $display("FAIL: %0d bytes of the picture memory differ", nbad); end
        for (j = 0; j < 12; j = j + 1) if (!seen[j]) begin errors = errors + 1; $display("FAIL: firmware line %0d missing or wrong", j); end
        if (errors == 0) $display("PASSED: tb_tgy_soc"); else $display("FAILED: %0d errors", errors);
        $finish;
    end
    initial begin #20_000_000; $display("FAIL: timeout (dADR %h dCYC %b dSTB %b dACK %b irq_pend %h irq_ext %h)", u_soc.dADR, u_soc.dCYC, u_soc.dSTB, u_soc.dACK, u_soc.irq_pend, u_soc.irq_ext); $finish; end
endmodule
