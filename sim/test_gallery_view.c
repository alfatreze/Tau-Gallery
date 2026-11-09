/* Host test of fw2/gallery/gallery_view.h (the viewport rules of M2). Compile with -DBUG=n for the mutants (1..9 must fail). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../fw2/gallery/gallery_view.h"
static int fails;
static void chk(const char *w, int ok) { printf("%s %s\n", ok ? "ok  " : "FAIL", w); if (!ok) fails++; }
static uint32_t rs = 12345u;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static int in_list(const gv_tile_t *l, uint32_t n, int tx, int ty) { for (uint32_t i = 0; i < n; i++) if (l[i].tx == tx && l[i].ty == ty) return 1; return 0; }

int main(void)
{
    /* ---- the ladder ---- */
    gv_ladder_t l = gv_ladder(4000, 3000);
    chk("ladder 4000 x 3000: fit 800 x 600, half 2000 x 1500, 1:1", l.n == 3 && l.stop[0].w == 800 && l.stop[0].h == 600 && l.stop[1].w == 2000 && l.stop[1].h == 1500 && l.stop[2].w == 4000 && l.stop[2].scale_q16 == (uint32_t)Q16);
    l = gv_ladder(1600, 1200);
    chk("ladder 1600 x 1200: fit is already 0.5, so no half stop (under 1.5x past fit)", l.n == 2 && l.stop[0].w == 800 && l.stop[1].w == 1600);
    l = gv_ladder(1000, 800);
    chk("ladder 1000 x 800: not big enough for a half stop", l.n == 2 && l.stop[0].w == 800 && l.stop[0].h == 640 && l.stop[1].w == 1000);
    l = gv_ladder(800, 720);
    chk("ladder 800 x 720: one stop, no zoom", l.n == 1 && l.stop[0].w == 800 && l.stop[0].scale_q16 == (uint32_t)Q16);
    l = gv_ladder(600, 400);
    chk("ladder 600 x 400: one stop, never enlarged", l.n == 1 && l.stop[0].w == 600 && l.stop[0].h == 400);
    l = gv_ladder(4000, 600);
    chk("ladder 4000 x 600 (a panorama): fit 800 x 120, half, 1:1", l.n == 3 && l.stop[0].w == 800 && l.stop[0].h == 120 && l.stop[1].h == 300);
    l = gv_ladder(1700, 700);
    chk("ladder 1700 x 700: fit 0.47 is within 1.5x of 0.5, no half stop", l.n == 2);
    l = gv_ladder(4000, 3000);
    chk("toggle: fit goes to 1:1, any other stop goes to fit", gv_toggle(&l, 0) == 2 && gv_toggle(&l, 1) == 0 && gv_toggle(&l, 2) == 0);
    chk("zoom step: in and out by one stop, no wrap", gv_zoom_step(&l, 0, 1) == 1 && gv_zoom_step(&l, 1, 1) == 2 && gv_zoom_step(&l, 2, 1) == 2 && gv_zoom_step(&l, 0, -1) == 0 && gv_zoom_step(&l, 2, -1) == 1);

    /* ---- clamp and alignment ---- */
    gv_stop_t s = gv_make_stop(4000, 3000, Q16 / 2);                  /* 2000 x 1500 */
    int32_t x, y;
    x = -100; y = -5; gv_clamp(&s, &x, &y); chk("clamp: below zero goes to 0", x == 0 && y == 0);
    x = 5000; y = 5000; gv_clamp(&s, &x, &y); chk("clamp: the far corner is size minus screen (1,200 x 780)", x == 1200 && y == 780);
    x = 1203; y = 7; gv_clamp(&s, &x, &y); chk("clamp: x is a multiple of 4, y is not forced to one", x == 1200 && y == 7);
    s = gv_make_stop(2001, 1500, Q16);
    x = 5000; y = 0; gv_clamp(&s, &x, &y); chk("clamp: the last true column is reachable (x limit rounds up to 1,204)", x == 1204);
    s = gv_make_stop(800, 600, Q16);
    x = 40; y = 40; gv_clamp(&s, &x, &y); chk("a stop 800 x 600 is centred: x 0, y -60", x == 0 && y == -60);
    s = gv_make_stop(600, 400, Q16);
    x = 40; y = 40; gv_clamp(&s, &x, &y); chk("a stop 600 x 400 is centred with even offsets: x -100, y -160", x == -100 && y == -160);
    s = gv_make_stop(601, 401, Q16);
    x = 0; y = 0; gv_clamp(&s, &x, &y); chk("a centred offset is rounded to an even number: 601 x 401 gives x -98, y -158 (not -99, -159)", x == -98 && y == -158);
    s = gv_make_stop(4000, 3000, Q16);
    int okal = 1;
    for (int i = 0; i < 5000; i++) { x = (int32_t)(rnd() % 9000) - 2000; y = (int32_t)(rnd() % 9000) - 2000; gv_clamp(&s, &x, &y); if ((x & 3) || x < 0 || y < 0 || x > 3200 || y > 2280) okal = 0; }
    chk("clamp: 5,000 random positions are aligned and inside [0, size - screen]", okal);
    chk("sizes: the window line (1,728 words) fits the 16-bit stride register, and the window is 3,538,944 bytes", GV_WIN_STRIDE_WORDS == 1728 && GV_WIN_STRIDE_WORDS < 65536 && (uint32_t)GV_WIN_W * GV_WIN_H * 3u == 3538944u);

    /* ---- the zoom mapping ---- */
    l = gv_ladder(4000, 3000); int32_t nx, ny;
    gv_zoom_map(&l.stop[0], &l.stop[1], 0, -60, &nx, &ny);
    chk("zoom fit to half: the centre (400, 300) of fit is (1,000, 750) of half: corner (600, 390)", nx == 600 && ny == 390);
    gv_zoom_map(&l.stop[1], &l.stop[2], nx, ny, &nx, &ny);
    chk("zoom half to 1:1: corner (1,600, 1,140)", nx == 1600 && ny == 1140);
    gv_zoom_map(&l.stop[2], &l.stop[0], nx, ny, &nx, &ny);
    chk("zoom 1:1 back to fit: the centred corner (0, -60)", nx == 0 && ny == -60);
    int okr = 1;
    for (int i = 0; i < 3000; i++) {
        x = 400 + (int32_t)(rnd() % 2401); y = 360 + (int32_t)(rnd() % 1561); gv_clamp(&l.stop[2], &x, &y);      /* interior positions: the centre-keeping map does not meet the half stop's edge */
        gv_zoom_map(&l.stop[2], &l.stop[1], x, y, &nx, &ny); int32_t bx, by; gv_zoom_map(&l.stop[1], &l.stop[2], nx, ny, &bx, &by);
        if (abs(bx - x) > 8 || abs(by - y) > 8) okr = 0;
    }
    chk("zoom round trip 1:1 to half and back stays within 8 pixels (3,000 interior positions)", okr);
    gv_zoom_map(&l.stop[2], &l.stop[1], 3200, 2280, &nx, &ny);
    chk("zoom out at the far corner lands inside the half stop (aligned, clamped to its edge): the view moves toward the middle", nx == 1200 && ny == 780);

    /* ---- pan speed and movement ---- */
    chk("pan speed: 4 for 8 frames, then 8 for 16, then 16", gv_speed(0) == 4 && gv_speed(7) == 4 && gv_speed(8) == 8 && gv_speed(23) == 8 && gv_speed(24) == 16 && gv_speed(5000) == 16);
    s = gv_make_stop(4000, 3000, Q16); x = 0; y = 0; int okm = 1;
    for (int f = 0; f < 400; f++) { gv_move(&s, &x, &y, 1, 1, (uint32_t)f); if ((x & 3) || x > 3200 || y > 2280) okm = 0; }
    chk("a held diagonal for 400 frames stays aligned and clamps at the far corner", okm && x == 3200 && y == 2280);
    for (int f = 0; f < 400; f++) gv_move(&s, &x, &y, -1, 0, (uint32_t)f);
    chk("then a held Left returns to 0 and stops", x == 0 && y == 2280);

    /* ---- the window ---- */
    s = gv_make_stop(4000, 3000, Q16); int32_t wx, wy; int okw = 1;
    for (int i = 0; i < 3000; i++) {
        x = (int32_t)(rnd() % 3201); y = (int32_t)(rnd() % 2281); gv_clamp(&s, &x, &y); gv_window_for(&s, x, y, &wx, &wy);
        if (wx % GV_TILE || wy % GV_TILE || wx < 0 || wy < 0 || wx + GV_WIN_W > (int32_t)gv_storage(s.w) || wy + GV_WIN_H > (int32_t)gv_storage(s.h)) okw = 0;
        if (x < wx || y < wy || x + GV_SCREEN_W > wx + GV_WIN_W || y + GV_SCREEN_H > wy + GV_WIN_H) okw = 0;
        if (!gv_window_ok(&s, wx, wy, x, y)) okw = 0;
    }
    chk("window: tile aligned, inside the stored stop, holds the viewport with its margins (3,000 random viewports)", okw);
    gv_window_for(&s, 3200, 2280, &wx, &wy);
    chk("window at the far corner is clamped to the stored stop (4,096 x 3,072 stored)", wx == 4096 - GV_WIN_W && wy == 3072 - GV_WIN_H);
    gv_window_for(&s, 1000, 800, &wx, &wy); int32_t px = 1000; int moved = 0;
    while (gv_window_ok(&s, wx, wy, px, 800) && px < 3200) { px += 4; moved++; }
    chk("window: a pan right leaves the safe zone after a bounded distance (needs a re-centre)", px < 3200 && moved > 5 && moved < 130);
    gv_window_for(&s, px, 800, &wx, &wy);
    chk("window: re-centring puts the viewport back in the safe zone", gv_window_ok(&s, wx, wy, px, 800));
    s = gv_make_stop(4000, 3000, Q16);
    chk("window margins: 30 pixels from the left edge is too close, 64 is enough", !gv_window_ok(&s, 1024, 1024, 1024 + 30, 1100) && gv_window_ok(&s, 1024, 1024, 1024 + 64, 1100));
    chk("window margins: 30 pixels from the bottom edge is too close, 64 is enough", !gv_window_ok(&s, 1024, 1024, 1100, 1024 + GV_WIN_H - GV_SCREEN_H - 30) && gv_window_ok(&s, 1024, 1024, 1100, 1024 + GV_WIN_H - GV_SCREEN_H - 64));
    chk("window margins: 30 pixels from the top and right edges is too close", !gv_window_ok(&s, 1024, 1024, 1100, 1024 + 30) && !gv_window_ok(&s, 1024, 1024, 1024 + GV_WIN_W - GV_SCREEN_W - 30, 1100));
    s = gv_make_stop(4000, 3000, Q16 / 2); gv_window_for(&s, 0, 0, &wx, &wy);
    chk("window: at the edge of the stop no re-centre is asked for (nothing lies beyond)", wx == 0 && wy == 0 && gv_window_ok(&s, wx, wy, 0, 0));
    s = gv_make_stop(800, 600, Q16 / 2 > 0 ? Q16 : Q16); gv_window_for(&s, 0, -60, &wx, &wy);
    chk("a stop smaller than the window: the window origin is 0, 0", wx == 0 && wy == 0);

    /* ---- the tiles a viewport needs ---- */
    gv_tile_t need[GV_MAX_TILES]; int okt = 1; int okfirst = 1, okord = 1, okdup = 1, okwin = 1, okcount = 1;
    s = gv_make_stop(4000, 3000, Q16);
    for (int i = 0; i < 400; i++) {
        x = (int32_t)(rnd() % 3201); y = (int32_t)(rnd() % 2281); gv_clamp(&s, &x, &y); gv_window_for(&s, x, y, &wx, &wy);
        const int dx = (int)(rnd() % 3) - 1, dy = (int)(rnd() % 3) - 1;
        const uint32_t n = gv_tiles_needed(&s, x, y, wx, wy, dx, dy, need);
        for (int k = 0; k < 400; k++) {                                                                  /* every pixel we sample of the screen has its tile in the list */
            const int px2 = x + (int)(rnd() % GV_SCREEN_W), py2 = y + (int)(rnd() % GV_SCREEN_H);
            if (px2 >= 0 && py2 >= 0 && px2 < 4000 && py2 < 3000 && !in_list(need, n, px2 / GV_TILE, py2 / GV_TILE)) okt = 0;
        }
        { const int cx0 = x / GV_TILE, cx1 = (x + GV_SCREEN_W - 1) / GV_TILE, cy0 = y / GV_TILE, cy1 = (y + GV_SCREEN_H - 1) / GV_TILE;
          for (int ty = cy0; ty <= cy1; ty++) for (int tx = cx0; tx <= cx1; tx++) if (!in_list(need, n, tx, ty)) okt = 0;
          uint32_t ns = 0; for (uint32_t j = 0; j < n; j++) if (!need[j].ring) ns++;
          if (ns != (uint32_t)((cx1 - cx0 + 1) * (cy1 - cy0 + 1))) okcount = 0; }
        int seen_ring = 0;
        for (uint32_t j = 0; j < n; j++) { if (need[j].ring) seen_ring = 1; else if (seen_ring) okfirst = 0; }
        for (uint32_t j = 0; j < n; j++) for (uint32_t k = j + 1; k < n; k++) if (need[j].tx == need[k].tx && need[j].ty == need[k].ty) okdup = 0;
        for (uint32_t j = 0; j < n; j++) if (need[j].tx * GV_TILE < wx || need[j].tx * GV_TILE >= wx + GV_WIN_W || need[j].ty * GV_TILE < wy || need[j].ty * GV_TILE >= wy + GV_WIN_H || need[j].tx >= 32 || need[j].ty >= 24) okwin = 0;
        { const int32_t cx = x + 400, cy = y + 360; int32_t last = -1;
          for (uint32_t j = 0; j < n && !need[j].ring; j++) { const int32_t mx = need[j].tx * GV_TILE + 64 - cx, my = need[j].ty * GV_TILE + 64 - cy, d = (mx * mx + my * my) / 16; if (d < last) okord = 0; last = d; } }
    }
    chk("tiles: every sampled screen pixel's tile is in the list (400 viewports x 400 pixels)", okt);
    chk("tiles: exactly the tiles the screen touches come first (count = rows x columns)", okcount && okfirst);
    chk("tiles: the screen tiles are ordered nearest the centre first", okord);
    chk("tiles: no tile twice, all inside the window and the stored stop", okdup && okwin);
    x = 1600; y = 1200; gv_window_for(&s, x, y, &wx, &wy);
    { gv_tile_t a[GV_MAX_TILES]; uint32_t n = gv_tiles_needed(&s, x, y, wx, wy, 1, 0, a), first = 0; while (first < n && !a[first].ring) first++;
      chk("tiles: moving right, the first ring tile is on the right of the screen", first < n && a[first].tx * GV_TILE >= x + GV_SCREEN_W - GV_TILE); }
    { gv_tile_t a[GV_MAX_TILES]; uint32_t n = gv_tiles_needed(&s, x, y, wx, wy, -1, 0, a), first = 0; while (first < n && !a[first].ring) first++;
      chk("tiles: moving left, the first ring tile is on the left of the screen", first < n && a[first].tx * GV_TILE < x); }
    { s = gv_make_stop(800, 600, Q16); gv_tile_t a[GV_MAX_TILES]; gv_window_for(&s, 0, -60, &wx, &wy); uint32_t n = gv_tiles_needed(&s, 0, -60, wx, wy, 0, 0, a);
      chk("tiles: a centred stop 800 x 600 needs its 7 x 5 stored tiles and no more", n == 7u * 5u); }

    /* ---- the queue and the command size ---- */
    gv_queue_t q; gv_q_clear(&q); gv_tile_t t; gv_tile_t j1 = { 1, 1, 0 }, j2 = { 2, 1, 0 }, j3 = { 3, 1, 1 };
    gv_q_push(&q, j1); gv_q_push(&q, j2); gv_q_push(&q, j3); gv_q_push(&q, j2);
    chk("queue: first in, first out; a tile queued twice is queued once", q.n == 3 && gv_q_pop(&q, &t) && t.tx == 1 && gv_q_pop(&q, &t) && t.tx == 2 && gv_q_pop(&q, &t) && t.tx == 3 && !gv_q_pop(&q, &t));
    gv_q_clear(&q); gv_q_push(&q, j1); gv_q_push(&q, j2); gv_q_push(&q, j3);
    gv_tile_t nd[3] = { { 3, 1, 0 }, { 9, 9, 0 }, { 2, 1, 1 } }; uint8_t landed[3] = { 0, 0, 1 };
    gv_q_retarget(&q, nd, 3, landed);
    chk("queue: a reversal keeps the new needed tiles that have not landed, in the new order, and drops the rest", q.n == 2 && gv_q_pop(&q, &t) && t.tx == 3 && gv_q_pop(&q, &t) && t.tx == 9);
    uint32_t rem = GV_TILE_BYTES * 3u, sum = 0, mx = 0; int okc = 1;
    while (rem) { const uint32_t c = gv_next_cmd_len(rem); if (c == 0 || c > GV_CMD_MAX || (c & 4095u)) okc = 0; if (c > mx) mx = c; sum += c; rem -= c; }
    chk("command size: three tiles (147,456 bytes) are read in commands of at most 48 KB, whole 4 KB pieces, and add up", okc && sum == GV_TILE_BYTES * 3u && mx == GV_CMD_MAX);
    chk("command size: a 4 KB remainder is one 4 KB command", gv_next_cmd_len(4096u) == 4096u && gv_next_cmd_len(100000u) == GV_CMD_MAX);
    /* ---- option A: chunks, rows and the base register ---- */
    { int okr = 1;
      for (int i = 0; i < 4000; i++) {
          const uint32_t stride = 4u * (800u + rnd() % 1248u), h = 720u + rnd() % 1500u, len = stride * h, padded = (len + 4095u) & ~4095u, nch = gv_nchunks(padded);
          const uint32_t c_lo = rnd() % nch, c_hi = c_lo + 1u + rnd() % (nch - c_lo);
          uint32_t rlo, rhi; gv_rows_loaded(stride, len, c_lo, c_hi, &rlo, &rhi);
          uint32_t blo = 0xFFFFFFFFu, bhi = 0;                                                  /* brute force: the rows whose every byte is inside the interval */
          for (uint32_t r = 0; r < h; r++) { const uint64_t s = (uint64_t)r * stride, e = s + stride; if (s >= (uint64_t)c_lo * GV_CHUNK && e <= (uint64_t)c_hi * GV_CHUNK && e <= len) { if (blo == 0xFFFFFFFFu) blo = r; bhi = r + 1u; } }
          if (blo == 0xFFFFFFFFu) { if (rhi > rlo) okr = 0; } else if (rlo != blo || rhi != bhi) okr = 0;
      }
      chk("rows loaded: the rows wholly inside the chunk interval, against brute force (4,000 levels and intervals)", okr); }
    { int okn = 1, okcov = 1, okadj = 1, okpre = 1;
      for (int i = 0; i < 600; i++) {
          const uint32_t stride = 4u * (800u + rnd() % 1248u), h = 720u + rnd() % 1500u, len = stride * h, padded = (len + 4095u) & ~4095u, nch = gv_nchunks(padded), rows = h;
          const uint32_t want = rnd() % (h - 719u); const int dir = (int)(rnd() % 3) - 1;
          uint32_t lo = 0, hi = 0, c, steps = 0; uint8_t seen[1024]; memset(seen, 0, sizeof seen);
          while (gv_next_chunk(stride, len, nch, lo, hi, want, dir, &c)) {
              if (c >= nch || seen[c]) { okn = 0; break; } seen[c] = 1;
              if (hi <= lo) { lo = c; hi = c + 1u; } else if (c + 1u == lo) lo = c; else if (c == hi) hi++; else { okadj = 0; break; }
              if (++steps > nch + 2u) { okn = 0; break; }
          }
          uint32_t rlo, rhi; gv_rows_loaded(stride, len, lo, hi, &rlo, &rhi);
          uint32_t need_end = want + 720u; if (need_end > rows) need_end = rows;
          if (!(rlo <= want && rhi >= need_end)) okcov = 0;
          { const uint32_t want2 = want > 300u ? want - 300u : 0u; memset(seen, 0, sizeof seen); steps = 0;                    /* the screen then moves UP: the top rows must be completed from below the interval */
            while (gv_next_chunk(stride, len, nch, lo, hi, want2, 0, &c)) { if (c >= nch || seen[c]) { okn = 0; break; } seen[c] = 1; if (c + 1u == lo) lo = c; else if (c == hi) hi++; else { okadj = 0; break; } if (++steps > nch + 2u) { okn = 0; break; } }
            gv_rows_loaded(stride, len, lo, hi, &rlo, &rhi); uint32_t ne2 = want2 + 720u; if (ne2 > rows) ne2 = rows; if (!(rlo <= want2 && rhi >= ne2)) okcov = 0; }
          if (dir > 0 && hi < nch && rhi < (want + 720u + GV_PREFETCH_ROWS < rows ? want + 720u + GV_PREFETCH_ROWS : rows)) okpre = 0;
          if (dir < 0 && lo > 0u && rlo > (want > GV_PREFETCH_ROWS ? want - GV_PREFETCH_ROWS : 0u)) okpre = 0;
      }
      chk("next chunk: from empty it loads adjacent chunks, each once, within the level, and stops (600 levels and views)", okn && okadj);
      chk("next chunk: when it stops, every row of the screen is loaded", okcov);
      chk("next chunk: the prefetch ahead in the direction of travel is loaded too (up to its limit or the level's end)", okpre); }
    { /* a held pan down with one chunk landing a frame: the screen is held inside the loaded rows, never ahead of them */
      const uint32_t stride = 6000u, h = 1500u, len = stride * h, nch = gv_nchunks((len + 4095u) & ~4095u);
      uint32_t lo = 0, hi = 0, c; int32_t want = 390, shown = -1; int okp = 1, hadshow = 0, lag = 0;
      for (int f = 0; f < 3000; f++) {
          if (f > 300 && want < (int32_t)(h - 720u)) want += 16;
          if (gv_next_chunk(stride, len, nch, lo, hi, (uint32_t)want, f > 300 ? 1 : 0, &c)) { if (hi <= lo) { lo = c; hi = c + 1u; } else if (c + 1u == lo) lo = c; else if (c == hi) hi++; }
          uint32_t rlo, rhi; gv_rows_loaded(stride, len, lo, hi, &rlo, &rhi); int32_t y;
          if (gv_display_y(want, rlo, rhi, &y)) { hadshow = 1; shown = y; if (y < (int32_t)rlo || y + 720 > (int32_t)rhi) okp = 0; if (y < want && f > 600) lag = 1; }
      }
      chk("a held pan with one 48 KB chunk a frame: the screen is always inside the loaded rows, and it reaches the end of the level", okp && hadshow && shown == (int32_t)(h - 720u));
      chk("display row: held to the loaded rows, and none is shown when less than a screenful is loaded", gv_display_y(100, 200, 1000, &shown) && shown == 200 && gv_display_y(900, 200, 1000, &shown) && shown == 280 && !gv_display_y(0, 0, 700, &shown));
      (void)lag; }
    { int okb = 1;
      for (int i = 0; i < 3000; i++) { const uint32_t sw = 800u + rnd() % 1249u, stride_b = (sw * 3u + 3u) & ~3u, plane = 0x400000u; const int32_t x = (int32_t)(rnd() % ((sw - 800u) / 4u + 1u)) * 4, y = (int32_t)(rnd() % 700u);
          const uint32_t base = gv_plane_base(plane, stride_b / 2u, x, y); if (base != plane + ((uint32_t)y * stride_b + (uint32_t)x * 3u) / 2u) okb = 0; }
      chk("plane base: the halfword address of pixel (x, y) for x a multiple of 4, against the byte formula (3,000 cases)", okb); }
    printf(fails ? "FAILED %d\n" : "PASSED\n", fails);
    return fails != 0;
}
