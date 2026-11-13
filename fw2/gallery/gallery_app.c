#pragma GCC optimize ("Os")
/* Tau Gallery M1: the first gallery (docs/M1_DESIGN.md). A 4 x 4 grid of 96 x 72 thumbnails on the 400 x 360 UI; A opens the selected picture: its 800 x 720 fit canvas (one contiguous run of the .tgy) lands in
 * the viewer plane through the landing path, then the viewer shows it. In the viewer, Left and Right go to the previous and next picture (the new one paints top to bottom as it lands) and B returns to the grid.
 * Data: the folder index tau-library.tdb is data slot 5 (TGIX, tools/gallery/tgy.py), the pictures are opened by path on slot 2. Build: APP=gallery bash fw2/build_gallery.sh. */
#include "../platform/platform.h"
#include "../gfx/gfx.h"
#include "../stream/slot.h"
#include "gallery_hw.h"
#include "gallery_fmt.h"
#include "tgy_hw.h"
#include "gallery_pages.h"
#include "gallery_settings_hw.h"
#include "../ui/theme.h"
#include "gallery_view.h"

#define BG   th_role[TR_BG_BOTTOM]                /* the colours are the theme's roles (ui/theme.c, docs/THEME_SPEC.md) */
#define FG   th_role[TR_TEXT_PRIMARY]
#define DIM  th_role[TR_TEXT_SECONDARY]
#define ACC  th_role[TR_ACCENT]
#define BAD  th_role[TR_ERROR]
#define GY0 22u                                  /* the top of the grid area (below the title line) */
#define STASH_X 400u                             /* the stash (shared rows 384 and below, columns 400..511): a 96 x 96 thumbnail waits here to be scaled down by the draw engine */
#define STASH_Y 384u
#define MAX_PP 48u                               /* the most entries on a page (the 8 x 6 grid) */
#define SLOT_PIC 2u
#define SLOT_IDX 5u
#define GAL_ZOOM_BASE 0x400000u                  /* the zoom plane (a raster zoom level at its own stride), SDRAM halfword address 8 MiB */
#define REP_DELAY (CLK_HZ * 2u / 5u)             /* a held direction key in the grid: first repeat after 400 ms */
#define REP_RATE (CLK_HZ / 12u)                  /* then about 12 pictures a second */
#define ZOOM_FRAME (CLK_HZ / 60u)                /* one pan step a frame */
#define ZOOM_CATCHUP 4                           /* most frames paid back in one pass */
#define LAND_CHUNK 131072u                       /* the largest single command measured in M0 (1.5 to 1.6 MB/s) */


static char *put_u(char *q, uint32_t v) { char t[10]; uint32_t n = 0; do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v); while (n) *q++ = t[--n]; return q; }
static char *put_s(char *q, const char *p) { while (*p) *q++ = *p++; return q; }

/* Reads len bytes at byte offset off of a data slot into dst, through 4 KB aligned commands into the tag zone (the slot files are padded to 4 KB). Returns 1 on success. */
static int read_bytes(uint32_t slot, uint32_t off, uint32_t len, uint8_t *dst)
{
    uint32_t pos = off & ~4095u;
    while (pos < off + len) {
        if (!slot_read_blocking(slot, pos, (uint32_t)(uintptr_t)_tag_start, 4096u, CLK_HZ)) return 0;
        const volatile uint32_t *w = GAL_TAGWIN;
        for (uint32_t i = 0; i < 1024u; i++) {
            const uint32_t b = pos + 4u * i, v = w[i];
            for (uint32_t k = 0; k < 4u; k++) { const uint32_t a = b + k; if (a >= off && a < off + len) dst[a - off] = (uint8_t)(v >> (8u * k)); }
        }
        pos += 4096u;
    }
    return 1;
}

/* The grid sizes of Tau Gallery 1.0 (docs/handoffs/SESSION_HANDOFF_2026-11-13_GALLERY_1_0_FIRMWARE.md): cols x rows of cw x ch cells at x0 + col * dx, GY0 + y0 + row * dy; ring = the selection frame's width; rad = the
 * corner radius; ratio = the draw engine's down-scale code (0 = 3:4, 1 = 2:3, 2 = 1:2) of the 96 x 96 thumbnail, 0xFF = drawn as it is. The last row is the version 0 index: 96 x 72 thumbnails, square corners. */
typedef struct { uint8_t cols, rows, cw, ch, x0, dx, y0, dy, ring, rad, ratio; } grid_t;
static const grid_t GRIDS[5] = { { 4, 3, 96, 96, 3, 99, 6, 102, 2, 8, 0xFF }, { 5, 4, 72, 72, 6, 78, 4, 76, 2, 8, 0 }, { 6, 4, 64, 64, 2, 66, 10, 74, 2, 6, 1 }, { 8, 6, 48, 48, 1, 49, 3, 51, 1, 4, 2 }, { 4, 4, 96, 72, 2, 100, 0, 76, 2, 0, 0xFF } };
static uint32_t gi, pp;                                                                             /* the grid in use and its entries a page */
#define GR GRIDS[gi]
static void set_grid(uint32_t g) { gi = g; pp = (uint32_t)GR.cols * GR.rows; }

static gx_head_t head; static gx_entry_t ent[MAX_PP];
static uint32_t sel, page, loaded_page = 0xFFFFFFFFu, n_on_page;                                    /* sel: the selected entry, an index into the library (or into the folders on the folder level) */
static uint32_t cur_first, cur_count, level, cur_folder;                                            /* the open folder is the range [cur_first, cur_first + cur_count) of the entries; level 1 shows the folders themselves */
#define EN(i) ent[(i) - (cur_first + page * pp)]                                                    /* the loaded page's record of entry i */
static uint8_t v1z_idx[32768u]; static uint32_t v1z_idx_ok;                                         /* the zoom's tile index; also the buffer of the index pages and the folder table while no picture is open */

static void say(const char *s, uint16_t color) { gfx_rect(0, 336, GFX_W, 24, BG); gfx_text(6, 338, s, color, BG); }
static void cell_xy(uint32_t k, uint32_t *x, uint32_t *y) { *x = GR.x0 + (k % GR.cols) * GR.dx; *y = GY0 + GR.y0 + (k / GR.cols) * GR.dy; }
/* The selection frame: four strips outside the cell; the cell's four corner notches take the frame colour and the outside of the frame is rounded, so the frame follows the thumbnail's rounded corners without
 * redrawing the thumbnail. off (on = 0) paints the same places in the background. */
static void frame(uint32_t k, uint16_t c)
{
    uint32_t x, y; cell_xy(k, &x, &y); const uint32_t t = GR.ring, w = GR.cw, h = GR.ch;
    gfx_rect(x - t, y - t, w + 2u * t, t, c); gfx_rect(x - t, y + h, w + 2u * t, t, c); gfx_rect(x - t, y, t, h, c); gfx_rect(x + w, y, t, h, c);
    if (GR.rad) { gfx_corners(x, y, w, h, GR.rad, c); if (c != BG) gfx_corners(x - t, y - t, w + 2u * t, h + 2u * t, GR.rad + t, BG); }
}

/* One thumbnail: its bytes (13,824 for 96 x 72, 18,432 for 96 x 96) are read in 4 KB pieces and written into the framebuffer two pixels at a time (the mailbox), 96 pixels = 192 bytes a row. A grid with a down-scale ratio
 * writes it to the stash instead and has the draw engine scale it into the cell; the corners are rounded last (the engine runs the commands in order). */
static int draw_thumb(uint32_t x, uint32_t y, uint32_t off)
{
    const uint32_t tb = head.thumb_bytes, ds = GR.ratio != 0xFFu, bx = ds ? STASH_X : x, by = ds ? STASH_Y : y;
    if (ds) gfx_fence();                                                                              /* the previous thumbnail's scaling has read the stash */
    for (uint32_t pos = off & ~4095u; pos < off + tb; pos += 4096u) {
        if (!slot_read_blocking(SLOT_IDX, pos, (uint32_t)(uintptr_t)_tag_start, 4096u, CLK_HZ)) return 0;
        const volatile uint32_t *w = GAL_TAGWIN;
        for (uint32_t i = 0; i < 1024u; i++) {
            const uint32_t a = pos + 4u * i;
            if (a < off || a >= off + tb) continue;
            const uint32_t rel = a - off, row = rel / 192u, col = (rel % 192u) / 2u;
            gfx_mb_write_buf((by + row) * GFX_STRIDE + bx + col, w[i]);
        }
    }
    if (ds) gfx_dsblit(STASH_X, STASH_Y, x, y, 96u, 96u, GR.ratio, GR.ratio);
    if (GR.rad && level == 0u) gfx_corners(x, y, GR.cw, GR.ch, GR.rad, BG);
    return 1;
}

static void copy_name(char *d, const char *src, uint32_t max) { uint32_t i = 0; while (i < max && src[i]) { d[i] = src[i]; i++; } d[i] = 0; }
/* The entries (or, on the folder level, the folder records) of the page into ent[]. The index bytes go through v1z_idx (free while the grid is shown). */
static int load_page(void)
{
    const uint32_t first = cur_first + page * pp, left = cur_first + cur_count - first; n_on_page = left < pp ? left : pp;
    uint8_t *buf = v1z_idx; v1z_idx_ok = 0u;
    if (level == 1u) {
        if (!read_bytes(SLOT_IDX, gx_folder_off(&head, first), n_on_page * GX1_FOLDER_BYTES, buf)) return 0;
        for (uint32_t k = 0; k < n_on_page; k++) {
            gx_folder_t f; if (gx_parse_folder(&head, buf + k * GX1_FOLDER_BYTES, GX1_FOLDER_BYTES, &f)) return 0;
            copy_name(ent[k].name, f.name, GX_NAME_MAX); ent[k].path[0] = 0; ent[k].thumb = f.cover; ent[k].size = f.count; ent[k].width = ent[k].height = 0u; ent[k].details_len = 0u; ent[k].taken = 0u;
        }
    } else {
        if (!read_bytes(SLOT_IDX, gx_entry_off(&head, first), n_on_page * head.entry_bytes, buf)) return 0;
        for (uint32_t k = 0; k < n_on_page; k++) if (gx_parse_entry_h(&head, buf + k * head.entry_bytes, head.entry_bytes, &ent[k])) return 0;
    }
    loaded_page = page; return 1;
}

static uint16_t dark_bg(void) { return (uint16_t)((BG >> 1) & 0x7BEFu); }
static void footer(void)
{
    char b[48], *q = put_s(b, ""); q = put_u(q, sel - cur_first + 1u); q = put_s(q, "/"); q = put_u(q, cur_count); *q = 0;
    gfx_rect(0, 336, GFX_W, 24, BG);
    gfx_text(6, 338, EN(sel).name, FG, BG);
    gfx_text(GFX_W - 16u - gfx_text_width(b), 338, b, DIM, BG);   /* gfx_text drops a glyph cell that starts within 16 px of the right edge: a 6 px margin lost the last digit */
}

static void draw_page(void)
{
    gfx_rect(0, 0, GFX_W, GFX_H, BG);
    gfx_text(6, 2, "TAU GALLERY", ACC, BG);
    if (loaded_page != page && !load_page()) { say("INDEX READ FAILED", BAD); return; }
    for (uint32_t k = 0; k < n_on_page; k++) {
        uint32_t x, y; cell_xy(k, &x, &y);
        if (!draw_thumb(x, y, gx_thumb_off(&head, ent[k].thumb))) { say("THUMBNAIL READ FAILED", BAD); return; }
        if (level == 1u) {                                                                           /* a folder tile: the cover with a darkened strip at the foot carrying the name and the number of pictures */
            char b[16], *q = b; uint32_t n = ent[k].size, d = 1u; for (uint32_t v = n; v >= 10u; v /= 10u) d++;
            const uint32_t room = 11u - d; for (uint32_t i = 0; i < room && ent[k].name[i]; i++) *q++ = ent[k].name[i];
            *q++ = ' '; q = put_u(q, n); *q = 0;
            gfx_rect(x, y + GR.ch - 16u, GR.cw, 16u, dark_bg()); gfx_text(x + 2u, y + GR.ch - 16u, b, FG, dark_bg());
            gfx_corners(x, y, GR.cw, GR.ch, GR.rad, BG);
        }
    }
    frame(sel - (cur_first + page * pp), ACC); footer();
}

static void select_to(uint32_t n)
{
    if (n < cur_first || n >= cur_first + cur_count) return;
    const uint32_t np = (n - cur_first) / pp;
    if (np != page) { sel = n; page = np; draw_page(); return; }
    frame(sel - (cur_first + page * pp), BG); sel = n; frame(sel - (cur_first + page * pp), ACC); footer();
}

/* Lands the picture's fit canvas in the viewer plane. Returns 0 when it is there, else a code for the status line. */
static uint8_t h4[4096]; static gt_info_t pic;                                    /* the first 4 KB of the open picture (its header and level table) and what gt_parse found in it */
/* The loader: a slim bar (160 x 4) in the middle of the screen, a track in DIM and the done part in ACC, so a picture that takes a second or two to open shows it is moving. */
static uint32_t zb_on, zb_px;                       /* the zoom's loader bar is drawn in the fit plane (below); its pixels saved */
#define LDR_X 120u
#define LDR_Y 178u
#define LDR_W 160u
#define LDR_H 2u
#define LDR_TRACK ((uint16_t)((DIM >> 1) & 0x7BEFu))     /* the track: the secondary text colour at half brightness */
static void loader_bar(uint32_t done, uint32_t total)
{
    const uint32_t w = total ? (uint32_t)((uint64_t)done * LDR_W / total) : 0u;
    gfx_rect(LDR_X, LDR_Y, LDR_W, LDR_H, LDR_TRACK);
    if (w) gfx_rect(LDR_X, LDR_Y, w > LDR_W ? LDR_W : w, LDR_H, ACC);
}
/* One landing of the canvas into the plane (restarts the landing counters). Returns 0, or 5 when a card read failed. */
static int land_canvas(const gt_info_t *t, uint32_t total, int show_progress)
{
    REG_WR(R_LAND_BASE, GAL_PLANE_BASE); REG_WR(R_LAND_CTRL, 1u);
    for (uint32_t c = 0; c < total; c += LAND_CHUNK) {
        const uint32_t len = total - c < LAND_CHUNK ? total - c : LAND_CHUNK;
        if (!slot_read_blocking(SLOT_PIC, t->canvas_off + c, 0x40000000u + c, len, CLK_HZ * 4u)) return 5;
        if (show_progress) { char b[24], *q = put_s(b, "LOADING "); q = put_u(q, (c + len) * 100u / total); q = put_s(q, "%"); *q = 0; say(b, ACC); loader_bar(c + len, total); }
    }
    return 0;
}
/* ---- container version 1 (docs/TGY_CONTAINER_V1.md): the picture is a tiled, compressed file; the hardware decoder (tgy_hw.c, docs/TGY_HW_DMA.md) draws it. The card read lands only the first part of the file (the header, the
 * thumbnail, the fit level: it comes first) in the landing window at GAL_LAND_BASE; the decoder fetches the tile streams and the quantisation tables from there and writes the tiles into the viewer plane. The fit level
 * of the files the converter writes is the whole 800 x 720 canvas (the black bars are part of it), so every byte of the plane is written and nothing needs clearing. ---- */
#define V1_IDX_MAX 2048u                          /* the tile index of the fit level: 12 bytes a tile, 42 tiles for 800 x 720 (the converter's fit level), room for 170 */
static tgy_file_t v1f; static tgy_level_t v1l; static uint8_t v1_idx[V1_IDX_MAX]; static uint32_t v1_on;       /* the open picture is a version 1 file shown by the decoder */
static int v1_read_idx(void *ctx, uint32_t off, uint32_t len, uint8_t *dst) { (void)ctx; return read_bytes(SLOT_PIC, off, len, dst); }
static int v1_land(uint32_t total, int show_progress)       /* lands bytes 0 .. total of the file at the landing window; 0 or 5 */
{
    REG_WR(R_LAND_BASE, GAL_LAND_BASE); REG_WR(R_LAND_CTRL, 1u);
    for (uint32_t c = 0; c < total; c += LAND_CHUNK) {
        const uint32_t len = total - c < LAND_CHUNK ? total - c : LAND_CHUNK;
        if (!slot_read_blocking(SLOT_PIC, c, 0x40000000u + c, len, CLK_HZ * 4u)) return 5;
        if (show_progress) { char b[24], *q = put_s(b, "LOADING "); q = put_u(q, (c + len) * 100u / total); q = put_s(q, "%"); *q = 0; say(b, ACC); loader_bar(c + len, total); }
    }
    return 0;
}
static int v1_tick(void *ctx, uint32_t tx, uint32_t ty)
{
    const int *show = ctx;
    if (*show) { const uint32_t done = ty * v1l.cols + tx + 1u, all = v1l.cols * v1l.rows; char b[24], *q = put_s(b, "DECODING "); q = put_u(q, done * 100u / all); q = put_s(q, "%"); *q = 0; say(b, ACC); loader_bar(done, all); }
    return 0;
}
static int open_v1(int show_progress)
{
    v1_on = 0u;
    if (!tgy_hw_present()) return 7;                                                 /* a version 1 picture needs the hardware decoder */
    if (!(REG_RD(R_LAND_STATUS) >> 31)) return 4;
    const uint32_t fsize = (uint32_t)h4[48] | (uint32_t)h4[49] << 8 | (uint32_t)h4[50] << 16 | (uint32_t)h4[51] << 24, base = GAL_LAND_BASE * 2u;
    int r = tgy_open(&v1f, h4, fsize, base);                                         /* first with the whole file in view, to learn where the fit level ends */
    if (r == TH_CRC || r == TH_MAGIC || r == TH_VERSION || r == TH_SHORT) return 3;
    if (r != TH_OK) return 8;
    if (tgy_find_level(&v1f, 2u, &v1l) != TH_OK && tgy_find_level(&v1f, 1u, &v1l) != TH_OK) return 8;
    uint32_t end = v1l.data_off + v1l.data_len; if (v1l.idx_off + v1l.idx_len > end) end = v1l.idx_off + v1l.idx_len;
    end = (end + 4095u) & ~4095u; if (end > fsize) end = fsize;
    if (v1_land(end, show_progress) && v1_land(end, show_progress)) return 5;        /* one retry, as for a version 0 picture */
    uint32_t t0 = cycles();
    while (!(REG_RD(R_LAND_STATUS) & 1u) && (uint32_t)(cycles() - t0) < CLK_HZ / 10u) { }
    if (REG_RD(R_LAND_OVF) != 0u || REG_RD(R_LAND_WORDS) != end / 4u) return 6;
    if (tgy_open(&v1f, h4, end, base) != TH_OK || tgy_find_level(&v1f, v1l.kind, &v1l) != TH_OK) return 8;
    if (v1l.idx_len > V1_IDX_MAX) return 8;
    r = tgy_load_index(&v1f, &v1l, v1_read_idx, 0, v1_idx, V1_IDX_MAX);
    if (r == TH_SHORT) return 2;
    if (r != TH_OK) return 8;
    int show = show_progress;
    r = tgy_hw_show(&v1f, &v1l, GAL_PLANE_BASE * 2u, v1_tick, &show);
    if (r == TH_HWERR) return 9;
    if (r == TH_TIMEOUT) return 10;
    if (r != TH_OK) return 8;
    pic.width = v1f.width; pic.height = v1f.height; pic.file_size = fsize; pic.canvas_off = 0u; pic.canvas_len = 0u; pic.thumb_off = 0u;
    { int32_t fx, fy, fw, fh; gv_fit_rect(v1f.width, v1f.height, GT_CANVAS_W, GT_CANVAS_H, &fx, &fy, &fw, &fh);                 /* where the PICTURE is on the fit canvas (the fit level is the whole canvas): the map and the zoom entry convert fit and level coordinates with it */
      pic.rx = (uint16_t)fx; pic.ry = (uint16_t)fy; pic.rw = (uint16_t)fw; pic.rh = (uint16_t)fh; }
    v1_on = 1u;
    return 0;
}
static int open_picture(uint32_t i, int show_progress)
{
    char path[GX_PATH_MAX + 2u]; path[0] = '/';
    { const char *s = EN(i).path; uint32_t n = 0; while (s[n] && n < GX_PATH_MAX) { path[1u + n] = s[n]; n++; } path[1u + n] = 0; }
    if (show_progress) { say("OPENING", ACC); loader_bar(0u, 1u); }
    if (!slot_open_path(SLOT_PIC, path)) return 1;
    if (!read_bytes(SLOT_PIC, 0u, 4096u, h4) && !read_bytes(SLOT_PIC, 0u, 4096u, h4)) return 2;      /* one retry of a failed read: a transient card error should not cost the picture */
    if (h4[0] == 'T' && h4[1] == 'G' && h4[2] == 'Y' && h4[3] == 0 && h4[4] == 1u && h4[5] == 0u) return open_v1(show_progress);       /* container version 1: the hardware decoder */
    v1_on = 0u;
    if (gt_parse(h4, 4096u, &pic)) return 3;
    if (!(REG_RD(R_LAND_STATUS) >> 31)) return 4;                                   /* no landing path in this bitstream */
    const uint32_t total = (pic.canvas_len + 4095u) & ~4095u;                         /* the file is padded to 4 KB: read the padding too, it lands just past the plane */
    if (land_canvas(&pic, total, show_progress) && land_canvas(&pic, total, show_progress)) return 5;     /* a failed read lands the whole picture again once (the counters restart with it); two failures are reported */
    uint32_t t0 = cycles();
    while (!(REG_RD(R_LAND_STATUS) & 1u) && (uint32_t)(cycles() - t0) < CLK_HZ / 10u) { }
    if (REG_RD(R_LAND_OVF) != 0u || REG_RD(R_LAND_WORDS) != total / 4u) return 6;   /* a word was dropped or lost: do not show a damaged picture */
    return 0;
}
static const char *const open_msg[] = { "", "OPEN FAILED", "HEADER READ FAILED", "NOT A TGY PICTURE", "NO LANDING PATH", "PICTURE READ FAILED", "PICTURE DAMAGED IN TRANSFER", "NEEDS THE HW DECODER", "PICTURE DAMAGED", "DECODER ERROR", "DECODER TIMEOUT" };

/* open_picture with the Info page's numbers: how long it took (ms), the landing's word counts (only when a landing ran), the status line text. */
static void zoom_list(void);
static gi_t info = { 0u, 0u, 0u, GI_NONE, 0u, 0u, 0, 0u, 0u, 0u, GI_NONE, GI_NONE, GI_NONE, GI_NONE, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u };
static gs_vals_t gset; static uint32_t last_act;                                              /* the persisted settings; the clock of the last key or card read (a save waits for a quiet second after it) */
static void fx_apply(void) { REG_WR(R_PIX_CTRL, gset.dither % 3u); }                                  /* the dither of the scan-out (the colour tables follow with the presets) */
static uint32_t path_crc(uint32_t i) { const char *s = EN(i).path; uint32_t n = 0; while (s[n]) n++; return gf_crc32((const uint8_t *)s, n) & GS_M31; }
static void remember(uint32_t i) { gset.last_idx = i; gset.last_crc = path_crc(i); }       /* the picture the next start resumes on: only one the viewer has shown */
static void settings_tick(void) { gs_hw_service(&gset, last_act); }
static int open_timed(uint32_t i, int show_progress)
{
    const uint32_t t0 = cycles();
    const int r = open_picture(i, show_progress);
    info.open_ms = (uint32_t)(cycles() - t0) / (CLK_HZ / 1000u);
    if (r == 0 || r >= 5) { info.words = REG_RD(R_LAND_WORDS); info.lost = REG_RD(R_LAND_OVF); } else { info.words = 0u; info.lost = 0u; }
    info.err = r ? open_msg[r] : 0;
    if (r == 0) zoom_list();
    last_act = cycles();
    return r;
}

/* ---- M2 zoom (docs/M2_DESIGN.md): a picture's zoom levels (kind 1) are its stops after fit, in file order. A raster level (option A) is landed in 48 KB chunks into the zoom plane at its own stride; a tile level
 * (option B) is landed tile by tile (48 KB, one card command) into a staging area and copied by Talos, a row piece at a time, into a 9 x 8 tile resident window, with a second window composed for a re-centre and
 * flipped in. A takes the last stop and back to fit, L1 / R1 step the stops, the D-pad pans, B goes back to fit. The screen is shown only where every pixel has landed; one card command or a bounded number of
 * Talos commands a pass, so the keys are polled between. ---- */
#define GAL_WIN0 0x900000u                       /* the resident windows (halfword addresses), 5.4 MB each, above the zoom plane (which ends at 0x400000 + 9 MiB / 2) */
#define GAL_WIN1 (GAL_WIN0 + GV_WIN_HW)
#define GAL_STAGE (GAL_WIN1 + GV_WIN_HW)         /* one landed tile (24,576 halfwords) before Talos copies it */
#define COPY_PER_PASS 64u                        /* Talos row-piece commands a pass */
static gt_zoom_t zs[GV_MAX_STOPS], zl; static uint32_t zn; static int zk = -1;              /* the picture's zoom levels, the one in use, its index (-1: the fit view) */
static gv_stop_t z_stop;
static uint32_t z_t0, z_first, t_copy_cyc, t_rc_t0;                                                    /* the Info page's zoom timings: the clock at the key, 1 until the first view is on screen, the Talos cycles of the tile being copied */
static int32_t z_wx, z_wy, z_sx, z_sy; static uint32_t z_shown;                               /* the wanted and the shown corner (level pixels); 1 when a zoom view is on screen */
static uint32_t z_stride_hw, z_nch, z_lo, z_hi;                                              /* option A: the level's row stride (halfwords), its chunks, the loaded chunk interval */
static uint32_t t_front, t_job, t_k, t_src_hw, t_src_stride, t_dst_hw, t_job_col, t_job_row, t_job_win, t_recentre, t_shown_win;      /* option B: window on screen, the tile copy under way, a re-centre being composed */
static int32_t t_wx[2], t_wy[2]; static uint8_t t_res[2][GV_WIN_TX * GV_WIN_TY];             /* each window's origin (level pixels) and which of its tiles are whole */
static void res_clear(uint32_t w) { for (uint32_t i = 0; i < GV_WIN_TX * GV_WIN_TY; i++) t_res[w][i] = 0u; }       /* no tile of window w is whole */
/* ---- zoom of a version 1 picture (docs/TGY_HW_DMA.md, "Zoom"): its full-resolution level (kind 1) is a compressed tile level. The one stop after fit is that level at 1:1; a tile is a card read of its stream (a 4 KB aligned
 * superset of it, landed at GAL_STREAM) and one decode by the hardware decoder straight into its place in the resident window (no staging copy, no Talos): the windows, the re-centre, the map and the keys are the version 0
 * tile level's own. The level's tile index (12 bytes a tile) is read from the card when the first zoom opens. The quantisation tables come from the prefix of the file landed when the picture opened. ---- */
#define GAL_STREAM (GAL_STAGE + 0x10000u)        /* one landed tile stream, 128 KB (halfword address): past the staging tile (0x6000) and the bar save area (GAL_STAGE + 0x8000, 0x3000 used) */
#define SDRAM_HZ 100000000u                      /* the decoder's cycle counter runs on the SDRAM clock */
#define V1Z_STREAM_MAX 122880u                   /* a stream longer than this (a tile of noise at the highest quality is about 60 KB) is not decoded */
#define V1Z_IDX_MAX 32768u                       /* the full level's tile index: 12 bytes a tile, 2,730 tiles (a 44 megapixel picture) */
static tgy_file_t v1fz; static tgy_level_t v1z; static uint32_t v1z_cap = V1Z_IDX_MAX, v1z_smax = V1Z_STREAM_MAX;      /* v1z_cap: the index size accepted (a variable so the host test can lower it) */
/* An index that does not fit the buffer (more than 2,730 tiles) stays on the card: its checksum is checked once in pieces, and each tile's 12 byte entry is read through a small block cache over the same buffer (v1c_n blocks of v1c_bs bytes,
 * least recently used out; variables so the host test can make them tiny). The index is row by row, so the entries a window needs are a few blocks. Above V1Z_VERIFY_MAX the checksum is not checked (reading it would take seconds):
 * an entry that is out of range is refused when it is used, as a damaged stream is stopped by the decoder. */
#define V1Z_VERIFY_MAX 262144u
static uint32_t v1c_bs = 4096u, v1c_n = 8u, v1c_pos[16], v1c_age[16], v1c_clock, v1c_reads;
static uint8_t *v1c_get(uint32_t pos)                                                        /* the block that starts at file offset pos (a multiple of v1c_bs), read from the card on a miss; 0 when the read failed */
{
    uint32_t victim = 0u;
    for (uint32_t i = 0; i < v1c_n; i++) {
        if (v1c_pos[i] == pos) { v1c_age[i] = ++v1c_clock; return v1z_idx + i * v1c_bs; }
        if (v1c_pos[i] == 0xFFFFFFFFu) { victim = i; break; }
        if (v1c_age[i] < v1c_age[victim]) victim = i;
    }
    const uint32_t end = v1z.idx_off + v1z.idx_len, len = end - pos < v1c_bs ? end - pos : v1c_bs;
    if (pos >= end || !read_bytes(SLOT_PIC, pos, len, v1z_idx + victim * v1c_bs)) return 0;
    v1c_pos[victim] = pos; v1c_age[victim] = ++v1c_clock; v1c_reads++;
    return v1z_idx + victim * v1c_bs;
}
static int v1_idx_entry(void *ctx, uint32_t off, uint8_t *out)                               /* the 12 bytes at file offset off (a block boundary may fall inside them) */
{
    (void)ctx;
    for (uint32_t done = 0; done < 12u; ) {
        const uint32_t a = off + done, bp = a - a % v1c_bs; const uint8_t *b = v1c_get(bp); if (!b) return 0;
        uint32_t n = v1c_bs - (a - bp); if (n > 12u - done) n = 12u - done;
        for (uint32_t k = 0; k < n; k++) out[done + k] = b[a - bp + k];
        done += n;
    }
    return 1;
}
typedef struct { uint32_t valid, buf, off_in_buf; int32_t tx, ty; } v1_pre_t;
static v1_pre_t v1_pre;
typedef struct { uint32_t valid, tw; int col, row; int32_t tx, ty; } v1_next_t;
static uint32_t pst[4], pcopy;                                                               /* the stamps of the pass being timed (start, planned, copy or need list done, job done) and 1 when the pass ran a Talos copy step */
static uint32_t z_stall_run, v1_pipe_off;                                                    /* frames in a row the shown view stood still; 1 when the pipeline is switched off (the Info page's A) */
static v1_next_t v1_next;                                                                    /* set by tile_pass: the tile after the one being started, when it also needs the card */
static void v1_zoom_list(void)                                                               /* a version 1 picture is open: its full level, if this core can decode it and it is upright */
{
    if (!v1_on || h4[18] != 1u) return;                                                      /* the header's EXIF orientation of the full level: only upright is shown (a rotated one needs a transposing decode) */
    const uint32_t fsize = (uint32_t)h4[48] | (uint32_t)h4[49] << 8 | (uint32_t)h4[50] << 16 | (uint32_t)h4[51] << 24;
    if (tgy_open(&v1fz, h4, fsize, GAL_LAND_BASE * 2u) != TH_OK || tgy_find_level(&v1fz, 1u, &v1z) != TH_OK) return;      /* with the whole file in view, to read the level table */
    gt_zoom_t z; z.layout = 2u; z.w = v1z.w; z.h = v1z.h; z.off = v1z.data_off; z.len = v1z.data_len; z.stride = 0u; z.ntx = v1z.cols; z.nty = v1z.rows; z.scale_q16 = 65536u; z.rx = z.ry = z.rw = z.rh = 0u;
    zs[zn++] = z; v1z_idx_ok = 0u;
}
static void zoom_list(void)                                                                  /* after a picture opens: its zoom levels, in file order */
{
    zn = 0u; zk = -1;
    if (v1_on) { v1_zoom_list(); return; }
    for (uint32_t k = 0; k < 8u && zn < GV_MAX_STOPS; k++) { gt_zoom_t z; const int r = gt_zoom_level(h4, 4096u, k, &z); if (r == GF_NOLEVEL) break; if (r == GF_OK) zs[zn++] = z; }
}
static void zoom_off(int k, int32_t *ox, int32_t *oy)                                        /* where the picture starts in stop k's canvas (k = -1: the fit canvas) */
{
    if (k < 0) { *ox = (int32_t)pic.rx; *oy = (int32_t)pic.ry; } else if (zs[k].layout == 0u) { *ox = (int32_t)zs[k].rx; *oy = (int32_t)zs[k].ry; } else { *ox = 0; *oy = 0; }
}
static uint32_t zoom_scale(int k) { return k < 0 ? (uint32_t)(((uint64_t)pic.rw * 65536u + pic.width / 2u) / pic.width) : zs[k].scale_q16; }
/* The loader bar of a zoom: the viewer plane has no overlay, so while the fit view stays up and the zoom's first view loads, a bar of the same look (160 x 2, a dark track and an ACC fill) is written into the middle of the fit plane (rows 358 and 359) and the picture's own pixels there are saved first and copied back as soon as the first view is on screen or the zoom is left. */
#define ZB_Y 358u
#define ZB_HW0 480u                              /* pixel 320 of a plane row: 320 x 3 / 2 halfwords */
#define ZB_HW 240u                               /* 160 pixels */
#define GAL_BARSAVE (GAL_STAGE + 0x8000u)        /* the saved pixels of the two rows, past the staging tile */
static uint32_t zb_row(uint32_t r) { return GAL_PLANE_BASE + (ZB_Y + r) * GAL_LINE_WORDS16 + ZB_HW0; }
static uint16_t zb_half(uint32_t h, uint32_t fillpx)       /* halfword h of the bar's row: two bytes of the R, G, B stream */
{
    const uint16_t tr = LDR_TRACK, ac = ACC; uint32_t v = 0;
    for (uint32_t b = 0; b < 2u; b++) {
        const uint32_t j = h * 2u + b, px = j / 3u, comp = j % 3u; const uint16_t c = px < fillpx ? ac : tr;
        const uint32_t byte = comp == 0u ? ((c >> 11) & 31u) << 3 : comp == 1u ? ((c >> 5) & 63u) << 2 : (c & 31u) << 3;
        v |= byte << (8u * b);
    }
    return (uint16_t)v;
}
static void zb_write(uint32_t from_px, uint32_t to_px, uint32_t fillpx)     /* the halfword pairs that cover pixels from_px to to_px, written with the fill up to fillpx */
{
    uint32_t h0 = (from_px * 3u / 2u) & ~1u, h1 = ((to_px * 3u + 1u) / 2u + 1u) & ~1u; if (h1 > ZB_HW) h1 = ZB_HW;
    for (uint32_t r = 0; r < 2u; r++) for (uint32_t h = h0; h < h1; h += 2u) gfx_mb_write(zb_row(r) + h, (uint32_t)zb_half(h, fillpx) | ((uint32_t)zb_half(h + 1u, fillpx) << 16));
}
static void zb_copy(int back)                              /* the two rows to the save area (back = 0) or from it */
{
    for (uint32_t r = 0; r < 2u; r++) for (uint32_t k = 0; k < 2u; k++) {
        const uint32_t o = k * (ZB_HW / 2u), save = GAL_BARSAVE + r * ZB_HW;
        if (back) gfx_blit_raw(save, zb_row(r), o, o, ZB_HW / 2u); else gfx_blit_raw(zb_row(r), save, o, o, ZB_HW / 2u);
    }
    gfx_fence();
}
static void zb_begin(void) { if (zb_on) return; zb_copy(0); zb_on = 1u; zb_px = 0u; zb_write(0u, 160u, 0u); }
static void zb_set(uint32_t done, uint32_t total)
{
    if (!zb_on) return;
    const uint32_t px = total ? (done > total ? 160u : (uint32_t)((uint64_t)done * 160u / total)) : 0u;
    if (px > zb_px) { zb_write(zb_px, px, px); zb_px = px; }
}
static void zb_end(void) { if (!zb_on) return; zb_on = 0u; zb_copy(1); }
static void zoom_shown_now(void) { zb_end(); if (z_first) { z_first = 0u; info.zoom_ms = (cycles() - z_t0) / (CLK_HZ / 1000u); } }       /* the first zoomed view is on screen */
static void zoom_leave(void)
{
    if (t_job) gfx_fence();                                                                  /* Talos commands of an abandoned copy retire before anything reuses their memory */
    zb_end();                                                                                /* the fit plane's own pixels back under the loader bar */
    REG_WR(R_VIEW_STRIDE, GAL_LINE_WORDS16); REG_WR(R_VIEW_BASE, GAL_PLANE_BASE);              /* the fit plane is still in SDRAM: back in one write */
    z_shown = 0u; t_job = 0u; t_recentre = 0u; zk = -1; v1_pre.valid = 0u;
}
/* Opens stop k with its screen corner at (nx, ny) of the stop's canvas (clamped here): the fit view shows while the level loads. */
static void zoom_open(int k, int32_t nx, int32_t ny)
{
    if (zs[k].layout == 2u && !v1z_idx_ok) {                                                  /* the first zoom of a version 1 picture: its tile index (checksum checked) */
        int ir;
        if (v1z.idx_len <= v1z_cap) ir = tgy_load_index(&v1fz, &v1z, v1_read_idx, 0, v1z_idx, V1Z_IDX_MAX);                    /* it fits: the whole index in RAM */
        else {                                                                                  /* it stays on the card: checked once, then read an entry at a time through the block cache */
            ir = v1z.idx_len <= V1Z_VERIFY_MAX ? tgy_verify_index(&v1fz, &v1z, v1_read_idx, 0, v1z_idx, V1Z_IDX_MAX) : TH_OK;
            v1z.idx = 0; v1z.fetch = v1_idx_entry; v1z.fctx = 0; v1c_clock = 0u; for (uint32_t i = 0; i < 16u; i++) { v1c_pos[i] = 0xFFFFFFFFu; v1c_age[i] = 0u; }
        }
        if (ir != TH_OK) { say("ZOOM INDEX READ FAILED", BAD); zk = -1; return; }
        v1z_idx_ok = 1u;
    }
    info.zt_n = info.zt_land_sum = info.zt_land_max = info.zt_dec_n = info.zt_dec_sum = info.zt_dec_max = info.zt_pass_max = info.zt_catch = info.zt_moved = info.zt_stall_run_max = info.zt_sp = info.zt_sc = info.zt_sn = info.zt_sj = info.zt_ss = 0u; v1_pre.valid = 0u; z_stall_run = 0u;      /* the tile statistics are those of this zoom */
    zl = zs[k]; zk = k; z_stop.w = zl.w; z_stop.h = zl.h; z_stop.scale_q16 = zl.scale_q16; gv_clamp(&z_stop, &nx, &ny);
    z_wx = nx; z_wy = ny; z_sx = -1; z_sy = -1; z_shown = 0u; z_t0 = cycles(); z_first = 1u;
    if (zl.layout == 0u) { z_stride_hw = zl.stride / 2u; z_nch = gv_nchunks((zl.len + 4095u) & ~4095u); z_lo = 0u; z_hi = 0u; }
    else { t_front = 0u; gv_window_for(&z_stop, z_wx, z_wy, &t_wx[0], &t_wy[0]); res_clear(0u); }
    zb_begin();
}
/* Goes to stop k (-1: fit). The fit view shows while the new level loads, and its first view keeps the screen centre on the same point of the picture. */
static void zoom_goto(int k)
{
    int32_t x = 0, y = 0, fox, foy, tox = 0, toy = 0, nx = 0, ny = 0;
    if (zk >= 0) { x = z_shown ? z_sx : z_wx; y = z_shown ? z_sy : z_wy; }                        /* the centre of what the screen shows, not of where the keys have taken the wanted corner */
    zoom_off(zk, &fox, &foy); const uint32_t sf = zoom_scale(zk);
    zoom_leave();
    if (k < 0) return;
    zoom_off(k, &tox, &toy);
    gv_map_corner(sf, fox, foy, zoom_scale(k), tox, toy, x, y, &nx, &ny);
    zoom_open(k, nx, ny);
}
/* ---- option A ---- */
/* Lands chunk c of the level (a read of the file's own bytes, at the same offset of the zoom plane), once more on a failure. 1 when it landed with every word counted. */
static int zoom_land(uint32_t c)
{
    const uint32_t padded = (zl.len + 4095u) & ~4095u, rest = padded - c * GV_CHUNK, len = rest < GV_CHUNK ? rest : GV_CHUNK, tc = cycles();
    for (int attempt = 0; attempt < 2; attempt++) {
        REG_WR(R_LAND_BASE, GAL_ZOOM_BASE + c * (GV_CHUNK / 2u)); REG_WR(R_LAND_CTRL, 1u);
        if (!slot_read_blocking(SLOT_PIC, zl.off + c * GV_CHUNK, 0x40000000u, len, CLK_HZ)) continue;
        const uint32_t t0 = cycles(); while (!(REG_RD(R_LAND_STATUS) & 1u) && (uint32_t)(cycles() - t0) < CLK_HZ / 10u) { }
        if (REG_RD(R_LAND_OVF) == 0u && REG_RD(R_LAND_WORDS) == len / 4u) { info.land_ms = (cycles() - tc) / (CLK_HZ / 1000u); return 1; }
    }
    return 0;
}
/* One pass of the raster zoom loop: on a frame tick the held direction moves the wanted corner; one chunk lands; the screen follows inside the loaded rows. Returns 1 when a chunk could not be read. */
static int raster_pass(int dirx, int diry, uint32_t held, int moving, int tick)
{
    (void)tick;
    if (moving) gv_move(&z_stop, &z_wx, &z_wy, dirx, diry, held);                              /* a frame a pass at most: the raster loader sets the pace of a version 0 pan */
    uint32_t c;
    if (gv_next_chunk(zl.stride, zl.len, z_nch, z_lo, z_hi, (uint32_t)z_wy, diry, &c)) {
        if (!zoom_land(c)) return 1;
        if (z_hi <= z_lo) { z_lo = c; z_hi = c + 1u; } else if (c + 1u == z_lo) z_lo = c; else z_hi++;
        if (!z_shown) { uint32_t need = (GV_SCREEN_H * zl.stride + GV_CHUNK - 1u) / GV_CHUNK + 1u; if (need > z_nch) need = z_nch; zb_set(z_hi - z_lo, need); }
    }
    uint32_t rlo, rhi; int32_t y;
    gv_rows_loaded(zl.stride, zl.len, z_lo, z_hi, &rlo, &rhi);
    if (gv_display_y(z_wy, rlo, rhi, &y) && (!z_shown || y != z_sy || z_wx != z_sx)) {
        REG_WR(R_VIEW_STRIDE, z_stride_hw); REG_WR(R_VIEW_BASE, gv_plane_base(GAL_ZOOM_BASE, z_stride_hw, z_wx, y)); z_sx = z_wx; z_sy = y; z_shown = 1u; zoom_shown_now();
    }
    return 0;
}
/* ---- option B ---- */
static uint32_t win_base(uint32_t w) { return w ? GAL_WIN1 : GAL_WIN0; }
/* Lands tile (tx, ty) of the open tile level into the staging area: one card command of 48 KB, once more on a failure. 1 when every word landed. */
static int tile_land(int32_t tx, int32_t ty)
{
    const uint32_t off = gt_tile_off(&zl, (uint32_t)tx, (uint32_t)ty);
    if (off == 0xFFFFFFFFu) return 0;
    const uint32_t tc = cycles();
    for (int attempt = 0; attempt < 2; attempt++) {
        REG_WR(R_LAND_BASE, GAL_STAGE); REG_WR(R_LAND_CTRL, 1u);
        if (!slot_read_blocking(SLOT_PIC, off, 0x40000000u, GT_TILE_BYTES, CLK_HZ)) continue;
        const uint32_t t0 = cycles(); while (!(REG_RD(R_LAND_STATUS) & 1u) && (uint32_t)(cycles() - t0) < CLK_HZ / 10u) { }
        if (REG_RD(R_LAND_OVF) == 0u && REG_RD(R_LAND_WORDS) == GT_TILE_BYTES / 4u) { info.land_ms = (cycles() - tc) / (CLK_HZ / 1000u); return 1; }
    }
    return 0;
}
/* A version 1 tile, pipelined: the decoder runs by itself, so while it decodes tile A the card reads the stream of the NEXT tile the zoom will want into the other stream buffer; the next pass starts that tile's decode at once, with no read.
 * A tile's stream is read as a 4 KB aligned superset into GAL_STREAM (buffer 0) or GAL_STREAM2 (buffer 1), once more on a failure, and decoded by the hardware straight into its place in window tw; the tile is whole when the decoder is done.
 * `pre` is the tile whose stream is already in a buffer (valid until a zoom opens or closes or another tile takes its buffer). The decoder reads the stream while it runs: the next read goes to the OTHER buffer. 1 on a failure. */
#define GAL_STREAM2 (GAL_STREAM + 0x10000u)
static uint32_t v1_stream_hw(uint32_t buf) { return buf ? GAL_STREAM2 : GAL_STREAM; }
static int v1_pre_matches(const v1_pre_t *p, int32_t tx, int32_t ty) { return p->valid && p->tx == tx && p->ty == ty; }      /* the prefetched stream is the wanted tile's */

static int v1_land_stream(int32_t tx, int32_t ty, uint32_t buf, uint32_t *off_in_buf)       /* the stream of tile (tx, ty) into buffer buf; 1 on a failure */
{
    uint32_t fo, len; if (tgy_tile_loc(&v1z, (uint32_t)tx, (uint32_t)ty, &fo, &len) != TH_OK || len == 0u || len > v1z_smax) return 1;
    if (v1_pre.valid && v1_pre.buf == buf) v1_pre.valid = 0u;                                 /* this read overwrites the buffer: whatever it held is gone, however the read ends */
    const uint32_t a = fo & ~4095u, e = (fo + len + 4095u) & ~4095u, n = e - a, tc = cycles(); int ok = 0;
    for (int attempt = 0; attempt < 2 && !ok; attempt++) {
        REG_WR(R_LAND_BASE, v1_stream_hw(buf)); REG_WR(R_LAND_CTRL, 1u);
        if (!slot_read_blocking(SLOT_PIC, a, 0x40000000u, n, CLK_HZ)) continue;
        const uint32_t t0 = cycles(); while (!(REG_RD(R_LAND_STATUS) & 1u) && (uint32_t)(cycles() - t0) < CLK_HZ / 10u) { }
        ok = REG_RD(R_LAND_OVF) == 0u && REG_RD(R_LAND_WORDS) == n / 4u;
    }
    if (!ok) return 1;
    const uint32_t us = (cycles() - tc) / (CLK_HZ / 1000000u);
    info.land_ms = us / 1000u; info.zt_n++; info.zt_land_sum += us; if (us > info.zt_land_max) info.zt_land_max = us;
    *off_in_buf = fo - a; return 0;
}
static int v1_tile_decode(uint32_t tw, int col, int row, int32_t tx, int32_t ty)
{
    uint32_t buf, off;
    if (v1_pre_matches(&v1_pre, tx, ty)) { buf = v1_pre.buf; off = v1_pre.off_in_buf; }                                     /* its stream was read while the previous tile decoded */
    else { buf = 0u; if (v1_land_stream(tx, ty, buf, &off)) return 1; }
    if (tgy_hw_start_from(&v1fz, &v1z, (uint32_t)tx, (uint32_t)ty, (win_base(tw) + gv_tile_hw(col, row)) * 2u, GV_WIN_STRIDE_WORDS * 2u, v1_stream_hw(buf) * 2u + off) != TH_OK) return 1;
    if (v1_next.valid && v1_next.tw == tw) {                                                                                  /* the decoder is running: read the next tile into the other buffer */
        uint32_t noff; const uint32_t nb = 1u - buf;
        if (!v1_land_stream(v1_next.tx, v1_next.ty, nb, &noff)) { v1_pre.valid = 1u; v1_pre.buf = nb; v1_pre.off_in_buf = noff; v1_pre.tx = v1_next.tx; v1_pre.ty = v1_next.ty; }
    }
    if (tgy_hw_finish() != TH_OK) { v1_pre.valid = 0u; return 1; }
    const uint32_t dec_us = tgy_hw_cycles() / (SDRAM_HZ / 1000000u);                                                         /* the decoder's own count of SDRAM-clock cycles for the tile */
    info.copy_ms = dec_us / 1000u;                                                                                            /* the Info page's ZOOM COPY row is the decode of the tile for a version 1 picture */
    info.zt_dec_n++; info.zt_dec_sum += dec_us; if (dec_us > info.zt_dec_max) info.zt_dec_max = dec_us;
    t_res[tw][row * GV_WIN_TX + col] = 1u;
    return 0;
}
/* Starts the copy of tile (tx, ty) into window tw at (col, row): from the front window when a re-centre is composing and the front has the tile whole (a Talos copy, no card), else from the card. 1 on a read failure. */
static int tile_job_start(uint32_t tw, int col, int row, int32_t tx, int32_t ty)
{
    int fc, fr;
    if (t_recentre && gv_win_tile(t_wx[t_front], t_wy[t_front], tx, ty, &fc, &fr) && t_res[t_front][fr * GV_WIN_TX + fc]) { t_src_hw = win_base(t_front) + gv_tile_hw(fc, fr); t_src_stride = GV_WIN_STRIDE_WORDS; }
    else if (zl.layout == 2u) return v1_tile_decode(tw, col, row, tx, ty);                      /* version 1: decoded into the window, no copy job */
    else { if (!tile_land(tx, ty)) return 1; t_src_hw = GAL_STAGE; t_src_stride = GV_TILE_HW; }
    t_dst_hw = win_base(tw) + gv_tile_hw(col, row); t_job_win = tw; t_job_col = (uint32_t)col; t_job_row = (uint32_t)row; t_k = 0u; t_job = 1u; t_copy_cyc = 0u;
    return 0;
}
static void tile_copy_step(void)                                                             /* up to COPY_PER_PASS row pieces; the tile counts as whole only after Talos has retired them all */
{
    const uint32_t tc = cycles();
    for (uint32_t n = 0; n < COPY_PER_PASS && t_k < GV_PIECES; n++, t_k++) { uint32_t so, dof; gv_piece(t_k, t_src_stride, &so, &dof); gfx_blit_raw(t_src_hw, t_dst_hw, so, dof, GV_PIECE_HW); }
    if (t_k >= GV_PIECES) { gfx_fence(); t_res[t_job_win][t_job_row * GV_WIN_TX + t_job_col] = 1u; t_job = 0u; t_copy_cyc += cycles() - tc; info.copy_ms = t_copy_cyc / (CLK_HZ / 1000u); } else t_copy_cyc += cycles() - tc;
}
#define FOLLOW_STEP 16                           /* pixels a frame the shown view moves toward the wanted corner when the tiles for the step are whole */
#define SLACK 96                                 /* how far ahead of the shown view the windows are built, toward the wanted corner */
static int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static void tile_show(int32_t x, int32_t y)                                                  /* the viewport (x, y) from the front window */
{
    REG_WR(R_VIEW_STRIDE, GV_WIN_STRIDE_WORDS); REG_WR(R_VIEW_BASE, gv_win_base(win_base(t_front), x, y, t_wx[t_front], t_wy[t_front]));
    z_sx = x; z_sy = y; z_shown = 1u; t_shown_win = t_front; zoom_shown_now();
}
/* One pass of the tile zoom loop. The keys move the WANTED corner (z_wx, z_wy) at once; the SHOWN view (z_sx, z_sy) follows it a step a frame, only where every tile of the step is whole, so a pan that outruns the card
 * slows to the card's pace instead of freezing and jumping. The windows are built around the shown view pulled toward the wanted corner by at most SLACK, and the tiles are fetched in the order of that view.
 * Returns 1 when a tile could not be read. */
static int tile_pass_inner(int dx, int dy, uint32_t held, int moving, int tick)
{
    pcopy = 0u;
    if (moving) for (int f = 0; f < tick; f++) gv_move(&z_stop, &z_wx, &z_wy, dx, dy, held);        /* tick = the frames that went by since the last pass (1 to ZOOM_CATCHUP): the pan's speed is in pixels a second, whatever the pass took */
    int32_t px = z_wx, py = z_wy;
    if (z_shown) { px = z_sx + clampi(z_wx - z_sx, -SLACK, SLACK); py = z_sy + clampi(z_wy - z_sy, -SLACK, SLACK); gv_clamp(&z_stop, &px, &py); }
    const int lx = dx ? dx : (z_wx > px ? 1 : z_wx < px ? -1 : 0), ly = dy ? dy : (z_wy > py ? 1 : z_wy < py ? -1 : 0);      /* the direction of the pan: the keys, else where the wanted corner lies */
    if (!t_job) {
        const uint32_t b = 1u - t_front;
        if (t_recentre) {
            if (gv_window_ok(&z_stop, t_wx[t_front], t_wy[t_front], px, py)) t_recentre = 0u;                                  /* the view came back into the window on screen: nothing to compose */
            else if (!gv_window_ok(&z_stop, t_wx[b], t_wy[b], px, py)) { gv_window_lead(&z_stop, px, py, lx, ly, &t_wx[b], &t_wy[b]); res_clear(b); }      /* the view left the window being composed: aim it at the view now */
        } else if (!gv_window_ok(&z_stop, t_wx[t_front], t_wy[t_front], px, py)) {                                             /* the view is near the window's edge: compose a new window in the other buffer */
            gv_window_lead(&z_stop, px, py, lx, ly, &t_wx[b], &t_wy[b]); res_clear(b); t_recentre = 1u; t_rc_t0 = cycles();
        }
    }
    const uint32_t tw = t_recentre ? 1u - t_front : t_front;
    if (zl.layout == 2u) pst[1] = cycles();                                                    /* the window planning is done */
    if (t_job) { pcopy = 1u; tile_copy_step(); if (zl.layout == 2u) pst[2] = pst[3] = cycles(); }
    else {
        const int32_t cx = (t_recentre && z_shown) ? z_sx : px, cy = (t_recentre && z_shown) ? z_sy : py;                      /* while composing, the tiles of the view on screen first: they let the flip happen at once */
        gv_tile_t need[GV_MAX_TILES]; const uint32_t n = gv_tiles_needed(&z_stop, cx, cy, t_wx[tw], t_wy[tw], lx, ly, need);
        if (zl.layout == 2u) pst[2] = cycles();                                            /* the needed list is made */
        for (uint32_t i = 0; i < n; i++) {
            int col, row; if (!gv_win_tile(t_wx[tw], t_wy[tw], need[i].tx, need[i].ty, &col, &row) || t_res[tw][row * GV_WIN_TX + col]) continue;
            v1_next.valid = 0u;
            if (zl.layout == 2u && !v1_pipe_off) for (uint32_t k = i + 1u; k < n; k++) {                      /* version 1: the next tile that needs the card is read while this one decodes */
                int c2, r2; if (!gv_win_tile(t_wx[tw], t_wy[tw], need[k].tx, need[k].ty, &c2, &r2) || t_res[tw][r2 * GV_WIN_TX + c2]) continue;
                v1_next.valid = 1u; v1_next.tw = tw; v1_next.col = c2; v1_next.row = r2; v1_next.tx = need[k].tx; v1_next.ty = need[k].ty; break;
            }
            if (tile_job_start(tw, col, row, need[i].tx, need[i].ty)) return 1;
            break;                                                                           /* one tile a pass: one card command */
        }
        if (zl.layout == 2u) pst[3] = cycles();                                            /* the tile (card read and decode) is done */
    }
    const uint32_t b2 = 1u - t_front;
    if (t_recentre && !t_job && (z_shown ? gv_screen_ready(&z_stop, z_sx, z_sy, t_wx[b2], t_wy[b2], t_res[b2]) : gv_screen_ready(&z_stop, px, py, t_wx[b2], t_wy[b2], t_res[b2]))) { t_front = b2; t_recentre = 0u; info.flip_ms = (cycles() - t_rc_t0) / (CLK_HZ / 1000u); }      /* the new window has the view on screen whole: flip */
    if (!z_shown && zb_on) {                                                                 /* the loader: the tiles of the first view that are whole */
        gv_tile_t scr[GV_MAX_TILES]; const uint32_t ns = gv_tiles_needed(&z_stop, z_wx, z_wy, t_wx[t_front], t_wy[t_front], 0, 0, scr); uint32_t on = 0u, whole = 0u;
        for (uint32_t i = 0; i < ns; i++) { if (scr[i].ring) continue; on++; int col, row; if (gv_win_tile(t_wx[t_front], t_wy[t_front], scr[i].tx, scr[i].ty, &col, &row) && t_res[t_front][row * GV_WIN_TX + col]) whole++; }
        zb_set(whole, on);
    }
    if (!z_shown) { if (gv_screen_ready(&z_stop, z_wx, z_wy, t_wx[t_front], t_wy[t_front], t_res[t_front])) tile_show(z_wx, z_wy); return 0; }
    if (t_front != t_shown_win) { tile_show(z_sx, z_sy); return 0; }                         /* the window flipped: the same view, from the new window */
    if (tick && (z_sx != z_wx || z_sy != z_wy)) {                                            /* follow the wanted corner: diagonal if its tiles are whole, else along one axis */
        const int32_t fs = FOLLOW_STEP * tick, nx = z_sx + clampi(z_wx - z_sx, -fs, fs), ny = z_sy + clampi(z_wy - z_sy, -fs, fs);
        info.zt_catch += (uint32_t)tick;                                                                     /* a frame the shown view is behind the wanted corner; zt_moved of them it could step (the rest are the stalls) */
        int stepped = 1;
        if (gv_screen_ready(&z_stop, nx, ny, t_wx[t_front], t_wy[t_front], t_res[t_front])) { tile_show(nx, ny); info.zt_moved += (uint32_t)tick; }
        else if (nx != z_sx && gv_screen_ready(&z_stop, nx, z_sy, t_wx[t_front], t_wy[t_front], t_res[t_front])) { tile_show(nx, z_sy); info.zt_moved += (uint32_t)tick; }
        else if (ny != z_sy && gv_screen_ready(&z_stop, z_sx, ny, t_wx[t_front], t_wy[t_front], t_res[t_front])) { tile_show(z_sx, ny); info.zt_moved += (uint32_t)tick; }
        else stepped = 0;
        if (stepped) z_stall_run = 0u; else { z_stall_run += (uint32_t)tick; if (z_stall_run > info.zt_stall_run_max) info.zt_stall_run_max = z_stall_run; }      /* frames in a row the view stood still while the keys were ahead of it: how long a freeze feels */
    } else if (tick) z_stall_run = 0u;
    return 0;
}
static int tile_pass(int dx, int dy, uint32_t held, int moving, int tick)                    /* the pass with its duration: the longest one is the Info page's LONGEST PASS (a slowdown in a pan is a long pass or a stalled frame) */
{
    if (zl.layout != 2u) return tile_pass_inner(dx, dy, held, moving, tick);                  /* the statistics are for version 1 pictures (the version 0 zoom has its own rows) */
    const uint32_t t0 = cycles(); pst[0] = pst[1] = pst[2] = pst[3] = t0; const int r = tile_pass_inner(dx, dy, held, moving, tick);
    const uint32_t t5 = cycles(), us = (t5 - t0) / (CLK_HZ / 1000000u), u = CLK_HZ / 1000000u;
    if (us > info.zt_pass_max) {                                                             /* the longest pass so far: where its time went (microseconds): planning, Talos copy step, needed list, the tile, the rest (show, follow, flip) */
        info.zt_pass_max = us; info.zt_sp = (pst[1] - pst[0]) / u; info.zt_sc = pcopy ? (pst[2] - pst[1]) / u : 0u; info.zt_sn = pcopy ? 0u : (pst[2] - pst[1]) / u; info.zt_sj = (pst[3] - pst[2]) / u; info.zt_ss = (t5 - pst[3]) / u;
    }
    return r;
}
static uint32_t zt_frames;                                                                    /* the pan frames counted in version 1 zooms (for the host test: they must follow the time, not the passes) */
static int zoom_pass(int dx, int dy, uint32_t held, int moving, int tick) { return zl.layout == 0u ? raster_pass(dx, dy, held, moving, tick) : tile_pass(dx, dy, held, moving, tick); }

/* ---- the map (docs/M2_DESIGN.md, the navigator): START while zoomed shows the fit view with the rectangle of the screen drawn on it ---- */
/* The viewer plane has no overlay, so the outline (2 pixels, ACC) is written into the fit plane and the picture's own pixels under it are saved first: a copy of a row piece or of a 3-halfword pixel pair per row with one fixed pair of
 * Talos bases for each half of the plane (a command offset is below 2^19 halfwords, a plane is 864,000). D-pad moves the rectangle, L1 and R1 change the stop (the rectangle's size), A opens that stop there, B or START go back to the view. */
#define MAP_SPR 0x1000u                          /* one row of ACC pixels, 1,200 halfwords, inside the GAL_BARSAVE area */
#define MAP_SAVE 0x2000u                         /* the saved pixels: 4 edge rows of 1,200, then the left and right edges at 3 halfwords a row */
#define MAP_VERT 4800u
static int32_t m_rx, m_ry, m_rw, m_rh; static uint32_t map_runs;      /* the rectangle on the fit canvas; how many times the map was opened */
static void map_blit(int op, int half, uint32_t poff, uint32_t soff, uint32_t n)
{
    const uint32_t pb = GAL_PLANE_BASE + (half ? 360u * GAL_LINE_WORDS16 : 0u);
    for (uint32_t d = 0; d < n; d += 127u) {
        const uint32_t w = n - d < 127u ? n - d : 127u;
        if (op == 0) gfx_blit_raw(pb, GAL_BARSAVE, poff + d, MAP_SAVE + soff + d, w);                 /* save the plane's pixels */
        else if (op == 1) gfx_blit_raw(GAL_BARSAVE, pb, MAP_SPR + d, poff + d, w);                    /* draw the outline from the sprite row */
        else gfx_blit_raw(GAL_BARSAVE, pb, MAP_SAVE + soff + d, poff + d, w);                         /* put the pixels back */
    }
}
static void map_ops(int op, int32_t rx, int32_t ry, int32_t rw, int32_t rh)
{
    const int32_t x0 = rx < 0 ? 0 : rx, x1 = rx + rw > 800 ? 800 : rx + rw;
    for (int half = 0; half < 2; half++) {
        const int32_t rlo = half ? 360 : 0, rhi = half ? 720 : 360;
        for (int e = 0; e < 4; e++) {                                                        /* the two top rows, the two bottom rows */
            const int32_t row = e < 2 ? ry + e : ry + rh - 4 + e;
            if (row < rlo || row >= rhi || x1 <= x0) continue;
            map_blit(op, half, (uint32_t)(row - rlo) * GAL_LINE_WORDS16 + (uint32_t)(x0 * 3 / 2), (uint32_t)e * 1200u, (uint32_t)((x1 - x0) * 3 / 2));
        }
        for (int side = 0; side < 2; side++) {                                               /* the left and right edges between them */
            const int32_t px = side ? rx + rw - GV_RECT_T : rx; if (px < 0 || px + GV_RECT_T > 800) continue;
            const int32_t r0 = ry + 2 > rlo ? ry + 2 : rlo, r1 = ry + rh - 2 < rhi ? ry + rh - 2 : rhi;
            for (int32_t row = r0 < 0 ? 0 : r0; row < r1; row++) {
                const uint32_t k = (uint32_t)(row - (ry + 2)); if (k >= 720u) continue;
                map_blit(op, half, (uint32_t)(row - rlo) * GAL_LINE_WORDS16 + (uint32_t)(px * 3 / 2), MAP_VERT + (uint32_t)side * 2160u + k * 3u, 3u);
            }
        }
    }
}
static void map_rect_of(int k, int32_t cx, int32_t cy)                                      /* the rectangle of stop k's screen centred on the fit point (cx, cy), kept over the picture */
{
    int32_t okx, oky, rw, rh, ry0; zoom_off(k, &okx, &oky);
    gv_map_rect(zoom_scale(-1), (int32_t)pic.rx, (int32_t)pic.ry, zoom_scale(k), okx, oky, okx, oky, &m_rx, &ry0, &rw, &rh);
    m_rw = rw; m_rh = rh; m_rx = cx - rw / 2; m_ry = cy - rh / 2;
    gv_map_clamp((int32_t)pic.rx, (int32_t)pic.ry, (int32_t)pic.rw, (int32_t)pic.rh, m_rw, m_rh, &m_rx, &m_ry);
}
static void zoom_reshow(void)                                                                /* the zoom view that was on screen, again */
{
    if (zl.layout == 0u) { REG_WR(R_VIEW_STRIDE, z_stride_hw); REG_WR(R_VIEW_BASE, gv_plane_base(GAL_ZOOM_BASE, z_stride_hw, z_sx, z_sy)); }
    else tile_show(z_sx, z_sy);
}
/* Returns 1 when a stop was opened from the map, 0 when it was left. */
static int map_mode(void)
{
    map_runs++;
    if (t_job) { gfx_fence(); t_job = 0u; }                                                  /* an abandoned tile copy: its tile is not marked whole, a later pass does it again */
    REG_WR(R_VIEW_STRIDE, GAL_LINE_WORDS16); REG_WR(R_VIEW_BASE, GAL_PLANE_BASE);              /* the fit plane */
    for (uint32_t h = 0; h < 1200u; h += 2u) gfx_mb_write(GAL_BARSAVE + MAP_SPR + h, (uint32_t)zb_half(h, 1000000u) | ((uint32_t)zb_half(h + 1u, 1000000u) << 16));      /* the sprite row: ACC all along */
    int mk = zk; int32_t okx, oky; zoom_off(zk, &okx, &oky);
    gv_map_rect(zoom_scale(-1), (int32_t)pic.rx, (int32_t)pic.ry, zoom_scale(zk), okx, oky, z_sx, z_sy, &m_rx, &m_ry, &m_rw, &m_rh);
    gv_map_clamp((int32_t)pic.rx, (int32_t)pic.ry, (int32_t)pic.rw, (int32_t)pic.rh, m_rw, m_rh, &m_rx, &m_ry);
    int32_t dx0 = m_rx, dy0 = m_ry, dw0 = m_rw, dh0 = m_rh;                                  /* what is drawn now */
    map_ops(0, dx0, dy0, dw0, dh0); map_ops(1, dx0, dy0, dw0, dh0); gfx_fence();
    uint32_t prev = REG_RD(R_INPUT) & 0xFFFFu, held = 0u, next_tick = cycles();
    for (;;) {
        const uint32_t keys = REG_RD(R_INPUT) & 0xFFFFu, edge = keys & ~prev; prev = keys;
        if (edge) last_act = cycles();
        settings_tick();
        int redraw = 0;
        if (edge & (KEY_B | KEY_START)) { map_ops(2, dx0, dy0, dw0, dh0); gfx_fence(); zoom_reshow(); return 0; }
        if (edge & KEY_A) {
            map_ops(2, dx0, dy0, dw0, dh0); gfx_fence();
            int32_t sx2, sy2; zoom_off(mk, &okx, &oky);
            gv_map_view(zoom_scale(-1), (int32_t)pic.rx, (int32_t)pic.ry, zoom_scale(mk), okx, oky, m_rx, m_ry, &sx2, &sy2);
            zoom_leave(); zoom_open(mk, sx2, sy2); return 1;
        }
        if ((edge & KEY_R1) && (uint32_t)mk + 1u < zn) { const int32_t cx = m_rx + m_rw / 2, cy = m_ry + m_rh / 2; mk++; map_rect_of(mk, cx, cy); redraw = 1; }
        if ((edge & KEY_L1) && mk > 0) { const int32_t cx = m_rx + m_rw / 2, cy = m_ry + m_rh / 2; mk--; map_rect_of(mk, cx, cy); redraw = 1; }
        const int dx = (keys & KEY_RIGHT) ? 1 : (keys & KEY_LEFT) ? -1 : 0, dy = (keys & KEY_DOWN) ? 1 : (keys & KEY_UP) ? -1 : 0;
        if ((int32_t)(cycles() - next_tick) >= 0) {
            next_tick = cycles() + ZOOM_FRAME; held = (dx || dy) ? held + 1u : 0u;
            if (dx || dy) { const int32_t st = gv_speed(held); m_rx += dx * st; m_ry += dy * st; gv_map_clamp((int32_t)pic.rx, (int32_t)pic.ry, (int32_t)pic.rw, (int32_t)pic.rh, m_rw, m_rh, &m_rx, &m_ry); redraw = 1; }
        }
        if (redraw && (m_rx != dx0 || m_ry != dy0 || m_rw != dw0 || m_rh != dh0)) {
            map_ops(2, dx0, dy0, dw0, dh0); map_ops(0, m_rx, m_ry, m_rw, m_rh); map_ops(1, m_rx, m_ry, m_rw, m_rh); gfx_fence();
            dx0 = m_rx; dy0 = m_ry; dw0 = m_rw; dh0 = m_rh;
        }
    }
}
static void run_details(void);
static void viewer(void)
{
    uint32_t prev = REG_RD(R_INPUT) & 0xFFFFu, held = 0u, next_tick = 0u;
    for (;;) {
        const uint32_t keys = REG_RD(R_INPUT) & 0xFFFFu, edge = keys & ~prev; prev = keys;
        if (edge) last_act = cycles();
        settings_tick();
        if (zk >= 0) {
            if ((edge & KEY_START) && z_shown) { map_mode(); held = 0u; next_tick = cycles(); prev = REG_RD(R_INPUT) & 0xFFFFu; continue; }                   /* START: the map */
            if (edge & (KEY_A | KEY_B)) { zoom_goto(-1); continue; }                                  /* A: back to fit; B: back one level, not to the grid */
            if (edge & KEY_R1) { if ((uint32_t)zk + 1u < zn) { zoom_goto(zk + 1); held = 0u; next_tick = cycles(); } continue; }                /* R1 and L1 step the stops */
            if (edge & KEY_L1) { zoom_goto(zk > 0 ? zk - 1 : -1); held = 0u; next_tick = cycles(); continue; }
            if (!((keys & KEY_SELECT) && (edge & (KEY_LEFT | KEY_RIGHT)))) {                          /* Select + Left/Right leave the zoom and go to the next picture below; anything else pans */
                const int dx = (keys & KEY_RIGHT) ? 1 : (keys & KEY_LEFT) ? -1 : 0, dy = (keys & KEY_DOWN) ? 1 : (keys & KEY_UP) ? -1 : 0;
                int tick = 0; const uint32_t now = cycles();                                           /* the frame clock keeps its phase: a pass longer than a frame is paid back with the frames that went by, so the pan's speed does not follow the pass lengths */
                if ((int32_t)(now - next_tick) >= 0 && zl.layout != 2u) { next_tick = now + ZOOM_FRAME; tick = 1; held = (dx || dy) ? held + 1u : 0u; }      /* version 0 pictures (a raster level or tiles read at 38 ms): one step a pass, as before; only version 1 pictures, whose passes are 9 to 25 ms, pay back the frames a pass took */
                else if ((int32_t)(now - next_tick) >= 0) {
                    uint32_t late = now - next_tick; tick = 1; next_tick += ZOOM_FRAME;
                    while (late >= ZOOM_FRAME && tick < ZOOM_CATCHUP) { late -= ZOOM_FRAME; tick++; next_tick += ZOOM_FRAME; }
                    if ((int32_t)(now - next_tick) >= 0) next_tick = now + ZOOM_FRAME;                       /* far behind (a re-centre, a slow card): drop the rest, no burst afterwards */
                    held = (dx || dy) ? held + (uint32_t)tick : 0u; zt_frames += (uint32_t)tick;
                }
                if (zoom_pass(dx, dy, held, tick && (dx || dy), tick)) { zoom_goto(-1); gal_view_off(); loaded_page = 0xFFFFFFFFu; draw_page(); say("PICTURE READ FAILED", BAD); return; }
                continue;
            }
            zoom_goto(-1);
        }
        if ((edge & KEY_SELECT) && !(edge & (KEY_LEFT | KEY_RIGHT))) {                                                                 /* Select: the details page (not Select + Left / Right) */
            gal_view_off(); run_details();                                                                                            /* the picture's plane in SDRAM is not touched by the page: the view is simply switched on again */
            if (!gal_view_on()) { loaded_page = 0xFFFFFFFFu; draw_page(); say("VIEWER DID NOT START", BAD); return; }
            prev = REG_RD(R_INPUT) & 0xFFFFu; held = 0u; next_tick = cycles(); continue;
        }
        if (edge & KEY_A) { if (zn > 0u) { zoom_goto((int)zn - 1); held = 0u; next_tick = cycles(); } continue; }                   /* A: the last stop (1:1) */
        if (edge & KEY_R1) { if (zn > 0u) { zoom_goto(0); held = 0u; next_tick = cycles(); } continue; }                                /* R1: the first stop */
        if (edge & KEY_B) { gal_view_off(); loaded_page = 0xFFFFFFFFu; draw_page(); return; }
        if (edge & (KEY_LEFT | KEY_RIGHT)) {
            const uint32_t rel = sel - cur_first, nrel = (edge & KEY_RIGHT) ? (rel + 1u) % cur_count : (rel + cur_count - 1u) % cur_count, n = cur_first + nrel;      /* the next picture of the folder, wrapping inside it */
            const uint32_t np = nrel / pp;
            if (np != page) { page = np; if (!load_page()) { gal_view_off(); draw_page(); return; } }
            sel = n;
            const int r = open_timed(sel, 0);
            if (r) { gal_view_off(); loaded_page = 0xFFFFFFFFu; draw_page(); say(open_msg[r], BAD); return; }
            remember(sel);
        }
    }
}

static void open_from_grid(void)
{
    const int r = open_timed(sel, 1);
    if (r) { say(open_msg[r], BAD); return; }
    if (!gal_view_on()) { say("VIEWER DID NOT START", BAD); return; }
    remember(sel);
    viewer();
}

/* ---- the menu and its pages (gallery_pages.h): Start on the grid opens them, B or Start closes the menu and the grid is drawn again ---- */
#define LINE_H 20u
#define PAGE_Y0 26u
static void page_clear(const char *title) { gfx_rect(0, 0, GFX_W, GFX_H, BG); gfx_text(6, 2, title, ACC, BG); }
static void page_foot(const char *left, uint32_t n, uint32_t of)
{
    char b[24], *q = put_u(b, n); q = put_s(q, "/"); q = put_u(q, of); *q = 0;
    gfx_rect(0, 336, GFX_W, 24, BG); gfx_text(6, 338, left, DIM, BG);
    if (of) gfx_text(GFX_W - 16u - gfx_text_width(b), 338, b, DIM, BG);
}
static void draw_menu(const gm_t *m)
{
    page_clear("TAU GALLERY");
    for (uint32_t i = 0; i < GM_COUNT; i++) {
        gfx_text(6, PAGE_Y0 + i * LINE_H, gm_names[i], i == m->sel ? FG : DIM, BG);
        if (i == m->sel) gfx_text(GFX_W - 32u, PAGE_Y0 + i * LINE_H, "<", ACC, BG);
    }
    page_foot("A OPEN   B CLOSE", 0, 0);
}
static void draw_controls(const gc_t *c)
{
    page_clear("CONTROLS");
    for (uint32_t r = 0; r < GC_VIS && c->top + r < GC_N; r++) {
        const gc_row_t *w = &gc_rows[c->top + r]; const uint32_t y = PAGE_Y0 + r * (LINE_H - 2u);
        if (!w->what[0]) { gfx_text(6, y, w->key, ACC, BG); continue; }
        gfx_text(14, y, w->key, c->top + r == c->sel ? ACC : FG, BG); gfx_text(190, y, w->what, DIM, BG);
    }
    page_foot("UP DOWN SCROLL   B BACK", c->sel + 1u, GC_N);
}
static void draw_help(const gh_t *h)
{
    const gh_page_t *pg = &gh_pages[h->page];
    page_clear(pg->title);
    for (uint32_t i = 0; i < pg->n; i++) {
        const gh_line_t *l = &pg->lines[i];
        if (l->style != GS_GAP) gfx_text(6, PAGE_Y0 + i * (LINE_H - 2u), l->s, l->style == GS_ACCENT ? ACC : l->style == GS_DIM ? DIM : FG, BG);
    }
    page_foot("UP DOWN PAGE   B BACK", h->page + 1u, GH_PAGES);
}
static void draw_theme(const gt_t *g, const gt_look_t *l)
{
    page_clear("THEME");
    for (uint32_t r = 0; r < GT_ROWS; r++) {
        const uint32_t y = PAGE_Y0 + r * LINE_H; const char *v = r == GT_THEME ? theme_name(l->theme) : r == GT_MODE ? (l->pol ? "LIGHT" : "DARK") : th_palette_name[l->pal];
        gfx_text(6, y, gt_labels[r], r == g->sel ? FG : DIM, BG); gfx_text(110, y, v, r == g->sel ? FG : DIM, BG);
        if (r == g->sel) gfx_text(GFX_W - 32u, y, "<", ACC, BG);
    }
    gfx_rect(110, PAGE_Y0 + 3u * LINE_H + 6u, 120, 14, ACC);                                       /* the accent as it is drawn with this theme */
    page_foot("UP DOWN ROW  LEFT RIGHT CHANGE  B BACK", 0, 0);
}
#define INFO_LINE_H 16u                          /* 19 rows from y 26 to 314 */
static void draw_info(void)
{
    gi_row_t r[GI_ROWS];
    info.pipe_off = v1_pipe_off; info.core_version = REG_RD(R_VERSION); info.count = head.count; info.view_caps = REG_RD(R_VIEW_CAPS);
    info.set_layout = gs_stats.layout; info.set_loads = gs_stats.loads; info.set_stores = gs_stats.stores;
    gi_format(&info, r); page_clear("INFO");
    for (uint32_t i = 0; i < GI_ROWS; i++) { gfx_text(6, PAGE_Y0 + i * INFO_LINE_H, r[i].label, DIM, BG); gfx_text(150, PAGE_Y0 + i * INFO_LINE_H, r[i].value, i == 5u && info.err ? BAD : FG, BG); }
    page_foot("B BACK   A TOGGLE ZOOM PIPELINE", 0, 0);
}
static void draw_settings(const gsp_t *sp)
{
    page_clear("SETTINGS");
    const char *val[GSP_ROWS] = { gset.resume ? "ON" : "OFF", gsp_grid_names[gset.grid % GSP_GRIDS], gsp_dither_names[gset.dither % GSP_DITHERS] };
    for (uint32_t i = 0; i < GSP_ROWS; i++) {
        const uint32_t y = PAGE_Y0 + i * 2u * LINE_H;
        gfx_text(6, y, gsp_labels[i], i == sp->row ? FG : DIM, BG); gfx_text(250, y, val[i], ACC, BG); if (i == sp->row) gfx_text(GFX_W - 32u, y, "<", ACC, BG);
    }
    gfx_text(6, PAGE_Y0 + 6u * LINE_H + 4u, sp->row == GSP_RESUME ? "ON: the grid opens on the last picture." : sp->row == GSP_GRID ? "Pictures on a page: 12, 20, 24 or 48." : "Spreads the 5 and 6 bit panel steps.", DIM, BG);
    page_foot("UP DOWN ROW   LEFT RIGHT CHANGE   B BACK", 0, 0);
}
static void frame_box(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t col)           /* a 1 px outline */
{
    gfx_rect(x, y, w, 1, col); gfx_rect(x, y + h - 1u, w, 1, col); gfx_rect(x, y, 1, h, col); gfx_rect(x + w - 1u, y, 1, h, col);
}
#define DET_LINE 16u
#define DET_HX0 6u
#define DET_HX1 204u
#define DET_HY0 164u
#define DET_HY1 246u
#define DET_HW 190u
#define DET_HH 76u
/* The histograms are counted from the picture's thumbnail in the index (the CPU does not read the viewer plane): the same 4 KB reads as draw_thumb, two pixels a word. Returns 0 when a read failed. */
static hs_t det_hist;
static int thumb_hist(uint32_t off, hs_t *h, uint32_t pw, uint32_t ph)       /* pw, ph: the picture's size from the index; only the picture's rectangle of the thumbnail is counted, not its black bars */
{
    hs_clear(h); const hs_rect_t rc = head.version == 1u ? (hs_rect_t){ 0u, 0u, HS_TW, 96u } : hs_rect(pw, ph);      /* a version 1 thumbnail is the picture cropped to the square: every pixel is the picture's */
    for (uint32_t pos = off & ~4095u; pos < off + head.thumb_bytes; pos += 4096u) {
        if (!slot_read_blocking(SLOT_IDX, pos, (uint32_t)(uintptr_t)_tag_start, 4096u, CLK_HZ)) return 0;
        const volatile uint32_t *w = GAL_TAGWIN;
        for (uint32_t i = 0; i < 1024u; i++) {
            const uint32_t a = pos + 4u * i;
            if (a < off || a >= off + head.thumb_bytes) continue;
            const uint32_t v = w[i], px = (a - off) / 2u, row = px / HS_TW, col = px % HS_TW;                   /* the word's two pixels are in the same row (96 is even and a thumbnail starts on a word) */
            if (hs_inside(&rc, row, col)) hs_add(h, (uint16_t)(v & 0xFFFFu));
            if (hs_inside(&rc, row, col + 1u)) hs_add(h, (uint16_t)(v >> 16));
        }
    }
    return 1;
}
#define DET_BX 15u                                       /* the bars: 32 bins of 5 pixels, 15 pixels in from the frame, 50 pixels high, 4 above its bottom edge */
#define DET_BW 5u
#define DET_BH 50u
#define DET_BB 4u
/* The picture details page (gallery_pages.h gd_*): the index's facts, placeholders for what is not read yet, and four histograms of the thumbnail. */
static void draw_details(const gd_t *d, const hs_t *hist)
{
    gd_row_t r[GD_ROWS]; gd_format(d, r); page_clear("DETAILS");
    for (uint32_t i = 0; i < GD_ROWS; i++) {
        const uint32_t y = PAGE_Y0 + i * DET_LINE, pending = i >= 4u;
        gfx_text(6, y, r[i].label, DIM, BG); gfx_text(110, y, r[i].value, pending ? DIM : FG, BG);
    }
    static const uint16_t bar_col[GD_HIST_N] = { 0xFFFFu, 0xF800u, 0x07E0u, 0x001Fu };
    for (uint32_t h = 0; h < GD_HIST_N; h++) {
        const uint32_t x = (h & 1u) ? DET_HX1 : DET_HX0, y = (h & 2u) ? DET_HY1 : DET_HY0;
        frame_box(x, y, DET_HW, DET_HH, DIM); gfx_text(x + 6u, y + 6u, gd_hist_labels[h], ACC, BG);
        if (!hist) { gfx_text(x + 6u, y + 34u, GD_HIST_NOTE, DIM, BG); continue; }
        const uint32_t m = hs_max(hist, h);
        for (uint32_t i = 0; i < HS_BINS; i++) {
            const uint32_t bh = hs_height(hist->bin[h][i], m, DET_BH);
            if (bh) gfx_rect(x + DET_BX + i * DET_BW, y + DET_HH - DET_BB - bh, DET_BW - 1u, bh, bar_col[h]);
        }
    }
    gfx_text(6, 324, hist ? GD_HIST_SRC : GD_HIST_NOTE, DIM, BG);
    page_foot("B OR SELECT BACK", 0, 0);
}
/* Waits for the next key press (an edge) and returns it. */
static uint32_t next_edge(uint32_t *prev)
{
    for (;;) { const uint32_t keys = REG_RD(R_INPUT) & 0xFFFFu, edge = keys & ~*prev; *prev = keys; if (edge) { last_act = cycles(); return edge; } settings_tick(); }
}
/* The details page of the selected picture; returns when B or Select closes it. The caller redraws what was under it. */
static void run_details(void)
{
    const gx_entry_t *e = &EN(sel); const gd_t d = { e->name, sel - cur_first, cur_count, e->size, e->width, e->height };
    uint32_t prev = REG_RD(R_INPUT) & 0xFFFFu; draw_details(&d, thumb_hist(gx_thumb_off(&head, e->thumb), &det_hist, e->width, e->height) ? &det_hist : 0);
    while (!gd_input(next_edge(&prev))) { }
}
static void run_pages(void)
{
    uint32_t prev = REG_RD(R_INPUT) & 0xFFFFu;
    gm_t m; gm_open(&m); draw_menu(&m);
    for (;;) {
        const uint32_t edge = next_edge(&prev); const int r = gm_input(&m, edge);
        if (r == GP_CLOSE) return;
        if (r == GP_STAY) { draw_menu(&m); continue; }
        if (r == GP_OPEN_THEME) {
            gt_t g; gt_open(&g); gt_look_t l = { th_theme, th_pol, th_pal }; draw_theme(&g, &l);
            for (;;) {
                const int k = gt_input(&g, &l, theme_count(), next_edge(&prev));
                if (k == GT_CLOSE) break;
                if (k == GT_CHANGED) { theme_set(l.theme, l.pol, l.pal); gset.theme = th_theme; gset.pol = th_pol; gset.pal = th_pal; }      /* applied at once; saved after a quiet second */
                draw_theme(&g, &l);
            }
        }
        else if (r == GP_OPEN_SETTINGS) { gsp_t sp = { 0 }; draw_settings(&sp); while (!gsp_input(&sp, &gset.resume, &gset.grid, &gset.dither, next_edge(&prev))) { fx_apply(); draw_settings(&sp); } }
        else if (r == GP_OPEN_CONTROLS) { gc_t c; gc_open(&c); draw_controls(&c); while (!gc_input(&c, next_edge(&prev))) draw_controls(&c); }
        else if (r == GP_OPEN_HELP) { gh_t h; gh_open(&h); draw_help(&h); while (!gh_input(&h, next_edge(&prev))) draw_help(&h); }
        else { draw_info(); for (;;) { const int k = gi_input(next_edge(&prev)); if (k == 1) break; if (k == 2) v1_pipe_off ^= 1u; draw_info(); } }
        draw_menu(&m);
    }
}

/* ---- the folders (version 1 index): the folder level and the pictures of one folder ---- */
static int folder_rec(uint32_t k, gx_folder_t *f)
{
    uint8_t b[GX1_FOLDER_BYTES];
    return read_bytes(SLOT_IDX, gx_folder_off(&head, k), GX1_FOLDER_BYTES, b) && !gx_parse_folder(&head, b, sizeof b, f);
}
static uint32_t grid_setting(void) { return gset.grid & 3u; }                                       /* the Settings choice of the grid size (0..3) */
static void apply_grid(void)                                                                      /* after the Settings page: the grid size may have changed; the same entry stays selected */
{
    if (level != 0u || head.version == 0u || gi == grid_setting()) return;
    set_grid(grid_setting()); page = (sel - cur_first) / pp; loaded_page = 0xFFFFFFFFu;
}
static void enter_folder(const gx_folder_t *f, uint32_t k)                                         /* the pictures of folder k: its range, the chosen grid */
{
    level = 0u; cur_first = f->first; cur_count = f->count; cur_folder = k; set_grid(head.version == 0u ? 4u : grid_setting());
    sel = cur_first; page = 0u; loaded_page = 0xFFFFFFFFu;
}
static void enter_folders(void)                                                                    /* the folder level: 4 x 3 tiles, the folder we came from selected */
{
    level = 1u; cur_first = 0u; cur_count = head.folder_count; set_grid(0u);
    sel = cur_folder; page = sel / pp; loaded_page = 0xFFFFFFFFu;
}
static int find_folder(uint32_t i, gx_folder_t *f, uint32_t *k)                                    /* the folder holding entry i: a binary search of the folder table (at most 8 small reads) */
{
    uint32_t lo = 0u, hi = head.folder_count;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2u;
        if (!folder_rec(mid, f)) return 0;
        if (i < f->first) hi = mid; else if (i >= f->first + f->count) lo = mid + 1u; else { *k = mid; return 1; }
    }
    return 0;
}

int main(void)
{
    if (!platform_version_ok()) platform_halt_pattern();
    theme_init();
    gfx_rect(0, 0, GFX_W, GFX_H, BG);
    platform_probe();
    gfx_text(6, 2, "TAU GALLERY", ACC, BG);
    { uint8_t *h = v1z_idx; int rc = 100;                                                       /* the boot-time read of the index head borrows the zoom index buffer (32 KB, unused until a zoom opens): 4 KB of RAM less, which the 128 KB link needs */
      if (read_bytes(SLOT_IDX, 0u, 4096u, h)) rc = gx_parse_head(h, 4096u, &head);
      if (rc == 100) { say("NO INDEX: TAU-LIBRARY.TDB NOT READ", BAD); for (;;) { } }
      if (rc) { char b[40], *q = put_s(b, "INDEX NOT USABLE E"); q = put_u(q, (uint32_t)rc); *q = 0; say(b, BAD); for (;;) { } }
      if (head.count == 0u) { say("NO PICTURES IN THE INDEX", BAD); for (;;) { } } }
    gs_hw_load(&gset); last_act = cycles(); fx_apply();
    cur_first = 0u; cur_count = head.count; cur_folder = 0u; level = 0u; set_grid(head.version == 0u ? 4u : grid_setting());
    if (head.version == 1u) {                                                                      /* the first folder, or the folder level when there are several */
        gx_folder_t f; if (!folder_rec(0u, &f)) { say("FOLDER TABLE READ FAILED", BAD); for (;;) { } }
        if (head.folder_count > 1u) enter_folders(); else enter_folder(&f, 0u);
    }
    theme_set(gset.theme, gset.pol, gset.pal); gset.theme = th_theme; gset.pol = th_pol; gset.pal = th_pal;      /* the saved look (a value this build does not have falls back to the default) */
    if (gset.resume && gset.last_idx < head.count) {                                           /* resume: only when it is the same picture (index and path CRC); a folder library resumes inside the picture's folder */
        gx_folder_t f; uint32_t k = 0u; int ok = 1;
        if (head.version == 1u) ok = find_folder(gset.last_idx, &f, &k);
        if (ok) {
            const uint32_t sv_level = level, sv_first = cur_first, sv_count = cur_count, sv_folder = cur_folder, sv_sel = sel, sv_page = page, sv_gi = gi;
            if (head.version == 1u) enter_folder(&f, k);
            page = (gset.last_idx - cur_first) / pp;
            const int loaded = load_page();
            const uint32_t i = loaded ? gs_resume_index(&gset, head.count, path_crc(gset.last_idx)) : 0u;
            if (loaded && (i != 0u || (gset.last_idx == 0u && path_crc(0u) == gset.last_crc))) { sel = i; page = (i - cur_first) / pp; }
            else { level = sv_level; cur_first = sv_first; cur_count = sv_count; cur_folder = sv_folder; sel = sv_sel; page = sv_page; set_grid(sv_gi); loaded_page = 0xFFFFFFFFu; }
        }
    }
    draw_page();
    uint32_t prev = 0, rep_key = 0u, rep_next = 0u;                                            /* a held direction key repeats: after REP_DELAY, then every REP_RATE */
    for (;;) {
        const uint32_t keys = REG_RD(R_INPUT) & 0xFFFFu, edge = keys & ~prev; prev = keys;
        if (edge) last_act = cycles();
        settings_tick();
        const uint32_t dkeys = KEY_LEFT | KEY_RIGHT | KEY_UP | KEY_DOWN, now = cycles();
        uint32_t dk = edge & dkeys, fresh = 1u;
        if (!edge && rep_key && (keys & rep_key) && (int32_t)(now - rep_next) >= 0) { dk = rep_key; fresh = 0u; }      /* the repeat: only the key that was pressed last */
        if (dk) {
            if (dk & KEY_LEFT) dk = KEY_LEFT; else if (dk & KEY_RIGHT) dk = KEY_RIGHT; else if (dk & KEY_UP) dk = KEY_UP; else dk = KEY_DOWN;
            if (fresh) rep_key = dk;
            const uint32_t n = cur_first + gv_grid_step(sel - cur_first, cur_count, GR.cols, dk == KEY_LEFT ? 0u : dk == KEY_RIGHT ? 1u : dk == KEY_UP ? 2u : 3u, fresh);
            if (n != sel) select_to(n);
            rep_next = cycles() + (fresh ? REP_DELAY : REP_RATE);                              /* from the end of the move: a page change takes its time, the repeat waits for it */
        }
        else if (edge & KEY_L1) { const uint32_t rel = sel - cur_first; select_to(cur_first + (rel >= pp ? rel - pp : 0u)); }
        else if (edge & KEY_R1) { const uint32_t rel = sel - cur_first; select_to(cur_first + (rel + pp < cur_count ? rel + pp : cur_count - 1u)); }
        else if (edge & KEY_A) {
            if (level == 1u) { gx_folder_t f; if (folder_rec(sel, &f)) { enter_folder(&f, sel); draw_page(); } else say("FOLDER READ FAILED", BAD); }
            else open_from_grid();
            rep_key = 0u;
        }
        else if (edge & KEY_B) { if (level == 0u && head.folder_count > 1u) { enter_folders(); draw_page(); } rep_key = 0u; }
        else if (edge & KEY_START) { run_pages(); apply_grid(); draw_page(); prev = REG_RD(R_INPUT) & 0xFFFFu; rep_key = 0u; }
        else if ((edge & KEY_SELECT) && level == 0u) { run_details(); draw_page(); prev = REG_RD(R_INPUT) & 0xFFFFu; rep_key = 0u; }
    }
}
