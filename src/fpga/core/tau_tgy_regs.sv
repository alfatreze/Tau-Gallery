// tau_tgy_regs.sv -- the TGY decoder's register block (clk_sys, the CPU's MMIO) and the clock crossings to the DMA (clk_mem, the SDRAM clock) (docs/TGY_HW_DMA.md).
// Registers (byte offsets in the MMIO page, all 32-bit): the block answers `reg_hit` for 0x220 .. 0x24F so the SoC can OR its read data in.
//   0x220 CTRL   (write)  bit 0 START_TILE, bit 1 LOAD_TABLES, bit 2 ABORT, bit 3 CLEAR (done, err, rejected). A start while busy is ignored and sets `rejected`. Reads 0.
//   0x224 STATUS (read)   [0] busy, [1] done (sticky), [2] err (sticky), [3] tables loaded, [4] a start was rejected (sticky), [11:8] error code (1 decoder or watchdog, 2 tile timeout, 15 abort), [31] present.
//   0x228 SRC    byte address in SDRAM of the tile stream (any byte)       0x22C LEN   stream length in bytes (20 bit)
//   0x230 DST    byte address of the tile's top left pixel (RGB888, any)   0x234 PITCH bytes from a pixel row to the next (16 bit)
//   0x238 GEOM   [17:16] format (0 grey, 1 4:4:4, 2 4:2:0), [15:8] width, [7:0] height of the tile in luma samples (1..128)
//   0x23C QY     byte address of the Y quantisation table (64 u16, zigzag, even)    0x240 QC   the same for Cb and Cr
//   0x244 CYCLES (read)   SDRAM-clock cycles the last tile took (valid after done)
//   0x248 STX    (K7, rotation) signed byte step in memory from one pixel of a source row to the next, 18 bit        0x24C STY  signed byte step from one source row to the next, 18 bit
//   GEOM bit 18 SCATTER: write every pixel at DST + x * STX + y * STY (DST = where source pixel (0, 0) goes) instead of whole rows at DST + y * PITCH; any rotation or mirror is a choice of the two steps, the pixels are those of the
//   plain decode. With bit 18 clear PITCH rules and STX / STY are ignored.
// Rules: write the configuration registers, THEN write CTRL in a separate store (the configuration crosses to the SDRAM clock when the command does; the CPU's store spacing covers the crossing). `irq` is
// done | err (a level, cleared by CLEAR). Commands and events cross as toggles through 2-flop synchronisers, so a start and an immediate read of STATUS cannot see the previous tile's flags.
// BUG (test only): 0 = correct; 1..8 = one deliberate fault each (7: GEOM bit 18 not stored, 8: STX written into STY), killed by sim/tb_tau_tgy_unit.v.
module tau_tgy_regs #(parameter integer BUG = 0) (
    input  wire        clk_sys, rst_sys,
    input  wire [9:0]  reg_addr,
    input  wire        reg_we,
    input  wire [31:0] reg_wdata,
    output wire        reg_hit,
    output reg  [31:0] reg_rdata,
    output wire        irq,
    // to the DMA (clk_mem)
    input  wire        clk_mem, rst_mem,
    output wire [25:0] cfg_src, cfg_dst, cfg_qy, cfg_qc,
    output wire [19:0] cfg_len,
    output wire [15:0] cfg_pitch,
    output wire [1:0]  cfg_fmt,
    output wire        cfg_scat,
    output wire [17:0] cfg_stx, cfg_sty,
    output wire [7:0]  cfg_w, cfg_h,
    output reg         cmd_tile, cmd_tables, cmd_abort,
    input  wire        ev_done, ev_err, ev_tables,
    input  wire [3:0]  err_code,
    input  wire [31:0] tile_cycles
);
    assign reg_hit = (reg_addr >= 10'h220) && (reg_addr <= 10'h24C);
    reg [25:0] r_src = 26'd0, r_dst = 26'd0, r_qy = 26'd0, r_qc = 26'd0; reg [19:0] r_len = 20'd0; reg [15:0] r_pitch = 16'd0; reg [18:0] r_geom = 19'd0; reg [17:0] r_stx = 18'd3, r_sty = 18'd0;
    assign cfg_src = r_src; assign cfg_dst = r_dst; assign cfg_qy = r_qy; assign cfg_qc = r_qc; assign cfg_len = r_len; assign cfg_pitch = r_pitch;
    assign cfg_scat = r_geom[18]; assign cfg_stx = r_stx; assign cfg_sty = r_sty;
    assign cfg_fmt = r_geom[17:16]; assign cfg_w = r_geom[15:8]; assign cfg_h = r_geom[7:0];
    // ---- clk_sys: flags and command toggles
    reg tg_tile = 1'b0, tg_tables = 1'b0, tg_abort = 1'b0;
    reg f_busy = 1'b0, f_done = 1'b0, f_err = 1'b0, f_tbl = 1'b0, f_rej = 1'b0; reg [3:0] f_code = 4'd0;
    // events back from clk_mem (toggles, synchronised here)
    reg ev_tg_done = 1'b0, ev_tg_err = 1'b0, ev_tg_tbl = 1'b0;
    always @(posedge clk_mem) begin
        if (ev_done) ev_tg_done <= ~ev_tg_done; if (ev_err) ev_tg_err <= ~ev_tg_err; if (ev_tables) ev_tg_tbl <= ~ev_tg_tbl;
    end
    reg [2:0] s1 = 3'd0, s2 = 3'd0, s3 = 3'd0;
    always @(posedge clk_sys) begin s1 <= {ev_tg_tbl, ev_tg_err, ev_tg_done}; s2 <= s1; s3 <= s2; end
    wire e_done = s2[0] ^ s3[0], e_err = s2[1] ^ s3[1], e_tbl = s2[2] ^ s3[2];
    wire w_ctrl = reg_we && reg_addr == 10'h220;
    wire go_tile = w_ctrl && reg_wdata[0] && !f_busy, go_tables = w_ctrl && reg_wdata[1] && !f_busy && !reg_wdata[0];
    always @(posedge clk_sys) begin
        if (rst_sys) begin tg_tile <= 1'b0; tg_tables <= 1'b0; tg_abort <= 1'b0; f_busy <= 1'b0; f_done <= 1'b0; f_err <= 1'b0; f_tbl <= 1'b0; f_rej <= 1'b0; f_code <= 4'd0; end
        else begin
            if (reg_we) case (reg_addr)
                10'h228: r_src <= reg_wdata[25:0];
                10'h22C: r_len <= reg_wdata[19:0];
                10'h230: if (BUG != 1) r_dst <= reg_wdata[25:0];
                10'h234: r_pitch <= reg_wdata[15:0];
                10'h238: r_geom <= (BUG == 7) ? {1'b0, reg_wdata[17:0]} : reg_wdata[18:0];
                10'h248: if (BUG == 8) r_sty <= reg_wdata[17:0]; else r_stx <= reg_wdata[17:0];
                10'h24C: r_sty <= reg_wdata[17:0];
                10'h23C: r_qy <= reg_wdata[25:0];
                10'h240: r_qc <= reg_wdata[25:0];
                default: ;
            endcase
            if (go_tile) begin tg_tile <= ~tg_tile; f_busy <= (BUG != 2); if (BUG != 3) begin f_done <= 1'b0; f_err <= 1'b0; end end
            if (go_tables) begin tg_tables <= ~tg_tables; f_busy <= 1'b1; end
            if (w_ctrl && (reg_wdata[0] || reg_wdata[1]) && f_busy && BUG != 4) f_rej <= 1'b1;
            if (w_ctrl && reg_wdata[2]) tg_abort <= ~tg_abort;
            if (w_ctrl && reg_wdata[3] && BUG != 5) begin f_done <= 1'b0; f_err <= 1'b0; f_rej <= 1'b0; end
            if (e_done) begin f_busy <= 1'b0; f_done <= 1'b1; end
            if (e_err) begin f_busy <= 1'b0; f_err <= 1'b1; f_code <= err_code; end
            if (e_tbl) begin f_busy <= 1'b0; f_tbl <= 1'b1; end
        end
    end
    assign irq = (BUG == 6) ? f_done : (f_done | f_err);
    always @* begin
        case (reg_addr)
            10'h224: reg_rdata = {1'b1, 19'd0, f_code, 3'd0, f_rej, f_tbl, f_err, f_done, f_busy};
            10'h228: reg_rdata = {6'd0, r_src};
            10'h22C: reg_rdata = {12'd0, r_len};
            10'h230: reg_rdata = {6'd0, r_dst};
            10'h234: reg_rdata = {16'd0, r_pitch};
            10'h238: reg_rdata = {13'd0, r_geom};
            10'h248: reg_rdata = {14'd0, r_stx};
            10'h24C: reg_rdata = {14'd0, r_sty};
            10'h23C: reg_rdata = {6'd0, r_qy};
            10'h240: reg_rdata = {6'd0, r_qc};
            10'h244: reg_rdata = tile_cycles;
            default: reg_rdata = 32'd0;
        endcase
    end
    // ---- clk_mem: commands (toggles synchronised, edge to a pulse)
    reg [2:0] c1 = 3'd0, c2 = 3'd0, c3 = 3'd0;
    always @(posedge clk_mem) begin
        c1 <= {tg_abort, tg_tables, tg_tile}; c2 <= c1; c3 <= c2;
        cmd_tile <= c2[0] ^ c3[0]; cmd_tables <= c2[1] ^ c3[1]; cmd_abort <= c2[2] ^ c3[2];
    end
endmodule
