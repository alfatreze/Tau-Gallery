// tau_tgy_wfifo.sv -- a two clock FIFO of 32-bit words for the TGY decoder's stream (docs/TGY_HW_CLOCK.md): written on the SDRAM clock (clk_w) by the DMA's packer, read on the decoder clock (clk_r) by the
// entropy decoder, show-ahead output (rvalid / rdata valid until pop). The memory is one simple dual port RAM (write clk_w, read clk_r); the two pointers cross as Gray codes through tau_cdc_gray_bus (each
// side sees the other's pointer a few clocks late, which only makes it look fuller / emptier than it is). CLEAR: the DMA holds `clear_req` (a level in clk_w) to empty the FIFO before a tile and after an error:
// the write side zeroes its pointer at once, the request crosses to clk_r (3 flops), the read side zeroes its pointer and drops its output, and `clear_ack` (the read side's state crossed back) tells the writer
// both sides are at zero; the writer keeps the request up a few more clocks (so the zero pointers have crossed too) and releases it, and the read side stays empty while it still sees the request.
// BUG (test only): 0 = correct; 1 full one word late (overwrites), 2 the read side ignores the clear, 3 empty ignores the crossed write pointer's lag (reads one word early).
module tau_tgy_wfifo #(parameter integer AW = 6, parameter integer BUG = 0) (
    input  wire        clk_w, clk_r,
    input  wire        clear_req,
    output wire        clear_ack,
    input  wire        push,
    input  wire [31:0] wdata,
    output wire        full,
    output wire        rvalid,
    output wire [31:0] rdata,
    input  wire        pop
);
    localparam integer W = AW + 1;
    (* ramstyle = "M10K, no_rw_check" *) reg [31:0] mem [0:(1 << AW) - 1];
    reg [W-1:0] wptr = {W{1'b0}}, rptr = {W{1'b0}};
    wire [W-1:0] rptr_w, wptr_r;
    tau_cdc_gray_bus #(.W(W), .STAGES(3)) u_w2r (.clk_src(clk_w), .d_src(wptr), .clk_dst(clk_r), .q_dst(wptr_r));
    tau_cdc_gray_bus #(.W(W), .STAGES(3)) u_r2w (.clk_src(clk_r), .d_src(rptr), .clk_dst(clk_w), .q_dst(rptr_w));
    wire clr_r;
    tau_cdc_sync1 #(.STAGES(3)) u_c1 (.clk_dst(clk_r), .d_src(clear_req), .q_dst(clr_r));
    tau_cdc_sync1 #(.STAGES(3)) u_c2 (.clk_dst(clk_w), .d_src(clr_r), .q_dst(clear_ack));
    // write side
    wire [W-1:0] used = wptr - rptr_w;
    assign full = (BUG == 1) ? (used > (1 << AW)) : (used >= (1 << AW));
    always @(posedge clk_w) begin
        if (clear_req) wptr <= {W{1'b0}};
        else if (push && !full) begin mem[wptr[AW-1:0]] <= wdata; wptr <= wptr + {{(W-1){1'b0}}, 1'b1}; end
    end
    // read side
    reg [31:0] raw, rq; reg rv = 1'b0, pend = 1'b0;
    wire cl = (BUG == 2) ? 1'b0 : clr_r;
    wire nonempty = (BUG == 3) ? ((wptr_r + {{(W-1){1'b0}}, 1'b1}) != rptr) : (wptr_r != rptr);
    always @(posedge clk_r) raw <= mem[rptr[AW-1:0]];
    always @(posedge clk_r) begin
        if (cl) begin rptr <= {W{1'b0}}; rv <= 1'b0; pend <= 1'b0; end
        else begin
            if (pop && rv) rv <= 1'b0;
            if (pend) begin rv <= 1'b1; pend <= 1'b0; rq <= raw; end
            else if (!rv && nonempty && !clr_r) begin rptr <= rptr + {{(W-1){1'b0}}, 1'b1}; pend <= 1'b1; end
        end
    end
    assign rvalid = rv && !clr_r;
    assign rdata = rq;
endmodule
