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
uint32_t gf_crc32(const uint8_t *b, uint32_t n);
#endif
