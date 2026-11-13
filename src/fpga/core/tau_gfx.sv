// ============================================================================
// tau_gfx.sv -- the graphics path top (Talos 2, T2, D-T02): joins tau_talos (command FIFO, draw
// engine, SDRAM master port, fonts, CLUT) and tau_scanout (video timing, line buffer). It
// replaces mp3_fb.sv with the identical port list, so core_game.vh changes only the module
// name. No behaviour change: sim/tb_tau_gfx_diff.v compares it with the preserved original
// (sim/oracle/tau_talos1_oracle.sv) cycle by cycle.
// ============================================================================

`default_nettype none

module tau_gfx #(
    // Mutation-test hook only (sim/tb_mp3_fb.v's -PBUG_IGNORE_BLIT_STRIDE=1 target
    // in make test-rtl-fb-mutation): 1 makes OP_BLIT step by the fixed 512 stride
    // COPY uses instead of the sticky blt_*_stride registers, reproducing the exact
    // hardcoded-stride bug B1 exists to fix. Never set outside that test.
    parameter BUG_IGNORE_BLIT_STRIDE = 0,
    // Mutation-test hook only (-PBUG_IGNORE_KEY=1, make test-rtl-fb-mutation): 1
    // disables B2's colour-key compare in A_COPYRD, so a keyed source pixel
    // always overwrites the destination instead of being dropped. Never set
    // outside that test.
    parameter BUG_IGNORE_KEY = 0,
    // Mutation-test hook only (-PBUG_SBLIT_NO_SCALE=1, make test-rtl-fb-mutation):
    // 1 forces B4's X-Bresenham to advance the source column on every output
    // pixel, ignoring char_num/char_den -- a naive 1:1 copy instead of a real
    // scaled blit. Never set outside that test.
    parameter BUG_SBLIT_NO_SCALE = 0,
    // Phase F B5, per PHASE_F_SPEC.md section 10's build plan: kept behind its
    // own parameter, separate from BLIT_ENABLE, specifically so it is droppable
    // on its own if it tips the compose pipeline into the documented -1.888 ns
    // timing cliff -- the rest of the blit engine (B1/B2/B4/B6) must not have
    // to come out with it. Inert (blend logic provably dead, same convention
    // as every other *_ENABLE parameter here) when 0.
    parameter BLIT_BLEND_ENABLE = 0,
    // Mutation-test hook only (-PBUG_BLEND_ALWAYS_SRC=1, make test-rtl-fb-mutation):
    // 1 makes a blended blit silently behave like a plain one (always write the
    // source pixel, ignoring blend_active entirely). Never set outside that test.
    parameter BUG_BLEND_ALWAYS_SRC = 0,
    // Mutation-test hook only (-PBUG_CBLIT_NO_LOOKUP=1, make test-rtl-fb-mutation):
    // 1 makes OP_CBLIT write the raw palette INDEX (zero-extended) instead of
    // looking it up in the CLUT -- proves the test actually exercises the CLUT
    // mechanism, not just that some value lands at the right address. Never set
    // outside that test.
    parameter BUG_CBLIT_NO_LOOKUP = 0,
    // Mutation-test hook only (-PBUG_IGNORE_REINDEX=1, make test-rtl-fb-mutation):
    // 1 forces the CLUT read address to the raw source index always, ignoring
    // the sticky blt_reindex offset -- proves B9's own test actually exercises
    // the offset add, not just that a CBLIT with reindex=0 still works. Never
    // set outside that test.
    parameter BUG_IGNORE_REINDEX = 0,
    // Mutation-test hook only (-PBUG_IGNORE_TEXT_LIGHT=1, make test-rtl-fb-mutation): 1 ignores
    // the text_light input and always uses the dark-polarity weight table -- proves the scene test
    // really exercises the second table. Never set outside that test.
    parameter BUG_IGNORE_TEXT_LIGHT = 0,
    // Mutation-test hook only (-PBUG_IGNORE_RC_CUT=1, make test-rtl-fb-mutation): 1 forces
    // rrect_cut to always read 0, degenerating B11's rounded-rect into a square rect (the LUT
    // mechanism is never actually exercised) -- proves the test catches a corner-cut table that
    // silently does nothing, not just that an RRECT with a real table still draws a fill. Never
    // set outside that test.
    parameter BUG_IGNORE_RC_CUT = 0,
    // Mutation-test hooks only (make test-rtl-fb-mutation): see tau_talos.sv (OP_CORNERS). Never set outside that test.
    parameter BUG_CORNERS_FILL = 0,
    parameter BUG_CORNERS_BG = 0,
    // Mutation-test hook only (-PBUG_IGNORE_BAR_HI=1, make test-rtl-fb-mutation): 1 forces
    // pre_bar_lit_raw's high 2 bits (cmd_glyph_hi) to always read 0, degenerating BAR's lit-row
    // count back to its old 7-bit-only behaviour -- proves the test actually exercises a lit count
    // above 127, not just that BAR with a small lit count still draws correctly. Never set outside
    // that test.
    parameter BUG_IGNORE_BAR_HI = 0,
    // Helios H2 (docs/HELIOS_SPEC.md section 5, B-340): double buffering via base-pointer swap.
    // Inert (dbuf_addr() is a no-op, byte-identical to today) when 0.
    parameter DBUF_ENABLE = 0,
    // Writable font memory (docs/features/FONT_SPEC.md, TAU_FONT_RAM): the glyph atlas is a RAM the CPU
    // loads (Latin-1, 6,112 words, 13-bit address) instead of the 3,040-word ASCII ROM. Inert (the old ROM,
    // byte-identical behaviour) when 0. FONT_BUG_* are mutation hooks for sim/tb_font_ram.v.
    parameter FONT_RAM_ENABLE = 0,
    parameter BUG_FONT_NO_HI = 0,        // 1: glyph bit 7 (cmd_glyph_hi[0]) ignored -> 0x80.. codes read the ASCII range
    parameter BUG_FONT_RANGE = 0,        // 1: the 0xA0.. range starts one glyph too high
    parameter BUG_FONT_NO_WRITE = 0,     // 1: the CPU write port is ignored
    parameter BUG_FENCE_ANY = 0,       // mutation hook (sim/tb_tau_gfx_t3.v): see tau_talos.sv
    parameter BUG_POP_STALE = 0,       // 1: the original command-pop race (sim/tb_tau_gfx_diff.v, tb_tau_gfx_t3.v MUT 5 only): see tau_talos.sv
    parameter CBLIT_BURST = 1,         // Talos 2 T3b: a CBLIT source row in one burst (0 = the per-pixel path); see tau_talos.sv
    parameter BUG_CBLIT_EARLY = 0, BUG_CBLIT_SHIFT = 0,  // mutation hooks (sim/tb_tau_gfx_cblit.v)
    parameter FILL_WORDS = 400,        // Talos 2 T3b: words read per scanout fill (512 = the old behaviour); see tau_talos.sv
    parameter BUG_VIEW = 0,            // mutation hook of the viewer fill (sim/tb_tau_gfx_view.v), see tau_talos.sv
    parameter VIEW_ENABLE = 0          // Tau Gallery M0.2: 1 adds the 800 x 720 RGB888 viewer (tau_scanout_v) beside the 400 x 360 UI; 0 = exactly the old module
) (
    input  wire        reset,
    input  wire        clk_sys,     // CPU / FIFO write domain
    input  wire        clk_sdram,   // SDRAM controller + engine (~100 MHz)
    input  wire        clk_vid,     // pixel clock (12 MHz)

    // CPU draw command (clk_sys domain) -------------------------------------
    input  wire        cmd_push,
    input  wire [3:0]  cmd_op,      // 0=RUN 1=RECT 2=CHAR 3=COPY 4=BLIT 5=BAR 6=SBLIT 7=CBLIT 8=RRECT (B11)
    input  wire [18:0] cmd_addr,    // word address of top-left, y*512+x
    input  wire [8:0]  cmd_w,       // RUN: run length; RECT: width (words)
    input  wire [8:0]  cmd_h,       // RECT: height (rows)
    input  wire [15:0] cmd_fg,      // RGB565 fill / glyph foreground
    input  wire [15:0] cmd_bg,      // RGB565 glyph background
    input  wire [6:0]  cmd_glyph,   // CHAR: ASCII code; BAR: lit-row count, LOW 7 bits (see cmd_glyph_hi)
    input  wire [1:0]  cmd_sx,      // CHAR: h scale 0=1x 1=1.5x 2=2x 3=3x
    input  wire [1:0]  cmd_sy,      // CHAR: v scale, same encoding
    // B12 (docs/AUDIT_TRAIL.md B-454): BAR's lit-row count widened 7->9 bits (max 127 -> 511), enough
    // for the tallest fullscreen bar (FS_FIG_H=323). A NEW, separate field rather than widening
    // cmd_glyph itself -- CHAR (ASCII, 7 bits is exactly right) and OP_RRECT (radius, clamped to 15)
    // both keep reading cmd_glyph's original 7-bit [14:8] slice completely unchanged; only BAR's own
    // lit-count math additionally consults this. Claims 2 of the FIFO word's 4 remaining padding bits
    // (CW stays 88) and 2 of R_FB_GO's genuinely-unused bits (15:16) at the tau_soc.v boundary -- see
    // that file's own comment. On any bitstream without this port at all (old RTL), the write simply
    // has nowhere to land; BAR_WIDE_READY() (fw/blit_probe.inc) is the runtime test that tells firmware
    // which behaviour it is actually talking to, so old and new bitstreams both behave safely.
    input  wire [1:0]  cmd_glyph_hi,
    // Writable font memory, clk_sys domain (tau_soc.v registers FONT_IDX/FONT_DATA); ignored when FONT_RAM_ENABLE = 0.
    input  wire        font_we,
    input  wire [12:0] font_waddr,
    input  wire [31:0] font_wdata,
    output wire        cmd_full,
    // Theme step 0b/gamma (docs/THEME_SPEC.md, tools/gen_text_gamma.py): 0 = the text weight table
    // fitted for light text on a dark ramp (cov_weight), 1 = the table fitted for dark text on a
    // light ramp (cov_weight_light). Already in the clk_sdram domain (the caller synchronises it);
    // quasi-static, changed only by the theme switch which repaints everything afterwards.
    input  wire        text_light,

    // Phase F B1 (section 9): sticky blit addressing state, from tau_soc.v's
    // R_BLT_IDX/R_BLT_DATA registers. Read only by OP_BLIT below; RUN/RECT/
    // CHAR/COPY are entirely unaffected (unchanged FB_BASE/512-stride math).
    input  wire [24:0] blt_src_base,
    input  wire [9:0]  blt_src_stride,
    input  wire [24:0] blt_dst_base,
    input  wire [9:0]  blt_dst_stride,
    // Phase F B2: colour-key transparency. Read only when blit_mode (OP_BLIT);
    // OP_COPY is untouched and never keys, matching B1's own precedent of
    // leaving COPY as the simple, unmodified case.
    input  wire        blt_key_en,
    input  wire [15:0] blt_key,
    // Phase F B5: alpha blend. Also read only when blit_mode; also shares B2's
    // destination pre-read phase (A_KEYDST), generalised below to trigger on
    // "blt_key_en OR blt_blend_en" instead of key alone.
    input  wire        blt_blend_en,
    input  wire [2:0]  blt_blend_mode,   // 0=DSP alpha, 1-4=PSX shift-add ratios
    input  wire [7:0]  blt_blend_alpha,  // DSP mode only, 0-255

    // B11 (section 5): the corner-cut lookup table, loaded by the CPU (clk_sys) through
    // tau_soc.v's R_RC_IDX/R_RC_DATA -- a bulk table load, own register pair (not folded into
    // R_BLT_IDX/DATA, same reasoning B8's own CLUT-vs-sticky-field split already gives). 16
    // entries x 5 bits, indexed by dy = r - row (never by the row itself -- see the module-header
    // comment on OP_RRECT). Plain flops (80 bits, no M10K), informal CDC: CPU-paced configuration
    // loaded once per radius change, not sampled every cycle, same precedent blt_src_base etc.
    // already use.
    input  wire [79:0] rc_cut_lut,

    // Phase F B9 (section 5, Tier 2): palette re-index for dim/highlight. An
    // 8-bit offset added to OP_CBLIT's palette index before the CLUT lookup --
    // the Genesis shadow/highlight trick (re-index, don't blend). Read only
    // when cblit_mode; every other opcode is unaffected. Default 0 is a true
    // no-op (index + 0 = index), so this needs no separate enable bit the way
    // B2/B5 do.
    input  wire [7:0]  blt_reindex,

    // Phase F B8 (section 5, "B8 detailed design"): 256-entry CLUT, loaded by
    // the CPU (clk_sys) through tau_soc.v's R_CLUT_IDX/R_CLUT_DATA. Independent
    // write (clk_sys) / read (clk_sdram) ports, a standard dual-clock M10K --
    // no synchronizer needed for the block RAM itself, and the write side is
    // CPU-paced configuration (a palette load, not a value sampled every
    // cycle), the same informal-CDC precedent blt_src_base etc. already use.
    input  wire        clut_wr,
    input  wire [7:0]  clut_waddr,
    input  wire [15:0] clut_wdata,

    // Helios H2 (B-340): double buffering. `dbuf_cpu_buf` is a plain port, but it is read on EVERY
    // ordinary (non-blit) dispatch -- as frequent as `text_light` -- so, like `text_light`, it arrives
    // here already synchronised into clk_sdram by the caller (`tau_cdc_sync1`, core_game.vh), never a
    // raw clk_sys level. It selects which physical copy of the VISIBLE framebuffer (row < V_ACT, i.e.
    // word address < DBUF_VISIBLE_WORDS) ordinary RECT/CHAR/COPY(non-blit) commands read and write;
    // every blit-mode opcode (BLIT/BAR/SBLIT/CBLIT/RRECT) already has fully firmware-programmable
    // addressing via the sticky blt_*_base fields and is untouched by this, and any address
    // >= DBUF_VISIBLE_WORDS (the packed off-screen stash region: art/thumbnail/Chladni/TIM1/probe rows)
    // is untouched regardless of dbuf_cpu_buf -- see the module-header note on why an unconditional
    // offset would corrupt that region. `dbuf_flip_req_tgl` toggles once per requested flip (clk_sys);
    // the flip itself is only ever applied during vertical blanking (clk_vid's own vs_pulse window),
    // synchronised into clk_sdram the same toggle-and-sample way `fill_req_tgl` already is below --
    // never a raw level crossing a clock domain. `dbuf_disp_buf`/`dbuf_flip_pending` are read back by
    // tau_soc for its status register; both are clk_sdram registers, informally sampled by clk_sys the
    // same way blt_* fields are sampled the other direction (a status bit changing at most once every
    // ~16.7 ms is not a hazard worth a formal synchroniser).
    input  wire        dbuf_cpu_buf,
    input  wire        dbuf_flip_req_tgl,
    output wire        dbuf_disp_buf,
    output wire        dbuf_flip_pending,

    // SDRAM master port (clk_sdram) -> wired to tau_sdram_ctrl in core_game.vh ----
    input  wire        sdram_init_complete,
    output wire  [24:0] p0_addr,
    output wire  [15:0] p0_data,
    output wire  [1:0]  p0_byte_en,
    output wire  [10:0] p0_wr_len,
    output wire         p0_wr_stream,
    input  wire [15:0] p0_q,
    output wire         p0_wr_req,
    output wire         p0_rd_req,
    output wire         p0_end_burst_req,
    input  wire        p0_available,
    input  wire        p0_ready,
    input  wire        p0_data_available,

    // Streaming-write source port: tau_sdram_ctrl pulls one word per burst beat from
    // here when p0_wr_stream was set on the request. Used by CHAR, whose rows
    // are varying-colour (a constant-data burst cannot express a glyph).
    input  wire [10:0] wsrc_addr,
    output wire [15:0] wsrc_q,
    // Sideband for tau_sdram_sched_a (see tau_talos.sv); open on the old path
    output wire        p0_is_fill,
    output wire [9:0]  p0_rd_len,

    // Video output (clk_vid domain) ------------------------------------------
    output wire  [23:0] video_rgb,
    output wire         video_de,
    output wire         video_hs,
    output wire         video_vs,
    // Helios beam position (B-267): the video line counter, 0..V_TOT-1 (V_TOT is 400, so 9 bits). The row being scanned
    // is vc - VOFF while VOFF <= vc < VOFF + V_ACT. Carried to the CPU by tau_cdc_gray_bus (it steps by +1 per line).
    output wire [8:0]  scan_vc,

    // Talos 2 T3 status (all clk_sys), read by tau_soc: see docs/features/TALOS_T3_SPEC.md
    output wire        gfx_idle,        // every accepted command has retired
    output wire [31:0] gfx_fence,       // FENCE commands retired (monotonic, wraps at 2^32)
    output wire [8:0]  gfx_free,        // free command-queue slots
    output wire [15:0] gfx_ovf,         // pushes dropped because the queue was full (saturating)
    output wire [15:0] gfx_underflow,   // scanout lines whose fill was late (saturating counter of the Gray count, wraps at 2^16)

    // Tau Gallery viewer (VIEW_ENABLE; ignored and may be left open when it is 0): see tau_talos.sv and docs/M0_DESIGN.md
    input  wire        view_en,         // clk_sys level: the requested mode, 1 = 800 x 720 viewer (scaler slot 1), 0 = 400 x 360 UI (slot 0); see tau_vidmode.sv
    input  wire [24:0] view_base,       // word address of plane line 0
    input  wire [15:0] view_stride,     // words per plane line
    input  wire [1:0]  pix_mode,        // clk_sys: the viewer's pixel effects (tau_pixfx): 0 off, 1 ordered dither, 2 ordered + temporal
    input  wire        pix_lut_on,      // clk_sys: the viewer's colour tables on
    input  wire        pix_lut_we,      // clk_sys: a colour table store strobe
    input  wire [17:0] pix_lut_word,    // clk_sys: {channel, index, value}
    output wire        pix_lut_busy,    // clk_sys: a store is under way
    output wire        vid_cur,         // clk_sys (synchronised): the mode on the bus
    output wire        vid_pend,        // clk_sys (synchronised): a switch is under way (the pending frame)
    output wire [15:0] vid_switches     // clk_sys: completed mode switches (wraps)
);

    wire        u_fill_req_tgl, u_vblank_tgl;
    wire [9:0]  u_fill_line_req;
    wire        fill_req_tgl, vblank_tgl;
    wire [9:0]  fill_line_req;
    wire        fill_done_inc, fill_seen;
    wire [9:0]  fill_done_line;
    wire        vlb_we, view_active_w, view_target;
    wire [11:0] vlb_waddr;
    wire [15:0] vlb_wdata;
    wire        lb_we;
    wire [9:0]  lb_waddr;
    wire [15:0] lb_wdata;

    tau_talos #(
        .BUG_IGNORE_BLIT_STRIDE(BUG_IGNORE_BLIT_STRIDE),
        .BUG_IGNORE_KEY(BUG_IGNORE_KEY),
        .BUG_SBLIT_NO_SCALE(BUG_SBLIT_NO_SCALE),
        .BLIT_BLEND_ENABLE(BLIT_BLEND_ENABLE),
        .BUG_BLEND_ALWAYS_SRC(BUG_BLEND_ALWAYS_SRC),
        .BUG_CBLIT_NO_LOOKUP(BUG_CBLIT_NO_LOOKUP),
        .BUG_IGNORE_REINDEX(BUG_IGNORE_REINDEX),
        .BUG_IGNORE_TEXT_LIGHT(BUG_IGNORE_TEXT_LIGHT),
        .BUG_IGNORE_RC_CUT(BUG_IGNORE_RC_CUT),
        .BUG_CORNERS_FILL(BUG_CORNERS_FILL),
        .BUG_CORNERS_BG(BUG_CORNERS_BG),
        .BUG_IGNORE_BAR_HI(BUG_IGNORE_BAR_HI),
        .DBUF_ENABLE(DBUF_ENABLE),
        .FONT_RAM_ENABLE(FONT_RAM_ENABLE),
        .BUG_FONT_NO_HI(BUG_FONT_NO_HI),
        .BUG_FONT_RANGE(BUG_FONT_RANGE),
        .BUG_FONT_NO_WRITE(BUG_FONT_NO_WRITE),
        .BUG_FENCE_ANY(BUG_FENCE_ANY),
        .BUG_POP_STALE(BUG_POP_STALE),
        .CBLIT_BURST(CBLIT_BURST), .BUG_CBLIT_EARLY(BUG_CBLIT_EARLY), .BUG_CBLIT_SHIFT(BUG_CBLIT_SHIFT), .FILL_WORDS(FILL_WORDS), .VIEW_ENABLE(VIEW_ENABLE), .BUG_VIEW(BUG_VIEW)
    ) u_talos (
        .reset(reset), .clk_sys(clk_sys), .clk_sdram(clk_sdram), .clk_vid(clk_vid),
        .cmd_push(cmd_push),
        .cmd_op(cmd_op),
        .cmd_addr(cmd_addr),
        .cmd_w(cmd_w),
        .cmd_h(cmd_h),
        .cmd_fg(cmd_fg),
        .cmd_bg(cmd_bg),
        .cmd_glyph(cmd_glyph),
        .cmd_sx(cmd_sx),
        .cmd_sy(cmd_sy),
        .cmd_glyph_hi(cmd_glyph_hi),
        .font_we(font_we),
        .font_waddr(font_waddr),
        .font_wdata(font_wdata),
        .cmd_full(cmd_full),
        .text_light(text_light),
        .blt_src_base(blt_src_base),
        .blt_src_stride(blt_src_stride),
        .blt_dst_base(blt_dst_base),
        .blt_dst_stride(blt_dst_stride),
        .blt_key_en(blt_key_en),
        .blt_key(blt_key),
        .blt_blend_en(blt_blend_en),
        .blt_blend_mode(blt_blend_mode),
        .blt_blend_alpha(blt_blend_alpha),
        .rc_cut_lut(rc_cut_lut),
        .blt_reindex(blt_reindex),
        .clut_wr(clut_wr),
        .clut_waddr(clut_waddr),
        .clut_wdata(clut_wdata),
        .dbuf_cpu_buf(dbuf_cpu_buf),
        .dbuf_flip_req_tgl(dbuf_flip_req_tgl),
        .dbuf_disp_buf(dbuf_disp_buf),
        .dbuf_flip_pending(dbuf_flip_pending),
        .sdram_init_complete(sdram_init_complete),
        .p0_addr(p0_addr),
        .p0_data(p0_data),
        .p0_byte_en(p0_byte_en),
        .p0_wr_len(p0_wr_len),
        .p0_wr_stream(p0_wr_stream),
        .p0_q(p0_q),
        .p0_wr_req(p0_wr_req),
        .p0_rd_req(p0_rd_req),
        .p0_end_burst_req(p0_end_burst_req),
        .p0_available(p0_available),
        .p0_ready(p0_ready),
        .p0_data_available(p0_data_available),
        .wsrc_addr(wsrc_addr),
        .wsrc_q(wsrc_q),
        .p0_is_fill(p0_is_fill), .p0_rd_len(p0_rd_len),
        .fill_req_tgl(fill_req_tgl), .fill_line_req(fill_line_req), .vblank_tgl(vblank_tgl),
        .lb_we(lb_we), .lb_waddr(lb_waddr), .lb_wdata(lb_wdata),
        .fill_done_inc(fill_done_inc), .fill_done_line(fill_done_line), .fill_seen(fill_seen),
        .gfx_idle(gfx_idle), .gfx_fence(gfx_fence), .gfx_free(gfx_free), .gfx_ovf(gfx_ovf),
        .view_en(view_target), .view_base(view_base), .view_stride(view_stride),
        .vlb_we(vlb_we), .vlb_waddr(vlb_waddr), .vlb_wdata(vlb_wdata), .view_active(view_active_w)
    );

    wire [23:0] u_rgb; wire u_de, u_hs, u_vs; wire [8:0] u_vc; wire [15:0] u_uf;
    wire        u_hold, u_fend;
    wire [23:0] slot_w;

    tau_scanout #(.H_TOT((VIEW_ENABLE != 0) ? 1666 : 500), .MODE_EN(VIEW_ENABLE)) u_scanout (
        .reset(reset), .clk_sys(clk_sys), .clk_sdram(clk_sdram), .clk_vid(clk_vid),
        .cmd_push(cmd_push),
        .lb_we(lb_we), .lb_waddr(lb_waddr), .lb_wdata(lb_wdata),
        .fill_done_inc(fill_done_inc), .fill_done_line(fill_done_line), .fill_seen(fill_seen),
        .underflow_cnt(u_uf),
        .fill_req_tgl(u_fill_req_tgl), .fill_line_req(u_fill_line_req), .vblank_tgl(u_vblank_tgl),
        .video_rgb(u_rgb), .video_de(u_de), .video_hs(u_hs), .video_vs(u_vs),
        .scan_vc(u_vc),
        .hold(u_hold), .slot_word(slot_w), .frame_end(u_fend)
    );

    generate
        if (VIEW_ENABLE != 0) begin : g_view
            // The viewer scan-out (tau_scanout_v) beside the UI one, both on the 40 MHz pixel clock (UI as 1666 x 400, viewer as 900 x 740). tau_vidmode picks the one on the
            // bus: at a frame wrap the other takes over from its first clock, and the scaler-slot word goes out on every line of the frame before (docs/M0_DESIGN.md section 2).
            wire        v_fill_req_tgl, v_vblank_tgl, v_fend, v_hold;
            wire [9:0]  v_fill_line_req, v_vc;
            wire [23:0] v_rgb; wire v_de, v_hs, v_vs; wire [15:0] v_uf;
            wire        cur, pend, tgt, target;
            wire [15:0] sw;
            tau_vidmode u_mode (.clk_vid(clk_vid), .reset(reset), .req_view(view_en), .ui_fend(u_fend), .v_fend(v_fend),
                .cur_view(cur), .pend(pend), .tgt_view(tgt), .hold_ui(u_hold), .hold_v(v_hold), .slot_word(slot_w), .target_view(target), .switches(sw));
            tau_scanout_v u_scanout_v (
                .reset(reset), .clk_sys(clk_sys), .clk_sdram(clk_sdram), .clk_vid(clk_vid),
                .enable(view_en), .hold(v_hold), .slot_word(slot_w),
                .fx_mode(pix_mode), .fx_lut_on(pix_lut_on), .fx_lut_we(pix_lut_we), .fx_lut_word(pix_lut_word), .fx_lut_busy(pix_lut_busy),
                .lb_we(vlb_we), .lb_waddr(vlb_waddr), .lb_wdata(vlb_wdata),
                .fill_done_line(fill_done_line), .fill_seen(fill_seen),
                .underflow_cnt(v_uf),
                .fill_req_tgl(v_fill_req_tgl), .fill_line_req(v_fill_line_req), .vblank_tgl(v_vblank_tgl),
                .video_rgb(v_rgb), .video_de(v_de), .video_hs(v_hs), .video_vs(v_vs), .scan_vc(v_vc), .frame_end(v_fend)
            );
            // Talos takes the mode of the NEXT frame (the target while a switch is pending) at the vblank edge of the frame in progress, so the fill engine is in the new mode when
            // the new frame's first request arrives. The level crosses clk_vid -> clk_sdram through two flops (quasi-static: it only changes at a frame wrap).
            reg [1:0] tgt_s = 2'b00;
            always @(posedge clk_sdram) tgt_s <= {tgt_s[0], target};
            assign view_target = tgt_s[1];
            reg [1:0] cur_s = 2'b00, pend_s = 2'b00;
            always @(posedge clk_sys) begin cur_s <= {cur_s[0], cur}; pend_s <= {pend_s[0], pend}; end
            assign vid_cur  = cur_s[1];
            assign vid_pend = pend_s[1];
            tau_cdc_gray_bus #(.W(16), .STAGES(3)) u_sw_cdc (.clk_src(clk_vid), .d_src(sw), .clk_dst(clk_sys), .q_dst(vid_switches));
            assign fill_req_tgl  = cur ? v_fill_req_tgl  : u_fill_req_tgl;
            assign fill_line_req = cur ? v_fill_line_req : u_fill_line_req;
            assign vblank_tgl    = cur ? v_vblank_tgl    : u_vblank_tgl;
            assign video_rgb     = cur ? v_rgb : u_rgb;
            assign video_de      = cur ? v_de  : u_de;
            assign video_hs      = cur ? v_hs  : u_hs;
            assign video_vs      = cur ? v_vs  : u_vs;
            assign scan_vc       = cur ? v_vc[8:0] : u_vc;
            assign gfx_underflow = cur_s[1] ? v_uf : u_uf;
        end else begin : g_noview
            assign view_target   = 1'b0;
            assign pix_lut_busy  = 1'b0;
            assign vid_cur       = 1'b0;
            assign vid_pend      = 1'b0;
            assign vid_switches  = 16'd0;
            assign u_hold        = 1'b0;
            assign slot_w        = 24'd0;
            assign fill_req_tgl  = u_fill_req_tgl;
            assign fill_line_req = u_fill_line_req;
            assign vblank_tgl    = u_vblank_tgl;
            assign video_rgb     = u_rgb;
            assign video_de      = u_de;
            assign video_hs      = u_hs;
            assign video_vs      = u_vs;
            assign scan_vc       = u_vc;
            assign gfx_underflow = u_uf;
        end
    endgenerate

endmodule

`default_nettype wire
