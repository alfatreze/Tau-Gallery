// =============================================================================
// tau_sdram_dmux.sv -- Tau Gallery: a secondary READ AND WRITE client D on the CPU scheduler port, chained behind tau_sdram_p2mux (docs/TGY_HW_DMA.md). The port speaks the client contract of
// tau_sdram_sched_a (request valid/ready with address, length and direction; write data PULLED with w_next / w_idx and presented two clocks later with byte enables; read data pushed with r_valid /
// r_data / r_last; one `done` per request, in order). Primary client C = the p2mux output (CPU, and the landing path behind it); secondary client D = the TGY decoder's DMA (stream reads, table reads,
// pixel row writes).
//   D is granted the port only while C offers nothing AND has nothing outstanding; from the moment D's request is accepted until its `done`, C is held (ready low), so the pulled write data and the
//   returned read data belong to exactly one client. C therefore waits at most one D request (a row write of up to 193 halfwords, a stream read of up to 32).
// Same structure as tau_sdram_p2mux (whose proven shape it follows) with D allowed to read and to choose its byte enables.
// BUG is a mutation hook for the testbench (0 in the build): 1 D may start while C has a request outstanding, 2 C is not held while D's request is in flight, 3 write data and byte enables of C
// presented during D's request, 4 `done` always goes to C, 5 D's address replaced by C's, 6 C's read data also reaches C while D reads, 7 D's byte enables forced to both bytes, 8 D sees C's read
// data, 9 D's direction forced to write, 10 D's length replaced by C's.
// =============================================================================
`default_nettype none

module tau_sdram_dmux #(
    parameter integer BUG = 0
) (
    input  wire        clk,
    input  wire        rst,
    // primary client C
    input  wire        c_valid,
    output wire        c_ready,
    input  wire        c_we,
    input  wire [24:0] c_addr,
    input  wire [9:0]  c_len,
    output wire        c_next,
    output wire [9:0]  c_idx,
    input  wire [15:0] c_wdata,
    input  wire [1:0]  c_be,
    output wire        c_r_valid,
    output wire [15:0] c_r_data,
    output wire        c_r_last,
    output wire        c_done,
    // secondary client D
    input  wire        d_valid,
    output wire        d_ready,
    input  wire        d_we,
    input  wire [24:0] d_addr,
    input  wire [9:0]  d_len,
    output wire        d_next,
    output wire [9:0]  d_idx,
    input  wire [15:0] d_wdata,
    input  wire [1:0]  d_be,
    output wire        d_r_valid,
    output wire [15:0] d_r_data,
    output wire        d_r_last,
    output wire        d_done,
    // scheduler port
    output wire        s_valid,
    input  wire        s_ready,
    output wire        s_we,
    output wire [24:0] s_addr,
    output wire [9:0]  s_len,
    input  wire        s_next,
    input  wire [9:0]  s_idx,
    output wire [15:0] s_wdata,
    output wire [1:0]  s_be,
    input  wire        s_r_valid,
    input  wire [15:0] s_r_data,
    input  wire        s_r_last,
    input  wire        s_done
);
    reg       sub_busy;                                       // D's request is accepted and not yet done
    reg [3:0] c_out;                                          // C's requests accepted and not yet done

    wire grant_d = !sub_busy && d_valid && !c_valid && ((BUG == 1) ? 1'b1 : (c_out == 4'd0));
    wire pass_c  = (BUG == 2) ? 1'b1 : !sub_busy;

    assign s_valid = grant_d ? d_valid : (pass_c && c_valid);
    assign s_we    = grant_d ? ((BUG == 9) ? 1'b1 : d_we) : c_we;
    assign s_addr  = (BUG == 5 && grant_d) ? c_addr : grant_d ? d_addr : c_addr;
    assign s_len   = (BUG == 10 && grant_d) ? c_len : grant_d ? d_len : c_len;
    assign c_ready = s_ready && !grant_d && pass_c;
    assign d_ready = s_ready && grant_d;

    wire acc_d  = s_valid && s_ready && grant_d;
    wire acc_c  = s_valid && s_ready && !grant_d;
    wire done_d = s_done && ((BUG == 4) ? 1'b0 : sub_busy);
    wire done_c = s_done && !done_d;

    always @(posedge clk) begin
        if (rst) begin sub_busy <= 1'b0; c_out <= 4'd0; end
        else begin
            if (acc_d) sub_busy <= 1'b1; else if (done_d) sub_busy <= 1'b0;
            c_out <= c_out + {3'd0, acc_c} - {3'd0, done_c};
        end
    end

    wire to_d = (BUG == 3) ? 1'b0 : sub_busy;
    assign c_next  = s_next && !sub_busy;
    assign c_idx   = s_idx;
    assign d_next  = s_next && sub_busy;
    assign d_idx   = s_idx;
    assign s_wdata = to_d ? d_wdata : c_wdata;
    assign s_be    = to_d ? ((BUG == 7) ? 2'b11 : d_be) : c_be;

    assign c_r_valid = s_r_valid && ((BUG == 6) ? 1'b1 : !sub_busy);
    assign c_r_data  = s_r_data;
    assign c_r_last  = s_r_last && ((BUG == 6) ? 1'b1 : !sub_busy);
    assign d_r_valid = s_r_valid && ((BUG == 8) ? 1'b1 : sub_busy);
    assign d_r_data  = s_r_data;
    assign d_r_last  = s_r_last && ((BUG == 8) ? 1'b1 : sub_busy);
    assign c_done    = done_c;
    assign d_done    = done_d;
endmodule

`default_nettype wire
