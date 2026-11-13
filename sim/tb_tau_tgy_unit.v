// Testbench of src/fpga/core/tau_tgy_unit.sv (register block with its clock crossings + DMA + decoder), driven the way the CPU will drive it: MMIO stores with a few clk_sys clocks between them, status polls,
// two UNRELATED clocks (clk_sys 66.7 MHz, clk_mem 100 MHz), the scheduler port model (sim/sp_model.v) behind it. Checks: the registers read back what was written; LOAD_TABLES; a tile started with START_TILE comes
// out right (whole memory compared as in tb_tau_tgy_dma.v) with done / irq / CYCLES; STATUS read right after a start already says busy and not done; a start while busy is rejected and harmless; CLEAR; ABORT
// (err, code 15) then a good tile. Mutants (-P BUG=n, regs 1..7, DMA 11..20) must fail.
// Run: iverilog -g2012 -P tb_tau_tgy_unit.BUG=n ...; vvp ... +VEC=file.hex
`timescale 1ns/1ps
module tb_tau_tgy_unit;
    parameter integer BUG = 0;
    parameter integer C444 = 1;
    localparam integer STREAM_WORDS = 3300, OFF_STREAM = 34, OFF_EXP = 34 + STREAM_WORDS, SLOT = OFF_EXP + 3 * 2048, MEMW = 18, MEMB = 2 << MEMW;
    reg clk_sys = 0, clk_mem = 0, clk_dec = 0, rst = 1; always #7.5 clk_sys = ~clk_sys; always #5 clk_mem = ~clk_mem; initial begin #3.3; forever #10 clk_dec = ~clk_dec; end
    reg [9:0] reg_addr = 0; reg reg_we = 0; reg [31:0] reg_wdata = 0; wire reg_hit, irq; wire [31:0] reg_rdata;
    wire p_valid, p_ready, p_we, p_next, p_r_valid, p_r_last, p_done; wire [24:0] p_addr; wire [9:0] p_len, p_idx; wire [15:0] p_wdata, p_r_data; wire [1:0] p_be;
    tau_tgy_unit #(.C444(C444), .BUG(BUG), .TMO(24'hFFFFFF)) dut (.clk_sys(clk_sys), .rst_sys(rst), .reg_addr(reg_addr), .reg_we(reg_we), .reg_wdata(reg_wdata), .reg_hit(reg_hit), .reg_rdata(reg_rdata), .irq(irq),
        .clk_mem(clk_mem), .rst_mem(rst), .clk_dec(clk_dec), .p_valid(p_valid), .p_ready(p_ready), .p_we(p_we), .p_addr(p_addr), .p_len(p_len), .p_next(p_next), .p_idx(p_idx), .p_wdata(p_wdata), .p_be(p_be),
        .p_r_valid(p_r_valid), .p_r_data(p_r_data), .p_r_last(p_r_last), .p_done(p_done));
    sp_model #(.SEED(33), .MEMW(MEMW), .RDYPCT(70)) port (.clk(clk_mem), .rst(rst), .valid(p_valid), .ready(p_ready), .we(p_we), .addr(p_addr), .len(p_len), .w_next(p_next), .w_idx(p_idx), .w_data(p_wdata), .w_be(p_be),
        .r_valid(p_r_valid), .r_data(p_r_data), .r_last(p_r_last), .done(p_done), .stall(1'b0));
    reg [63:0] mem [0:SLOT * 12]; reg [7:0] gold [0:MEMB - 1];
    reg [8*200-1:0] vecfile; integer errors = 0, k, base, f, w, h, len, cyc, nbad, pitch, srcb, dstb; reg [31:0] v;
    integer SRC_BASE = 'h1000, TBL_Y = 'h200, TBL_C = 'h300, DST_BASE = 'h8000;
    task fail(input [255:0] m); begin errors = errors + 1; if (errors < 12) $display("FAIL: %0s (t=%0t)", m, $time); end endtask
    task mmio_w(input [9:0] a, input [31:0] d); begin @(posedge clk_sys); reg_addr <= a; reg_we <= 1; reg_wdata <= d; @(posedge clk_sys); reg_we <= 0; reg_addr <= 0; repeat (3) @(posedge clk_sys); end endtask
    task mmio_r(input [9:0] a); begin reg_addr <= a; @(posedge clk_sys); #1; v = reg_rdata; @(posedge clk_sys); reg_addr <= 0; end endtask
    task setbyte(input integer a, input [7:0] val); begin if (a % 2 == 0) port.mem[a / 2][7:0] = val; else port.mem[a / 2][15:8] = val; gold[a] = val; end endtask
    task snapshot; integer j; begin for (j = 0; j < MEMB; j = j + 1) gold[j] = (j % 2 == 0) ? port.mem[j / 2][7:0] : port.mem[j / 2][15:8]; end endtask
    task put_tile(input integer tt); integer j; reg [63:0] wd; begin
        for (j = 0; j < 128; j = j + 1) begin wd = mem[tt * SLOT + 2 + j / 4]; setbyte((j < 64 ? TBL_Y : TBL_C) + 2 * (j % 64), wd[16 * (j % 4) +: 8]); setbyte((j < 64 ? TBL_Y : TBL_C) + 2 * (j % 64) + 1, wd[16 * (j % 4) + 8 +: 8]); end
        for (j = 0; j < len; j = j + 1) begin wd = mem[tt * SLOT + OFF_STREAM + j / 8]; setbyte(srcb + j, wd[8 * (j % 8) +: 8]); end
    end endtask
    task put_expected(input integer tt); integer y, x, c; reg [63:0] wd; begin
        for (y = 0; y < h; y = y + 1) for (x = 0; x < w; x = x + 1) for (c = 0; c < 3; c = c + 1) begin wd = mem[tt * SLOT + OFF_EXP + c * 2048 + y * 16 + x / 8]; gold[dstb + y * pitch + 3 * x + c] = wd[8 * (x % 8) +: 8]; end
    end endtask
    integer stx_i, sty_i;
    task put_expected_sc(input integer tt); integer y, x, c; reg [63:0] wd; begin
        for (y = 0; y < h; y = y + 1) for (x = 0; x < w; x = x + 1) for (c = 0; c < 3; c = c + 1) begin wd = mem[tt * SLOT + OFF_EXP + c * 2048 + y * 16 + x / 8]; gold[dstb + x * stx_i + y * sty_i + c] = wd[8 * (x % 8) +: 8]; end
    end endtask
    task compare(input integer lbl); integer j; begin
        nbad = 0;
        for (j = 0; j < MEMB; j = j + 1) if (((j % 2 == 0) ? port.mem[j / 2][7:0] : port.mem[j / 2][15:8]) !== gold[j]) begin nbad = nbad + 1; if (nbad < 4) $display("check %0d: byte %0d (0x%0h) = %0h want %0h", lbl, j, j, (j % 2 == 0) ? port.mem[j / 2][7:0] : port.mem[j / 2][15:8], gold[j]); end
        if (nbad != 0) fail("memory differs");
    end endtask
    task program_tile; begin
        mmio_w(10'h228, srcb); mmio_w(10'h22C, len); mmio_w(10'h230, dstb); mmio_w(10'h234, pitch); mmio_w(10'h238, (f << 16) | (w << 8) | h); mmio_w(10'h23C, TBL_Y); mmio_w(10'h240, TBL_C);
    end endtask
    task poll_idle(input integer maxpoll); integer n; begin
        n = 0; mmio_r(10'h224); while (v[0] && n < maxpoll) begin repeat (7 + (n % 5)) @(posedge clk_sys); mmio_r(10'h224); n = n + 1; end
        if (n >= maxpoll) fail("never became idle");
    end endtask
    initial begin
        if (!$value$plusargs("VEC=%s", vecfile)) begin $display("no +VEC"); $finish; end
        $readmemh(vecfile, mem);
        repeat (6) @(posedge clk_sys); rst <= 0; repeat (6) @(posedge clk_sys);
        mmio_r(10'h224); if (v !== 32'h80000000) begin fail("STATUS after reset is not 'present, idle'"); $display("  got %h", v); end
        // ---- tile 4: a small 4:2:0 tile (48 x 48), odd source and odd destination
        base = 4 * SLOT; f = mem[base][17:16]; w = mem[base][15:8]; h = mem[base][7:0]; len = mem[base + 1]; pitch = 397; srcb = SRC_BASE + 1; dstb = DST_BASE + 1; snapshot; put_tile(4); put_expected(4);
        program_tile;
        mmio_r(10'h228); if (v !== srcb) fail("SRC reads back wrong"); mmio_r(10'h22C); if (v !== len) fail("LEN reads back wrong"); mmio_r(10'h230); if (v !== dstb) fail("DST reads back wrong");
        mmio_r(10'h234); if (v !== pitch) fail("PITCH reads back wrong"); mmio_r(10'h238); if (v !== ((f << 16) | (w << 8) | h)) fail("GEOM reads back wrong"); mmio_r(10'h23C); if (v !== TBL_Y) fail("QY reads back wrong"); mmio_r(10'h240); if (v !== TBL_C) fail("QC reads back wrong");
        mmio_w(10'h220, 32'h2);                                                     // LOAD_TABLES
        mmio_r(10'h224); if (!v[0]) fail("STATUS right after LOAD_TABLES is not busy");
        poll_idle(2000); mmio_r(10'h224); if (!v[3] || v[1] || v[2]) fail("tables flag after LOAD_TABLES wrong");
        mmio_w(10'h220, 32'h1);                                                     // START_TILE
        mmio_r(10'h224); if (!v[0] || v[1]) fail("STATUS right after START is not busy and not done");
        mmio_w(10'h220, 32'h1); mmio_r(10'h224); if (!v[4]) fail("a start while busy was not reported as rejected");
        poll_idle(40000); mmio_r(10'h224);
        if (!v[1] || v[2] || v[0]) fail("STATUS after the tile is not 'done, no error'"); if (!irq) fail("irq not raised at done");
        mmio_r(10'h244); if (v == 0) fail("CYCLES is zero"); else $display("tile 4 (%0d bytes, %0dx%0d, fmt %0d): %0d SDRAM clocks", len, w, h, f, v);
        compare(1);
        // ---- the second tile: a second START after done runs again (same memory image, expected unchanged); ABORT in the middle of the real sparse tile
        base = 6 * SLOT; f = mem[base][17:16]; w = mem[base][15:8]; h = mem[base][7:0]; len = mem[base + 1]; pitch = 400; srcb = SRC_BASE; dstb = DST_BASE; snapshot; put_tile(6); put_expected(6);
        program_tile; mmio_w(10'h220, 32'h2); poll_idle(2000); mmio_r(10'h224); if (!v[1]) fail("done flag lost before the next start");
        mmio_w(10'h220, 32'h1); mmio_r(10'h224); if (!v[0] || v[1]) fail("a start did not clear the old done flag (STATUS right after START)");
        repeat (6000) @(posedge clk_sys); mmio_w(10'h220, 32'h4);                   // ABORT
        poll_idle(4000); mmio_r(10'h224); if (!v[2] || v[11:8] != 4'd15 || v[1]) fail("abort: STATUS not 'err, code 15'"); if (!irq) fail("irq not raised at err");
        mmio_r(10'h224); if (!v[2]) fail("err flag lost before CLEAR"); mmio_w(10'h220, 32'h8); mmio_r(10'h224); if (v[2] || v[1]) fail("CLEAR did not clear err");
        mmio_w(10'h220, 32'h1); poll_idle(80000); mmio_r(10'h224); if (!v[1] || v[2]) fail("tile after abort not done"); compare(2);
        if (!irq) fail("irq not raised at done (second tile)"); mmio_w(10'h220, 32'h8); mmio_r(10'h224); if (v[1] || v[2] || v[4]) fail("CLEAR did not clear done / err / rejected"); if (irq) fail("irq still up after CLEAR");
        // ---- K7: the scatter registers. Tile 4 (48 x 48, 4:2:0) written transposed and mirrored (STX = +400 bytes, STY = -3 bytes), from the registers; then the same tile with bit 18 clear writes rows again
        base = 4 * SLOT; f = mem[base][17:16]; w = mem[base][15:8]; h = mem[base][7:0]; len = mem[base + 1]; pitch = 400; srcb = SRC_BASE + 1; stx_i = 400; sty_i = -3; dstb = DST_BASE + 1 + (h - 1) * 3;
        snapshot; put_tile(4); put_expected_sc(4);
        mmio_w(10'h228, srcb); mmio_w(10'h22C, len); mmio_w(10'h230, dstb); mmio_w(10'h234, pitch); mmio_w(10'h23C, TBL_Y); mmio_w(10'h240, TBL_C);
        mmio_w(10'h248, stx_i); mmio_w(10'h24C, sty_i); mmio_w(10'h238, (1 << 18) | (f << 16) | (w << 8) | h);
        mmio_r(10'h248); if (v !== (stx_i & 32'h3FFFF)) fail("STX reads back wrong"); mmio_r(10'h24C); if (v !== (sty_i & 32'h3FFFF)) fail("STY reads back wrong");
        mmio_r(10'h238); if (v !== ((1 << 18) | (f << 16) | (w << 8) | h)) fail("GEOM bit 18 reads back wrong");
        mmio_w(10'h220, 32'h2); poll_idle(2000); mmio_w(10'h220, 32'h8); mmio_w(10'h220, 32'h1); poll_idle(120000); mmio_r(10'h224); if (!v[1] || v[2]) fail("scatter tile not done"); compare(3);
        mmio_w(10'h220, 32'h8);
        base = 4 * SLOT; pitch = 397; srcb = SRC_BASE + 1; dstb = DST_BASE + 1; stx_i = 3; sty_i = pitch; snapshot; put_tile(4); put_expected(4);
        program_tile; mmio_w(10'h220, 32'h2); poll_idle(2000); mmio_w(10'h220, 32'h8); mmio_w(10'h220, 32'h1); poll_idle(80000); mmio_r(10'h224); if (!v[1] || v[2]) fail("tile after the scatter tile not done"); compare(4);
        if (errors == 0) $display("UNIT PASSED (0 failures)"); else $display("UNIT FAILED %0d", errors);
        $finish;
    end
endmodule
