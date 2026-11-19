#include "tgy_hw.h"
#include "gallery_fmt.h"
#include "gallery_rot.h"
#include "../platform/mmio.h"

#define TH_HEAD 64u
#define TH_LEVEL 48u
#define TH_IDX 12u
#define TH_TIMEOUT_CYC (CLK_HZ / 2u)
static uint32_t th16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t th32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t g_status, g_loaded_y = 0xFFFFFFFFu, g_loaded_c = 0xFFFFFFFFu;

int tgy_open(tgy_file_t *f, const uint8_t *img, uint32_t n, uint32_t base)
{
    if (n < 4096u) return TH_SHORT;
    tgy_hw_forget();                                                              /* a new file (the landing window is reused for every picture): the decoder's tables are stale */
    if (img[0] != 'T' || img[1] != 'G' || img[2] != 'Y' || img[3] != 0) return TH_MAGIC;
    if (th16(img + 4) != 1u || th16(img + 6) != TH_HEAD) return TH_VERSION;
    if (gf_crc32(img, 52u) != th32(img + 52)) return TH_CRC;
    const uint32_t nlev = th32(img + 20), lt = th32(img + 24), tab = th32(img + 40), tlen = th32(img + 44), size = th32(img + 48);
    if (n > size || (base & 1u)) return base & 1u ? TH_ODD : TH_RANGE;                 /* n bytes of the file are resident: the whole file, or a prefix (the fit level first) */
    if (nlev < 1u || nlev > 16u || lt < TH_HEAD || lt > 4096u || nlev * TH_LEVEL > 4096u - lt) return TH_RANGE;         /* the level table and the tables are inside the first 4 KB (container spec section 1) */
    if (tab < TH_HEAD || tab > 4096u || tlen < 2u || tlen > 4096u - tab) return TH_RANGE;
    const uint32_t nt = th16(img + tab);
    if (nt > 8u || 2u + nt * 128u > tlen || (tab & 1u)) return TH_RANGE;
    f->hdr = img; f->base = base; f->n = n; f->size = size; f->width = th32(img + 8); f->height = th32(img + 12); f->nlevels = nlev; f->level_off = lt; f->tables_off = tab; f->ntables = nt;
    return TH_OK;
}

int tgy_level(const tgy_file_t *f, uint32_t i, tgy_level_t *l)
{
    if (i >= f->nlevels) return TH_NOLEVEL;
    const uint8_t *e = f->hdr + f->level_off + i * TH_LEVEL;
    const uint32_t w = th32(e), h = th32(e + 4), layout = e[16], mode = e[17], fmt = e[18], kind = e[19], qy = e[20], qc = e[21], log2 = e[22];
    if (layout != 1u || mode != 1u || log2 != 7u) return TH_UNSUPPORTED;
    if (fmt > 2u) return TH_UNSUPPORTED;                                           /* 0 grey, 1 4:4:4 (the core's decoder is built with C444, K6), 2 4:2:0 */
    if (w < 1u || h < 1u || w > 65535u || h > 65535u) return TH_RANGE;
    const uint32_t cols = (w + TH_TILE - 1u) / TH_TILE, rows = (h + TH_TILE - 1u) / TH_TILE;
    const uint32_t doff = th32(e + 24), dlen = th32(e + 28), ioff = th32(e + 32), ilen = th32(e + 36);
    if (ilen != rows * cols * TH_IDX || ioff < TH_HEAD || ioff > f->n || ilen > f->n - ioff) return TH_RANGE;
    if (doff < TH_HEAD || doff > f->n || dlen > f->n - doff) return TH_RANGE;
    if (qy >= f->ntables || (fmt != 0u && qc >= f->ntables)) return TH_RANGE;
    l->w = w; l->h = h; l->cols = cols; l->rows = rows; l->data_off = doff; l->data_len = dlen; l->idx_off = ioff; l->idx_len = ilen; l->idx_crc = th32(e + 40);
    l->rx = (uint16_t)th16(e + 8); l->ry = (uint16_t)th16(e + 10); l->rw = (uint16_t)th16(e + 12); l->rh = (uint16_t)th16(e + 14);
    l->fmt = (uint8_t)fmt; l->kind = (uint8_t)kind; l->qy = (uint8_t)qy; l->qc = (uint8_t)(fmt != 0u ? qc : qy); l->idx = 0; l->fetch = 0; l->fctx = 0;
    return TH_OK;
}

int tgy_find_level(const tgy_file_t *f, unsigned kind, tgy_level_t *l)
{
    for (uint32_t i = 0; i < f->nlevels; i++) { tgy_level_t t; if (tgy_level(f, i, &t) == TH_OK && t.kind == kind) { *l = t; return TH_OK; } }
    return TH_NOLEVEL;
}
int tgy_load_index(const tgy_file_t *f, tgy_level_t *l, tgy_read_fn rd, void *ctx, uint8_t *buf, uint32_t cap)
{
    (void)f;
    if (l->idx_len > cap) return TH_RANGE;
    if (!rd(ctx, l->idx_off, l->idx_len, buf)) return TH_SHORT;
    if (gf_crc32(buf, l->idx_len) != l->idx_crc) return TH_CRC;
    l->idx = buf;
    return TH_OK;
}

int tgy_verify_index(const tgy_file_t *f, const tgy_level_t *l, tgy_read_fn rd, void *ctx, uint8_t *buf, uint32_t cap)
{
    (void)f;
    if (cap == 0u) return TH_RANGE;
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t pos = 0; pos < l->idx_len; ) {
        const uint32_t n = l->idx_len - pos < cap ? l->idx_len - pos : cap;
        if (!rd(ctx, l->idx_off + pos, n, buf)) return TH_SHORT;
        c = gf_crc32_step(c, buf, n); pos += n;
    }
    return ~c == l->idx_crc ? TH_OK : TH_CRC;
}

int tgy_hw_present(void) { return (REG_RD(R_TGY_STATUS) >> 31) & 1u; }
uint32_t tgy_hw_status(void) { return g_status; }
void tgy_hw_forget(void) { g_loaded_y = g_loaded_c = 0xFFFFFFFFu; }

static int wait_idle(void)
{
    const uint32_t t0 = REG_RD(R_CYCLES); uint32_t s;
    do { s = REG_RD(R_TGY_STATUS); if (!(s & 1u)) break; } while (REG_RD(R_CYCLES) - t0 < TH_TIMEOUT_CYC);
    g_status = s;
    return (s & 1u) ? TH_TIMEOUT : TH_OK;
}

int tgy_hw_tables(const tgy_file_t *f, const tgy_level_t *l)
{
    const uint32_t qy = f->base + f->tables_off + 2u + 128u * l->qy, qc = f->base + f->tables_off + 2u + 128u * l->qc;
    if ((qy | qc) & 1u) return TH_ODD;
    if (qy == g_loaded_y && qc == g_loaded_c) return TH_OK;
    if (!tgy_hw_present()) return TH_ABSENT;
    if (REG_RD(R_TGY_STATUS) & 1u) return TH_BUSY;
    REG_WR(R_TGY_QY, qy); REG_WR(R_TGY_QC, qc); REG_WR(R_TGY_CTRL, 8u); REG_WR(R_TGY_CTRL, 2u);
    int r = wait_idle(); if (r != TH_OK) return r;
    if ((g_status & 4u) || !(g_status & 8u)) return TH_HWERR;
    g_loaded_y = qy; g_loaded_c = qc;
    return TH_OK;
}

int tgy_tile_loc(const tgy_level_t *l, uint32_t tx, uint32_t ty, uint32_t *off, uint32_t *len)
{
    if (tx >= l->cols || ty >= l->rows || (!l->idx && !l->fetch)) return TH_RANGE;
    uint8_t tmp[TH_IDX]; const uint8_t *ent;
    if (l->idx) ent = l->idx + (ty * l->cols + tx) * TH_IDX;
    else if (l->fetch(l->fctx, l->idx_off + (ty * l->cols + tx) * TH_IDX, tmp)) ent = tmp;      /* an index kept on the card: one entry through the caller's cache */
    else return TH_SHORT;
    const uint32_t o = th32(ent), n = th32(ent + 4);
    if (o > l->data_len || n > l->data_len - o || n >= (1u << 20)) return TH_RANGE;
    *off = l->data_off + o; *len = n;                                             /* a file offset: where the tile's stream starts in the file */
    return TH_OK;
}

static struct { uint32_t on; int32_t stx, sty; } g_sc;                              /* the scatter mode of the next tile starts (K7): off after reset */
void tgy_hw_scatter(int on, int32_t stx, int32_t sty) { g_sc.on = on ? 1u : 0u; g_sc.stx = stx; g_sc.sty = sty; }

static uint32_t g_hist_on;                                                          /* the histogram counts the tiles that start now (K4) */
static void hist_wait(void) { for (uint32_t i = 0; i < 100000u && (REG_RD(R_HIST_CTRL) & 0x100u); i++) { } }
void tgy_hw_hist_begin(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
{
    REG_WR(R_HIST_CTRL, 2u); hist_wait();                                            /* clear (counting off while it sweeps) */
    REG_WR(R_HIST_RECT0, (y0 << 16) | x0); REG_WR(R_HIST_RECT1, (y1 << 16) | x1);
    REG_WR(R_HIST_CTRL, 1u); g_hist_on = 1u;
}
void tgy_hw_hist_end(void) { REG_WR(R_HIST_CTRL, 0u); g_hist_on = 0u; }
int tgy_hw_hist_read256(uint32_t out[4][256], uint32_t *n)
{
    for (uint32_t k = 0; k < 4u; k++)
        for (uint32_t i = 0; i < 256u; i++) {
            REG_WR(R_HIST_IDX, (k << 8) | i); uint32_t v = REG_RD(R_HIST_DATA), t = 0;
            while ((v >> 31) && t++ < 100000u) v = REG_RD(R_HIST_DATA);
            if (v >> 31) return 1;                                                   /* never answered */
            out[k][i] = v & 0xFFFFFFu;
        }
    *n = REG_RD(R_HIST_N) & 0xFFFFFFu;
    return 0;
}
int tgy_hw_start_from(const tgy_file_t *f, const tgy_level_t *l, uint32_t tx, uint32_t ty, uint32_t dst, uint32_t pitch, uint32_t src)
{
    uint32_t foff, len; int r = tgy_tile_loc(l, tx, ty, &foff, &len); if (r != TH_OK) return r;
    r = tgy_hw_tables(f, l); if (r != TH_OK) return r;
    const uint32_t x0 = tx * TH_TILE, y0 = ty * TH_TILE, tw = (l->w - x0 < TH_TILE) ? l->w - x0 : TH_TILE, th = (l->h - y0 < TH_TILE) ? l->h - y0 : TH_TILE;
    if (g_hist_on) REG_WR(R_HIST_ORG, (y0 << 16) | x0);                              /* where this tile's pixel (0, 0) is, for the window test */
    REG_WR(R_TGY_SRC, src); REG_WR(R_TGY_LEN, len); REG_WR(R_TGY_DST, dst); REG_WR(R_TGY_PITCH, pitch);
    if (g_sc.on) { REG_WR(R_TGY_STX, (uint32_t)g_sc.stx & 0x3FFFFu); REG_WR(R_TGY_STY, (uint32_t)g_sc.sty & 0x3FFFFu); }
    REG_WR(R_TGY_GEOM, (g_sc.on ? (1u << 18) : 0u) | ((uint32_t)l->fmt << 16) | (tw << 8) | th); REG_WR(R_TGY_QY, f->base + f->tables_off + 2u + 128u * l->qy); REG_WR(R_TGY_QC, f->base + f->tables_off + 2u + 128u * l->qc);
    REG_WR(R_TGY_CTRL, 8u);                                                        /* CLEAR, then START in a separate store (the configuration crosses with the command) */
    REG_WR(R_TGY_CTRL, 1u);
    return TH_OK;                                                                  /* the decoder runs by itself from here: the stream at `src` must stay as it is until tgy_hw_finish says done */
}

int tgy_hw_finish(void)
{
    int r = wait_idle();
    REG_WR(R_IRQ_PEND, 0x10u);                                                     /* the done interrupt is not used by this driver: drop its pending bit */
    if (r != TH_OK) { REG_WR(R_TGY_CTRL, 4u); return r; }
    if (g_status & 0x10u) return TH_BUSY;                                          /* the start was rejected: the decoder was busy */
    if (g_status & 4u) return TH_HWERR;
    return TH_OK;
}

uint32_t tgy_hw_cycles(void) { return REG_RD(R_TGY_CYCLES); }                       /* SDRAM-clock cycles the last tile took (valid after done) */

int tgy_hw_tile_from(const tgy_file_t *f, const tgy_level_t *l, uint32_t tx, uint32_t ty, uint32_t dst, uint32_t pitch, uint32_t src)
{
    int r = tgy_hw_start_from(f, l, tx, ty, dst, pitch, src); if (r != TH_OK) return r;
    return tgy_hw_finish();
}

int tgy_hw_tile(const tgy_file_t *f, const tgy_level_t *l, uint32_t tx, uint32_t ty, uint32_t dst, uint32_t pitch)
{
    uint32_t foff, len; int r = tgy_tile_loc(l, tx, ty, &foff, &len); if (r != TH_OK) return r;
    return tgy_hw_tile_from(f, l, tx, ty, dst, pitch, f->base + foff);          /* the stream is where the file is resident: base + its file offset */
}

int tgy_hw_level(const tgy_file_t *f, const tgy_level_t *l, uint32_t dst0, uint32_t pitch, int (*tick)(void *ctx, uint32_t tx, uint32_t ty), void *ctx)
{
    for (uint32_t ty = 0; ty < l->rows; ty++)
        for (uint32_t tx = 0; tx < l->cols; tx++) {
            int r = tgy_hw_tile(f, l, tx, ty, dst0 + ty * TH_TILE * pitch + tx * TH_TILE * 3u, pitch); if (r != TH_OK) return r;
            if (tick && tick(ctx, tx, ty)) return TH_ABORT;
        }
    return TH_OK;
}

int tgy_hw_level_rot(const tgy_file_t *f, const tgy_level_t *l, uint32_t dst0, uint32_t pitch, uint32_t o, int (*tick)(void *ctx, uint32_t tx, uint32_t ty), void *ctx)
{
    if (o >= GR_NO) return TH_RANGE;
    tgy_hw_scatter(1, gr_stx(o, (int32_t)pitch), gr_sty(o, (int32_t)pitch)); int r = TH_OK;
    for (uint32_t ty = 0; ty < l->rows && r == TH_OK; ty++)
        for (uint32_t tx = 0; tx < l->cols && r == TH_OK; tx++) {
            r = tgy_hw_tile(f, l, tx, ty, dst0 + gr_tile_off(o, l->w, l->h, tx * TH_TILE, ty * TH_TILE, pitch), pitch);
            if (r == TH_OK && tick && tick(ctx, tx, ty)) r = TH_ABORT;
        }
    tgy_hw_scatter(0, 3, 0);
    return r;
}

int tgy_hw_show(const tgy_file_t *f, const tgy_level_t *l, uint32_t plane, int (*tick)(void *ctx, uint32_t tx, uint32_t ty), void *ctx)
{
    if ((uint32_t)l->rx + l->w > GT_CANVAS_W || (uint32_t)l->ry + l->h > GT_CANVAS_H) return TH_RANGE;      /* a tile must not leave the canvas: the decoder writes whole tiles */
    return tgy_hw_level(f, l, plane + (uint32_t)l->ry * GT_STRIDE + (uint32_t)l->rx * 3u, GT_STRIDE, tick, ctx);
}
