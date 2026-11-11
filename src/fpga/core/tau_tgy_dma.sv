// tau_tgy_dma.sv -- the TGY decoder's SDRAM DMA (docs/TGY_HW_DMA.md): one scheduler client port (the contract of tau_sdram_sched_a, through tau_sdram_dmux) that
//   (1) loads the two quantisation tables (64 u16 each, zigzag, even byte address) into tau_tgy_dec,
//   (2) fetches a tile STREAM from any byte address in SDRAM (halfword reads of up to 32, an odd start byte is dropped) and feeds the decoder as 32-bit words,
//   (3) writes the decoded RGB888 pixels into a picture plane with a byte pitch, at any byte address (one request per pixel row, the first and last halfword partial when the row starts or ends on an odd byte,
//       through the byte enables), with the neighbouring bytes untouched.
// One clock (the SDRAM clock): the decoder runs here too, so no data crosses a clock domain; the commands, the configuration and the events cross in tau_tgy_regs. A tile: cmd_tile with cfg_* stable
// (the tables loaded before by cmd_tables). Rows are collected into an 8-row ring of halfword arrays (4 byte-wide RAMs, 1,024 entries each: bank = arrival row, a pixel's three bytes land in three different
// arrays), `hold` pauses the decoder's row starts while 4 or more rows wait to be written (the pixel stage's pipeline is 3 clocks deep, rows can be 1 pixel wide: 4 waiting + up to 3 in flight < 8).
// Events (1-clock pulses): ev_done after the last row is in SDRAM, ev_err with err_code (1 decoder or watchdog error, 2 tile timeout, 15 abort), ev_tables when both tables are loaded.
// BUG (test only): 0 = correct; 1..12 = one deliberate fault each, killed by sim/tb_tau_tgy_dma.v.
module tau_tgy_dma #(parameter integer C444 = 0, parameter integer BUG = 0, parameter [23:0] TMO = 24'hFFFFFF) (
    input  wire        clk, rst,
    input  wire [25:0] cfg_src, cfg_dst, cfg_qy, cfg_qc,
    input  wire [19:0] cfg_len,
    input  wire [15:0] cfg_pitch,
    input  wire [1:0]  cfg_fmt,
    input  wire [7:0]  cfg_w, cfg_h,
    input  wire        cmd_tile, cmd_tables, cmd_abort,
    output wire        busy,
    output reg         ev_done, ev_err, ev_tables,
    output reg  [3:0]  err_code,
    output reg  [31:0] tile_cycles,
    // scheduler client port
    output reg         p_valid,
    input  wire        p_ready,
    output reg         p_we,
    output reg  [24:0] p_addr,
    output reg  [9:0]  p_len,
    input  wire        p_next,
    input  wire [9:0]  p_idx,
    output reg  [15:0] p_wdata,
    output reg  [1:0]  p_be,
    input  wire        p_r_valid,
    input  wire [15:0] p_r_data,
    input  wire        p_r_last,
    input  wire        p_done
);
    // ---- top state
    localparam [1:0] M_IDLE = 0, M_TABLES = 1, M_TILE = 2, M_DRAIN = 3;
    reg [1:0] mode; assign busy = (mode != M_IDLE);
    reg [25:0] src, dst, qy, qc; reg [19:0] len; reg [15:0] pitch; reg [1:0] fmt; reg [7:0] w, h;
    reg [23:0] tcyc; reg dec_fin; reg [3:0] pend_err; reg err_pend; reg [2:0] dec_rst_cnt;
    // ---- port state
    localparam [1:0] P_IDLE = 0, P_REQ = 1, P_WAIT = 2;
    localparam [1:0] K_TBL = 0, K_STR = 1, K_WR = 2;
    reg [1:0] pst, pk;
    // ---- tables
    reg tbl_stage;                      // 0 = Y requested/to request, 1 = C
    reg tbl_req_done;                   // the request of the current stage was issued
    reg [5:0] tbl_cnt; reg tbl_qsel;
    // ---- stream fetch
    reg [24:0] s_hw; reg [20:0] hw_left; reg skip;
    reg [15:0] sf [0:127]; reg [6:0] sw_p, sr_p; reg [7:0] scount; reg [15:0] sfq; reg hv;
    reg [63:0] acc; reg [3:0] nb;
    wire str_inflight = (pst != P_IDLE) && (pk == K_STR);
    wire fetch_done = (hw_left == 21'd0) && !str_inflight;
    wire dec_in_ready; wire deliver;
    wire tail_ok = fetch_done && (scount == 8'd0) && !hv && (nb != 4'd0);
    wire in_valid_w = (nb >= 4'd4) || tail_ok;
    assign deliver = in_valid_w && dec_in_ready;
    // ---- decoder
    reg dec_start; wire dec_busy, dec_done, dec_err; wire dec_px_valid, dec_px_last; wire [6:0] dec_px_x, dec_px_y; wire [7:0] dec_px_r, dec_px_g, dec_px_b;
    reg  dec_qwe; reg [5:0] dec_qidx; reg dec_qsel; reg [15:0] dec_qval;
    reg [4:0] rows_arr, rows_wr; wire [4:0] occ = rows_arr - rows_wr;
    wire hold = (BUG == 1) ? 1'b0 : (occ >= 5'd4);
    tau_tgy_dec #(.C444(C444)) dec (.clk(clk), .rst(rst || dec_rst_cnt != 3'd0), .qwe(dec_qwe), .qsel(dec_qsel), .qidx(dec_qidx), .qval(dec_qval), .start(dec_start), .fmt(fmt), .tw(w), .th(h), .tile_len(len),
        .in_valid(in_valid_w), .in_word(acc[31:0]), .in_ready(dec_in_ready), .hold(hold), .busy(dec_busy), .done(dec_done), .err(dec_err),
        .px_valid(dec_px_valid), .px_x(dec_px_x), .px_y(dec_px_y), .px_r(dec_px_r), .px_g(dec_px_g), .px_b(dec_px_b), .px_last(dec_px_last));

    // ---- row ring: record per row and the 4 byte arrays
    reg [24:0] rec_addr [0:7]; reg [8:0] rec_nhw [0:7]; reg rec_b0 [0:7]; reg rec_lodd [0:7];
    reg [25:0] arr_row_addr;
    reg row_first;                      // the next row to arrive is the tile's first (rows_arr wraps, so it cannot tell)
    wire [25:0] ra_now = (dec_px_x == 7'd0) ? ((row_first && BUG != 10) ? dst : arr_row_addr + {10'd0, pitch}) : arr_row_addr;
    wire        b0_now = ra_now[0];
    wire [8:0]  o_now = {8'd0, b0_now} + {2'd0, dec_px_x} * 9'd3;
    wire [2:0]  bank = rows_arr[2:0];
    reg [7:0]  m0 [0:1023]; reg [7:0] m1 [0:1023]; reg [7:0] m2 [0:1023]; reg [7:0] m3 [0:1023];
    reg [3:0]  aw_we; reg [9:0] aw_a0, aw_a1, aw_a2, aw_a3; reg [7:0] aw_d0, aw_d1, aw_d2, aw_d3;
    integer i;
    reg [8:0] mm; reg [7:0] hk; reg [1:0] aid; reg [9:0] aaddr; reg [7:0] adat;
    always @* begin
        aw_we = 4'd0; aw_a0 = 10'd0; aw_a1 = 10'd0; aw_a2 = 10'd0; aw_a3 = 10'd0; aw_d0 = 8'd0; aw_d1 = 8'd0; aw_d2 = 8'd0; aw_d3 = 8'd0;
        for (i = 0; i < 3; i = i + 1) begin
            mm = o_now + i[8:0]; hk = mm[8:1]; aid = {hk[0], mm[0]}; aaddr = {bank, hk[7:1]};
            adat = (i == 0) ? dec_px_r : (i == 1) ? ((BUG == 2) ? dec_px_b : dec_px_g) : ((BUG == 2) ? dec_px_g : dec_px_b);
            if (dec_px_valid && !(BUG == 3 && i == 2)) begin
                case (aid)
                    2'd0: begin aw_we[0] = 1'b1; aw_a0 = aaddr; aw_d0 = adat; end
                    2'd1: begin aw_we[1] = 1'b1; aw_a1 = aaddr; aw_d1 = adat; end
                    2'd2: begin aw_we[2] = 1'b1; aw_a2 = aaddr; aw_d2 = adat; end
                    default: begin aw_we[3] = 1'b1; aw_a3 = aaddr; aw_d3 = adat; end
                endcase
            end
        end
    end
    always @(posedge clk) begin
        if (aw_we[0]) m0[aw_a0] <= aw_d0; if (aw_we[1]) m1[aw_a1] <= aw_d1; if (aw_we[2]) m2[aw_a2] <= aw_d2; if (aw_we[3]) m3[aw_a3] <= aw_d3;
    end
    wire [2:0] wslot = rows_wr[2:0];
    reg  [7:0] rd0, rd1, rd2, rd3; reg idx1_0; reg [1:0] be1;
    wire [9:0] radr = {wslot, p_idx[7:1]};
    wire       rb0 = rec_b0[wslot], rlodd = rec_lodd[wslot]; wire [8:0] rnhw = rec_nhw[wslot];
    wire [1:0] be_comb = {!(p_idx[8:0] == rnhw - 9'd1 && rlodd && BUG != 4), !(p_idx == 10'd0 && rb0 && BUG != 5)};
    always @(posedge clk) begin
        rd0 <= m0[radr]; rd1 <= m1[radr]; rd2 <= m2[radr]; rd3 <= m3[radr];
        idx1_0 <= p_idx[0]; be1 <= be_comb;
        p_wdata <= idx1_0 ? {rd3, rd2} : {rd1, rd0};
        p_be <= (BUG == 6) ? 2'b11 : be1;
    end

    // ---- stream FIFO read (synchronous) and the byte packer
    wire pop_hw = !hv && (scount != 8'd0);
    always @(posedge clk) begin
        if (pop_hw) sfq <= sf[sr_p];
        if (p_r_valid && pst == P_WAIT && pk == K_STR) sf[sw_p] <= p_r_data;
    end

    // ---- main control
    wire [24:0] row_hw = rec_addr[wslot];
    wire want_tbl = (mode == M_TABLES) && !tbl_req_done;
    wire want_wr  = (mode == M_TILE) && (rows_arr != rows_wr);
    wire [20:0] rlen_full = (hw_left > 21'd32) ? 21'd32 : hw_left;
    wire want_str = (mode == M_TILE) && (hw_left != 21'd0) && ((BUG == 7) || ((8'd128 - scount) >= rlen_full[7:0]));
    reg [24:0] req_addr_n; reg [9:0] req_len_n; reg req_we_n; reg [1:0] req_k_n; reg req_go;
    always @* begin
        req_go = 1'b0; req_addr_n = 25'd0; req_len_n = 10'd0; req_we_n = 1'b0; req_k_n = K_TBL;
        if (pst == P_IDLE && mode != M_DRAIN && !err_pend) begin
            if (want_tbl) begin req_go = 1'b1; req_addr_n = (tbl_stage ? qc[25:1] : qy[25:1]); req_len_n = 10'd64; req_we_n = 1'b0; req_k_n = K_TBL; end
            else if (want_wr) begin req_go = 1'b1; req_addr_n = row_hw; req_len_n = {1'b0, rec_nhw[wslot]}; req_we_n = 1'b1; req_k_n = K_WR; end
            else if (want_str) begin req_go = 1'b1; req_addr_n = s_hw; req_len_n = rlen_full[9:0]; req_we_n = 1'b0; req_k_n = K_STR; end
        end
    end

    always @(posedge clk) begin
        ev_done <= 1'b0; ev_err <= 1'b0; ev_tables <= 1'b0; dec_start <= 1'b0; dec_qwe <= 1'b0;
        if (dec_rst_cnt != 3'd0) dec_rst_cnt <= dec_rst_cnt - 3'd1;
        if (rst) begin
            mode <= M_IDLE; pst <= P_IDLE; p_valid <= 1'b0; err_pend <= 1'b0; dec_rst_cnt <= 3'd0; scount <= 8'd0; hv <= 1'b0; nb <= 4'd0; acc <= 64'd0;
            rows_arr <= 5'd0; rows_wr <= 5'd0; tcyc <= 24'd0; tile_cycles <= 32'd0; err_code <= 4'd0; dec_fin <= 1'b0; sw_p <= 7'd0; sr_p <= 7'd0; hw_left <= 21'd0;
        end else begin
            // ---- port FSM
            case (pst)
                P_IDLE: if (req_go) begin p_valid <= 1'b1; p_we <= req_we_n; p_addr <= req_addr_n; p_len <= req_len_n; pk <= req_k_n; pst <= P_REQ; end
                P_REQ: if (!p_ready && mode == M_DRAIN) begin p_valid <= 1'b0; pst <= P_IDLE; end      // an unaccepted request is withdrawn (acceptance is valid && ready at one edge, seen identically on both sides)
                else if (p_ready) begin
                    p_valid <= 1'b0; pst <= P_WAIT;
                    if (pk == K_TBL) begin tbl_req_done <= 1'b1; tbl_cnt <= 6'd0; tbl_qsel <= tbl_stage; end
                    if (pk == K_STR) begin s_hw <= s_hw + {15'd0, p_len}; hw_left <= hw_left - {11'd0, p_len}; end
                end
                P_WAIT: if (p_done) begin
                    pst <= P_IDLE;
                    if (pk == K_WR) rows_wr <= rows_wr + 5'd1;
                    if (pk == K_TBL) begin
                        if (tbl_stage) begin ev_tables <= 1'b1; mode <= M_IDLE; end
                        else begin tbl_stage <= 1'b1; tbl_req_done <= 1'b0; end
                    end
                end
                default: pst <= P_IDLE;
            endcase
            // table words go to the decoder
            if (pst == P_WAIT && pk == K_TBL && p_r_valid) begin dec_qwe <= 1'b1; dec_qsel <= tbl_qsel; dec_qidx <= tbl_cnt; dec_qval <= p_r_data; tbl_cnt <= tbl_cnt + 6'd1; end
            // stream words into the FIFO
            if (pst == P_WAIT && pk == K_STR && p_r_valid) begin sw_p <= sw_p + 7'd1; end
            // FIFO count: pushes and pops
            scount <= scount + ((pst == P_WAIT && pk == K_STR && p_r_valid) ? 8'd1 : 8'd0) - (pop_hw ? 8'd1 : 8'd0);
            if (pop_hw) begin sr_p <= sr_p + 7'd1; hv <= 1'b1; end
            // byte packer
            if (deliver) begin acc <= acc >> 32; nb <= (nb > 4'd4) ? nb - 4'd4 : 4'd0; end
            else if (hv && nb <= 4'd4) begin
                hv <= 1'b0;
                if (skip) begin acc[8 * nb +: 8] <= sfq[15:8]; nb <= nb + 4'd1; skip <= 1'b0; end
                else begin acc[8 * nb +: 16] <= sfq; nb <= nb + 4'd2; end
            end
            // pixel rows: bookkeeping at the last pixel of a row
            if (dec_px_valid) begin
                arr_row_addr <= ra_now;
                if (dec_px_x == w[6:0] - 7'd1 || (w == 8'd128 && dec_px_x == 7'd127)) begin
                    rec_addr[bank] <= ra_now[25:1]; rec_b0[bank] <= b0_now;
                    rec_nhw[bank] <= ({8'd0, b0_now} + {1'b0, w} * 9'd3 + 9'd1) >> 1; rec_lodd[bank] <= ({8'd0, b0_now} + {1'b0, w} * 9'd3) & 9'd1;
                    rows_arr <= rows_arr + 5'd1; row_first <= 1'b0;
                end
            end
            // ---- mode control
            case (mode)
                M_IDLE: begin
                    if (cmd_tables) begin mode <= M_TABLES; tbl_stage <= 1'b0; tbl_req_done <= 1'b0; qy <= cfg_qy; qc <= cfg_qc; end
                    else if (cmd_tile) begin
                        mode <= M_TILE; src <= cfg_src; dst <= cfg_dst; len <= cfg_len; pitch <= cfg_pitch; fmt <= cfg_fmt; w <= cfg_w; h <= cfg_h;
                        s_hw <= cfg_src[25:1]; hw_left <= (({20'd0, cfg_src[0]} + {1'b0, cfg_len} + 21'd1) >> 1); skip <= cfg_src[0] && (BUG != 8);
                        scount <= 8'd0; hv <= 1'b0; nb <= 4'd0; acc <= 64'd0; sw_p <= 7'd0; sr_p <= 7'd0; rows_arr <= 5'd0; rows_wr <= 5'd0; tcyc <= 24'd0; dec_fin <= 1'b0; dec_start <= 1'b1; row_first <= 1'b1;
                    end
                end
                M_TABLES: if (cmd_abort) begin err_pend <= 1'b1; pend_err <= 4'd15; end
                M_TILE: begin
                    tcyc <= tcyc + 24'd1;
                    if (dec_done) dec_fin <= 1'b1;
                    if (dec_err && !err_pend) begin err_pend <= 1'b1; pend_err <= 4'd1; end
                    else if (cmd_abort && !err_pend) begin err_pend <= 1'b1; pend_err <= 4'd15; end
                    else if (tcyc == TMO && BUG != 9 && !err_pend) begin err_pend <= 1'b1; pend_err <= 4'd2; end
                    else if (dec_fin && rows_arr == rows_wr && pst == P_IDLE) begin
                        mode <= M_IDLE; ev_done <= 1'b1; tile_cycles <= {8'd0, tcyc};
                    end
                end
                default: ;
            endcase
            if (err_pend && mode != M_DRAIN) begin mode <= M_DRAIN; dec_rst_cnt <= 3'd4; end
            if (mode == M_DRAIN && pst == P_IDLE && dec_rst_cnt == 3'd0) begin
                mode <= M_IDLE; err_pend <= 1'b0; ev_err <= 1'b1; err_code <= pend_err; hv <= 1'b0; scount <= 8'd0; nb <= 4'd0; acc <= 64'd0; rows_arr <= 5'd0; rows_wr <= 5'd0; hw_left <= 21'd0;
            end
        end
    end
endmodule
