/* TGY pixel pipeline, C reference (docs/TGY_RECONSTRUCTION_V0.md, the TGY profile): from one tile's symbol-layer stream to RGB rows, block by block. Dequantise, libjpeg's islow IDCT, the tile-local h2v2 fancy chroma
 * upsampling, libjpeg's integer YCbCr to RGB; arithmetic in int64 where the specification is arbitrary precision (so extreme coefficients agree with the reference; a hardware version needs only the valid range).
 * Memory: the planes of one tile (tgy_planes_t: 24,576 bytes used for 4:2:0, 49,152 for 4:4:4), the decoder state (tgy_dec_t, 3,766 bytes), 128 coefficients and a few dozen bytes; the RGB rows go out through a callback,
 * so a caller writes them straight to their destination. Held to the conformance vectors and to the Python reference (sim/test_tgy_pix_c.py). */
#ifndef TGY_PIX_H
#define TGY_PIX_H
#include "tgy_dec.h"
enum { TGY_GREY = 0, TGY_444 = 1, TGY_420 = 2 };
typedef struct { uint8_t y[128 * 128]; uint8_t c[2][128 * 128]; } tgy_planes_t;          /* Y, then Cb and Cr; a 4:2:0 tile uses the first 64 x 64 of each chroma plane */
typedef void (*tgy_row_fn)(void *ctx, unsigned row, const uint8_t *rgb);                /* one RGB888 row of the tile (width * 3 bytes) */
/* Dequantises a block (zigzag coefficients, zigzag quantisation table) and inverse transforms it into 64 samples (row major). */
void tgy_dequant_idct(const int16_t *zz, const uint16_t *qzz, uint8_t *out);
/* The IDCT alone on 64 dequantised coefficients in natural order. */
void tgy_idct_natural(const int32_t *in, uint8_t *out);
void tgy_ycc_to_rgb(unsigned y, unsigned cb, unsigned cr, uint8_t *rgb);
/* Row r (0 .. 2*ch - 1) of the tile-local h2v2 fancy upsampling of a chroma plane of cw x ch samples (row stride cstride), cropped to `width` samples. */
void tgy_upsample_row(const uint8_t *plane, unsigned cstride, unsigned cw, unsigned ch, unsigned r, unsigned width, uint8_t *out);
/* Decodes one tile of a picture and emits its rows. sub: TGY_GREY, TGY_444 or TGY_420. rows[i], cols[i]: the blocks of component i in the tile (clipped at the picture's edge). w, h: the tile's size in luma samples
 * (at most 128 x 128, smaller at the right and bottom edges). qy, qc: the zigzag quantisation tables. Returns 0, or 1 if the stream was impossible (rows already emitted are then not meaningful). */
int tgy_tile_rgb(tgy_dec_t *d, tgy_planes_t *pl, const uint8_t *stream, uint32_t len, unsigned sub, const uint8_t *rows, const uint8_t *cols, unsigned w, unsigned h,
                 const uint16_t *qy, const uint16_t *qc, tgy_row_fn emit, void *ctx);
#endif
