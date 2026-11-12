// tb_fullcore.v -- the WHOLE core (core_top: the real VexRiscv SoC, the real SDRAM PHY and scheduler against sim/sdram_chip_model.sv, the video scan-out, the landing path, the TGY decoder) driven by a model of the
// Pocket's host (docs/FULLCORE_SIM.md). The host model speaks the APF bridge protocol of core_bridge_cmd.v: it loads the firmware image into data slot 1 (command 0x0082, bridge writes, 0x008F), answers the core's
// "ready to run" (0x0140), leaves reset (0x0011), and then serves the core's target commands from files in a directory: 0x0190 (get file name), 0x0192 (open file: the path is mapped to a file of the directory by
// its last component), 0x0180 (data slot read: the words are written to the bridge address the core gave, big-endian), 0x0188 (flush). The video output is captured to PPM files (one per frame on request).
//   +ROM=firmware.rom  +DIR=data directory (tau-library.tdb, the pictures)  +OUT=prefix of the frames  +SCRIPT=n (which scenario)  +MAXUS=microseconds of simulated time
// Not Intel's code: the Altera primitives come from the Quartus install's altera_mf.v (never committed) and sim/fullcore/altera_pll_model.v.
`timescale 1ps/1ps
module tb_fullcore;
    reg clk74a = 0, clk74b = 0;
    always #6734 clk74a = ~clk74a;
    initial begin #3100; forever #6734 clk74b = ~clk74b; end
    reg [31:0] bridge_addr = 0, bridge_wr_data = 0; reg bridge_rd = 0, bridge_wr = 0; wire [31:0] bridge_rd_data; wire bridge_endian_little;
    reg [31:0] cont1_key = 0;
    wire [12:0] dram_a; wire [1:0] dram_ba, dram_dqm; wire [15:0] dram_dq; wire dram_clk, dram_cke, dram_ras_n, dram_cas_n, dram_we_n;
    wire [23:0] video_rgb; wire video_rgb_clock, video_rgb_clock_90, video_de, video_skip, video_vs, video_hs;
    tri [7:0] cb2, cb3, cb1; tri [7:4] cb0; tri cp30, cp31, ptsi, ptso, ptsck, ptsd, asda; tri [15:0] cr0dq, cr1dq, srdq;
    core_top dut (
        .clk_74a(clk74a), .clk_74b(clk74b),
        .cart_tran_bank2(cb2), .cart_tran_bank3(cb3), .cart_tran_bank1(cb1), .cart_tran_bank0(cb0), .cart_tran_pin30(cp30), .cart_tran_pin31(cp31),
        .port_ir_rx(1'b0), .port_tran_si(ptsi), .port_tran_so(ptso), .port_tran_sck(ptsck), .port_tran_sd(ptsd),
        .cram0_dq(cr0dq), .cram0_wait(1'b0), .cram1_dq(cr1dq), .cram1_wait(1'b0),
        .dram_a(dram_a), .dram_ba(dram_ba), .dram_dq(dram_dq), .dram_dqm(dram_dqm), .dram_clk(dram_clk), .dram_cke(dram_cke), .dram_ras_n(dram_ras_n), .dram_cas_n(dram_cas_n), .dram_we_n(dram_we_n),
        .sram_dq(srdq), .vblank(1'b0), .dbg_rx(1'b0), .user2(1'b0), .aux_sda(asda),
        .video_rgb(video_rgb), .video_rgb_clock(video_rgb_clock), .video_rgb_clock_90(video_rgb_clock_90), .video_de(video_de), .video_skip(video_skip), .video_vs(video_vs), .video_hs(video_hs),
        .audio_adc(1'b0), .bridge_endian_little(bridge_endian_little), .bridge_addr(bridge_addr), .bridge_rd(bridge_rd), .bridge_rd_data(bridge_rd_data), .bridge_wr(bridge_wr), .bridge_wr_data(bridge_wr_data),
        .cont1_key(cont1_key), .cont2_key(32'd0), .cont3_key(32'd0), .cont4_key(32'd0), .cont1_joy(32'd0), .cont2_joy(32'd0), .cont3_joy(32'd0), .cont4_joy(32'd0),
        .cont1_trig(16'd0), .cont2_trig(16'd0), .cont3_trig(16'd0), .cont4_trig(16'd0));
    // ---- the SDRAM chip (timing checked) ----
    wire [15:0] fpga_oe = dut.u_sdram_sched_a.u_phy.dq_oe;
    sdram_chip_model #(.MEM_LOG2(24), .VERBOSE(1), .INIT_WAIT_PS(100000000), .INIT_NOP_MIN(100)) chip (.dram_clk(dram_clk), .dram_cke(dram_cke), .dram_ras_n(dram_ras_n), .dram_cas_n(dram_cas_n), .dram_we_n(dram_we_n),
        .dram_dqm(dram_dqm), .dram_ba(dram_ba), .dram_a(dram_a), .dram_dq(dram_dq), .fpga_oe(fpga_oe), .cap_clk(dut.clk_sdram));
    // ---- the host's bridge ----
    task bw(input [31:0] a, input [31:0] d); begin @(posedge clk74a); bridge_addr <= a; bridge_wr_data <= d; bridge_wr <= 1; @(posedge clk74a); bridge_wr <= 0; end endtask
    reg [31:0] rdv;
    task br(input [31:0] a); begin @(posedge clk74a); bridge_addr <= a; bridge_rd <= 1; @(posedge clk74a); bridge_rd <= 0; repeat (4) @(posedge clk74a); rdv = bridge_rd_data; end endtask
    reg [15:0] hres; parameter [63:0] WDPS = 64'd60000000000;
    task hcmd(input [15:0] cmd, input [31:0] p20, input [31:0] p24, input [31:0] p28); integer n; begin
        bw(32'hF8000020, p20); bw(32'hF8000024, p24); bw(32'hF8000028, p28); bw(32'hF8000000, {16'h434D, cmd});
        n = 0; br(32'hF8000000); while (rdv[31:16] != 16'h4F4B && n < 100000) begin br(32'hF8000000); n = n + 1; end
        hres = rdv[15:0]; if (n >= 100000) $display("host: command %h timed out (reads %h)", cmd, rdv);
    end endtask
    // ---- files ----
    reg [8*300-1:0] romfile, dirpath, outpfx; integer maxus = 20000, scenario = 0;
    reg [7:0] rom [0:262143]; integer rom_n, fd;
    reg [7:0] fbuf [0:16777215]; integer fsize;                       // the file the host reads a slot from
    reg [8*300-1:0] slotfile [0:15]; integer slotsize [0:15];
    task load_file(input [8*300-1:0] path); integer f, n; begin
        f = $fopen(path, "rb"); fsize = 0;
        if (f != 0) begin n = $fread(fbuf, f); fsize = n; $fclose(f); end
    end endtask
    // ---- the target command server ----
    reg [7:0] pb [0:255];
    integer i, k, len_off, srv_on = 0; reg [31:0] t0, p20, p24, p28, p2c, w; reg [7:0] ch; integer fidx, nchar, found; reg [8*64-1:0] want;
    integer target_cmds = 0, reads_done = 0, opens_done = 0; reg [8*300-1:0] cur_file;
    task datatable_set(input integer slot_index, input [31:0] id, input [31:0] size); begin bw(32'hF8002000 + 8 * slot_index, id); bw(32'hF8002004 + 8 * slot_index, size); end endtask
    task serve_one; begin
        br(32'hF8001000); t0 = rdv;
        if (t0[31:16] == 16'h636D) begin
            target_cmds = target_cmds + 1;
            if (target_cmds <= 60) begin $display("host: target command %h at %0t us", t0[15:0], $time / 1000000); $fflush(); end
            br(32'hF8001020); p20 = rdv; br(32'hF8001024); p24 = rdv; br(32'hF8001028); p28 = rdv; br(32'hF800102C); p2c = rdv;
            case (t0[15:0])
                16'h0140: begin bw(32'hF8001000, 32'h6F6B0000); $display("host: ready to run acknowledged at %0t", $time); $fflush(); end
                16'h0188: bw(32'hF8001000, 32'h6F6B0000);
                16'h0190: begin                                                        // get the file name of a slot: a response struct at the pointer: [length][path, NUL padded]
                    put_str(p24, "/Assets/tau_gallery/common/photos/template.tgy");
                    bw(32'hF8001000, 32'h6F6B0000);
                end
                16'h0192: begin                                                        // open a file into a slot: read the path from the struct at the pointer (word 0 = length, then the path)
                    nchar = 0;
                    for (i = 1; i < 64; i = i + 1) begin br(32'hF8002000 + p24 + 4 * i); w = rdv; for (k = 0; k < 4; k = k + 1) begin ch = w[31 - 8 * k -: 8]; if (ch >= 8'h20 && ch < 8'h7F && nchar < 255) begin pb[nchar] = ch; nchar = nchar + 1; end end end
                    srv_open(p20[15:0]);
                end
                16'h0180: begin srv_read(p20[15:0], p24, p28, p2c); end
                default: begin $display("host: unhandled target command %h", t0[15:0]); bw(32'hF8001000, 32'h6F6B0001); end
            endcase
        end
    end endtask
    task put_str(input [31:0] off, input [8*64-1:0] str); integer n, q; reg [31:0] wd; reg [7:0] c; begin
        n = 0; for (q = 63; q >= 0; q = q - 1) if (str[8*q +: 8] != 0 && n == 0) n = q + 1;            // the string is right aligned in the vector: its length
        bw(32'hF8002000 + off, n);
        for (q = 0; q < 63; q = q + 1) begin
            wd = 0; for (k = 0; k < 4; k = k + 1) begin c = ((4 * q + k) < n) ? str[8 * (n - 1 - (4 * q + k)) +: 8] : 8'd0; wd = {wd[23:0], c}; end
            bw(32'hF8002000 + off + 4 + 4 * q, wd);
        end
    end endtask
    task reg_fixed(input [15:0] slot, input [8*20-1:0] nm, input integer tindex); begin       // tindex = the slot's position in data.json (the data table is indexed by position)
        $sformat(cur_file, "%0s/%0s", dirpath, nm); load_file(cur_file);
        if (fsize > 0) begin slotfile[slot] = cur_file; slotsize[slot] = fsize; datatable_set(tindex, slot, fsize); $display("host: slot %0d = %0s (%0d bytes)", slot, nm, fsize); end
        else $display("host: slot %0d (%0s) has no file in the data directory", slot, nm);
    end endtask
    task srv_open(input [15:0] slot); integer j, last, b; reg [8*300-1:0] nm; begin
        last = -1; for (j = 0; j < nchar; j = j + 1) if (pb[j] == "/") last = j;
        nm = 0; b = 0; for (j = last + 1; j < nchar; j = j + 1) begin nm = {nm[8*299-1:0], pb[j]}; b = b + 1; end
        $sformat(cur_file, "%0s/%0s", dirpath, nm);
        load_file(cur_file); opens_done = opens_done + 1;
        if (fsize > 0) begin slotfile[slot] = cur_file; slotsize[slot] = fsize; datatable_set(slot, slot, fsize); bw(32'hF8001000, 32'h6F6B0000); end
        else begin $display("host: open of %0s failed", cur_file); bw(32'hF8001000, 32'h6F6B0003); end
    end endtask
    task srv_read(input [15:0] slot, input [31:0] off, input [31:0] baddr, input [31:0] len); integer n, nw, m; reg [31:0] d; begin
        bw(32'hF8001000, 32'h62750000);                                                  // busy
        if (slotfile[slot] == 0 || off + len > slotsize[slot]) begin $display("host: read of slot %0d outside the file (off %0d len %0d size %0d)", slot, off, len, slotsize[slot]); bw(32'hF8001000, 32'h6F6B0002); end
        else begin
            load_file(slotfile[slot]); nw = len / 4;
            for (n = 0; n < nw; n = n + 1) begin d = {fbuf[off + 4*n], fbuf[off + 4*n + 1], fbuf[off + 4*n + 2], fbuf[off + 4*n + 3]}; bw(baddr + 4 * n, d); repeat (3) @(posedge clk74a); end
            bw(32'hF8001000, 32'h6F6B0000); reads_done = reads_done + 1;
        end
    end endtask
    always begin
        wait (srv_on); serve_one; repeat (8) @(posedge clk74a);
    end
    // ---- video capture ----
    integer ci; integer frames = 0, fpx = 0, fy = 0, fw = 0, fh = 0, capture_n = -1, cfile; reg de_q = 0, vs_q = 0, hs_q = 0; reg [23:0] line [0:2047]; reg [23:0] frame [0:1048575];
    always @(posedge video_rgb_clock) begin
        de_q <= video_de; vs_q <= video_vs;
        if (video_de) begin line[fpx] = video_rgb; fpx = fpx + 1; end
        if (de_q && !video_de) begin                                             // end of an active line
            for (ci = 0; ci < fpx && fy * 1024 + ci < 1048576; ci = ci + 1) frame[fy * 1024 + ci] = line[ci];   // its own index: the boot loop's `i` is shared by name
            fw = fpx; fy = fy + 1; fpx = 0;
        end
        if (!vs_q && video_vs) begin                                             // vsync rising: the previous frame is complete
            fh = fy; fy = 0; frames = frames + 1;
            if (capture_n == frames - 1 && fh > 0) begin save_frame; capture_n = -1; end
        end
    end
    task save_frame; integer x, y; reg [8*340-1:0] nm; begin
        $sformat(nm, "%0s_%0d.ppm", outpfx, frames); cfile = $fopen(nm, "wb");
        $fwrite(cfile, "P6\n%0d %0d\n255\n", fw, fh);
        for (y = 0; y < fh; y = y + 1) for (x = 0; x < fw; x = x + 1) $fwrite(cfile, "%c%c%c", frame[y * 1024 + x][23:16], frame[y * 1024 + x][15:8], frame[y * 1024 + x][7:0]);
        $fclose(cfile); $display("video: frame %0d (%0d x %0d) saved as %0s", frames, fw, fh, nm);
    end endtask
    task grab; begin capture_n = frames; wait (capture_n == -1); end endtask
    // ---- where is the CPU: with +TRACE, the CPU's fetch and exception state every 100 us once the core runs (python3 sim/fullcore/run.py --extra +TRACE) ----
    reg trace_on = 0; integer npc = 0, cpu_edges = 0; initial trace_on = $test$plusargs("TRACE");
    initial forever begin #(64'd100000000); if (srv_on && npc < 200 && trace_on) begin npc = npc + 1; $display("cpu: rst=%b clk edges=%0d flushPending=%b flushCtr=%h booted=%b pcReg=%h iCYC=%b iADR=%h dCYC=%b | haltIt=%b lineLoader_valid=%b fetchPc_valid=%b stage0_valid=%b flush=%b fetchValid=%b | irq_ext=%h wdog_irq=%b exc=%b intJump=%b hadExc=%b MIE=%b mtvec=%h mcause=%h mepc=%h mtval=%h (t = %0d us)", dut.u_soc.rst, cpu_edges, dut.u_soc.cpu.IBusCachedPlugin_cache.lineLoader_flushPending, dut.u_soc.cpu.IBusCachedPlugin_cache.lineLoader_flushCounter, dut.u_soc.cpu.IBusCachedPlugin_fetchPc_booted, dut.u_soc.cpu.IBusCachedPlugin_fetchPc_pcReg, dut.u_soc.iCYC, {dut.u_soc.iADR, 2'b00}, dut.u_soc.dCYC, dut.u_soc.cpu.IBusCachedPlugin_cache_io_cpu_prefetch_haltIt, dut.u_soc.cpu.IBusCachedPlugin_cache.lineLoader_valid, dut.u_soc.cpu.IBusCachedPlugin_fetchPc_output_valid, dut.u_soc.cpu.IBusCachedPlugin_iBusRsp_stages_0_output_valid, dut.u_soc.cpu.IBusCachedPlugin_iBusRsp_flush, dut.u_soc.cpu.IBusCachedPlugin_cache.io_cpu_fetch_isValid, dut.u_soc.irq_ext, dut.u_soc.wdog_irq, dut.u_soc.cpu.CsrPlugin_exception, dut.u_soc.cpu.CsrPlugin_interruptJump, dut.u_soc.cpu.CsrPlugin_hadException, dut.u_soc.cpu.CsrPlugin_mstatus_MIE, {dut.u_soc.cpu.CsrPlugin_mtvec_base, 2'b00}, dut.u_soc.cpu.CsrPlugin_mcause_exceptionCode, dut.u_soc.cpu.CsrPlugin_mepc, dut.u_soc.cpu.CsrPlugin_mtval, $time / 1000000); end end
    always @(posedge dut.u_soc.clk) cpu_edges = cpu_edges + 1;
    // ---- issue 026: the reset history of the CPU and, from its exit, the first 400 clocks of its fetch (+RSTTRACE)
    reg rsttrace = 0; initial rsttrace = $test$plusargs("RSTTRACE"); integer rt_n = 0; reg rst_q = 1'bx;
    always @(dut.u_soc.rst) if (rsttrace) begin $display("rst: u_soc.rst = %b at %0t ps", dut.u_soc.rst, $time); end
    always @(posedge dut.u_soc.clk) begin
        rst_q <= dut.u_soc.rst;
        if (rsttrace && rst_q === 1'b1 && dut.u_soc.rst === 1'b0) rt_n = 400;
        if (rsttrace && rt_n > 0) begin rt_n = rt_n - 1;
            if (rt_n > 395 || dut.u_soc.cpu.IBusCachedPlugin_cache.lineLoader_flushCounter >= 8'h7C || dut.u_soc.cpu.IBusCachedPlugin_cache.lineLoader_flushCounter < 8'h02) $display("fetch: pcReg=%h fvalid=%b isValid=%b data=%h phys=%h memcmd=%b iCYC=%b iACK=%b haltIt=%b flushPend=%b flushCtr=%h loader_valid=%b decInstr=%h exc=%b", dut.u_soc.cpu.IBusCachedPlugin_fetchPc_pcReg, dut.u_soc.cpu.IBusCachedPlugin_fetchPc_output_valid,
                dut.u_soc.cpu.IBusCachedPlugin_cache_io_cpu_fetch_isValid, dut.u_soc.cpu.IBusCachedPlugin_cache_io_cpu_fetch_data, dut.u_soc.cpu.IBusCachedPlugin_cache_io_cpu_fetch_physicalAddress, dut.u_soc.cpu.IBusCachedPlugin_cache_io_mem_cmd_valid,
                dut.u_soc.iCYC, dut.u_soc.iACK, dut.u_soc.cpu.IBusCachedPlugin_cache_io_cpu_prefetch_haltIt, dut.u_soc.cpu.IBusCachedPlugin_cache.lineLoader_flushPending, dut.u_soc.cpu.IBusCachedPlugin_cache.lineLoader_flushCounter, dut.u_soc.cpu.IBusCachedPlugin_cache.lineLoader_valid,
                dut.u_soc.cpu.decode_INSTRUCTION, dut.u_soc.cpu.CsrPlugin_exception);
            $display("   tags: wrEn=%b wrAddr=%h wrData=%h rdEn=%b rdAddr=%h tag[0]=%h tag[1]=%h tag[127]=%h tagRdPort=%h fetchHit=%b decHit=%b | wtv=%b fire=%b rspv=%b fctr=%h _zz_2_now=%b", dut.u_soc.cpu.IBusCachedPlugin_cache._zz_2, dut.u_soc.cpu.IBusCachedPlugin_cache.lineLoader_write_tag_0_payload_address, dut.u_soc.cpu.IBusCachedPlugin_cache._zz_ways_0_tags_port, dut.u_soc.cpu.IBusCachedPlugin_cache._zz_fetchStage_read_waysValues_0_tag_valid_1, dut.u_soc.cpu.IBusCachedPlugin_cache._zz_fetchStage_read_waysValues_0_tag_valid, dut.u_soc.cpu.IBusCachedPlugin_cache.ways_0_tags[0], dut.u_soc.cpu.IBusCachedPlugin_cache.ways_0_tags[1], dut.u_soc.cpu.IBusCachedPlugin_cache.ways_0_tags[127], dut.u_soc.cpu.IBusCachedPlugin_cache._zz_ways_0_tags_port1, dut.u_soc.cpu.IBusCachedPlugin_cache.fetchStage_hit_valid, dut.u_soc.cpu.IBusCachedPlugin_cache.decodeStage_hit_valid, dut.u_soc.cpu.IBusCachedPlugin_cache.lineLoader_write_tag_0_valid, dut.u_soc.cpu.IBusCachedPlugin_cache.lineLoader_fire, dut.u_soc.cpu.IBusCachedPlugin_cache.io_mem_rsp_valid, dut.u_soc.cpu.IBusCachedPlugin_cache.lineLoader_flushCounter, dut.u_soc.cpu.IBusCachedPlugin_cache._zz_2);
        end
    end
    // ---- the CPU self-check (issue 026): after the host's reset exit the CPU must run the firmware: no exception taken, and the PC must leave the reset area
    integer exc_n = 0, pc_max = 0; reg run_on = 0;
    always @(posedge dut.u_soc.clk) if (srv_on) begin
        if (dut.u_soc.cpu.CsrPlugin_exception === 1'b1) exc_n = exc_n + 1;
        if (dut.u_soc.cpu.IBusCachedPlugin_fetchPc_pcReg > pc_max && dut.u_soc.cpu.IBusCachedPlugin_fetchPc_pcReg < 32'h00100000) pc_max = dut.u_soc.cpu.IBusCachedPlugin_fetchPc_pcReg;
    end
    // ---- the boot and the scenario ----
    task wait_us(input integer us); reg [63:0] t; begin t = us; t = t * 64'd1000000; #(t); end endtask   // 64 bit: 36 ms in ps overflows an integer
    initial begin #(WDPS); $display("host: WATCHDOG at 60 ms of simulated time, stuck before the scenario (status %0d, target %h)", hres, rdv); $fflush(); $finish; end
    initial begin
        if (!$value$plusargs("ROM=%s", romfile)) begin $display("no +ROM"); $finish; end
        if (!$value$plusargs("DIR=%s", dirpath)) dirpath = ".";
        if (!$value$plusargs("OUT=%s", outpfx)) outpfx = "build/fullcore/frame";
        if (!$value$plusargs("MAXUS=%d", maxus)) maxus = 20000;
        if (!$value$plusargs("SCRIPT=%d", scenario)) scenario = 0;
        fd = $fopen(romfile, "rb"); rom_n = $fread(rom, fd); $fclose(fd);
        $display("host: firmware %0d bytes, data directory %0s", rom_n, dirpath);
        for (i = 0; i < 16; i = i + 1) begin slotfile[i] = 0; slotsize[i] = 0; end
        wait_us(5);                                                              // the PLLs lock
        hcmd(16'h0000, 0, 0, 0); while (hres < 2) begin wait_us(1); hcmd(16'h0000, 0, 0, 0); end
        $display("host: core status %0d at %0t", hres, $time); $fflush();
        hcmd(16'h0090, 32'd1700000000, 32'h20261111, 32'h00200000);              // the clock
        datatable_set(0, 32'd1, rom_n);
        hcmd(16'h0082, 32'd1, rom_n, 0);                                         // request write of slot 1 (the firmware)
        $display("host: slot 1 write request answered %0d at %0t", hres, $time); $fflush();
        // about the Pocket's pace: 40 clocks a word (the loader's FIFO is 4 words deep)
        for (i = 0; i < (rom_n + 3) / 4; i = i + 1) begin bw(4 * i, {rom[4*i], rom[4*i+1], rom[4*i+2], rom[4*i+3]}); repeat (40) @(posedge clk74a); end
        $display("host: firmware written at %0t", $time); $fflush();
        $display("host: RAM words 0..3 = %h %h %h %h (firmware image starts %h)", {dut.u_soc.u_main_ram.a3[0], dut.u_soc.u_main_ram.a2[0], dut.u_soc.u_main_ram.a1[0], dut.u_soc.u_main_ram.a0[0]}, {dut.u_soc.u_main_ram.a3[1], dut.u_soc.u_main_ram.a2[1], dut.u_soc.u_main_ram.a1[1], dut.u_soc.u_main_ram.a0[1]}, {dut.u_soc.u_main_ram.a3[2], dut.u_soc.u_main_ram.a2[2], dut.u_soc.u_main_ram.a1[2], dut.u_soc.u_main_ram.a0[2]}, {dut.u_soc.u_main_ram.a3[3], dut.u_soc.u_main_ram.a2[3], dut.u_soc.u_main_ram.a1[3], dut.u_soc.u_main_ram.a0[3]}, {rom[3], rom[2], rom[1], rom[0]}); $fflush();
        hcmd(16'h008F, 0, 0, 0);                                                 // all slots loaded
        $display("host: all slots complete answered %0d at %0t", hres, $time); $fflush();
        // wait for the core's ready-to-run, then leave reset
        i = 0; br(32'hF8001000); while (rdv[31:16] != 16'h636D && i < 5000) begin wait_us(1); br(32'hF8001000); i = i + 1; end
        $display("host: target says %h at %0t", rdv, $time); $fflush();
        // the slots with a fixed file name in data.json (dist/Cores/.../data.json: 4 tau-loading.bin, 5 tau-library.tdb, 6 tau-cold.bin, 8 tau-assets.bin) are known to the host from the start: it opens them itself
        reg_fixed(4, "tau-loading.bin", 2); reg_fixed(5, "tau-library.tdb", 3); reg_fixed(6, "tau-cold.bin", 4); reg_fixed(8, "tau-assets.bin", 5);
        bw(32'hF8001000, 32'h6F6B0000); hcmd(16'h0011, 0, 0, 0);
        $display("host: reset released at %0t", $time); $fflush();
        srv_on = 1;
        if (scenario == 0) begin wait_us(maxus); end
        else if (scenario == 2) begin wait_us(maxus); grab; end                       // one frame after maxus
        else if (scenario == 1) begin repeat (12) begin wait_us(maxus / 12); $fflush(); grab; $fflush(); end end
        $display("host: done at %0t: %0d target commands, %0d reads, %0d opens, %0d frames", $time, target_cmds, reads_done, opens_done, frames);
        $display("cpu check: %0d exceptions after reset exit, highest fetch address %h", exc_n, pc_max);
        if (exc_n == 0 && pc_max > 32'h100) $display("FULLCORE CPU PASSED"); else $display("FULLCORE CPU FAILED");
        $finish;
    end
endmodule
