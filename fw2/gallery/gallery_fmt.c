#include "gallery_fmt.h"

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

uint32_t gf_crc32(const uint8_t *b, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++) { c ^= b[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    return ~c;
}

int gx_parse_head(const uint8_t *b, uint32_t n, gx_head_t *h)
{
    if (n < GX_HEAD_BYTES) return GF_SHORT;
    if (b[0] != 'T' || b[1] != 'G' || b[2] != 'I' || b[3] != 'X') return GF_MAGIC;
    if (rd16(b + 4) != 0u || rd16(b + 6) != GX_HEAD_BYTES || rd32(b + 12) != GX_ENTRY_BYTES || rd16(b + 24) != GX_THUMB_W || rd16(b + 26) != GX_THUMB_H) return GF_VERSION;
    h->count = rd32(b + 8); h->entries_off = rd32(b + 16); h->atlas_off = rd32(b + 20);
    if (h->count > 0x100000u || h->entries_off < GX_HEAD_BYTES || h->atlas_off < h->entries_off + h->count * GX_ENTRY_BYTES) return GF_RANGE;      /* 1 M pictures is far beyond any card; it also keeps the offsets below 2^32 */
    return GF_OK;
}
uint32_t gx_entry_off(const gx_head_t *h, uint32_t i) { return h->entries_off + i * GX_ENTRY_BYTES; }
uint32_t gx_thumb_off(const gx_head_t *h, uint32_t thumb) { return h->atlas_off + thumb * GX_THUMB_BYTES; }

static void copy_str(char *dst, const uint8_t *src, uint32_t max)
{
    uint32_t i = 0;
    while (i < max && src[i]) { dst[i] = (char)src[i]; i++; }
    dst[i] = 0;
}
int gx_parse_entry(const uint8_t *r, uint32_t n, gx_entry_t *e)
{
    if (n < GX_ENTRY_BYTES) return GF_SHORT;
    copy_str(e->path, r, GX_PATH_MAX); copy_str(e->name, r + GX_PATH_MAX, GX_NAME_MAX);
    e->size = rd32(r + 136); e->width = rd32(r + 140); e->height = rd32(r + 144); e->thumb = rd32(r + 148);
    if (e->path[0] == 0) return GF_RANGE;
    return GF_OK;
}

int gt_parse(const uint8_t *b, uint32_t n, gt_info_t *t)
{
    if (n < 128u) return GF_SHORT;
    if (b[0] != 'T' || b[1] != 'G' || b[2] != 'Y' || b[3] != 0) return GF_MAGIC;
    if (rd16(b + 4) != 0u || rd16(b + 6) != 64u) return GF_VERSION;
    if (gf_crc32(b, 52u) != rd32(b + 52)) return GF_CRC;
    const uint32_t nlev = rd32(b + 20), lt = rd32(b + 24), fsize = rd32(b + 48);
    t->width = rd32(b + 8); t->height = rd32(b + 12); t->file_size = fsize; t->thumb_off = rd32(b + 28);
    if (lt < 64u || nlev == 0u || nlev > 16u || lt + nlev * 48u > n) return GF_RANGE;
    for (uint32_t i = 0; i < nlev; i++) {
        const uint8_t *e = b + lt + i * 48u;
        if (e[16] != 0u || e[17] != 0u || e[18] != 0u || e[19] != 0u) continue;                       /* layout raster, mode RAW, RGB888, kind fit canvas */
        if (rd32(e) != GT_CANVAS_W || rd32(e + 4) != GT_CANVAS_H || rd32(e + 20) != GT_STRIDE) continue;
        const uint32_t off = rd32(e + 24), len = rd32(e + 28);
        if (len != GT_STRIDE * GT_CANVAS_H || off < 128u || off > fsize || len > fsize - off) return GF_RANGE;
        t->canvas_off = off; t->canvas_len = len; t->rx = (uint16_t)rd16(e + 8); t->ry = (uint16_t)rd16(e + 10); t->rw = (uint16_t)rd16(e + 12); t->rh = (uint16_t)rd16(e + 14);
        return GF_OK;
    }
    return GF_NOLEVEL;
}

int gt_zoom_level(const uint8_t *b, uint32_t n, uint32_t k, gt_zoom_t *z)
{
    if (n < 128u) return GF_SHORT;
    if (b[0] != 'T' || b[1] != 'G' || b[2] != 'Y' || b[3] != 0) return GF_MAGIC;
    if (rd16(b + 4) != 0u || rd16(b + 6) != 64u) return GF_VERSION;
    if (gf_crc32(b, 52u) != rd32(b + 52)) return GF_CRC;
    const uint32_t nlev = rd32(b + 20), lt = rd32(b + 24), fsize = rd32(b + 48), width = rd32(b + 8);
    if (lt < 64u || nlev == 0u || nlev > 16u || lt + nlev * 48u > n || width == 0u) return GF_RANGE;
    uint32_t seen = 0;
    for (uint32_t i = 0; i < nlev; i++) {
        const uint8_t *e = b + lt + i * 48u;
        if ((e[16] != 0u && e[16] != 1u) || e[17] != 0u || e[18] != 0u || e[19] != 1u) continue;          /* layout raster or tile128, mode RAW, RGB888, kind zoom: anything else is not ours */
        if (seen++ != k) continue;
        const uint32_t w = rd32(e), h = rd32(e + 4), stride = rd32(e + 20), off = rd32(e + 24), len = rd32(e + 28);
        uint32_t ntx = 0, nty = 0;
        if (e[16] == 1u) {
            if (w == 0u || h == 0u || w > 16384u || h > 16384u || stride != GT_TILE * 3u) return GF_RANGE;
            ntx = (w + GT_TILE - 1u) / GT_TILE; nty = (h + GT_TILE - 1u) / GT_TILE;
            if (len != ntx * nty * GT_TILE_BYTES) return GF_RANGE;
        } else {
            const uint32_t rx = rd16(e + 8), ry = rd16(e + 10), rw = rd16(e + 12), rh = rd16(e + 14);
            if (w < GT_CANVAS_W || h < GT_CANVAS_H || w > 16384u || h > 16384u || stride != ((w * 3u + 3u) & ~3u) || len != stride * h) return GF_RANGE;
            if (rw == 0u || rh == 0u || rx + rw > w || ry + rh > h) return GF_RANGE;
            z->rx = (uint16_t)rx; z->ry = (uint16_t)ry; z->rw = (uint16_t)rw; z->rh = (uint16_t)rh;
        }
        if (off < 128u || (off & 4095u) || off > fsize || len > fsize - off) return GF_RANGE;
        z->layout = e[16]; z->w = w; z->h = h; z->off = off; z->len = len; z->stride = stride; z->ntx = ntx; z->nty = nty;
        { const uint32_t cw = e[16] == 1u ? w : z->rw; z->scale_q16 = (uint32_t)(((uint64_t)cw * 65536u + width / 2u) / width); }
        return GF_OK;
    }
    return GF_NOLEVEL;
}
uint32_t gt_row_off(const gt_zoom_t *z, uint32_t row) { return (z->layout == 0u && row < z->h) ? z->off + row * z->stride : 0xFFFFFFFFu; }
uint32_t gt_tile_off(const gt_zoom_t *z, uint32_t tx, uint32_t ty) { return (z->layout == 1u && tx < z->ntx && ty < z->nty) ? z->off + (ty * z->ntx + tx) * GT_TILE_BYTES : 0xFFFFFFFFu; }
