// tau_tgy_dcore.sv -- the decoder-clock half of the TGY decoder unit (docs/TGY_HW_CLOCK.md): tau_tgy_dec (entropy decoder, IDCT, pixel stage) on clk_dec (50 MHz), the pixel row ring it writes, and every crossing to
// the SDRAM-clock half (tau_tgy_dma, clk_mem):
//   in:  start_tog, rst_tog (toggles, 3 flop synchronisers, the edge is the command), the configuration buses (stable for the whole tile: the DMA latches them before it toggles), the quantisation table write port
//        (written on clk_mem into the tile controller's two clock RAM while the decoder is idle), the stream words (show-ahead FIFO read side), rows_wr (the DMA's count of rows written to SDRAM, Gray coded across);
//   out: fin_tog (the tile is done: delayed 8 clk_dec clocks so the last rows_arr has crossed first), err_tog, rows_arr (the count of rows in the ring, Gray coded across), the ring's read port on clk_mem and the
//        records of the rows (written on clk_dec BEFORE rows_arr counts them: data before pointer).
// The pixel rows go into a ring of 8 rows (4 byte-wide dual clock RAMs of 1,024 entries, written here, read by the DMA's writer; a pixel's three bytes land in three different arrays) and `hold` pauses the decoder's
// row starts while HOLD_T or more rows wait to be written (the pixel stage is 3 clocks deep and rows can be one pixel wide: 4 waiting + up to 3 in flight < 8). Nothing here is reset by the system reset except the
// state machines: the toggles keep their value, so a reset cannot fake a command.
// BUG (test only): 0 = correct; 1 hold never asserted, 2 green and blue swapped, 3 a pixel's third byte lost, 4 the first row's address is not the destination.
module tau_tgy_dcore #(parameter integer C444 = 0, parameter integer BUG = 0, parameter integer HOLD_T = 4) (
    input  wire        clk_dec, rst_dec, clk_mem,
    input  wire        start_tog, rst_tog,
    input  wire [1:0]  cfg_fmt,
    input  wire [7:0]  cfg_w, cfg_h,
    input  wire [19:0] cfg_len,
    input  wire [25:0] cfg_dst,
    input  wire [15:0] cfg_pitch,
    input  wire        q_we, q_sel,
    input  wire [5:0]  q_idx,
    input  wire [15:0] q_val,
    input  wire        wf_valid,
    input  wire [31:0] wf_data,
    output wire        wf_pop,
    output reg         fin_tog = 1'b0, err_tog = 1'b0,
    input  wire [4:0]  rows_wr,
    output wire [4:0]  rows_arr_m,
    input  wire [9:0]  r_addr,
    output reg  [7:0]  r_d0, r_d1, r_d2, r_d3,
    input  wire [2:0]  r_slot,
    output wire [24:0] rec_addr_o,
    output wire [8:0]  rec_nhw_o,
    output wire        rec_b0_o, rec_lodd_o
);
    // ---- commands from the SDRAM clock
    wire st_s, rt_s; reg st_q = 1'b0, rt_q = 1'b0;
    tau_cdc_sync1 #(.STAGES(3)) u_st (.clk_dst(clk_dec), .d_src(start_tog), .q_dst(st_s));
    tau_cdc_sync1 #(.STAGES(3)) u_rt (.clk_dst(clk_dec), .d_src(rst_tog),   .q_dst(rt_s));
    always @(posedge clk_dec) begin st_q <= st_s; rt_q <= rt_s; end
    wire start_p = st_s ^ st_q, rst_p = rt_s ^ rt_q;
    reg [2:0] rst_cnt = 3'd0;
    always @(posedge clk_dec) if (rst_p) rst_cnt <= 3'd5; else if (rst_cnt != 3'd0) rst_cnt <= rst_cnt - 3'd1;
    wire drst = rst_dec || (rst_cnt != 3'd0);

    // ---- the decoder
    reg [4:0] rows_arr = 5'd0; wire [4:0] rows_wr_d;
    tau_cdc_gray_bus #(.W(5), .STAGES(3)) u_rw (.clk_src(clk_mem), .d_src(rows_wr), .clk_dst(clk_dec), .q_dst(rows_wr_d));
    tau_cdc_gray_bus #(.W(5), .STAGES(3)) u_ra (.clk_src(clk_dec), .d_src(rows_arr), .clk_dst(clk_mem), .q_dst(rows_arr_m));
    wire [4:0] occ = rows_arr - rows_wr_d;
    wire hold = (BUG == 1) ? 1'b0 : (occ >= HOLD_T);
    wire dec_in_ready, dec_busy, dec_done, dec_err, px_valid, px_last; wire [6:0] px_x, px_y; wire [7:0] px_r, px_g, px_b;
    tau_tgy_dec #(.C444(C444)) dec (.clk(clk_dec), .rst(drst), .clk_q(clk_mem), .qwe(q_we), .qsel(q_sel), .qidx(q_idx), .qval(q_val), .start(start_p), .fmt(cfg_fmt), .tw(cfg_w), .th(cfg_h), .tile_len(cfg_len),
        .in_valid(wf_valid), .in_word(wf_data), .in_ready(dec_in_ready), .hold(hold), .busy(dec_busy), .done(dec_done), .err(dec_err),
        .px_valid(px_valid), .px_x(px_x), .px_y(px_y), .px_r(px_r), .px_g(px_g), .px_b(px_b), .px_last(px_last));
    assign wf_pop = wf_valid && dec_in_ready;

    // ---- events back
    reg [7:0] fd = 8'd0;
    always @(posedge clk_dec) begin fd <= {fd[6:0], dec_done}; if (fd[7]) fin_tog <= ~fin_tog; if (dec_err) err_tog <= ~err_tog; end

    // ---- pixel rows into the ring
    reg [24:0] rec_addr [0:7]; reg [8:0] rec_nhw [0:7]; reg rec_b0 [0:7]; reg rec_lodd [0:7];
    reg [25:0] arr_row_addr = 26'd0; reg row_first = 1'b0;
    wire [25:0] ra_now = (px_x == 7'd0) ? ((row_first && BUG != 4) ? cfg_dst : arr_row_addr + {10'd0, cfg_pitch}) : arr_row_addr;
    wire        b0_now = ra_now[0];
    wire [8:0]  o_now = {8'd0, b0_now} + {2'd0, px_x} * 9'd3;
    wire [2:0]  bank = rows_arr[2:0];
    (* ramstyle = "M10K, no_rw_check" *) reg [7:0] m0 [0:1023];
    (* ramstyle = "M10K, no_rw_check" *) reg [7:0] m1 [0:1023];
    (* ramstyle = "M10K, no_rw_check" *) reg [7:0] m2 [0:1023];
    (* ramstyle = "M10K, no_rw_check" *) reg [7:0] m3 [0:1023];
    reg [3:0] aw_we; reg [9:0] aw_a0, aw_a1, aw_a2, aw_a3; reg [7:0] aw_d0, aw_d1, aw_d2, aw_d3;
    integer i; reg [8:0] mm; reg [7:0] hk; reg [1:0] aid; reg [9:0] aaddr; reg [7:0] adat;
    always @* begin
        aw_we = 4'd0; aw_a0 = 10'd0; aw_a1 = 10'd0; aw_a2 = 10'd0; aw_a3 = 10'd0; aw_d0 = 8'd0; aw_d1 = 8'd0; aw_d2 = 8'd0; aw_d3 = 8'd0;
        for (i = 0; i < 3; i = i + 1) begin
            mm = o_now + i[8:0]; hk = mm[8:1]; aid = {hk[0], mm[0]}; aaddr = {bank, hk[7:1]};
            adat = (i == 0) ? px_r : (i == 1) ? ((BUG == 2) ? px_b : px_g) : ((BUG == 2) ? px_g : px_b);
            if (px_valid && !(BUG == 3 && i == 2)) begin
                case (aid)
                    2'd0: begin aw_we[0] = 1'b1; aw_a0 = aaddr; aw_d0 = adat; end
                    2'd1: begin aw_we[1] = 1'b1; aw_a1 = aaddr; aw_d1 = adat; end
                    2'd2: begin aw_we[2] = 1'b1; aw_a2 = aaddr; aw_d2 = adat; end
                    default: begin aw_we[3] = 1'b1; aw_a3 = aaddr; aw_d3 = adat; end
                endcase
            end
        end
    end
    always @(posedge clk_dec) begin
        if (aw_we[0]) m0[aw_a0] <= aw_d0; if (aw_we[1]) m1[aw_a1] <= aw_d1; if (aw_we[2]) m2[aw_a2] <= aw_d2; if (aw_we[3]) m3[aw_a3] <= aw_d3;
    end
    always @(posedge clk_mem) begin r_d0 <= m0[r_addr]; r_d1 <= m1[r_addr]; r_d2 <= m2[r_addr]; r_d3 <= m3[r_addr]; end
    assign rec_addr_o = rec_addr[r_slot]; assign rec_nhw_o = rec_nhw[r_slot]; assign rec_b0_o = rec_b0[r_slot]; assign rec_lodd_o = rec_lodd[r_slot];
    wire [8:0] row_bytes = {1'b0, cfg_w} * 9'd3;
    always @(posedge clk_dec) begin
        if (drst) begin rows_arr <= 5'd0; row_first <= 1'b0; end
        else begin
            if (start_p) row_first <= 1'b1;
            if (px_valid) begin
                arr_row_addr <= ra_now;
                if (px_x == cfg_w[6:0] - 7'd1 || (cfg_w == 8'd128 && px_x == 7'd127)) begin
                    rec_addr[bank] <= ra_now[25:1]; rec_b0[bank] <= b0_now;
                    rec_nhw[bank] <= ({8'd0, b0_now} + row_bytes + 9'd1) >> 1; rec_lodd[bank] <= ({8'd0, b0_now} + row_bytes) & 9'd1;
                    rows_arr <= rows_arr + 5'd1; row_first <= 1'b0;
                end
            end
        end
    end
endmodule
