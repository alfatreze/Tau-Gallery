// tau_tgy_unit.sv -- the TGY hardware decoder as the core will see it: register block (clk_sys MMIO) + DMA and decoder (clk_mem) with one scheduler client port (for tau_sdram_dmux, secondary client D).
// Not instantiated in the core yet (docs/TGY_HW_DMA.md "Integration"). BUG selects mutants of tau_tgy_regs (1..6) or of tau_tgy_dma (11..20 = DMA BUG 1..10, 21..24 = the decoder-clock half's BUG 1..4) for the testbench. Clocks: clk_sys (registers), clk_mem (SDRAM, the DMA), clk_dec (the decoder).
module tau_tgy_unit #(parameter integer C444 = 0, parameter integer BUG = 0, parameter [23:0] TMO = 24'hFFFFFF) (
    input  wire        clk_sys, rst_sys,
    input  wire [9:0]  reg_addr,
    input  wire        reg_we,
    input  wire [31:0] reg_wdata,
    output wire        reg_hit,
    output wire [31:0] reg_rdata,
    output wire        irq,
    input  wire        clk_mem, rst_mem,
    input  wire        clk_dec,           // the decoder clock (50 MHz), asynchronous to the other two
    output wire        p_valid,
    input  wire        p_ready,
    output wire        p_we,
    output wire [24:0] p_addr,
    output wire [9:0]  p_len,
    input  wire        p_next,
    input  wire [9:0]  p_idx,
    output wire [15:0] p_wdata,
    output wire [1:0]  p_be,
    input  wire        p_r_valid,
    input  wire [15:0] p_r_data,
    input  wire        p_r_last,
    input  wire        p_done
);
    wire r_hit, h_hit; wire [31:0] r_rdata, h_rdata;
    wire tap_valid; wire [6:0] tap_x, tap_y; wire [7:0] tap_r, tap_g, tap_b;
    assign reg_hit = r_hit | h_hit; assign reg_rdata = r_rdata | h_rdata;
    tau_hist_if #(.BUG(BUG >= 31 ? BUG - 30 : 0)) hist (.clk_sys(clk_sys), .rst_sys(rst_sys), .reg_addr(reg_addr), .reg_we(reg_we), .reg_wdata(reg_wdata), .reg_hit(h_hit), .reg_rdata(h_rdata),
        .clk_dec(clk_dec), .px_valid(tap_valid), .px_x(tap_x), .px_y(tap_y), .px_r(tap_r), .px_g(tap_g), .px_b(tap_b));
    wire [25:0] cfg_src, cfg_dst, cfg_qy, cfg_qc; wire [19:0] cfg_len; wire [15:0] cfg_pitch; wire [1:0] cfg_fmt; wire [7:0] cfg_w, cfg_h;
    wire cmd_tile, cmd_tables, cmd_abort, busy, ev_done, ev_err, ev_tables; wire [3:0] err_code; wire [31:0] tile_cycles;
    tau_tgy_regs #(.BUG(BUG < 10 ? BUG : 0)) regs (.clk_sys(clk_sys), .rst_sys(rst_sys), .reg_addr(reg_addr), .reg_we(reg_we), .reg_wdata(reg_wdata), .reg_hit(r_hit), .reg_rdata(r_rdata), .irq(irq),
        .clk_mem(clk_mem), .rst_mem(rst_mem), .cfg_src(cfg_src), .cfg_dst(cfg_dst), .cfg_qy(cfg_qy), .cfg_qc(cfg_qc), .cfg_len(cfg_len), .cfg_pitch(cfg_pitch), .cfg_fmt(cfg_fmt), .cfg_w(cfg_w), .cfg_h(cfg_h),
        .cmd_tile(cmd_tile), .cmd_tables(cmd_tables), .cmd_abort(cmd_abort), .ev_done(ev_done), .ev_err(ev_err), .ev_tables(ev_tables), .err_code(err_code), .tile_cycles(tile_cycles));
    reg [2:0] rsd = 3'b111; always @(posedge clk_dec) rsd <= {rsd[1:0], rst_mem};      // reset released synchronously in the decoder clock domain
    tau_tgy_dma #(.C444(C444), .BUG(BUG >= 11 ? BUG - 10 : 0), .TMO(TMO)) dma (.clk(clk_mem), .rst(rst_mem), .clk_dec(clk_dec), .rst_dec(rsd[2]), .cfg_src(cfg_src), .cfg_dst(cfg_dst), .cfg_qy(cfg_qy), .cfg_qc(cfg_qc), .cfg_len(cfg_len), .cfg_pitch(cfg_pitch),
        .cfg_fmt(cfg_fmt), .cfg_w(cfg_w), .cfg_h(cfg_h), .cmd_tile(cmd_tile), .cmd_tables(cmd_tables), .cmd_abort(cmd_abort), .busy(busy), .ev_done(ev_done), .ev_err(ev_err), .ev_tables(ev_tables), .err_code(err_code), .tile_cycles(tile_cycles),
        .p_valid(p_valid), .p_ready(p_ready), .p_we(p_we), .p_addr(p_addr), .p_len(p_len), .p_next(p_next), .p_idx(p_idx), .p_wdata(p_wdata), .p_be(p_be), .p_r_valid(p_r_valid), .p_r_data(p_r_data), .p_r_last(p_r_last), .p_done(p_done),
        .tap_valid(tap_valid), .tap_x(tap_x), .tap_y(tap_y), .tap_r(tap_r), .tap_g(tap_g), .tap_b(tap_b));
endmodule
