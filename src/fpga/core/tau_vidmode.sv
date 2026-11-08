// =============================================================================
// tau_vidmode.sv -- Tau Gallery video mode sequencer (M0.3, docs/M0_DESIGN.md section 2): 400 x 360 UI (scaler slot 0) or 800 x 720 viewer (slot 1).
//
// Sequence (the design of the `test/720` branch, B3): a request (clk_sys level, synchronised) is noticed at a frame wrap of the mode on the bus; the next frame is the
// PENDING frame: its geometry is still the old one, but every line carries the target scaler-slot word, which the Pocket applies from the next frame; at the wrap after
// that the other scan-out takes over from its frame start (the one that leaves is held with its counters at 0) and the target mode is the current one. So the geometry
// and the scaler slot change together, at a frame boundary, and the output never shows a partial frame of the new mode.
//
// Outputs: cur_view (the mode on the bus), pend / tgt_view (a switch is under way), hold_ui / hold_v (the scan-out that is not on the bus), slot_word (the end-of-line
// word: {10'b0, slot, 13'b0}: scaler slot in bits [23:13], function 0), switches (completed switches, wraps at 2^16), target_view (the mode the graphics path must be
// in for the next frame: the request while pending, else the current one).
//
// BUG is a mutation hook for the testbench (0 in the build): 1 no pending frame, 2 the slot word shows the current slot while pending, 3 the scan-out that leaves is not
// held, 4 the switch counter never counts.
// =============================================================================
`default_nettype none

module tau_vidmode #(
    parameter integer BUG = 0
) (
    input  wire        clk_vid,
    input  wire        reset,
    input  wire        req_view,          // clk_sys level: 1 = viewer (slot 1), 0 = UI (slot 0)
    input  wire        ui_fend,           // the last clock of a UI frame
    input  wire        v_fend,            // the last clock of a viewer frame
    output reg         cur_view    = 1'b0,
    output reg         pend        = 1'b0,
    output reg         tgt_view    = 1'b0,
    output wire        hold_ui,
    output wire        hold_v,
    output wire [23:0] slot_word,
    output wire        target_view,
    output reg  [15:0] switches    = 16'd0
);
    reg [2:0] req_s = 3'b000;
    always @(posedge clk_vid) req_s <= {req_s[1:0], req_view};
    wire req = req_s[2];

    wire fend = cur_view ? v_fend : ui_fend;

    always @(posedge clk_vid) begin
        if (reset) begin
            cur_view <= 1'b0; pend <= 1'b0; tgt_view <= 1'b0;
        end else if (fend) begin
            if (pend) begin
                cur_view <= tgt_view;
                pend     <= 1'b0;
                if (BUG != 4) switches <= switches + 16'd1;
            end else if (req != cur_view) begin
                if (BUG == 1) begin cur_view <= req; if (BUG != 4) switches <= switches + 16'd1; end   // BUG 1: switches at once, no pending frame
                else begin pend <= 1'b1; tgt_view <= req; end
            end
        end
    end

    assign hold_ui     = (BUG == 3) ? 1'b0 : cur_view;                  // BUG 3: a scan-out that is not on the bus keeps running
    assign hold_v      = (BUG == 3) ? 1'b0 : ~cur_view;
    assign target_view = pend ? tgt_view : cur_view;
    wire   slot_bit    = (BUG == 2) ? cur_view : target_view;
    assign slot_word   = {10'd0, slot_bit, 13'd0};
endmodule

`default_nettype wire
