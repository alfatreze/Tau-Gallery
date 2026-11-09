/* fw2/gallery/gallery_settings.h -- Tau Gallery M1.6 slice (a) (docs/M1_DESIGN.md section 6): the persisted settings of the gallery. Pure: no registers. Host-tested (sim/test_gallery_settings.py).
 * The Pocket saves the core's persisted interact.json variables; the firmware reaches them as words of a register file (gallery_settings.c). Only words below 16 are used (the narrow file reaches them).
 *   word 0 (variable 10)  bits 30..27 the layout version (GS_LAYOUT_VERSION; anything else = the file is not ours, use the defaults)
 *   word 1 (variable 11)  appearance: reserved for the Theme slice (accent, theme, polarity, font, the player's bit positions); this slice never writes it
 *   word 2 (variable 12)  bit 0 resume OFF (stored inverted: the player's default of this variable has bit 0 clear, so the package needs no change and a fresh card resumes)
 *   word 5 (variable 15)  bits 30..0 the CRC-32 of the last picture's path, 31 bits
 *   word 6 (variable 16)  bits 30..0 the last picture's index number
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
#define GS_W_RESUME 2u
#define GS_W_CRC    5u
#define GS_W_INDEX  6u
#define GS_M31 0x7FFFFFFFu
typedef struct { uint32_t resume, last_idx, last_crc; } gs_vals_t;
static inline gs_vals_t gs_defaults(void) { gs_vals_t v = { 1u, 0u, 0u }; return v; }
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
    v.resume = (w[GS_W_RESUME] & 1u) ^ 1u;
    v.last_crc = w[GS_W_CRC] & GS_M31;
    v.last_idx = w[GS_W_INDEX] & GS_M31;
    return v;
}
/* Puts v into w[0..15] and returns the bit mask of the words that changed (bit k = word k). Word 1 and every bit that is not ours stay. */
static inline uint32_t gs_pack(uint32_t *w, const gs_vals_t *v)
{
    uint32_t o[GS_WORDS], m = 0;
    for (uint32_t i = 0; i < GS_WORDS; i++) o[i] = w[i];
    w[GS_W_LAYOUT] = (w[GS_W_LAYOUT] & ~(15u << 27)) | (GS_LAYOUT_VERSION << 27);
#if BUG == 2
    w[GS_W_RESUME] = (w[GS_W_RESUME] & 1u) | ((v->resume & 1u) ^ 1u);                           /* keeps the old bit: never turns on again */
#else
    w[GS_W_RESUME] = (w[GS_W_RESUME] & ~1u) | ((v->resume & 1u) ^ 1u);
#endif
    w[GS_W_CRC]    = (w[GS_W_CRC] & 0x80000000u) | (v->last_crc & GS_M31);
    w[GS_W_INDEX]  = (w[GS_W_INDEX] & 0x80000000u) | (v->last_idx & GS_M31);
    for (uint32_t i = 0; i < GS_WORDS; i++) if (w[i] != o[i]) m |= 1u << i;
    return m;
}
/* The order the changed words are written: word 0 last, so a power loss in between leaves the layout of the old file. */
static const uint8_t gs_write_order[] = { GS_W_RESUME, GS_W_CRC, GS_W_INDEX, GS_W_LAYOUT };
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
