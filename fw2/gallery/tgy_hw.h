/* TGY hardware decoder driver (docs/TGY_HW_DMA.md) and the container v1 reader it needs (docs/TGY_CONTAINER_V1.md). A compressed picture file is resident in SDRAM (landed by the landing path, any even byte address
 * `base`). The CPU never reads SDRAM: it holds the first 4 KB of the file (header, level table, table directory) and the tile index of the level in use in RAM (read from the card), and the driver parses those; the tile
 * streams and the quantisation tables are fetched by the decoder's DMA from SDRAM itself. Per tile it programs the registers,
 * starts the decoder and waits; the decoder writes the RGB888 rows into the picture plane (any byte address, any pitch). The tile checksums are NOT checked on this path (the CPU would have to read every stream); the header and the
 * tile index checksums are. Pure C99 on top of REG_RD / REG_WR (mmio.h): the host test builds it with a model of the hardware (sim/test_tgy_hw.py). */
#ifndef TGY_HW_H
#define TGY_HW_H
#include <stdint.h>
enum { TH_OK = 0, TH_SHORT = 1, TH_MAGIC = 2, TH_VERSION = 3, TH_CRC = 4, TH_RANGE = 5, TH_NOLEVEL = 6, TH_UNSUPPORTED = 7, TH_ABSENT = 8, TH_BUSY = 9, TH_TIMEOUT = 10, TH_HWERR = 11, TH_ODD = 12, TH_ABORT = 13 };
#define TH_TILE 128u
typedef struct { const uint8_t *hdr; uint32_t base, n, size, width, height, nlevels, level_off, tables_off, ntables; } tgy_file_t;
typedef struct { uint32_t w, h, data_off, data_len, idx_off, idx_len, idx_crc, cols, rows; uint16_t rx, ry, rw, rh; uint8_t fmt, kind, qy, qc; const uint8_t *idx; } tgy_level_t;      /* fmt: 0 grey, 1 4:4:4, 2 4:2:0; kind: 1 full, 2 fit */
/* Opens the file: img = a RAM copy of its first 4,096 bytes, n = how many bytes of the file are resident in SDRAM (all of it, or a prefix: the fit level comes first, so landing the first part is enough to show the picture),
 * base = the SDRAM byte address of file byte 0 (even). Checks magic, version, header size, header checksum, n <= the file size, the level table and the tables section (both inside the first 4 KB); a level that is not entirely
 * inside the n bytes is not offered by tgy_level / tgy_find_level. */
int tgy_open(tgy_file_t *f, const uint8_t *img, uint32_t n, uint32_t base);
/* Reads a level's tile index into buf (cap bytes) through rd(ctx, file offset, length, dst) (the app reads it from the card), checks its checksum; l->idx points at buf afterwards. rd returns non-zero when it read. */
typedef int (*tgy_read_fn)(void *ctx, uint32_t off, uint32_t len, uint8_t *dst);
int tgy_load_index(const tgy_file_t *f, tgy_level_t *l, tgy_read_fn rd, void *ctx, uint8_t *buf, uint32_t cap);
/* Level i as a tile level the decoder can run (layout 1, mode 1, tile size 128, 4:2:0 or grey), checked; TH_UNSUPPORTED for another kind of level (a RAW level, 4:4:4 on this core), TH_RANGE for a damaged one. */
int tgy_level(const tgy_file_t *f, uint32_t i, tgy_level_t *l);
int tgy_find_level(const tgy_file_t *f, unsigned kind, tgy_level_t *l);      /* the first supported tile level of that kind (1 full, 2 fit), TH_NOLEVEL if none */
int tgy_hw_present(void);                                                    /* 1 if the core has the decoder */
/* Loads the level's quantisation tables into the decoder (skipped when this file's pair is already loaded). */
int tgy_hw_tables(const tgy_file_t *f, const tgy_level_t *l);
/* Decodes tile (tx, ty) of the level into the plane: dst = SDRAM byte address of the tile's first pixel, pitch = bytes per plane row. Waits (up to 0.5 s) and returns TH_OK, TH_HWERR (tgy_hw_status() has the error code) or TH_TIMEOUT. */
int tgy_hw_tile(const tgy_file_t *f, const tgy_level_t *l, uint32_t tx, uint32_t ty, uint32_t dst, uint32_t pitch);
/* Where tile (tx, ty)'s stream is in the FILE: its file offset and length (from the tile index in l->idx; TH_RANGE for a tile outside the level or an entry outside the level's data). */
int tgy_tile_loc(const tgy_level_t *l, uint32_t tx, uint32_t ty, uint32_t *off, uint32_t *len);
/* The same decode with the stream at an explicit SDRAM byte address `src` (the app lands one tile's stream wherever it likes, for the zoom: the file is not resident). The tables are still the file's (f->base). */
int tgy_hw_tile_from(const tgy_file_t *f, const tgy_level_t *l, uint32_t tx, uint32_t ty, uint32_t dst, uint32_t pitch, uint32_t src);
/* The whole level: the picture's first pixel goes to dst0, tick(ctx, tx, ty) after each tile (may be 0; return non-zero to stop: TH_ABORT). */
int tgy_hw_level(const tgy_file_t *f, const tgy_level_t *l, uint32_t dst0, uint32_t pitch, int (*tick)(void *ctx, uint32_t tx, uint32_t ty), void *ctx);
/* The level on the 800 x 720 viewer canvas at its own rectangle: plane = SDRAM byte address of canvas pixel (0, 0), pitch 2,400. */
int tgy_hw_show(const tgy_file_t *f, const tgy_level_t *l, uint32_t plane, int (*tick)(void *ctx, uint32_t tx, uint32_t ty), void *ctx);
uint32_t tgy_hw_status(void);                                                /* STATUS as of the last tile */
void tgy_hw_forget(void);                                                    /* drop the tables cache (after a reset of the decoder) */
#endif
