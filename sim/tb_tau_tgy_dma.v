// Testbench of src/fpga/core/tau_tgy_dma.sv (stream fetch, table load, pixel row writer around the whole decoder) against a model of one scheduler client port (sim/sp_model.v, random ready, 2-clock write lead,
// byte enables). Memory in the model holds, at byte addresses chosen per tile (odd and even stream start, odd and even destination, odd and even pitch): the two quantisation tables, the tile stream, a picture
// plane. After each tile EVERY byte of the memory is compared: the tile's RGB bytes (from the Python reference through sim/gen_tgy_dec_vectors.py) at dst + y * pitch + 3 x + c, all other bytes unchanged
// (sentinel pattern), so a wrong byte enable, address or row length shows. Scenarios after the vectors: abort in mid-tile, a corrupt stream (decoder error), a starved port (tile timeout), each followed by a good tile.
// Run: iverilog -g2012 -P tb_tau_tgy_dma.BUG=n -P tb_tau_tgy_dma.C444=c ...; vvp ... +VEC=file.hex +N=count
`timescale 1ns/1ps
module tb_tau_tgy_dma;
    parameter integer BUG = 0;
    parameter integer C444 = 1;
    localparam integer STREAM_WORDS = 3300, OFF_STREAM = 34, OFF_EXP = 34 + STREAM_WORDS, SLOT = OFF_EXP + 3 * 2048, MEMW = 18, MEMB = 2 << MEMW;
    reg clk = 0, rst = 1, clk_dec = 0; always #5 clk = ~clk; initial begin #3.3; forever #10 clk_dec = ~clk_dec; end
    reg [2:0] rsd = 3'b111; always @(posedge clk_dec) rsd <= {rsd[1:0], rst}; wire rst_dec = rsd[2];
    reg [25:0] cfg_src = 0, cfg_dst = 0, cfg_qy = 0, cfg_qc = 0; reg [19:0] cfg_len = 0; reg [15:0] cfg_pitch = 0; reg [1:0] cfg_fmt = 0; reg [7:0] cfg_w = 0, cfg_h = 0;
    reg cmd_tile = 0, cmd_tables = 0, cmd_abort = 0; wire busy, ev_done, ev_err, ev_tables; wire [3:0] err_code; wire [31:0] tile_cycles;
    wire p_valid, p_ready, p_we, p_next, p_r_valid, p_r_last, p_done; wire [24:0] p_addr; wire [9:0] p_len, p_idx; wire [15:0] p_wdata, p_r_data; wire [1:0] p_be; reg stall = 0, stall_slow = 0, slow_en = 0; integer slow_c = 0; wire stall_w = stall | stall_slow;
    always @(posedge clk) if (slow_en) begin slow_c <= slow_c + 1; stall_slow <= ((slow_c % 9000) > 5500); end else stall_slow <= 0;      // a slow SDRAM: the port refuses everything 3,500 clocks of every 9,000
    tau_tgy_dma #(.C444(C444), .BUG(BUG), .TMO(24'd1200000)) dut (.clk(clk), .rst(rst), .clk_dec(clk_dec), .rst_dec(rst_dec), .cfg_src(cfg_src), .cfg_dst(cfg_dst), .cfg_qy(cfg_qy), .cfg_qc(cfg_qc), .cfg_len(cfg_len), .cfg_pitch(cfg_pitch), .cfg_fmt(cfg_fmt),
        .cfg_w(cfg_w), .cfg_h(cfg_h), .cmd_tile(cmd_tile), .cmd_tables(cmd_tables), .cmd_abort(cmd_abort), .busy(busy), .ev_done(ev_done), .ev_err(ev_err), .ev_tables(ev_tables), .err_code(err_code), .tile_cycles(tile_cycles),
        .p_valid(p_valid), .p_ready(p_ready), .p_we(p_we), .p_addr(p_addr), .p_len(p_len), .p_next(p_next), .p_idx(p_idx), .p_wdata(p_wdata), .p_be(p_be), .p_r_valid(p_r_valid), .p_r_data(p_r_data), .p_r_last(p_r_last), .p_done(p_done));
    sp_model #(.SEED(21), .MEMW(MEMW), .RDYPCT(70)) port (.clk(clk), .rst(rst), .valid(p_valid), .ready(p_ready), .we(p_we), .addr(p_addr), .len(p_len), .w_next(p_next), .w_idx(p_idx), .w_data(p_wdata), .w_be(p_be),
        .r_valid(p_r_valid), .r_data(p_r_data), .r_last(p_r_last), .done(p_done), .stall(stall_w));
    reg [63:0] mem [0:SLOT * 12];
    reg [7:0] gold [0:MEMB - 1];
    reg [8*200-1:0] vecfile; integer nt, t, errors = 0, base, f, w, h, len, k, cyc, ev_d, ev_e, ev_t, skipped = 0, ran = 0, bad, nbad; reg [3:0] last_code;
    integer SRC_BASE = 'h1000, TBL_Y = 'h200, TBL_C = 'h300, DST_BASE = 'h8000; integer src_off, dst_off, pitch, srcb, dstb;
    always @(posedge clk) begin if (ev_done) ev_d = ev_d + 1; if (ev_err) begin ev_e = ev_e + 1; last_code = err_code; end if (ev_tables) ev_t = ev_t + 1; end
    task setbyte(input integer a, input [7:0] v); begin if (a % 2 == 0) port.mem[a / 2][7:0] = v; else port.mem[a / 2][15:8] = v; gold[a] = v; end endtask
    task snapshot; integer j; begin for (j = 0; j < MEMB; j = j + 1) gold[j] = (j % 2 == 0) ? port.mem[j / 2][7:0] : port.mem[j / 2][15:8]; end endtask
    task put_tile(input integer tt); integer j; reg [63:0] wd; begin
        for (j = 0; j < 128; j = j + 1) begin wd = mem[tt * SLOT + 2 + j / 4]; setbyte((j < 64 ? TBL_Y : TBL_C) + 2 * (j % 64), wd[16 * (j % 4) +: 8]); setbyte((j < 64 ? TBL_Y : TBL_C) + 2 * (j % 64) + 1, wd[16 * (j % 4) + 8 +: 8]); end
        for (j = 0; j < len; j = j + 1) begin wd = mem[tt * SLOT + OFF_STREAM + j / 8]; setbyte(srcb + j, wd[8 * (j % 8) +: 8]); end
    end endtask
    task put_expected(input integer tt); integer y, x, c; reg [63:0] wd; begin
        for (y = 0; y < h; y = y + 1) for (x = 0; x < w; x = x + 1) for (c = 0; c < 3; c = c + 1) begin
            wd = mem[tt * SLOT + OFF_EXP + c * 2048 + y * 16 + x / 8]; gold[dstb + y * pitch + 3 * x + c] = wd[8 * (x % 8) +: 8];
        end
    end endtask
    task compare(input integer lbl); integer j; begin
        nbad = 0;
        for (j = 0; j < MEMB; j = j + 1) if (((j % 2 == 0) ? port.mem[j / 2][7:0] : port.mem[j / 2][15:8]) !== gold[j]) begin nbad = nbad + 1; if (nbad < 4) $display("test %0d: byte %0d (0x%0h) = %0h want %0h", lbl, j, j, (j % 2 == 0) ? port.mem[j / 2][7:0] : port.mem[j / 2][15:8], gold[j]); end
        if (nbad != 0) errors = errors + 1;
    end endtask
    task run_cmd_tile(input integer maxc); begin
        ev_d = 0; ev_e = 0; cfg_src <= srcb; cfg_len <= len; cfg_dst <= dstb; cfg_pitch <= pitch; cfg_fmt <= f; cfg_w <= w; cfg_h <= h; @(posedge clk);
        cmd_tile <= 1; @(posedge clk); cmd_tile <= 0; cyc = 0;
        while (ev_d == 0 && ev_e == 0 && cyc < maxc) begin @(posedge clk); cyc = cyc + 1; end
        repeat (6) @(posedge clk);
    end endtask
    task load_tables; begin
        ev_t = 0; cfg_qy <= TBL_Y; cfg_qc <= TBL_C; @(posedge clk); cmd_tables <= 1; @(posedge clk); cmd_tables <= 0; cyc = 0;
        while (ev_t == 0 && cyc < 20000) begin @(posedge clk); cyc = cyc + 1; end
        repeat (4) @(posedge clk);
        if (ev_t != 1 || busy) begin errors = errors + 1; $display("tables: ev %0d busy %0d cyc %0d", ev_t, busy, cyc); end
    end endtask
    initial begin
        if (!$value$plusargs("VEC=%s", vecfile)) begin $display("no +VEC"); $finish; end
        if (!$value$plusargs("N=%d", nt)) nt = 10;
        $readmemh(vecfile, mem);
        repeat (4) @(posedge clk); rst <= 0; repeat (2) @(posedge clk);
        for (t = 0; t < nt; t = t + 1) begin
            base = t * SLOT; f = mem[base][17:16]; w = mem[base][15:8]; h = mem[base][7:0]; len = mem[base + 1];
            if (f == 1 && C444 == 0) skipped = skipped + 1;
            else begin
                ran = ran + 1;
                src_off = (t % 2); dst_off = ((t / 2) % 2); pitch = (t % 3 == 0) ? 400 : (t % 3 == 1) ? 397 : 512;        // odd and even starts, odd and even pitch
                srcb = SRC_BASE + src_off; dstb = DST_BASE + dst_off; snapshot; put_tile(t); put_expected(t);
                load_tables; run_cmd_tile(3000000);
                if (cyc >= 3000000 || ev_d != 1 || ev_e != 0 || busy) begin errors = errors + 1; $display("test %0d fmt %0d %0dx%0d: cyc %0d done %0d err %0d (code %0d)", t, f, w, h, cyc, ev_d, ev_e, last_code); end
                compare(t);
                if (len > 5000) $display("tile %0d (%0d bytes, %0dx%0d, src odd %0d, dst odd %0d, pitch %0d): %0d clocks to the last row in memory", t, len, w, h, src_off, dst_off, pitch, tile_cycles);
            end
        end
        $display("%0d tiles run, %0d skipped (4:4:4 without C444)", ran, skipped);
        // scenarios, on tile 6 (the sparse 128 x 128 photo tile)
        base = 6 * SLOT; f = mem[base][17:16]; w = mem[base][15:8]; h = mem[base][7:0]; len = mem[base + 1]; pitch = 400; srcb = SRC_BASE + 1; dstb = DST_BASE + 1; snapshot; put_tile(6); put_expected(6);
        load_tables;
        run_cmd_tile(1);                                                              // start, then abort after a while
        repeat (20000) @(posedge clk); cmd_abort <= 1; @(posedge clk); cmd_abort <= 0; cyc = 0; while (ev_e == 0 && cyc < 100000) begin @(posedge clk); cyc = cyc + 1; end repeat (10) @(posedge clk);
        if (ev_e != 1 || last_code != 4'd15 || busy) begin errors = errors + 1; $display("abort: err events %0d code %0d busy %0d", ev_e, last_code, busy); end else $display("abort: ev_err code 15, idle");
        run_cmd_tile(3000000); compare(100);                                          // the tile after an abort (the picture memory holds a partial first try: rewrite expected over it)
        if (ev_d != 1 || ev_e != 0 || nbad != 0) begin errors = errors + 1; $display("tile after abort wrong: done %0d err %0d bad bytes %0d", ev_d, ev_e, nbad); end else $display("tile after abort: ok");
        // corrupt stream: 0xFF bytes
        snapshot; for (k = 0; k < 200; k = k + 1) setbyte(srcb + k, 8'hFF); len = 200; load_tables; run_cmd_tile(300000);
        if (ev_e != 1 || last_code != 4'd1 || busy) begin errors = errors + 1; $display("corrupt stream: err events %0d code %0d busy %0d done %0d", ev_e, last_code, busy, ev_d); end else $display("corrupt stream: ev_err code 1, idle");
        // starved port: the tile times out
        len = mem[base + 1]; snapshot; put_tile(6);
        ev_d = 0; ev_e = 0; cfg_src <= srcb; cfg_len <= len; cfg_dst <= dstb; cfg_pitch <= pitch; cfg_fmt <= f; cfg_w <= w; cfg_h <= h; @(posedge clk); cmd_tile <= 1; @(posedge clk); cmd_tile <= 0; cyc = 0;
        while (!(p_valid && p_we) && cyc < 3000000) begin @(posedge clk); cyc = cyc + 1; end                       // the first row write is requested: from now on the SDRAM refuses everything
        stall = 1; while (ev_e == 0 && cyc < 3000000) begin @(posedge clk); cyc = cyc + 1; end stall = 0; repeat (200) @(posedge clk);
        if (ev_e != 1 || last_code != 4'd2 || busy) begin errors = errors + 1; $display("timeout: err events %0d code %0d busy %0d", ev_e, last_code, busy); end else $display("timeout: ev_err code 2, idle");
        // a slow SDRAM: the writer falls behind, the ring fills, `hold` must stop the decoder's row starts (no row may be overwritten)
        len = mem[base + 1]; snapshot; put_tile(6); put_expected(6); load_tables; slow_en = 1; run_cmd_tile(8000000); slow_en = 0; repeat (50) @(posedge clk); compare(101);
        if (ev_d != 1 || ev_e != 0 || nbad != 0) begin errors = errors + 1; $display("slow SDRAM: done %0d err %0d bad bytes %0d", ev_d, ev_e, nbad); end else $display("slow SDRAM: ok (%0d clocks)", tile_cycles);
        if (errors == 0) $display("DMA PASSED (0 failures)"); else $display("DMA FAILED %0d", errors);
        $finish;
    end
endmodule
