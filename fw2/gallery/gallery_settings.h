/* fw2/gallery/gallery_settings.h -- Tau Gallery M1.6 slice (a) (docs/M1_DESIGN.md section 6): the persisted settings of the gallery. Pure: no registers. Host-tested (sim/test_gallery_settings.py).
 * The Pocket saves the core's persisted interact.json variables; the firmware reaches them as words of a register file (gallery_settings.c). Only words below 16 are used (the narrow file reaches them).
 *   word 0 (variable 10)  bits 30..27 the layout version (GS_LAYOUT_VERSION; anything else = the file is not ours, use the defaults)
 *   word 1 (variable 11)  appearance, the player's bit positions: accent colour bits 4..0 (0..18), theme bits 10..5, polarity bit 11 (0 dark, 1 light); bits 14..12 (the font) are reserved and kept
 *   word 2 (variable 12)  bit 0 resume OFF (stored inverted: the player's default of this variable has bit 0 clear, so the package needs no change and a fresh card resumes)
 *   word 5 (variable 15)  bits 30..0 the CRC-32 of the last picture's path, 31 bits
 *   word 6 (variable 16)  bits 30..0 the last picture's index number
 *   word 8 (variable 20)  Gallery 1.0 look settings, in a persisted variable the gallery does not otherwise use (the Pocket keeps at most 16 variables of a core: a 17th is dropped silently): grid size bits 1..0
 *                         (0 4 x 3, 1 5 x 4, 2 6 x 4, 3 8 x 6), dither bits 3..2 STORED AS (dither + 2) % 3 so that the package's default 0 is the default setting (0 stored = ordered, 1 = ordered + moving, 2 = off),
 *                         colour preset bits 6..4 (0 none; presets are numbered by the pixel-effects code). Bit 31 is never touched; other bits stay
 * Bits that are not ours stay (read-modify-write); bit 31 is never touched. */
#ifndef GALLERY_SETTINGS_H
#define GALLERY_SETTINGS_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
#define GS_LAYOUT_VERSION 1u
#define GS_WORDS 16u
#define GS_W_LAYOUT 0u
#define GS_W_LOOK   1u
#define GS_W_RESUME 2u
#define GS_W_CRC    5u
#define GS_W_INDEX  6u
#define GS_W_FX     8u
#define GS_M31 0x7FFFFFFFu
typedef struct { uint32_t resume, last_idx, last_crc, theme, pol, pal, grid, dither, lut; } gs_vals_t;      /* theme, pol, pal: raw as saved (6, 1 and 5 bits); the theme module validates them against the build */
static inline gs_vals_t gs_defaults(void) { gs_vals_t v = { 1u, 0u, 0u, 0u, 0u, 1u, 0u, 1u, 0u }; return v; }
static inline int gs_same(const gs_vals_t *a, const gs_vals_t *b) { return a->resume == b->resume && a->last_idx == b->last_idx && a->last_crc == b->last_crc && a->theme == b->theme && a->pol == b->pol && a->grid == b->grid && a->dither == b->dither && a->lut == b->lut && a->pal == b->pal; }
static inline int gs_layout_ok(const uint32_t *w) { return ((w[GS_W_LAYOUT] >> 27) & 15u) == GS_LAYOUT_VERSION; }
/* The values held by the words w[0..15]; a word set that is not our layout, or any out-of-range field, gives the default of that field. */
static inline gs_vals_t gs_unpack(const uint32_t *w)
{
    gs_vals_t v = gs_defaults();
#if BUG == 1
    (void)gs_layout_ok;                                                                        /* the layout is not checked */
#else
    if (!gs_layout_ok(w)) return v;
#endif
    v.pal = w[GS_W_LOOK] & 31u; v.theme = (w[GS_W_LOOK] >> 5) & 63u; v.pol = (w[GS_W_LOOK] >> 11) & 1u;
    v.resume = (w[GS_W_RESUME] & 1u) ^ 1u;
    v.last_crc = w[GS_W_CRC] & GS_M31;
    v.last_idx = w[GS_W_INDEX] & GS_M31;
    v.grid = w[GS_W_FX] & 3u; v.lut = (w[GS_W_FX] >> 4) & 7u;
    { const uint32_t r = (w[GS_W_FX] >> 2) & 3u; v.dither = r > 2u ? 1u : (r + 1u) % 3u; }     /* stored as (dither + 2) % 3; a value this build does not have falls back to the default */
    return v;
}
/* Puts v into w[0..15] and returns the bit mask of the words that changed (bit k = word k). Every bit that is not ours stays (word 1 bits 14..12 and 30, the mark of the player's layout). */
static inline uint32_t gs_pack(uint32_t *w, const gs_vals_t *v)
{
    uint32_t o[GS_WORDS], m = 0;
    for (uint32_t i = 0; i < GS_WORDS; i++) o[i] = w[i];
    w[GS_W_LAYOUT] = (w[GS_W_LAYOUT] & ~(15u << 27)) | (GS_LAYOUT_VERSION << 27);
#if BUG == 6
    w[GS_W_LOOK] = (w[GS_W_LOOK] & ~0xFFFu) | (v->pal & 31u) | ((v->theme & 63u) << 5);                  /* the polarity bit is dropped */
#else
    w[GS_W_LOOK] = (w[GS_W_LOOK] & ~0xFFFu) | (v->pal & 31u) | ((v->theme & 63u) << 5) | ((v->pol & 1u) << 11);
#endif
#if BUG == 2
    w[GS_W_RESUME] = (w[GS_W_RESUME] & 1u) | ((v->resume & 1u) ^ 1u);                           /* keeps the old bit: never turns on again */
#else
    w[GS_W_RESUME] = (w[GS_W_RESUME] & ~1u) | ((v->resume & 1u) ^ 1u);
#endif
    w[GS_W_CRC]    = (w[GS_W_CRC] & 0x80000000u) | (v->last_crc & GS_M31);
    w[GS_W_INDEX]  = (w[GS_W_INDEX] & 0x80000000u) | (v->last_idx & GS_M31);
#if BUG == 8
    w[GS_W_FX] = (w[GS_W_FX] & ~0x7Fu) | (v->grid & 3u) | ((((v->dither % 3u) + 2u) % 3u) << 2);                // the colour preset is dropped
#else
    w[GS_W_FX] = (w[GS_W_FX] & ~0x7Fu) | (v->grid & 3u) | ((((v->dither % 3u) + 2u) % 3u) << 2) | ((v->lut & 7u) << 4);
#endif
    for (uint32_t i = 0; i < GS_WORDS; i++) if (w[i] != o[i]) m |= 1u << i;
    return m;
}
/* The order the changed words are written: word 0 last, so a power loss in between leaves the layout of the old file. */
static const uint8_t gs_write_order[] = { GS_W_LOOK, GS_W_RESUME, GS_W_CRC, GS_W_INDEX, GS_W_FX, GS_W_LAYOUT };
/* Resume at boot: the index to select, 0 when resume is off or the saved picture is not the one at that index of this library (index too high, or its path CRC differs). */
static inline uint32_t gs_resume_index(const gs_vals_t *v, uint32_t count, uint32_t crc_at_saved)
{
    if (!v->resume) return 0u;
#if BUG == 3
    if (v->last_idx >= count + 1u) return 0u;                                                    /* off by one: an index equal to the count passes */
#else
    if (v->last_idx >= count) return 0u;
#endif
#if BUG == 4
    (void)crc_at_saved;                                                                          /* the path is not compared */
#else
    if ((crc_at_saved & GS_M31) != v->last_crc) return 0u;
#endif
    return v->last_idx;
}
/* An APF save can disturb a card read in flight: publish only after a quiet second (no key, no card read) since `last_activity` (clock cycles), and only when something changed. */
static inline int gs_publish_due(uint32_t now, uint32_t last_activity, uint32_t hz, int changed)
{
#if BUG == 5
    return changed && (int32_t)(now - last_activity) >= (int32_t)(hz / 100u);                    /* a hundredth of a second */
#else
    return changed && (int32_t)(now - last_activity) >= (int32_t)hz;
#endif
}
#endif
