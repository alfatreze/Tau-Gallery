`timescale 1ns/1ps
// tau_scanout_v: the 800 x 720 packed RGB888 viewer scan-out (M0). Every active pixel of two enabled frames is compared with a golden picture, the first frame must be
// black (enable low), the frame is 666,000 clocks with 720 lines of 800 clocks, the sync positions are the ones Tau V ran on a Pocket, and a slow filler must be counted.
// Parameters: BUG (the DUT mutation, 0 = none), SLOW (1: a filler too slow for the line rate, the picture is not checked, the late counter must count).
module tb_tau_scanout_v #(parameter integer BUG = 0, parameter integer SLOW = 0);
    reg clk_vid = 0, clk_sdram = 0, clk_sys = 0, reset = 1;
    always #12.5 clk_vid = ~clk_vid;       // 40 MHz
    always #5    clk_sdram = ~clk_sdram;   // 100 MHz
    always #7.5  clk_sys = ~clk_sys;       // 66.667 MHz

    reg enable = 1'b0, enable_set = 1'b0, hold = 1'b0;
    reg [1:0] fx_mode = 2'd0; reg fx_lut_on = 1'b0;      // the pixel effects off: the picture must reach the output exactly as the plane holds it
    wire [23:0] slot_word = 24'h002000;                  // scaler slot 1: bits [23:13] = 1, function [2:0] = 0
    wire frame_end;
    wire        lb_we_w;
    reg         lb_we = 0; reg [11:0] lb_waddr = 0; reg [15:0] lb_wdata = 0;
    reg  [9:0]  fill_done_line = 0; reg fill_seen = 0;
    wire [15:0] underflow;
    wire        fill_req_tgl, vblank_tgl; wire [9:0] fill_line_req;
    wire [23:0] rgb; wire de, hs, vs; wire [9:0] scan_vc;

    tau_scanout_v #(.BUG(BUG)) dut (
        .reset(reset), .clk_sys(clk_sys), .clk_sdram(clk_sdram), .clk_vid(clk_vid), .enable(enable), .hold(hold), .slot_word(slot_word),
        .fx_mode(fx_mode), .fx_lut_on(fx_lut_on), .fx_lut_we(1'b0), .fx_lut_word(18'd0), .fx_lut_busy(),
        .lb_we(lb_we), .lb_waddr(lb_waddr), .lb_wdata(lb_wdata), .fill_done_line(fill_done_line), .fill_seen(fill_seen),
        .underflow_cnt(underflow), .fill_req_tgl(fill_req_tgl), .fill_line_req(fill_line_req), .vblank_tgl(vblank_tgl),
        .video_rgb(rgb), .video_de(de), .video_hs(hs), .video_vs(vs), .scan_vc(scan_vc), .frame_end(frame_end));

    // ---- the golden picture ----------------------------------------------
    function [23:0] pix(input integer x, input integer y);
        reg [31:0] h;
        begin
            h = x * 32'd2654435761 + y * 32'd40503 + 32'd12345;
            h = h ^ (h >> 13); h = h * 32'd1274126177; h = h ^ (h >> 16);
            pix = h[23:0];
        end
    endfunction
    function [7:0] pbyte(input integer y, input integer k);     // byte k of plane line y: pixel k/3, component k%3 in the order R, G, B
        reg [23:0] p;
        begin
            p = pix(k / 3, y);
            pbyte = (k % 3 == 0) ? p[23:16] : (k % 3 == 1) ? p[15:8] : p[7:0];
        end
    endfunction

    // ---- the filler (what tau_talos will do): 1,200 words per requested line, GAP clk_sdram clocks per word ----
    localparam integer GAP = SLOW ? 4 : 1;
    reg [2:0] req_s = 0; reg busy = 0, pend = 0; reg [9:0] cur_line = 0, want_line = 0; integer cnt = 0, gap = 0;
    always @(posedge clk_sdram) begin
        req_s <= {req_s[1:0], fill_req_tgl};
        lb_we <= 1'b0;
        if (req_s[2] ^ req_s[1]) begin pend <= 1'b1; want_line <= fill_line_req; end
        if (!busy) begin
            if (pend) begin busy <= 1'b1; pend <= 1'b0; cur_line <= want_line; cnt <= 0; gap <= 0; end
        end else begin
            if (gap > 0) gap <= gap - 1;
            else if (cnt < 1200) begin
                lb_we <= 1'b1; lb_waddr <= {cur_line[0], cnt[10:0]};
                lb_wdata <= {pbyte(cur_line, 2 * cnt + 1), pbyte(cur_line, 2 * cnt)};
                cnt <= cnt + 1; gap <= GAP - 1;
            end else begin
                fill_done_line <= cur_line; fill_seen <= 1'b1; busy <= 1'b0;
            end
        end
    end

    // ---- the checker ---------------------------------------------------------
    integer cyc = 0, errors = 0, frame = 0;
    reg de_q = 0, hs_q = 0, vs_q = 0, tgl_q = 0;
    integer x = 0, y = -1, de_clks = 0, de_lines = 0, hs_len = 0, hs_between = 0, vs_hs = 0, last_de_fall = 0, last_vs_rise = 0, vb_cycle = 0, black_bad = 0, pix_bad = 0, slot_bad = 0, nz_bad = 0, frame_clks = 0;
    integer hs_after_vs = 0, hs_before_vs = 0, first_de_seen = 0, vs_pulses = 0;
    reg in_vs = 0, hs_pos_chk = 0;
    task fail(input [255:0] msg); begin errors = errors + 1; if (errors < 12) $display("FAIL: %0s (frame %0d, cycle %0d)", msg, frame, cyc); end endtask

    always @(posedge clk_vid) begin
        cyc <= cyc + 1;
        de_q <= de; hs_q <= hs; vs_q <= vs; tgl_q <= vblank_tgl;
        if (vblank_tgl != tgl_q) vb_cycle <= cyc;
        // sideband: the scaler-slot word in the first clock after DE falls, zero in every other blank clock
        if (!de) begin
            if (de_q) begin if (rgb !== slot_word) slot_bad = slot_bad + 1; end
            else if (rgb !== 24'd0) nz_bad = nz_bad + 1;
        end
        // pixels
        if (de) begin
            if (!de_q) begin y = y + 1; x = 0; de_lines = de_lines + 1; end
            if (frame == 0) begin if (rgb !== 24'd0) black_bad = black_bad + 1; end
            else if (!SLOW) begin if (rgb !== pix(x, y)) pix_bad = pix_bad + 1; end
            x = x + 1;
            if (x > 800) fail("more than 800 active clocks in a line");
        end
        if (!de && de_q) begin
            if (x != 800) fail("active line is not 800 clocks");
            last_de_fall = cyc; hs_between = 0; hs_pos_chk = 1;
        end
        // horizontal sync: width and position (24 clocks after the end of the active line, 840 - 816)
        if (hs && !hs_q) begin
            hs_len = 1; hs_between = hs_between + 1;
            if (in_vs) vs_pulses = vs_pulses + 1;
            if (hs_pos_chk) begin hs_pos_chk = 0; if (cyc - last_de_fall != 24) fail("hs not 24 clocks after the end of the active line"); end
        end
        if (hs && hs_q) hs_len = hs_len + 1;
        if (!hs && hs_q && hs_len != 40) fail("hs is not 40 clocks wide");
        // vertical sync: 4 lines, 7 hs pulses after the last active line, 10 hs pulses before the next one, vblank toggle one clock ahead
        if (vs && !vs_q) begin
            in_vs = 1; vs_pulses = 0;
            if (frame > 0 || y == 719) begin
                if (hs_between != 7) fail("vs does not start 7 line times after the last active line");
                if (vb_cycle != cyc - 1) fail("vblank toggle is not one clock before vs");
            end
            if (last_vs_rise != 0) begin
                frame_clks = cyc - last_vs_rise;
                if (frame_clks != 666000) fail("frame is not 666,000 clocks");
            end
            last_vs_rise <= cyc; hs_between = 0;
            if (frame > 0 && de_lines != 720) fail("frame did not have 720 active lines");
            de_lines = 0; y = -1; frame = frame + 1;
            if (frame == 1) enable_set = 1;
        end
        if (!vs && vs_q) begin
            in_vs = 0; hs_after_vs = 0;
            if (vs_pulses != 4) fail("vs is not 4 lines");
        end
        if (!vs && hs && !hs_q && !in_vs) hs_after_vs = hs_after_vs + 1;
        if (de && !de_q && frame > 0 && y == 0 && hs_after_vs != 10) fail("first active line is not 10 hs pulses after vs");
    end

    always @(posedge clk_sys) if (enable_set) enable <= 1'b1;

    initial begin
        repeat (20) @(posedge clk_vid); reset = 0;
        wait (frame == 3); @(negedge vs); repeat (200) @(posedge clk_vid);
        if (SLOW) begin
            if (underflow == 0) fail("slow filler: no late line was counted");
        end else begin
            if (underflow != 0) fail("fast filler: a late line was counted");
            if (black_bad != 0) fail("frame 0 was not black with enable low");
            if (slot_bad != 0) fail("the slot word is missing after DE falls");
            if (nz_bad != 0) fail("a blank clock is not zero");
            if (pix_bad != 0) fail("pixels differ from the golden picture");
        end
        $display("%s  BUG=%0d SLOW=%0d: black_bad=%0d pix_bad=%0d slot_bad=%0d nz_bad=%0d late=%0d errors=%0d", errors == 0 ? "PASS" : "FAILED", BUG, SLOW, black_bad, pix_bad, slot_bad, nz_bad, underflow, errors);
        if (errors != 0) $finish(1); else $finish;
    end
    initial begin #400000000; $display("TIMEOUT"); $finish(1); end
endmodule
