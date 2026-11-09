/* Tau Gallery file formats as the firmware reads them (docs/M1_DESIGN.md 3; the writer is tools/gallery/tgy.py). Pure functions on byte buffers, no hardware: a host test runs them on files the Python tool wrote.
 * The reader checks what it needs and refuses what it does not know: it never trusts an offset or a length it has not bounds-checked. */
#ifndef GALLERY_FMT_H
#define GALLERY_FMT_H
#include <stdint.h>
#define GX_THUMB_W 96u
#define GX_THUMB_H 72u
#define GX_THUMB_BYTES (GX_THUMB_W * GX_THUMB_H * 2u)       /* 13,824 */
#define GX_HEAD_BYTES 32u
#define GX_ENTRY_BYTES 160u
#define GX_PATH_MAX 96u
#define GX_NAME_MAX 40u
#define GT_CANVAS_W 800u
#define GT_CANVAS_H 720u
#define GT_STRIDE 2400u
enum { GF_OK = 0, GF_SHORT = 1, GF_MAGIC = 2, GF_VERSION = 3, GF_CRC = 4, GF_NOLEVEL = 5, GF_RANGE = 6 };

typedef struct { uint32_t count, entries_off, atlas_off; } gx_head_t;
/* The first 32 bytes of the index. Checks the magic, the version, the entry size and the thumbnail size. */
int gx_parse_head(const uint8_t *b, uint32_t n, gx_head_t *h);
uint32_t gx_entry_off(const gx_head_t *h, uint32_t i);                    /* byte offset of entry i in the file */
uint32_t gx_thumb_off(const gx_head_t *h, uint32_t thumb);                /* byte offset of thumbnail number `thumb` (the entry's thumb field) */
typedef struct { char path[GX_PATH_MAX + 1u]; char name[GX_NAME_MAX + 1u]; uint32_t size, width, height, thumb; } gx_entry_t;
int gx_parse_entry(const uint8_t *rec, uint32_t n, gx_entry_t *e);        /* one 160-byte record; strings are cut at their NUL or their field end, never read past it */

typedef struct { uint32_t width, height, file_size, canvas_off, canvas_len, thumb_off; uint16_t rx, ry, rw, rh; } gt_info_t;
/* The first bytes (at least 128) of a .tgy: header checksum and the first level that is a raw RGB888 raster fit canvas of 800 x 720 with stride 2,400, inside the file. */
int gt_parse(const uint8_t *b, uint32_t n, gt_info_t *t);
/* M2 zoom levels (docs/M2_DESIGN.md sections 3 and 6): after the fit canvas, kind-1 levels. Layout 0: full-width row strips on a canvas of at least 800 x 720 (stride = ceil(w * 3 / 4) * 4 bytes, a row's
 * offset is off + row * stride, rect = the picture on the canvas); layout 1: raw 128 x 128 tiles, a tile's offset is off + (row * tiles across + column) * 49,152; no index. */
#define GT_TILE 128u
#define GT_TILE_BYTES 49152u
typedef struct { uint32_t layout, w, h, off, len, stride, ntx, nty, scale_q16; uint16_t rx, ry, rw, rh; } gt_zoom_t;      /* layout 0: raster rows (option A); layout 1: tiles (option B). w and h are the stored size */      /* scale_q16: the level against the source (Q16), from the header's width */
/* The k-th (0-based) zoom level (kind 1, layout 0 or 1, raw, RGB888) of the file whose first bytes (at least 128, the header and the level table) are in b, checked: a raster level has w >= 800, h >= 720,
 * the exact stride, a length of exactly h rows and a rect inside the canvas; a tile level a 384-byte tile row and a length of exactly its tiles; both a 4 KB aligned offset inside the file. Levels of any other
 * layout or kind are skipped. GF_NOLEVEL when there is no k-th one; GF_RANGE for a zoom level that is damaged. */
int gt_zoom_level(const uint8_t *b, uint32_t n, uint32_t k, gt_zoom_t *z);
uint32_t gt_row_off(const gt_zoom_t *z, uint32_t row);                      /* a raster level's row: its byte offset in the file, 0xFFFFFFFF past the last row */
uint32_t gt_tile_off(const gt_zoom_t *z, uint32_t tx, uint32_t ty);       /* the byte offset of a tile in the file, 0xFFFFFFFF when the tile is not in the level */
uint32_t gf_crc32(const uint8_t *b, uint32_t n);
#endif
