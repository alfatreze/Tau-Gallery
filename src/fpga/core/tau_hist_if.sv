// =============================================================================
// tau_hist_if.sv -- the CPU side of the histogram unit (K4): the registers on clk_sys, the crossings to the decoder clock and tau_hist itself.
//
// Registers (byte offsets in the MMIO page, the same decode as the decoder's: reg_addr = byte offset, reg_we a one-cycle write strobe):
//   0x260 HIST_CTRL   write: [0] count on / off, [1] clear (a 1 starts the sweep of all bins to 0)        read: [8] busy (a clear or a read is under way), [0] count on
//   0x264 HIST_RECT0  write / read: [9:0] window x0, [25:16] window y0 (canvas pixels, inclusive)
//   0x268 HIST_RECT1  write / read: [9:0] window x1, [25:16] window y1 (exclusive)
//   0x26C HIST_ORG    write / read: [9:0] x, [25:16] y: the canvas position of the next tile's pixel (0, 0); set it before the tile is started
//   0x270 HIST_IDX    write: {histogram [9:8] 0 luma 1 red 2 green 3 blue, bin [7:0]}: starts a read of that bin (and of the pixel count)
//   0x274 HIST_DATA   read: [31] busy, [23:0] the bin read by the last HIST_IDX
//   0x278 HIST_N      read: [23:0] the number of pixels counted, as of the last HIST_IDX
// The window, origin and enable change only while the decoder is idle (the firmware sets them before it starts a tile), so they cross to the decoder clock through two flops each; the
// first pixel of a tile comes many clocks after the start command. The clear and the read are handshakes (a toggle there, a toggle back), busy is high from the write until the answer.
// Counting happens from the decoder's pixel stage: px_* is that stage's output on the decoder clock (clk_dec).
//
// BUG is a mutation hook for the testbench (0 in the build): 1 reads the pixel count of the wrong moment (before the bin), 2 the clear does not raise busy, 3 HIST_DATA does not report busy.
// =============================================================================
`default_nettype none

module tau_hist_if #(
    parameter integer BUG = 0
) (
    input  wire        clk_sys,
    input  wire        rst_sys,
    input  wire [9:0]  reg_addr,
    input  wire        reg_we,
    input  wire [31:0] reg_wdata,
    output wire        reg_hit,
    output wire [31:0] reg_rdata,

    input  wire        clk_dec,
    input  wire        px_valid,
    input  wire [6:0]  px_x,
    input  wire [6:0]  px_y,
    input  wire [7:0]  px_r,
    input  wire [7:0]  px_g,
    input  wire [7:0]  px_b
);
    localparam [9:0] A_CTRL = 10'h260, A_RECT0 = 10'h264, A_RECT1 = 10'h268, A_ORG = 10'h26C, A_IDX = 10'h270, A_DATA = 10'h274, A_N = 10'h278;
    assign reg_hit = (reg_addr == A_CTRL) || (reg_addr == A_RECT0) || (reg_addr == A_RECT1) || (reg_addr == A_ORG) || (reg_addr == A_IDX) || (reg_addr == A_DATA) || (reg_addr == A_N);

    // ---- registers (clk_sys) ------------------------------------------------------
    reg        en_r = 1'b0;
    reg [19:0] rect0 = 20'd0, rect1 = 20'd0, org = 20'd0;                                // {y[9:0], x[9:0]}
    reg [9:0]  idx_hold = 10'd0;
    reg        clr_tgl = 1'b0, rd_tgl = 1'b0;
    reg [1:0]  clr_ack_s = 2'b00, rd_ack_s = 2'b00;
    wire       clr_ack_tgl_d, rd_ack_tgl_d;                                                // from the decoder clock
    wire       clr_busy_s = (clr_tgl != clr_ack_s[1]);
    wire       rd_busy_s  = (rd_tgl  != rd_ack_s[1]);
    wire       busy = ((BUG == 2) ? 1'b0 : clr_busy_s) || rd_busy_s;

    wire [23:0] cap_data_d, cap_n_d;                                                       // held by the decoder-clock side until the next read request

    always @(posedge clk_sys) begin
        clr_ack_s <= {clr_ack_s[0], clr_ack_tgl_d};
        rd_ack_s  <= {rd_ack_s[0],  rd_ack_tgl_d};
        if (rst_sys) begin
            en_r <= 1'b0;
        end else if (reg_we) begin
            case (reg_addr)
                A_CTRL:  begin en_r <= reg_wdata[0]; if (reg_wdata[1] && !busy) clr_tgl <= ~clr_tgl; end
                A_RECT0: rect0 <= {reg_wdata[25:16], reg_wdata[9:0]};
                A_RECT1: rect1 <= {reg_wdata[25:16], reg_wdata[9:0]};
                A_ORG:   org   <= {reg_wdata[25:16], reg_wdata[9:0]};
                A_IDX:   if (!busy) begin idx_hold <= reg_wdata[9:0]; rd_tgl <= ~rd_tgl; end
                default: ;
            endcase
        end
    end

    reg [31:0] rd;
    always @* begin
        case (reg_addr)
            A_CTRL:  rd = {23'd0, busy, 7'd0, en_r};
            A_RECT0: rd = {6'd0, rect0[19:10], 6'd0, rect0[9:0]};
            A_RECT1: rd = {6'd0, rect1[19:10], 6'd0, rect1[9:0]};
            A_ORG:   rd = {6'd0, org[19:10], 6'd0, org[9:0]};
            A_DATA:  rd = {(BUG == 3) ? 1'b0 : rd_busy_s, 7'd0, cap_data_d};
            A_N:     rd = {8'd0, cap_n_d};          // the answer is held by the decoder-clock side until the next request: stable while busy is low
            default: rd = 32'd0;
        endcase
    end
    assign reg_rdata = reg_hit ? rd : 32'd0;

    // ---- into the decoder clock ----------------------------------------------------------
    reg        en_a = 1'b0, en_b = 1'b0;
    reg [19:0] r0_a = 20'd0, r0_b = 20'd0, r1_a = 20'd0, r1_b = 20'd0, og_a = 20'd0, og_b = 20'd0;
    reg [2:0]  clr_s = 3'b000;
    reg [2:0]  rq_s  = 3'b000;
    reg        clr_ack_tgl = 1'b0, rd_ack_tgl = 1'b0;
    reg [9:0]  rd_addr = 10'd0;
    reg [3:0]  rd_wait = 4'd0;
    reg        rd_go = 1'b0;
    reg [23:0] cd = 24'd0, cn = 24'd0;
    wire       clr_busy_d;
    wire [23:0] rd_data, n_counted;
    wire       clr_req_sync = clr_s[1];                                                    // two flops: the toggle in the decoder clock (tau_hist sees the change)
    always @(posedge clk_dec) begin
        en_a <= en_r; en_b <= en_a;
        r0_a <= rect0; r0_b <= r0_a; r1_a <= rect1; r1_b <= r1_a; og_a <= org; og_b <= og_a;
        clr_s <= {clr_s[1:0], clr_tgl};
        rq_s  <= {rq_s[1:0], rd_tgl};
        // the clear: acknowledged when the sweep has ended
        if (clr_s[2] != clr_ack_tgl && !clr_busy_d && (clr_s[2] == clr_s[1])) clr_ack_tgl <= clr_s[2];
        // the read: on a request, put the address on the read port, wait for the 2-clock latency, capture, acknowledge
        if (rq_s[2] != rq_s[1] && !rd_go) begin rd_addr <= idx_hold; rd_go <= 1'b1; rd_wait <= 4'd5; end
        else if (rd_go) begin
            if (rd_wait != 4'd0) rd_wait <= rd_wait - 4'd1;
            else begin cd <= rd_data; cn <= (BUG == 1) ? n_counted + 24'd1 : n_counted; rd_ack_tgl <= rq_s[2]; rd_go <= 1'b0; end
        end
    end
    assign clr_ack_tgl_d = clr_ack_tgl;
    assign rd_ack_tgl_d  = rd_ack_tgl;
    assign cap_data_d = cd;
    assign cap_n_d    = cn;

    tau_hist #(.BUG(0)) u_hist (
        .clk(clk_dec), .rst(1'b0),
        .px_valid(px_valid), .px_x(px_x), .px_y(px_y), .px_r(px_r), .px_g(px_g), .px_b(px_b),
        .en(en_b), .org_x(og_b[9:0]), .org_y(og_b[19:10]), .rect_x0(r0_b[9:0]), .rect_y0(r0_b[19:10]), .rect_x1(r1_b[9:0]), .rect_y1(r1_b[19:10]),
        .clr_req_tgl(clr_req_sync), .clr_busy(clr_busy_d),
        .rd_addr(rd_addr), .rd_data(rd_data), .n_counted(n_counted));
endmodule

`default_nettype wire
