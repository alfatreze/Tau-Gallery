#include "gallery_fmt.h"

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

uint32_t gf_crc32_step(uint32_t c, const uint8_t *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) { c ^= b[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    return c;
}
uint32_t gf_crc32(const uint8_t *b, uint32_t n) { return ~gf_crc32_step(0xFFFFFFFFu, b, n); }

int gx_parse_head(const uint8_t *b, uint32_t n, gx_head_t *h)
{
    if (n < GX_HEAD_BYTES) return GF_SHORT;
    if (b[0] != 'T' || b[1] != 'G' || b[2] != 'I' || b[3] != 'X') return GF_MAGIC;
    const uint32_t ver = rd16(b + 4);
    if (ver == 0u) {
        if (rd16(b + 6) != GX_HEAD_BYTES || rd32(b + 12) != GX_ENTRY_BYTES || rd16(b + 24) != GX_THUMB_W || rd16(b + 26) != GX_THUMB_H) return GF_VERSION;
        h->version = 0u; h->count = rd32(b + 8); h->entries_off = rd32(b + 16); h->atlas_off = rd32(b + 20); h->entry_bytes = GX_ENTRY_BYTES;
        h->thumb_w = GX_THUMB_W; h->thumb_h = GX_THUMB_H; h->thumb_bytes = GX_THUMB_BYTES; h->folder_count = 1u; h->folders_off = 0u; h->details_off = 0u; h->details_len = 0u; h->sort = 0u;
        if (h->count > 0x100000u || h->entries_off < GX_HEAD_BYTES || h->atlas_off < h->entries_off + h->count * GX_ENTRY_BYTES) return GF_RANGE;      /* 1 M pictures is far beyond any card; it also keeps the offsets below 2^32 */
        return GF_OK;
    }
    if (ver != 1u) return GF_VERSION;
    if (n < GX1_HEAD_BYTES) return GF_SHORT;
    if (rd16(b + 6) != GX1_HEAD_BYTES || rd16(b + 12) != GX1_ENTRY_BYTES || rd16(b + 14) != GX1_THUMB_W || rd16(b + 16) != GX1_THUMB_H || rd16(b + 32) != GX1_FOLDER_BYTES) return GF_VERSION;
    if (gf_crc32(b, 60u) != rd32(b + 60)) return GF_CRC;
    h->version = 1u; h->count = rd32(b + 8); h->folder_count = rd16(b + 18); h->entries_off = rd32(b + 20); h->atlas_off = rd32(b + 24); h->folders_off = rd32(b + 28);
    h->sort = rd16(b + 34); h->details_off = rd32(b + 36); h->details_len = rd32(b + 40); h->entry_bytes = GX1_ENTRY_BYTES; h->thumb_w = GX1_THUMB_W; h->thumb_h = GX1_THUMB_H; h->thumb_bytes = GX1_THUMB_BYTES;
    if (h->count > GX_MAX_ENTRIES || h->folder_count < 1u || h->folder_count > GX_MAX_FOLDERS || h->sort > 2u) return GF_RANGE;
    /* the sections in order, without overlap; 64-bit sums so nothing wraps */
    if (h->entries_off < GX1_HEAD_BYTES) return GF_RANGE;
    if ((uint64_t)h->atlas_off < (uint64_t)h->entries_off + (uint64_t)h->count * GX1_ENTRY_BYTES) return GF_RANGE;
    if ((uint64_t)h->folders_off < (uint64_t)h->atlas_off + (uint64_t)h->count * GX1_THUMB_BYTES) return GF_RANGE;
    if ((uint64_t)h->details_off < (uint64_t)h->folders_off + (uint64_t)h->folder_count * GX1_FOLDER_BYTES) return GF_RANGE;
    if ((uint64_t)h->details_off + (uint64_t)h->details_len > 0xFFFFFFFFull) return GF_RANGE;
    return GF_OK;
}
int gx_check_size(const gx_head_t *h, uint32_t file_size)
{
    uint64_t end = (uint64_t)h->atlas_off + (uint64_t)h->count * h->thumb_bytes;
    if (h->version == 1u && (uint64_t)h->details_off + h->details_len > end) end = (uint64_t)h->details_off + h->details_len;
    return end <= file_size ? GF_OK : GF_RANGE;
}
uint32_t gx_entry_off(const gx_head_t *h, uint32_t i) { return h->entries_off + i * h->entry_bytes; }
uint32_t gx_thumb_off(const gx_head_t *h, uint32_t thumb) { return h->atlas_off + thumb * h->thumb_bytes; }
uint32_t gx_folder_off(const gx_head_t *h, uint32_t k) { return h->folders_off + k * GX1_FOLDER_BYTES; }

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
    e->folder = 0u; e->flags = 0u; e->details_off = 0u; e->details_len = 0u; e->taken = 0u;
    if (e->path[0] == 0) return GF_RANGE;
    return GF_OK;
}
int gx_parse_entry_h(const gx_head_t *h, const uint8_t *r, uint32_t n, gx_entry_t *e)
{
    if (n < h->entry_bytes) return GF_SHORT;
    int rc = gx_parse_entry(r, GX_ENTRY_BYTES, e);
    if (rc || h->version == 0u) return rc;
    e->folder = (uint16_t)rd16(r + 152); e->flags = (uint16_t)rd16(r + 154); e->details_off = rd32(r + 156); e->details_len = (uint16_t)rd16(r + 160); e->taken = rd32(r + 164);
    if (e->thumb >= h->count || e->folder >= h->folder_count || e->details_len > GX_MAX_DETAILS || (uint64_t)e->details_off + e->details_len > h->details_len) return GF_RANGE;
    return GF_OK;
}
int gx_parse_folder(const gx_head_t *h, const uint8_t *r, uint32_t n, gx_folder_t *f)
{
    if (n < GX1_FOLDER_BYTES) return GF_SHORT;
    copy_str(f->name, r, GX_FOLDER_NAME); f->first = rd32(r + 32); f->count = rd32(r + 36); f->cover = rd32(r + 40);
    if (((f->count < 1u) && h->count != 0u) || (uint64_t)f->first + f->count > h->count || (h->count != 0u && f->cover >= h->count)) return GF_RANGE;
    return GF_OK;
}
uint32_t gx_details_abs(const gx_head_t *h, const gx_entry_t *e) { return h->details_off + e->details_off; }
int gx_details_ok(const uint8_t *d, uint32_t n)
{
    uint32_t i = 0;
    while (i < n) { if (i + 2u > n || i + 2u + d[i + 1u] > n) return 0; i += 2u + d[i + 1u]; }
    return 1;
}
int gx_details_find(const uint8_t *d, uint32_t n, uint8_t tag, const uint8_t **v, uint32_t *len)
{
    uint32_t i = 0;
    while (i < n) {
        if (i + 2u > n || i + 2u + d[i + 1u] > n) return 0;
        if (d[i] == tag) { *v = d + i + 2u; *len = d[i + 1u]; return 1; }
        i += 2u + d[i + 1u];
    }
    return 0;
}

int gt_parse(const uint8_t *b, uint32_t n, gt_info_t *t)
{
    if (n < 128u) return GF_SHORT;
    if (b[0] != 'T' || b[1] != 'G' || b[2] != 'Y' || b[3] != 0) return GF_MAGIC;
    if (rd16(b + 4) != 0u || rd16(b + 6) != 64u) return GF_VERSION;
    if (gf_crc32(b, 52u) != rd32(b + 52)) return GF_CRC;
    const uint32_t nlev = rd32(b + 20), lt = rd32(b + 24), fsize = rd32(b + 48);
    t->width = rd32(b + 8); t->height = rd32(b + 12); t->file_size = fsize; t->thumb_off = rd32(b + 28);
    if (lt < 64u || nlev == 0u || nlev > 16u || lt > n || nlev * 48u > n - lt) return GF_RANGE;
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
    if (lt < 64u || nlev == 0u || nlev > 16u || lt > n || nlev * 48u > n - lt || width == 0u) return GF_RANGE;
    uint32_t seen = 0;
    for (uint32_t i = 0; i < nlev; i++) {
        const uint8_t *e = b + lt + i * 48u;
        if ((e[16] != 0u && e[16] != 1u) || e[17] != 0u || e[18] != 0u || e[19] != 1u) continue;          /* layout raster or tile128, mode RAW, RGB888, kind zoom: anything else is not ours */
        if (seen++ != k) continue;
        const uint32_t w = rd32(e), h = rd32(e + 4), stride = rd32(e + 20), off = rd32(e + 24), len = rd32(e + 28);
        uint32_t ntx = 0, nty = 0;
        if (e[16] == 1u) {
            if (w < GT_CANVAS_W || h < GT_CANVAS_H || w > 16384u || h > 16384u || stride != GT_TILE * 3u) return GF_RANGE;      /* a window cannot show an axis shorter than the screen */
            ntx = (w + GT_TILE - 1u) / GT_TILE; nty = (h + GT_TILE - 1u) / GT_TILE;
            if (len != ntx * nty * GT_TILE_BYTES) return GF_RANGE;
        } else {
            const uint32_t rx = rd16(e + 8), ry = rd16(e + 10), rw = rd16(e + 12), rh = rd16(e + 14);
            if (w < GT_CANVAS_W || h < GT_CANVAS_H || w > 16384u || h > 16384u || stride != ((w * 3u + 3u) & ~3u) || len != stride * h || len > GT_RASTER_MAX_BYTES) return GF_RANGE;
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
