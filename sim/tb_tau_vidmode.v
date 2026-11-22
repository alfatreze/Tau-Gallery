// ============================================================================
// tb_tau_vidmode.v -- Tau Gallery M0.3: the mode sequencer with both scan-outs (UI 400 x 360 as 1666 x 400 at 40 MHz, viewer 800 x 720 as 900 x 740) and the output mux.
// The sequence UI -> viewer -> UI is requested at arbitrary times; the bench checks, frame by frame: the geometry (active lines and clocks, frame length), the scaler-slot word
// in the RGB word after DE falls on EVERY line (the target slot during the pending frame, the current one otherwise), zero in every other blank clock, no DE together with hs
// or vs, the geometry changing exactly at a frame wrap (a whole frame of the new mode starts at its first clock), and the switch counter. Mutants BUG 1..4 must FAIL.
//   make test-rtl-vidmode
// ============================================================================
`timescale 1ns/1ps
`default_nettype none

module tb_tau_vidmode #(parameter integer BUG = 0);
    reg clk_vid = 0, clk_sys = 0, clk_sdram = 0, reset = 1;
    always #12.5 clk_vid = ~clk_vid;
    always #7.5  clk_sys = ~clk_sys;
    always #5    clk_sdram = ~clk_sdram;

    reg req = 0;
    wire cur_view, pend, tgt_view, hold_ui, hold_v, target_view; wire [23:0] slot_word; wire [15:0] switches;
    wire ui_fend, v_fend;
    wire [23:0] u_rgb, v_rgb; wire u_de, u_hs, u_vs, v_de, v_hs, v_vs; wire [8:0] u_vc; wire [9:0] v_vc; wire [15:0] u_uf, v_uf;
    wire u_ftgl, u_vbl, v_ftgl, v_vbl; wire [9:0] u_fl, v_fl;

    tau_vidmode #(.BUG(BUG)) vm (.clk_vid(clk_vid), .reset(reset), .req_view(req), .ui_fend(ui_fend), .v_fend(v_fend), .cur_view(cur_view), .pend(pend), .tgt_view(tgt_view),
        .hold_ui(hold_ui), .hold_v(hold_v), .slot_word(slot_word), .target_view(target_view), .switches(switches));
    tau_scanout #(.H_TOT(1666), .MODE_EN(1)) ui (.reset(reset), .clk_sys(clk_sys), .clk_sdram(clk_sdram), .clk_vid(clk_vid), .cmd_push(1'b0),
        .lb_we(1'b0), .lb_waddr(10'd0), .lb_wdata(16'd0), .fill_done_inc(1'b0), .fill_done_line(10'd0), .fill_seen(1'b0), .underflow_cnt(u_uf),
        .fill_req_tgl(u_ftgl), .fill_line_req(u_fl), .vblank_tgl(u_vbl), .video_rgb(u_rgb), .video_de(u_de), .video_hs(u_hs), .video_vs(u_vs), .scan_vc(u_vc),
        .hold(hold_ui), .slot_word(slot_word), .frame_end(ui_fend));
    tau_scanout_v vw (.reset(reset), .clk_sys(clk_sys), .clk_sdram(clk_sdram), .clk_vid(clk_vid), .enable(1'b1), .view_dbl(1'b0), .hold(hold_v), .slot_word(slot_word),
        .fx_mode(2'd0), .fx_lut_on(1'b0), .fx_lut_we(1'b0), .fx_lut_word(18'd0), .fx_lut_busy(),
        .lb_we(1'b0), .lb_waddr(12'd0), .lb_wdata(16'd0), .fill_done_line(10'd0), .fill_seen(1'b0), .underflow_cnt(v_uf),
        .fill_req_tgl(v_ftgl), .fill_line_req(v_fl), .vblank_tgl(v_vbl), .video_rgb(v_rgb), .video_de(v_de), .video_hs(v_hs), .video_vs(v_vs), .scan_vc(v_vc), .frame_end(v_fend));

    // the muxed bus, as tau_gfx builds it
    wire [23:0] rgb = cur_view ? v_rgb : u_rgb;
    wire de = cur_view ? v_de : u_de, hs = cur_view ? v_hs : u_hs, vs = cur_view ? v_vs : u_vs;

    // ---- the bench's own model of the sequence (an independent statement of the spec, not a copy of the DUT) ----------------------------------------------------------
    // m_geom: the geometry of the frame in progress (0 UI, 1 viewer), m_slot: the scaler slot every line of it must carry, m_pend / m_tgt: a switch is under way.
    integer cyc = 0, errors = 0, frame = 0, f_start = 0, de_lines = 0, de_clks = 0, line_bad = 0, slot_bad = 0, nz_bad = 0, de_hs = 0, lines_with_slot = 0, view_frames = 0, ui_frames = 0;
    reg de_q = 0, m_geom = 0, m_slot = 0, m_pend = 0, m_tgt = 0;
    reg [2:0] req_m = 0; always @(posedge clk_vid) req_m <= {req_m[1:0], req};     // the request as the sequencer sees it (three clocks late, requests are far from a wrap)
    task fail(input [255:0] msg); begin errors = errors + 1; if (errors < 14) $display("FAIL: %0s (frame %0d, cycle %0d)", msg, frame, cyc); end endtask
    wire fend_bus = cur_view ? v_fend : ui_fend;

    always @(posedge clk_vid) begin
        cyc <= cyc + 1; de_q <= de;
        if (!reset) begin
            if (fend_bus) begin
                if (frame >= 1) begin                                  // the frame that just ended, checked against the model
                    if (m_geom) begin
                        if (de_lines != 720) fail("viewer frame: not 720 active lines");
                        if (cyc - f_start + 1 != 666000) fail("viewer frame: not 666,000 clocks");
                        view_frames = view_frames + 1;
                    end else begin
                        if (de_lines != 360) fail("UI frame: not 360 active lines");
                        if (cyc - f_start + 1 != 666400) fail("UI frame: not 666,400 clocks");
                        ui_frames = ui_frames + 1;
                    end
                    if (line_bad != 0) fail("a line did not have the right number of active clocks");
                    if (lines_with_slot != de_lines) fail("not every line carried the slot word");
                end
                // the model decides what the next frame is
                if (m_pend) begin m_geom = m_tgt; m_pend = 0; m_slot = m_geom; end
                else if (req_m[2] != m_geom) begin m_pend = 1; m_tgt = req_m[2]; m_slot = m_tgt; end
                else m_slot = m_geom;
                frame = frame + 1; f_start = cyc + 1; de_lines = 0; line_bad = 0; lines_with_slot = 0;
            end
            if (de) begin
                if (!de_q) begin de_lines = de_lines + 1; de_clks = 0; end
                de_clks = de_clks + 1;
                if (hs || vs) de_hs = de_hs + 1;
            end
            if (!de && de_q) begin
                if (de_clks != (m_geom ? 800 : 400)) line_bad = line_bad + 1;
                if (rgb === {10'd0, m_slot, 13'd0}) lines_with_slot = lines_with_slot + 1; else slot_bad = slot_bad + 1;
            end else if (!de && rgb !== 24'd0) nz_bad = nz_bad + 1;
        end
    end

    initial begin
        repeat (20) @(posedge clk_vid); reset = 0;
        // frame 0 and 1 in the UI, then request the viewer in the middle of a frame
        repeat (700000) @(posedge clk_vid);
        req = 1;
        wait (switches == 1);
        repeat (700000) @(posedge clk_vid);
        req = 0;
        wait (switches == 2);
        repeat (700000) @(posedge clk_vid);
        if (switches != 2) fail("switch counter");
        if (slot_bad != 0) fail("a slot word was wrong or missing");
        if (nz_bad != 0) fail("a blank clock was not zero");
        if (de_hs != 0) fail("DE together with hs or vs");
        $display("%s  BUG=%0d: frames=%0d (UI %0d, viewer %0d) switches=%0d slot_bad=%0d nz_bad=%0d errors=%0d", errors == 0 ? "PASS" : "FAILED", BUG, frame, ui_frames, view_frames, switches, slot_bad, nz_bad, errors);
        if (errors != 0) $finish(1); else $finish;
    end
    initial begin #400000000; $display("FAILED TIMEOUT"); $finish(1); end
endmodule
