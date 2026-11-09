#pragma GCC optimize ("Os")
/* Tau Gallery persisted settings: the register-file side of gallery_settings.h (docs/M1_DESIGN.md section 6). NEVER replace this with a file write (fw/settings.inc has the history: two such mechanisms destroyed libraries). */
#include "gallery_settings.h"
#include "gallery_settings_hw.h"
#include "../platform/mmio.h"
#define R_SET_IDX 0x8000006Cu
#define R_SET_DAT 0x80000070u

gs_stats_t gs_stats;
static gs_vals_t pub;                    /* the values as last published or adopted */
static uint32_t next_poll;

static uint32_t set_rd(uint32_t k) { REG_WR(R_SET_IDX, k); return REG_RD(R_SET_DAT); }
static void     set_wr(uint32_t k, uint32_t v) { REG_WR(R_SET_IDX, k); REG_WR(R_SET_DAT, v); }
static void read_words(uint32_t *w) { for (uint32_t i = 0; i < GS_WORDS; i++) w[i] = 0u; for (uint32_t n = 0; n < sizeof gs_write_order; n++) w[gs_write_order[n]] = set_rd(gs_write_order[n]); }

void gs_hw_load(gs_vals_t *v)
{
    uint32_t w[GS_WORDS]; read_words(w);
    pub = gs_unpack(w); *v = pub;
    gs_stats.layout = (w[GS_W_LAYOUT] >> 27) & 15u; gs_stats.loads++;
}

static void publish(const gs_vals_t *v)
{
    uint32_t w[GS_WORDS]; read_words(w);                                  /* read-modify-write: bits that are not ours stay */
    const uint32_t m = gs_pack(w, v);
    for (uint32_t n = 0; n < sizeof gs_write_order; n++) { const uint32_t k = gs_write_order[n]; if (m & (1u << k)) set_wr(k, w[k]); }      /* word 0 last */
    pub = *v; gs_stats.layout = (w[GS_W_LAYOUT] >> 27) & 15u; gs_stats.stores++;
}

int gs_hw_service(gs_vals_t *v, uint32_t last_activity)
{
    const int changed = v->resume != pub.resume || v->last_idx != pub.last_idx || v->last_crc != pub.last_crc;
    if (changed) { if (gs_publish_due(cycles(), last_activity, CLK_HZ, 1)) publish(v); return 0; }
    if ((int32_t)(cycles() - next_poll) < 0) return 0;
    next_poll = cycles() + CLK_HZ / 2u;
    uint32_t w[GS_WORDS]; read_words(w);                                  /* has the Pocket's Core Settings moved one of our words? */
    const gs_vals_t now = gs_unpack(w);
    if (now.resume == pub.resume && now.last_idx == pub.last_idx && now.last_crc == pub.last_crc) return 0;
    pub = now; *v = now; gs_stats.menu_changes++;
    return 1;
}
