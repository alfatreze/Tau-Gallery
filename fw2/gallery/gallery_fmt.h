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

/* The library index (TGIX): version 0 (one list, 96 x 72 thumbnails, 160-byte entries) and version 1 (docs/features/TGIX_V1_SPEC.md: folders, details, 96 x 96 thumbnails, 176-byte entries). A reader works on the
 * head, so every function below takes the head the parse filled in; nothing is read past a bound that was checked. */
#define GX1_HEAD_BYTES 64u
#define GX1_ENTRY_BYTES 176u
#define GX1_FOLDER_BYTES 48u
#define GX1_THUMB_W 96u
#define GX1_THUMB_H 96u
#define GX1_THUMB_BYTES (GX1_THUMB_W * GX1_THUMB_H * 2u)    /* 18,432 */
#define GX_FOLDER_NAME 32u
#define GX_MAX_ENTRIES 65535u
#define GX_MAX_FOLDERS 255u
#define GX_MAX_DETAILS 255u
typedef struct {
    uint32_t version, count, entries_off, atlas_off, entry_bytes, thumb_w, thumb_h, thumb_bytes;
    uint32_t folder_count, folders_off, details_off, details_len, sort;      /* version 0: one folder (the whole list), no details */
} gx_head_t;
/* The head of the index (at least 64 bytes for version 1, 32 for version 0). Checks the magic, the version, the header CRC (version 1), the entry and thumbnail sizes, the counts and that the sections lie in order
 * without overlapping or wrapping. GF_VERSION for a version or a size it does not know, GF_CRC for a damaged header, GF_RANGE for impossible offsets. */
int gx_parse_head(const uint8_t *b, uint32_t n, gx_head_t *h);
/* The same checks against the size of the file (a section past the end is GF_RANGE); call it when the size is known. */
int gx_check_size(const gx_head_t *h, uint32_t file_size);
uint32_t gx_entry_off(const gx_head_t *h, uint32_t i);                    /* byte offset of entry i in the file */
uint32_t gx_thumb_off(const gx_head_t *h, uint32_t thumb);                /* byte offset of thumbnail number `thumb` (the entry's thumb field) */
uint32_t gx_folder_off(const gx_head_t *h, uint32_t k);                   /* byte offset of folder k (version 1; the version 0 list has none) */
typedef struct { char path[GX_PATH_MAX + 1u]; char name[GX_NAME_MAX + 1u]; uint32_t size, width, height, thumb, details_off, taken; uint16_t folder, flags, details_len; } gx_entry_t;
int gx_parse_entry(const uint8_t *rec, uint32_t n, gx_entry_t *e);        /* a version 0 record (160 bytes); strings are cut at their NUL or their field end, never read past it */
int gx_parse_entry_h(const gx_head_t *h, const uint8_t *rec, uint32_t n, gx_entry_t *e);   /* either version: the record of the head's entry size, with folder, details and thumbnail number checked against the head */
typedef struct { char name[GX_FOLDER_NAME + 1u]; uint32_t first, count, cover; uint16_t parent1, nfold; } gx_folder_t;      /* parent1: the parent folder's index plus 1 (0: top level); nfold: the folders under it (0: a leaf). A container's range is all its descendants' pictures. Both come from the record's last four bytes, which older files hold as zeros */
int gx_parse_folder(const gx_head_t *h, const uint8_t *rec, uint32_t n, gx_folder_t *f);   /* one 48-byte record; GF_RANGE when its entries lie outside the list or it is empty (an empty library has one empty folder) */
uint32_t gx_details_abs(const gx_head_t *h, const gx_entry_t *e);         /* byte offset of the entry's details in the file */
/* Details items (`tag, length, bytes`): 1 if the run is well formed (no item reaches past n); gx_details_find: 1 and the item's bytes when the tag is present (an unknown tag is skipped; a run that overruns finds nothing). */
int gx_details_ok(const uint8_t *d, uint32_t n);
int gx_details_find(const uint8_t *d, uint32_t n, uint8_t tag, const uint8_t **v, uint32_t *len);
enum { GD_FORMAT = 1, GD_CAMERA = 2, GD_LENS = 3, GD_TAKEN = 4, GD_SHUTTER = 5, GD_APERTURE = 6, GD_ISO = 7, GD_FOCAL = 8 };

typedef struct { uint32_t width, height, file_size, canvas_off, canvas_len, thumb_off; uint16_t rx, ry, rw, rh; } gt_info_t;
/* The first bytes (at least 128) of a .tgy: header checksum and the first level that is a raw RGB888 raster fit canvas of 800 x 720 with stride 2,400, inside the file. */
int gt_parse(const uint8_t *b, uint32_t n, gt_info_t *t);
/* M2 zoom levels (docs/M2_DESIGN.md sections 3 and 6): after the fit canvas, kind-1 levels. Layout 0: full-width row strips on a canvas of at least 800 x 720 (stride = ceil(w * 3 / 4) * 4 bytes, a row's
 * offset is off + row * stride, rect = the picture on the canvas); layout 1: raw 128 x 128 tiles, a tile's offset is off + (row * tiles across + column) * 49,152; no index. */
#define GT_RASTER_MAX_BYTES 9437184u            /* the most bytes of a raster zoom level: what the zoom plane holds (9 MiB) */
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
uint32_t gf_crc32_step(uint32_t state, const uint8_t *b, uint32_t n);      /* the same in pieces: start with 0xFFFFFFFF, feed the pieces, the checksum is ~state */
#endif
