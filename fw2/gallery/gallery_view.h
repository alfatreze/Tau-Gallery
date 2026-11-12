/* fw2/gallery/gallery_view.h -- Tau Gallery M2.1 (docs/M2_DESIGN.md section 4): the rules of the zoomed viewer, as pure functions. No registers, no drawing. Host-tested (sim/test_gallery_view.py).
 * A picture has a ladder of at most three stops (fit, 1/2, 1:1). A viewport is the top-left corner of the 800 x 720 screen inside one stop's pixels. The tiles of a stop are 128 x 128; a resident window of
 * 9 x 8 tiles (1,152 x 1,024 pixels) holds the viewport plus a ring; a pan inside it only changes the base register, the window re-centres when the viewport nears its edge. Positions are int32 pixels; a
 * negative x or y is an axis shorter than the screen, centred (nothing to pan). */
#ifndef GALLERY_VIEW_H
#define GALLERY_VIEW_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
#define GV_SCREEN_W 800
#define GV_SCREEN_H 720
#define GV_TILE 128
#define GV_TILE_BYTES 49152u                 /* 128 x 128 x 3 */
#define GV_WIN_TX 11                         /* the resident window, in tiles */
#define GV_WIN_TY 10
#define GV_WIN_W (GV_WIN_TX * GV_TILE)       /* 1,408 */
#define GV_WIN_H (GV_WIN_TY * GV_TILE)       /* 1,280 */
#define GV_WIN_STRIDE_WORDS (GV_WIN_W * 3 / 2)   /* 2,112 words a window line (R_VIEW_STRIDE takes 16 bits) */
#define GV_MARGIN 128                        /* re-centre when the viewport is closer than this to a window edge that is not the level's edge: the ring left ahead then lasts about as long as composing the next window takes */
#define GV_BEHIND 128                        /* a window placed for a pan keeps this much (to the next tile) behind the viewport, the rest of its slack ahead of it */
#define GV_CMD_MAX 49152u                    /* the most bytes of one card command while panning (about 30 ms): the keys are polled between commands */
#define GV_MAX_STOPS 3
#define GV_MAX_TILES (GV_WIN_TX * GV_WIN_TY)
#define Q16 65536

/* ---- the ladder ---- */
typedef struct { uint32_t w, h, scale_q16; } gv_stop_t;      /* the stop's size in its own pixels, and its scale against the source (Q16) */
typedef struct { gv_stop_t stop[GV_MAX_STOPS]; uint32_t n; } gv_ladder_t;
static inline uint32_t gv_round_div(uint64_t a, uint64_t b) { return (uint32_t)((a + b / 2u) / b); }
static inline gv_stop_t gv_make_stop(uint32_t sw, uint32_t sh, uint32_t scale_q16)
{
    gv_stop_t s; s.scale_q16 = scale_q16;
    s.w = gv_round_div((uint64_t)sw * scale_q16, Q16); s.h = gv_round_div((uint64_t)sh * scale_q16, Q16);
    if (s.w == 0u) s.w = 1u;
    if (s.h == 0u) s.h = 1u;
    return s;
}
/* The stops for a source of sw x sh pixels. Fit is the M1 canvas (never enlarged). 1/2 only for a big source (1,600 pixels wide or 1,440 tall) and only when it is at least 1.5x past the previous stop;
 * 1:1 only when the source is larger than the screen. A source no larger than the screen has the one stop. */
static inline gv_ladder_t gv_ladder(uint32_t sw, uint32_t sh)
{
    gv_ladder_t l; uint32_t fq = Q16;
    l.n = 0;
    { uint64_t a = (uint64_t)GV_SCREEN_W * Q16 / sw, b = (uint64_t)GV_SCREEN_H * Q16 / sh; fq = (uint32_t)(a < b ? a : b); if (fq > (uint32_t)Q16) fq = Q16; }
    l.stop[l.n++] = gv_make_stop(sw, sh, fq);
    const int big = sw > (uint32_t)GV_SCREEN_W || sh > (uint32_t)GV_SCREEN_H;
    if (!big) return l;
#if BUG == 8
    if (sw >= 1600u || sh >= 1440u) {                                                            /* no 1.5x rule: a half stop right next to fit */
#else
    if ((sw >= 1600u || sh >= 1440u) && (uint64_t)(Q16 / 2) * 2u >= (uint64_t)fq * 3u) {           /* 0.5 >= 1.5 * fit */
#endif
        l.stop[l.n++] = gv_make_stop(sw, sh, Q16 / 2);
    }
    l.stop[l.n++] = gv_make_stop(sw, sh, Q16);
    return l;
}
/* A (the fit/1:1 key): from fit to the last stop, from any other stop back to fit. */
static inline uint32_t gv_toggle(const gv_ladder_t *l, uint32_t cur) { return cur == 0u ? l->n - 1u : 0u; }
/* L1 / R1: one stop out or in, no wrap. */
static inline uint32_t gv_zoom_step(const gv_ladder_t *l, uint32_t cur, int dir) { return dir > 0 ? (cur + 1u < l->n ? cur + 1u : cur) : (cur ? cur - 1u : 0u); }

/* ---- clamp and alignment ---- */
static inline int32_t gv_floor4(int32_t v) { return v & ~3; }
static inline int32_t gv_clamp_axis(int32_t v, int32_t size, int32_t screen, int align)
{
    if (size <= screen) { int32_t o = (screen - size) / 2; return -(o & ~1); }              /* centred: an even offset, as the M1 canvas */
    int32_t hi = size - screen;
    if (align) hi = (hi + 3) & ~3;                                                           /* the last true column is reachable; the 0 to 3 columns past the edge are the tile padding (black) */
    if (v < 0) v = 0;
    if (align) v = gv_floor4(v);
    if (v > hi) v = hi;
    return v;
}
static inline void gv_clamp(const gv_stop_t *s, int32_t *x, int32_t *y)
{
#if BUG == 2
    *x = gv_clamp_axis(*x, (int32_t)s->w, GV_SCREEN_W, 0);                                   /* the 4-pixel alignment is dropped */
#else
    *x = gv_clamp_axis(*x, (int32_t)s->w, GV_SCREEN_W, 1);
#endif
#if BUG == 1
    *y = gv_clamp_axis(*y, (int32_t)s->h, GV_SCREEN_H + 1, 0);                               /* the bottom limit is one row too low */
#else
    *y = gv_clamp_axis(*y, (int32_t)s->h, GV_SCREEN_H, 0);
#endif
}
/* The stored size of a stop: the tiles are whole, the padding is black. */
static inline uint32_t gv_storage(uint32_t v) { return (v + GV_TILE - 1u) / GV_TILE * GV_TILE; }

/* ---- the zoom mapping: the centre of the screen stays on the same point of the picture ---- */
static inline void gv_zoom_map(const gv_stop_t *from, const gv_stop_t *to, int32_t x, int32_t y, int32_t *nx, int32_t *ny)
{
    const int64_t cx = (int64_t)x + GV_SCREEN_W / 2, cy = (int64_t)y + GV_SCREEN_H / 2;      /* the screen centre in the old stop (for a centred axis this is size / 2) */
#if BUG == 4
    int64_t tx = (int64_t)x * to->scale_q16 / from->scale_q16, ty = (int64_t)y * to->scale_q16 / from->scale_q16;      /* the corner is scaled, not the centre */
    *nx = (int32_t)tx; *ny = (int32_t)ty;
#else
    const int64_t tcx = cx * to->scale_q16 / from->scale_q16, tcy = cy * to->scale_q16 / from->scale_q16;
    *nx = (int32_t)(tcx - GV_SCREEN_W / 2); *ny = (int32_t)(tcy - GV_SCREEN_H / 2);
#endif
    gv_clamp(to, nx, ny);
}

/* ---- pan speed and movement ---- */
/* The card delivers about one 128 px column of tiles in 270 ms (7 tiles of 38 ms), so a pan faster than 480 px/s only waits for it in bursts: the pan is capped at 8 pixels a frame. */
static inline int32_t gv_speed(uint32_t held_frames)
{
#if BUG == 5
    return held_frames < 8u ? 4 : 10;                                                        /* a final speed that is not a multiple of 4 pixels */
#else
    return held_frames < 8u ? 4 : 8;
#endif
}
/* One frame of a held direction (dirx, diry in -1, 0, 1). */
static inline void gv_move(const gv_stop_t *s, int32_t *x, int32_t *y, int dirx, int diry, uint32_t held)
{
    const int32_t st = gv_speed(held);
    *x += dirx * st; *y += diry * st;
    gv_clamp(s, x, y);
}

/* ---- the tiles a viewport needs ---- */
typedef struct { int16_t tx, ty; uint8_t ring; } gv_tile_t;      /* ring = 0 for a tile the screen touches, 1 for one of the one-tile ring */
static inline int32_t gv_floordiv(int32_t a, int32_t b) { int32_t q = a / b; if ((a % b != 0) && ((a < 0) != (b < 0))) q--; return q; }
/* The window origin (tile aligned, in stop pixels) that puts the viewport near the middle of the window, clamped to the stored stop. */
static inline void gv_window_for(const gv_stop_t *s, int32_t vx, int32_t vy, int32_t *wx, int32_t *wy)
{
    const int32_t sw = (int32_t)gv_storage(s->w), sh = (int32_t)gv_storage(s->h);
    int32_t ox = gv_floordiv(vx + GV_SCREEN_W / 2 - GV_WIN_W / 2 + GV_TILE / 2, GV_TILE) * GV_TILE, oy = gv_floordiv(vy + GV_SCREEN_H / 2 - GV_WIN_H / 2 + GV_TILE / 2, GV_TILE) * GV_TILE;
#if BUG == 7
    (void)sh;
#else
    if (oy > sh - GV_WIN_H) oy = sh - GV_WIN_H;
    if (ox > sw - GV_WIN_W) ox = sw - GV_WIN_W;
#endif
    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;
    *wx = ox; *wy = oy;
}
/* One axis of a window placed for a pan. d > 0: the viewport sits GV_BEHIND to GV_BEHIND + 127 pixels from the trailing edge; d < 0: the same from the other edge; d == 0: the middle. Clamped to the stored stop. */
static inline int32_t gv_lead_axis(int32_t v, int32_t span, int32_t win, int32_t store, int d)
{
    int32_t o = gv_floordiv(v + span / 2 - win / 2 + GV_TILE / 2, GV_TILE) * GV_TILE;
#if BUG == 19
    if (d < 0) o = gv_floordiv(v - GV_BEHIND, GV_TILE) * GV_TILE; else if (d > 0) o = -gv_floordiv(-(v + span + GV_BEHIND - win), GV_TILE) * GV_TILE;
#else
    if (d > 0) o = gv_floordiv(v - GV_BEHIND, GV_TILE) * GV_TILE; else if (d < 0) o = -gv_floordiv(-(v + span + GV_BEHIND - win), GV_TILE) * GV_TILE;
#endif
    if (o > store - win) o = store - win;
    if (o < 0) o = 0;
    return o;
}
/* The window for a viewport that is panning in direction (dx, dy): placed so the pan has most of the window's slack ahead of it. (0, 0) is the same as gv_window_for. */
static inline void gv_window_lead(const gv_stop_t *s, int32_t vx, int32_t vy, int dx, int dy, int32_t *wx, int32_t *wy)
{
    *wx = gv_lead_axis(vx, GV_SCREEN_W, GV_WIN_W, (int32_t)gv_storage(s->w), dx);
    *wy = gv_lead_axis(vy, GV_SCREEN_H, GV_WIN_H, (int32_t)gv_storage(s->h), dy);
}
/* 1 when the viewport still has GV_MARGIN pixels to every window edge, except an edge that is the edge of the stored stop (nothing lies beyond it). */
static inline int gv_window_ok(const gv_stop_t *s, int32_t wx, int32_t wy, int32_t vx, int32_t vy)
{
    const int32_t sw = (int32_t)gv_storage(s->w), sh = (int32_t)gv_storage(s->h);
    if (vx < wx || vy < wy || vx + GV_SCREEN_W > wx + GV_WIN_W || vy + GV_SCREEN_H > wy + GV_WIN_H) return s->w <= GV_SCREEN_W && s->h <= GV_SCREEN_H;      /* a stop no bigger than the screen has no window to leave */
    if (wx > 0 && vx - wx < GV_MARGIN) return 0;
    if (wy > 0 && vy - wy < GV_MARGIN) return 0;
    if (wx + GV_WIN_W < sw && (wx + GV_WIN_W) - (vx + GV_SCREEN_W) < GV_MARGIN) return 0;
    if (wy + GV_WIN_H < sh && (wy + GV_WIN_H) - (vy + GV_SCREEN_H) < GV_MARGIN) return 0;
    return 1;
}
/* The tiles of the window for a viewport, screen tiles first (nearest the screen centre first), then the ring (the ones ahead in the direction of travel first). dx, dy: the direction of travel. Returns the count (at most GV_MAX_TILES). */
static inline uint32_t gv_tiles_needed(const gv_stop_t *s, int32_t vx, int32_t vy, int32_t wx, int32_t wy, int dx, int dy, gv_tile_t *out)
{
    const int32_t ntx = (int32_t)gv_storage(s->w) / GV_TILE, nty = (int32_t)gv_storage(s->h) / GV_TILE;
    const int32_t cx = vx + GV_SCREEN_W / 2, cy = vy + GV_SCREEN_H / 2;
    int32_t key[GV_MAX_TILES]; uint32_t n = 0;
    for (int32_t ty = wy / GV_TILE; ty < wy / GV_TILE + GV_WIN_TY && ty < nty; ty++) {
        for (int32_t tx = wx / GV_TILE; tx < wx / GV_TILE + GV_WIN_TX && tx < ntx; tx++) {
            const int32_t x0 = tx * GV_TILE, y0 = ty * GV_TILE;
#if BUG == 3
            const int on = x0 < vx + GV_SCREEN_W - GV_TILE && x0 + GV_TILE > vx && y0 < vy + GV_SCREEN_H && y0 + GV_TILE > vy;      /* the last column of tiles is missed */
#else
            const int on = x0 < vx + GV_SCREEN_W && x0 + GV_TILE > vx && y0 < vy + GV_SCREEN_H && y0 + GV_TILE > vy;
#endif
            const int32_t mx = x0 + GV_TILE / 2 - cx, my = y0 + GV_TILE / 2 - cy;
            /* screen tiles: by distance to the screen centre. Ring tiles, three tiers: the strip the pan will bring on screen next (ahead along a travelling axis, within the screen's band on the other axis: the visible columns and rows, nearest first), then the corners ahead (nearest first), then the rest by distance. */
            int32_t k;
            if (on) k = (mx * mx + my * my) / 16;
            else {
                const int32_t vr = vx + GV_SCREEN_W, vb = vy + GV_SCREEN_H;
                const int aheadx = dx > 0 ? x0 >= vr : dx < 0 ? x0 + GV_TILE <= vx : 0, aheady = dy > 0 ? y0 >= vb : dy < 0 ? y0 + GV_TILE <= vy : 0;
                const int overx = x0 < vr && x0 + GV_TILE > vx, overy = y0 < vb && y0 + GV_TILE > vy;
                const int32_t gapx = x0 >= vr ? x0 - vr : x0 + GV_TILE <= vx ? vx - (x0 + GV_TILE) : 0, gapy = y0 >= vb ? y0 - vb : y0 + GV_TILE <= vy ? vy - (y0 + GV_TILE) : 0;
#if BUG == 20
                k = 0x30000000 + (gapx * gapx + gapy * gapy) / 16;                         /* no strip tier */
#else
                if ((aheadx && overy) || (aheady && overx)) k = 0x10000000 + ((aheadx && overy) ? gapx : gapy);
                else if (aheadx || aheady) k = 0x20000000 + gapx + gapy;
                else k = 0x30000000 + (gapx * gapx + gapy * gapy) / 16;
#endif
#if BUG == 21
                if (k < 0x20000000) k = 0x10000000 + 0xFFFFF - (k - 0x10000000);          /* the strip farthest first */
#endif
            }
            key[n] = k; out[n].tx = (int16_t)tx; out[n].ty = (int16_t)ty; out[n].ring = on ? 0u : 1u; n++;
        }
    }
    /* Stable bottom-up merge sort of the indices by key (ties keep their raster order, exactly what the insertion sort of earlier builds gave; that one cost 4 ms of CPU on every tile pass with 110 tiles, measured on a Pocket,
     * docs/AUDIT_TRAIL.md B-1097). */
    uint8_t ia[GV_MAX_TILES], ib[GV_MAX_TILES]; uint8_t *src = ia, *dst = ib;
    for (uint32_t i = 0; i < n; i++) ia[i] = (uint8_t)i;
    for (uint32_t w = 1u; w < n; w *= 2u) {
        for (uint32_t lo = 0; lo < n; lo += 2u * w) {
            const uint32_t mid = lo + w < n ? lo + w : n, hi = lo + 2u * w < n ? lo + 2u * w : n; uint32_t a = lo, b = mid, o = lo;
#if BUG == 23
            while (a < mid && b < hi) dst[o++] = key[src[a]] < key[src[b]] ? src[a++] : src[b++];                  /* not stable: equal keys take the right one first */
#else
            while (a < mid && b < hi) dst[o++] = key[src[b]] < key[src[a]] ? src[b++] : src[a++];
#endif
            while (a < mid) dst[o++] = src[a++];
            while (b < hi) dst[o++] = src[b++];
        }
        uint8_t *t = src; src = dst; dst = t;
    }
    gv_tile_t tmp[GV_MAX_TILES];
    for (uint32_t i = 0; i < n; i++) tmp[i] = out[src[i]];
    for (uint32_t i = 0; i < n; i++) out[i] = tmp[i];
    return n;
}

/* ---- the tile job queue and the command size ---- */
/* The next card command of a read of `remaining` bytes: at most GV_CMD_MAX, a whole number of 4 KB pieces. */
static inline uint32_t gv_next_cmd_len(uint32_t remaining)
{
#if BUG == 6
    return remaining < GV_CMD_MAX + 4096u ? remaining : GV_CMD_MAX + 4096u;                  /* one piece over the limit */
#else
    return remaining < GV_CMD_MAX ? remaining : GV_CMD_MAX;
#endif
}
typedef struct { gv_tile_t job[GV_MAX_TILES]; uint32_t head, n; } gv_queue_t;
static inline void gv_q_clear(gv_queue_t *q) { q->head = 0; q->n = 0; }
static inline int gv_q_has(const gv_queue_t *q, int tx, int ty) { for (uint32_t i = 0; i < q->n; i++) { const gv_tile_t *t = &q->job[(q->head + i) % GV_MAX_TILES]; if (t->tx == tx && t->ty == ty) return 1; } return 0; }
static inline void gv_q_push(gv_queue_t *q, gv_tile_t t) { if (q->n < GV_MAX_TILES && !gv_q_has(q, t.tx, t.ty)) { q->job[(q->head + q->n) % GV_MAX_TILES] = t; q->n++; } }
static inline int gv_q_pop(gv_queue_t *q, gv_tile_t *t) { if (!q->n) return 0; *t = q->job[q->head]; q->head = (q->head + 1u) % GV_MAX_TILES; q->n--; return 1; }
/* A direction reversal or a jump: keep only the queued tiles that the new needed list still contains (the command in flight finishes), in the new priority order. */
static inline void gv_q_retarget(gv_queue_t *q, const gv_tile_t *need, uint32_t nneed, const uint8_t *landed)
{
    gv_queue_t k; gv_q_clear(&k);
    for (uint32_t i = 0; i < nneed; i++) if (!landed[i]) gv_q_push(&k, need[i]);
#if BUG == 9
    (void)q;                                                                                 /* the old queue is kept as it was */
#else
    *q = k;
#endif
}

/* ---- option A: a raster zoom level is read in chunks into a plane the size of the level; rows are what the screen needs ---- */
/* The file's rows are `stride` bytes apart; a chunk is GV_CHUNK bytes (a whole number of 4 KB pieces) of the level, landed at the same offset of the zoom plane, so the plane holds the level at its own
 * stride and a pan is a register change. The chunks loaded are always one contiguous interval [c_lo, c_hi): loading extends it at either end. A row is usable only when ALL of its bytes are loaded. */
#define GV_CHUNK GV_CMD_MAX
#define GV_PREFETCH_ROWS 240                 /* rows loaded ahead of the screen in the direction of travel */
static inline uint32_t gv_nchunks(uint32_t padded_len) { return (padded_len + GV_CHUNK - 1u) / GV_CHUNK; }
/* The rows [rlo, rhi) that lie wholly inside chunks [c_lo, c_hi) of a level of `len` bytes of rows. */
static inline void gv_rows_loaded(uint32_t stride, uint32_t len, uint32_t c_lo, uint32_t c_hi, uint32_t *rlo, uint32_t *rhi)
{
    if (c_hi <= c_lo) { *rlo = 0; *rhi = 0; return; }
    const uint64_t b0 = (uint64_t)c_lo * GV_CHUNK; uint64_t b1 = (uint64_t)c_hi * GV_CHUNK;
    if (b1 > len) b1 = len;
#if BUG == 10
    *rlo = (uint32_t)(b0 / stride);                                                          /* the row that starts before the interval counts */
#else
    *rlo = (uint32_t)((b0 + stride - 1u) / stride);
#endif
    *rhi = (uint32_t)(b1 / stride);
    if (*rhi < *rlo) *rhi = *rlo;
}
/* The next chunk to land (returns 1 and sets *out), or 0 when nothing is needed. Order: the screen's own rows first (the top if it is not covered, then the bottom), then the prefetch ahead in the direction
 * of travel (dir > 0 down, < 0 up, 0 none). From an empty interval the first chunk is the one that holds the first byte of row want_y. One chunk a call: the caller does one a pass of its loop. */
static inline int gv_next_chunk(uint32_t stride, uint32_t len, uint32_t nch, uint32_t c_lo, uint32_t c_hi, uint32_t want_y, int dir, uint32_t *out)
{
    const uint32_t rows = len / stride;
    if (c_hi <= c_lo) { uint32_t c = (uint32_t)((uint64_t)want_y * stride / GV_CHUNK); if (c >= nch) c = nch - 1u; *out = c; return 1; }
    uint32_t rlo, rhi; gv_rows_loaded(stride, len, c_lo, c_hi, &rlo, &rhi);
    uint32_t need_end = want_y + GV_SCREEN_H; if (need_end > rows) need_end = rows;
#if BUG == 11
    (void)c_lo;                                                                              /* the top of the screen is never completed */
#else
    if (rlo > want_y && c_lo > 0u) { *out = c_lo - 1u; return 1; }
#endif
    if (rhi < need_end && c_hi < nch) { *out = c_hi; return 1; }
    uint32_t ahead_end = want_y + GV_SCREEN_H + GV_PREFETCH_ROWS; if (ahead_end > rows) ahead_end = rows;
    const uint32_t ahead_start = want_y > GV_PREFETCH_ROWS ? want_y - GV_PREFETCH_ROWS : 0u;
#if BUG == 12
    if (dir < 0 && c_hi < nch && rhi < ahead_end) { *out = c_hi; return 1; }                 /* the prefetch goes the wrong way */
    if (dir > 0 && c_lo > 0u && rlo > ahead_start) { *out = c_lo - 1u; return 1; }
#else
    if (dir > 0 && c_hi < nch && rhi < ahead_end) { *out = c_hi; return 1; }
    if (dir < 0 && c_lo > 0u && rlo > ahead_start) { *out = c_lo - 1u; return 1; }
#endif
    return 0;
}
/* The row at which the screen may be shown: the wanted one, held inside the loaded rows [rlo, rhi) so that no pixel that has not landed is ever on screen. Returns 0 when fewer than a screenful is loaded. */
static inline int gv_display_y(int32_t want_y, uint32_t rlo, uint32_t rhi, int32_t *y)
{
    if (rhi < rlo + GV_SCREEN_H) return 0;
#if BUG == 13
    *y = want_y; return 1;                                                                    /* the screen runs ahead of the loaded rows */
#else
    int32_t v = want_y; if (v < (int32_t)rlo) v = (int32_t)rlo; if (v > (int32_t)(rhi - GV_SCREEN_H)) v = (int32_t)(rhi - GV_SCREEN_H);
    *y = v; return 1;
#endif
}
/* The halfword address of plane line 0 for a viewport at (x, y) of a raster level whose plane starts at plane_hw and whose rows are stride_hw halfwords apart. x is a multiple of 4: four pixels are six halfwords. */
static inline uint32_t gv_plane_base(uint32_t plane_hw, uint32_t stride_hw, int32_t x, int32_t y)
{
#if BUG == 14
    return plane_hw + (uint32_t)y * stride_hw + (uint32_t)(x >> 2) * 5u;                      /* five halfwords for four pixels */
#else
    return plane_hw + (uint32_t)y * stride_hw + (uint32_t)(x >> 2) * 6u;
#endif
}

/* ---- option B: tile levels copied by Talos into a resident window (docs/M2_DESIGN.md sections 3, 4 and 9) ---- */
/* A window is 9 x 8 tiles at tile-aligned origin (wx, wy) of the level; its tile (col, row) sits at halfword col * 192 + row * 128 * 1,728 from the window's base. The Talos BLIT moves a tile in row pieces: a row is at
 * most 127 words, the strides are 10 bits, so a 192-halfword tile row is two pieces of 96 and each command carries its own offsets (docs section 9, M2.0). */
#define GV_TILE_HW 192u                      /* a tile row in halfwords: 128 pixels * 3 bytes / 2 */
#define GV_PIECE_HW 96u                      /* halfwords in one BLIT command (at most 127) */
#define GV_PIECES (GV_TILE * 2u)             /* BLIT commands that copy one tile: 128 rows x 2 pieces */
#define GV_WIN_HW ((uint32_t)GV_WIN_W * GV_WIN_H * 3u / 2u)      /* halfwords in one window */
/* Where tile (tx, ty) is in a window with origin (wx, wy): returns 1 and sets col and row, 0 when it lies outside. */
static inline int gv_win_tile(int32_t wx, int32_t wy, int32_t tx, int32_t ty, int *col, int *row)
{
    const int32_t c = tx - wx / GV_TILE, r = ty - wy / GV_TILE;
    if (c < 0 || r < 0 || c >= GV_WIN_TX || r >= GV_WIN_TY) return 0;
    *col = (int)c; *row = (int)r; return 1;
}
/* The halfword offset of the top-left pixel of the tile at (col, row) of a window. */
static inline uint32_t gv_tile_hw(int col, int row)
{
#if BUG == 15
    return (uint32_t)row * GV_TILE * GV_WIN_STRIDE_WORDS + (uint32_t)col * 190u;                  /* a tile column is not 192 halfwords */
#else
    return (uint32_t)row * GV_TILE * GV_WIN_STRIDE_WORDS + (uint32_t)col * GV_TILE_HW;
#endif
}
/* Command k (0 .. GV_PIECES - 1) of a tile copy: the source and destination offsets of its piece, relative to the tile's top-left in the source (rows src_stride halfwords apart) and in the window. */
static inline void gv_piece(uint32_t k, uint32_t src_stride, uint32_t *src_off, uint32_t *dst_off)
{
    const uint32_t row = k / 2u, piece = k % 2u;
#if BUG == 16
    *src_off = row * src_stride + piece * GV_PIECE_HW; *dst_off = row * GV_TILE_HW + piece * GV_PIECE_HW;      /* the destination steps by a tile row, not a window line */
#else
    *src_off = row * src_stride + piece * GV_PIECE_HW; *dst_off = row * GV_WIN_STRIDE_WORDS + piece * GV_PIECE_HW;
#endif
}
/* The halfword address of plane line 0 when the viewport (vx, vy) is shown from the window whose base is wbase and origin (wx, wy). vx - wx is a multiple of 4. */
static inline uint32_t gv_win_base(uint32_t wbase, int32_t vx, int32_t vy, int32_t wx, int32_t wy)
{
    return wbase + (uint32_t)(vy - wy) * GV_WIN_STRIDE_WORDS + (uint32_t)((vx - wx) >> 2) * 6u;
}
/* 1 when the viewport (vx, vy) may be shown from a window: it lies inside the window and every tile the screen touches is resident (res[row * 9 + col] != 0). A stop smaller than the screen is not shown from a window. */
static inline int gv_screen_ready(const gv_stop_t *s, int32_t vx, int32_t vy, int32_t wx, int32_t wy, const uint8_t *res)
{
    if (vx < wx || vy < wy || vx + GV_SCREEN_W > wx + GV_WIN_W || vy + GV_SCREEN_H > wy + GV_WIN_H) return 0;
    const int32_t ntx = (int32_t)gv_storage(s->w) / GV_TILE, nty = (int32_t)gv_storage(s->h) / GV_TILE;
    for (int32_t ty = vy / GV_TILE; ty <= (vy + GV_SCREEN_H - 1) / GV_TILE && ty < nty; ty++)
        for (int32_t tx = vx / GV_TILE; tx <= (vx + GV_SCREEN_W - 1) / GV_TILE && tx < ntx; tx++) {
            int col, row; if (!gv_win_tile(wx, wy, tx, ty, &col, &row)) return 0;
#if BUG == 17
            (void)res;                                                                           /* residency is not looked at */
#else
            if (!res[row * GV_WIN_TX + col]) return 0;
#endif
        }
    return 1;
}

/* The corner of the screen in a new stop that keeps the screen centre on the same point of the picture. A canvas can carry the picture at an offset (a raster level padded to 800 x 720, the fit canvas): (ofx, ofy)
 * is where the picture starts in the old canvas, (otx, oty) in the new one (0, 0 for a tile level). s_from and s_to are the stops' scales against the source (Q16). No clamp: the caller clamps in the new stop. */
static inline void gv_map_corner(uint32_t s_from, int32_t ofx, int32_t ofy, uint32_t s_to, int32_t otx, int32_t oty, int32_t x, int32_t y, int32_t *nx, int32_t *ny)
{
#if BUG == 18
    (void)ofx; (void)ofy; (void)otx; (void)oty; const int64_t cx = (int64_t)x + GV_SCREEN_W / 2, cy = (int64_t)y + GV_SCREEN_H / 2;      /* the padding of the canvases is ignored */
    *nx = (int32_t)(cx * s_to / s_from - GV_SCREEN_W / 2); *ny = (int32_t)(cy * s_to / s_from - GV_SCREEN_H / 2);
#else
    const int64_t cx = (int64_t)x + GV_SCREEN_W / 2 - ofx, cy = (int64_t)y + GV_SCREEN_H / 2 - ofy;
    *nx = (int32_t)(cx * s_to / s_from + otx - GV_SCREEN_W / 2); *ny = (int32_t)(cy * s_to / s_from + oty - GV_SCREEN_H / 2);
#endif
}

/* ---- the map: the fit view with the rectangle of a stop's screen drawn on it, to jump to another part of the picture ---- */
#define GV_RECT_T 2                          /* the outline is 2 pixels thick: a pixel pair is 3 halfwords, so an edge is a whole number of them */
/* The screen of a stop (its corner (vx, vy) in the stop's canvas) on the fit canvas. (fox, foy): where the picture starts on the fit canvas, sf: the fit scale (Q16); (okx, oky) and sk the same for the stop. rx is a multiple of 4,
 * rw and rh are even (no smaller than 8). */
static inline void gv_map_rect(uint32_t sf, int32_t fox, int32_t foy, uint32_t sk, int32_t okx, int32_t oky, int32_t vx, int32_t vy, int32_t *rx, int32_t *ry, int32_t *rw, int32_t *rh)
{
    int64_t w = ((int64_t)GV_SCREEN_W * sf + sk / 2u) / sk, h = ((int64_t)GV_SCREEN_H * sf + sk / 2u) / sk;
    w &= ~(int64_t)1; h &= ~(int64_t)1; if (w < 8) w = 8; if (h < 8) h = 8;
#if BUG == 22
    *rx = gv_floor4((int32_t)((int64_t)vx * sf / sk + fox)); *ry = (int32_t)((int64_t)vy * sf / sk + foy);      /* the stop's padding is ignored */
#else
    *rx = gv_floor4((int32_t)((int64_t)(vx - okx) * sf / sk + fox)); *ry = (int32_t)((int64_t)(vy - oky) * sf / sk + foy);
#endif
    *rw = (int32_t)w; *rh = (int32_t)h;
}
/* The corner in a stop's canvas for a rectangle at (rx, ry) on the fit canvas: the inverse of gv_map_rect. No clamp: the caller clamps in the stop. */
static inline void gv_map_view(uint32_t sf, int32_t fox, int32_t foy, uint32_t sk, int32_t okx, int32_t oky, int32_t rx, int32_t ry, int32_t *vx, int32_t *vy)
{
    *vx = (int32_t)((int64_t)(rx - fox) * sk / sf + okx); *vy = (int32_t)((int64_t)(ry - foy) * sk / sf + oky);
}
/* Keeps a rectangle (rw x rh) over the picture (pw x ph at (px, py) on the fit canvas): inside it, with rx on a multiple of 4; a rectangle larger than the picture is centred on it. */
static inline void gv_map_clamp(int32_t px, int32_t py, int32_t pw, int32_t ph, int32_t rw, int32_t rh, int32_t *rx, int32_t *ry)
{
    if (rw >= pw) *rx = gv_floor4(px - (rw - pw) / 2);
    else { const int32_t lo = (px + 3) & ~3, hi = gv_floor4(px + pw - rw); *rx = *rx < lo ? lo : *rx > hi ? (hi < lo ? lo : hi) : *rx; *rx = gv_floor4(*rx); }
    if (rh >= ph) *ry = py - (rh - ph) / 2;
    else { const int32_t lo = py, hi = py + ph - rh; *ry = *ry < lo ? lo : *ry > hi ? hi : *ry; }
}
#endif
