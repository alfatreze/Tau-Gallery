// =============================================================================
// tau_sdram_p2mux.sv -- Tau Gallery M0.4: a secondary WRITE client on the CPU scheduler port (port 2) instead of a fourth scheduler port (docs/M0_DESIGN.md section 6).
//
// The scheduler port speaks the client contract of tau_sdram_sched_a (request valid/ready with address, length and direction; write data PULLED by the port with w_next / w_idx and
// presented two clocks later; read data returned with r_valid / r_data / r_last; one `done` per request, in order). Primary client C = tau_sdram_a_cpu (reads and writes, 32-bit
// accesses), secondary client L = the landing path's burst master (writes only, bursts of up to 16 halfwords).
//
//   L is granted the port only while C offers nothing AND has nothing outstanding; from the moment L's request is accepted until its `done` C is held (ready low), so the pulled data
//   (w_next, w_idx, data two clocks later) belongs to exactly one client. C therefore waits at most one burst. L never reads: all read data belongs to C.
//
// The idea and the structure follow Tau V's `tau_tvc_wmux` (same owner, hardware-proven shape); written here for a port that also reads. Cost in Tau V's probe: about 80 ALMs, no
// RAM, against 345 ALMs for a fourth scheduler port.
//
// BUG is a mutation hook for the testbench (0 in the build): 1 L may start while C has a request outstanding, 2 C is not held while L's request is in flight, 3 write data of C
// presented during L's burst, 4 `done` always goes to C, 5 L's address replaced by C's, 6 C's read data is also routed to L's done, 7 L's byte enables not forced to both bytes (C's are used).
// =============================================================================
`default_nettype none

module tau_sdram_p2mux #(
    parameter integer BUG = 0
) (
    input  wire        clk,
    input  wire        rst,
    // primary client C (tau_sdram_a_cpu)
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
    // secondary client L (the landing path's burst master), writes only
    input  wire        l_valid,
    output wire        l_ready,
    input  wire [24:0] l_addr,
    input  wire [9:0]  l_len,
    output wire        l_next,
    output wire [9:0]  l_idx,
    input  wire [15:0] l_wdata,
    output wire        l_done,
    // scheduler port (port 2 of tau_sdram_sched_a)
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
    reg       sub_busy;                                       // L's request is accepted and not yet done
    reg [3:0] c_out;                                          // C's requests accepted and not yet done

    wire grant_l = !sub_busy && l_valid && !c_valid && ((BUG == 1) ? 1'b1 : (c_out == 4'd0));
    wire pass_c  = (BUG == 2) ? 1'b1 : !sub_busy;

    assign s_valid = grant_l ? l_valid : (pass_c && c_valid);
    assign s_we    = grant_l ? 1'b1 : c_we;
    assign s_addr  = (BUG == 5 && grant_l) ? c_addr : grant_l ? l_addr : c_addr;
    assign s_len   = grant_l ? l_len : c_len;
    assign c_ready = s_ready && !grant_l && pass_c;
    assign l_ready = s_ready && grant_l;

    wire acc_l  = s_valid && s_ready && grant_l;
    wire acc_c  = s_valid && s_ready && !grant_l;
    wire done_l = s_done && ((BUG == 4) ? 1'b0 : sub_busy);
    wire done_c = s_done && !done_l;

    always @(posedge clk) begin
        if (rst) begin sub_busy <= 1'b0; c_out <= 4'd0; end
        else begin
            if (acc_l) sub_busy <= 1'b1; else if (done_l) sub_busy <= 1'b0;
            c_out <= c_out + {3'd0, acc_c} - {3'd0, done_c};
        end
    end

    reg  c_we_q = 1'b0;                                       // BUG 6 bookkeeping: the direction of C's last accepted request
    always @(posedge clk) if (acc_c) c_we_q <= c_we;
    wire to_l = (BUG == 3) ? 1'b0 : sub_busy;
    assign c_next  = s_next && !sub_busy;
    assign c_idx   = s_idx;
    assign l_next  = s_next && sub_busy;
    assign l_idx   = s_idx;
    assign s_wdata = to_l ? l_wdata : c_wdata;
    assign s_be    = (BUG == 7) ? c_be : (to_l ? 2'b11 : c_be);

    assign c_r_valid = s_r_valid && !sub_busy;
    assign c_r_data  = s_r_data;
    assign c_r_last  = s_r_last && !sub_busy;
    assign c_done    = done_c;
    assign l_done    = done_l || (BUG == 6 && done_c && c_we_q);                // BUG 6: a C write's done also reaches L

`ifdef FORMAL
`include "p2mux_props.vh"          // proof-only properties, formal/p2mux_props.vh (not synthesised)
`endif
endmodule

`default_nettype wire
