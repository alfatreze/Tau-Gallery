// tau_tgy_dma.sv -- the TGY decoder's SDRAM DMA, the SDRAM-clock half of the unit (docs/TGY_HW_DMA.md, docs/TGY_HW_CLOCK.md): one scheduler client port (the contract of tau_sdram_sched_a, through tau_sdram_dmux) that
//   (1) loads the two quantisation tables (64 u16 each, zigzag, even byte address) into the decoder's table RAM,
//   (2) fetches a tile STREAM from any byte address in SDRAM (halfword reads of up to 32, an odd start byte is dropped), packs it into exactly ceil(len / 4) 32-bit words and pushes them into the word FIFO (tau_tgy_wfifo)
//       that the decoder reads,
//   (3) writes the decoded RGB888 pixels into a picture plane with a byte pitch, at any byte address (one request per pixel row, the first and last halfword partial when the row starts or ends on an odd byte,
//       through the byte enables), with the neighbouring bytes untouched.
// Two clocks: this module runs on the SDRAM clock (clk); the decoder, its pixel stage and the row ring run on clk_dec (50 MHz, tau_tgy_dcore: the decoder's logic does not close at 100 MHz, docs/TGY_HW_CLOCK.md). The
// crossings: word FIFO (here -> dcore), the ring's rows (dcore -> here, counted by rows_arr / rows_wr in Gray code), start / reset toggles, done / error toggles, the configuration buses (latched here at cmd_tile,
// stable until the next one). A tile: cmd_tile with cfg_* stable (the tables loaded before by cmd_tables); first the word FIFO is cleared (handshake), then the decoder is started and the stream flows.
// Events (1-clock pulses): ev_done after the last row is in SDRAM, ev_err with err_code (1 decoder or watchdog error, 2 tile timeout, 15 abort), ev_tables when both tables are loaded.
// The scheduler's read data and `done` are registered once on the way in (their paths from the scheduler are long at 100 MHz).
// BUG (test only): 0 = correct; 2 no FIFO space check, 3 no odd-start skip, 4 last byte enable, 5 first byte enable, 6 enables forced, 7 no timeout, 8 the word FIFO is not cleared before a tile,
// 9 the finished tile does not wait for the last rows (done at once), 10 words not limited to ceil(len / 4) (equivalent: the FIFO is cleared before every tile, so it is not in the sweep), 11..14 mutants of tau_tgy_dcore (1..4).
module tau_tgy_dma #(parameter integer C444 = 0, parameter integer BUG = 0, parameter [23:0] TMO = 24'hFFFFFF) (
    input  wire        clk, rst,
    input  wire        clk_dec, rst_dec,
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
    input  wire        p_done,
    output wire        tap_valid,
    output wire [6:0]  tap_x, tap_y,
    output wire [7:0]  tap_r, tap_g, tap_b
);
    // ---- scheduler inputs, registered once
    reg rv_q = 1'b0, dn_q = 1'b0; reg [15:0] rd_q = 16'd0;
    always @(posedge clk) begin rv_q <= p_r_valid; rd_q <= p_r_data; dn_q <= p_done; end
    // ---- top state
    localparam [1:0] M_IDLE = 0, M_TABLES = 1, M_TILE = 2, M_DRAIN = 3;
    reg [1:0] mode; assign busy = (mode != M_IDLE);
    reg [25:0] src, dst, qy, qc; reg [19:0] len; reg [15:0] pitch; reg [1:0] fmt; reg [7:0] w, h;
    reg [23:0] tcyc; reg dec_fin; reg [3:0] pend_err; reg err_pend; reg [4:0] drain_cnt;
    reg start_tog = 1'b0, rst_tog = 1'b0;                       // not reset: a reset must not fake a command in the other clock domain
    // tile start sequence: clear the word FIFO (handshake), then start the decoder and let the stream flow
    reg [1:0] tph; reg [3:0] tdly; reg clear_req; wire clear_ack;
    wire run = (mode == M_TILE) && (tph == 2'd3);
    // ---- port state
    localparam [1:0] P_IDLE = 0, P_REQ = 1, P_WAIT = 2;
    localparam [1:0] K_TBL = 0, K_STR = 1, K_WR = 2;
    reg [1:0] pst, pk;
    // ---- tables
    reg tbl_stage; reg tbl_req_done; reg [5:0] tbl_cnt; reg tbl_qsel;
    reg dec_qwe; reg [5:0] dec_qidx; reg dec_qsel; reg [15:0] dec_qval;
    // ---- stream fetch
    reg [24:0] s_hw; reg [20:0] hw_left; reg skip; reg [17:0] words_left;
    reg [15:0] sf [0:127]; reg [6:0] sw_p, sr_p; reg [7:0] scount; reg [15:0] sfq; reg hv;
    reg [63:0] acc; reg [3:0] nb;
    wire str_inflight = (pst != P_IDLE) && (pk == K_STR);
    wire fetch_done = (hw_left == 21'd0) && !str_inflight;
    wire wf_full; wire tail_ok = fetch_done && (scount == 8'd0) && !hv && (nb != 4'd0);
    wire push = run && (nb >= 4'd4 || tail_ok) && (words_left != 18'd0 || BUG == 10) && !wf_full;
    // ---- the other clock
    wire wf_rvalid, wf_pop; wire [31:0] wf_rdata;
    tau_tgy_wfifo #(.AW(6)) wfifo (.clk_w(clk), .clk_r(clk_dec), .clear_req(clear_req), .clear_ack(clear_ack), .push(push), .wdata(acc[31:0]), .full(wf_full), .rvalid(wf_rvalid), .rdata(wf_rdata), .pop(wf_pop));
    wire fin_tog, err_tog; wire [4:0] rows_arr_m; reg [4:0] rows_wr;
    wire [9:0] radr; wire [7:0] rd0, rd1, rd2, rd3; wire [2:0] wslot = rows_wr[2:0];
    wire [24:0] rec_addr_o; wire [8:0] rec_nhw_o; wire rec_b0_o, rec_lodd_o;
    tau_tgy_dcore #(.C444(C444), .BUG(BUG >= 11 ? BUG - 10 : 0)) dcore (.clk_dec(clk_dec), .rst_dec(rst_dec), .clk_mem(clk), .start_tog(start_tog), .rst_tog(rst_tog), .cfg_fmt(fmt), .cfg_w(w), .cfg_h(h), .cfg_len(len), .cfg_dst(dst), .cfg_pitch(pitch),
        .q_we(dec_qwe), .q_sel(dec_qsel), .q_idx(dec_qidx), .q_val(dec_qval), .wf_valid(wf_rvalid), .wf_data(wf_rdata), .wf_pop(wf_pop), .fin_tog(fin_tog), .err_tog(err_tog),
        .rows_wr(rows_wr), .rows_arr_m(rows_arr_m), .r_addr(radr), .r_d0(rd0), .r_d1(rd1), .r_d2(rd2), .r_d3(rd3), .r_slot(wslot), .rec_addr_o(rec_addr_o), .rec_nhw_o(rec_nhw_o), .rec_b0_o(rec_b0_o), .rec_lodd_o(rec_lodd_o),
        .tap_valid(tap_valid), .tap_x(tap_x), .tap_y(tap_y), .tap_r(tap_r), .tap_g(tap_g), .tap_b(tap_b));
    wire fin_s, err_s; reg fin_q = 1'b0, err_q = 1'b0;
    tau_cdc_sync1 #(.STAGES(3)) u_fin (.clk_dst(clk), .d_src(fin_tog), .q_dst(fin_s));
    tau_cdc_sync1 #(.STAGES(3)) u_err (.clk_dst(clk), .d_src(err_tog), .q_dst(err_s));
    always @(posedge clk) begin fin_q <= fin_s; err_q <= err_s; end
    wire fin_p = fin_s ^ fin_q, derr_p = err_s ^ err_q;

    // ---- the writer's data path: the ring's read port (on clk), two clocks of lead, byte enables
    reg idx1_0; reg [1:0] be1;
    assign radr = {wslot, p_idx[7:1]};
    wire [8:0] rnhw = rec_nhw_o; wire rb0 = rec_b0_o, rlodd = rec_lodd_o;
    wire [1:0] be_comb = {!(p_idx[8:0] == rnhw - 9'd1 && rlodd && BUG != 4), !(p_idx == 10'd0 && rb0 && BUG != 5)};
    always @(posedge clk) begin
        idx1_0 <= p_idx[0]; be1 <= be_comb;
        p_wdata <= idx1_0 ? {rd3, rd2} : {rd1, rd0};
        p_be <= (BUG == 6) ? 2'b11 : be1;
    end

    // ---- stream FIFO read (synchronous) and the byte packer
    wire pop_hw = !hv && (scount != 8'd0);
    always @(posedge clk) begin
        if (pop_hw) sfq <= sf[sr_p];
        if (rv_q && pst == P_WAIT && pk == K_STR) sf[sw_p] <= rd_q;
    end

    // ---- main control
    wire want_tbl = (mode == M_TABLES) && !tbl_req_done;
    wire want_wr  = (mode == M_TILE) && (rows_arr_m != rows_wr);
    wire [20:0] rlen_full = (hw_left > 21'd32) ? 21'd32 : hw_left;
    wire want_str = run && (hw_left != 21'd0) && ((BUG == 2) || ((8'd128 - scount) >= rlen_full[7:0]));
    reg [24:0] req_addr_n; reg [9:0] req_len_n; reg req_we_n; reg [1:0] req_k_n; reg req_go;
    always @* begin
        req_go = 1'b0; req_addr_n = 25'd0; req_len_n = 10'd0; req_we_n = 1'b0; req_k_n = K_TBL;
        if (pst == P_IDLE && mode != M_DRAIN && !err_pend) begin
            if (want_tbl) begin req_go = 1'b1; req_addr_n = (tbl_stage ? qc[25:1] : qy[25:1]); req_len_n = 10'd64; req_we_n = 1'b0; req_k_n = K_TBL; end
            else if (want_wr) begin req_go = 1'b1; req_addr_n = rec_addr_o; req_len_n = {1'b0, rec_nhw_o}; req_we_n = 1'b1; req_k_n = K_WR; end
            else if (want_str) begin req_go = 1'b1; req_addr_n = s_hw; req_len_n = rlen_full[9:0]; req_we_n = 1'b0; req_k_n = K_STR; end
        end
    end

    always @(posedge clk) begin
        ev_done <= 1'b0; ev_err <= 1'b0; ev_tables <= 1'b0; dec_qwe <= 1'b0;
        if (rst) begin
            mode <= M_IDLE; pst <= P_IDLE; p_valid <= 1'b0; err_pend <= 1'b0; scount <= 8'd0; hv <= 1'b0; nb <= 4'd0; acc <= 64'd0; clear_req <= 1'b0; tph <= 2'd0; drain_cnt <= 5'd0;
            rows_wr <= 5'd0; tcyc <= 24'd0; tile_cycles <= 32'd0; err_code <= 4'd0; dec_fin <= 1'b0; sw_p <= 7'd0; sr_p <= 7'd0; hw_left <= 21'd0; words_left <= 18'd0;
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
                P_WAIT: if (dn_q) begin
                    pst <= P_IDLE;
                    if (pk == K_WR) rows_wr <= rows_wr + 5'd1;
                    if (pk == K_TBL) begin
                        if (tbl_stage) begin ev_tables <= 1'b1; mode <= M_IDLE; end
                        else begin tbl_stage <= 1'b1; tbl_req_done <= 1'b0; end
                    end
                end
                default: pst <= P_IDLE;
            endcase
            // table words go to the decoder's table RAM
            if (pst == P_WAIT && pk == K_TBL && rv_q) begin dec_qwe <= 1'b1; dec_qsel <= tbl_qsel; dec_qidx <= tbl_cnt; dec_qval <= rd_q; tbl_cnt <= tbl_cnt + 6'd1; end
            // stream halfwords into the halfword FIFO
            if (pst == P_WAIT && pk == K_STR && rv_q) sw_p <= sw_p + 7'd1;
            scount <= scount + ((pst == P_WAIT && pk == K_STR && rv_q) ? 8'd1 : 8'd0) - (pop_hw ? 8'd1 : 8'd0);
            if (pop_hw) begin sr_p <= sr_p + 7'd1; hv <= 1'b1; end
            // byte packer: a word into the FIFO when the decoder side has room
            if (push) begin acc <= acc >> 32; nb <= (nb > 4'd4) ? nb - 4'd4 : 4'd0; if (BUG != 10) words_left <= words_left - 18'd1; if (words_left == 18'd1 && BUG != 10) hw_left <= 21'd0; end
            else if (hv && nb <= 4'd4) begin
                hv <= 1'b0;
                if (skip) begin acc[8 * nb +: 8] <= sfq[15:8]; nb <= nb + 4'd1; skip <= 1'b0; end
                else begin acc[8 * nb +: 16] <= sfq; nb <= nb + 4'd2; end
            end
            // ---- mode control
            case (mode)
                M_IDLE: begin
                    if (cmd_tables) begin mode <= M_TABLES; tbl_stage <= 1'b0; tbl_req_done <= 1'b0; qy <= cfg_qy; qc <= cfg_qc; end
                    else if (cmd_tile) begin
                        mode <= M_TILE; src <= cfg_src; dst <= cfg_dst; len <= cfg_len; pitch <= cfg_pitch; fmt <= cfg_fmt; w <= cfg_w; h <= cfg_h;
                        s_hw <= cfg_src[25:1]; hw_left <= (({20'd0, cfg_src[0]} + {1'b0, cfg_len} + 21'd1) >> 1); skip <= cfg_src[0] && (BUG != 3); words_left <= (({1'b0, cfg_len} + 21'd3) >> 2);
                        scount <= 8'd0; hv <= 1'b0; nb <= 4'd0; acc <= 64'd0; sw_p <= 7'd0; sr_p <= 7'd0; tcyc <= 24'd0; dec_fin <= 1'b0; tph <= 2'd0; clear_req <= (BUG != 8);
                    end
                end
                M_TABLES: if (cmd_abort) begin err_pend <= 1'b1; pend_err <= 4'd15; end
                M_TILE: begin
                    tcyc <= tcyc + 24'd1;
                    case (tph)
                        2'd0: if (clear_ack || BUG == 8) begin tdly <= 4'd8; tph <= 2'd1; end                      // the decoder side has zeroed its FIFO pointer
                        2'd1: if (tdly == 4'd0) begin clear_req <= 1'b0; tdly <= 4'd8; tph <= 2'd2; end else tdly <= tdly - 4'd1;   // keep the request up so the zero pointers cross, then release
                        2'd2: if (!clear_ack || BUG == 8) begin if (tdly == 4'd0) begin tph <= 2'd3; start_tog <= ~start_tog; end else tdly <= tdly - 4'd1; end   // released on the other side too: start the decoder
                        default: ;
                    endcase
                    if (fin_p) dec_fin <= 1'b1;
                    if (derr_p && !err_pend) begin err_pend <= 1'b1; pend_err <= 4'd1; end
                    else if (cmd_abort && !err_pend) begin err_pend <= 1'b1; pend_err <= 4'd15; end
                    else if (tcyc == TMO && BUG != 7 && !err_pend) begin err_pend <= 1'b1; pend_err <= 4'd2; end
                    else if (dec_fin && (rows_arr_m == rows_wr || BUG == 9) && pst == P_IDLE) begin
                        mode <= M_IDLE; ev_done <= 1'b1; tile_cycles <= {8'd0, tcyc};
                    end
                end
                default: ;
            endcase
            if (err_pend && mode != M_DRAIN) begin mode <= M_DRAIN; rst_tog <= ~rst_tog; drain_cnt <= 5'd24; clear_req <= 1'b0; end
            if (mode == M_DRAIN) begin
                if (drain_cnt != 5'd0) drain_cnt <= drain_cnt - 5'd1;
                if (pst == P_IDLE && drain_cnt == 5'd0) begin
                    mode <= M_IDLE; err_pend <= 1'b0; ev_err <= 1'b1; err_code <= pend_err; hv <= 1'b0; scount <= 8'd0; nb <= 4'd0; acc <= 64'd0; rows_wr <= 5'd0; hw_left <= 21'd0; words_left <= 18'd0;
                end
            end
        end
    end
endmodule
