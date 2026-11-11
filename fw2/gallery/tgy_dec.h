/* TGY symbol layer v0, DECODER (docs/TGY_SYMBOL_LAYER_V0.md): the firmware reference. Pure C99, no libc, no allocation, total: any byte string decodes to SOMETHING in bounded time (an Exp-Golomb exponent above
 * TGY_MAX_EXP stops the tile and sets the error flag, values saturate to int16), so a corrupt tile can neither hang nor corrupt memory. Held to docs/conformance/tgy_v0_vectors.json (sim/test_tgy_dec_c.py).
 * State: 1,250 contexts of a 16-bit probability and an 8-bit count (3,750 bytes), plus the range decoder. */
#ifndef TGY_DEC_H
#define TGY_DEC_H
#include <stdint.h>
#define TGY_N_CTX 1250u
#define TGY_MAX_EXP 15u                        /* valid streams never exceed exponent 11 (DC) or 9 (AC magnitude) */
typedef struct {
    const uint8_t *p, *end; uint32_t range, code;       /* the TVC range decoder */
    uint16_t prob[TGY_N_CTX]; uint8_t cnt[TGY_N_CTX];    /* probability of a 0 (15 bit) and the count that sets the adaptation rate */
    uint8_t err;                                         /* set when a stream was impossible; the tile's data is then meaningless */
} tgy_dec_t;
/* Starts a tile: every context fresh, the range decoder on data[0 .. len). */
void tgy_tile_begin(tgy_dec_t *d, const uint8_t *data, uint32_t len);
/* Decodes one block of component class cc (0 = Y, 1 = Cb or Cr) into out[64] (zigzag order). left and above are the neighbouring blocks of the same component inside the tile, or 0. */
void tgy_block(tgy_dec_t *d, unsigned cc, const int16_t *left, const int16_t *above, int16_t *out);
/* Decodes a whole tile: ncomp components, comp i being rows[i] x cols[i] blocks in raster order; out receives the blocks of component 0, then 1, then 2 (64 int16 each). Returns 0, or 1 if the stream was impossible. */
int tgy_tile(tgy_dec_t *d, const uint8_t *data, uint32_t len, unsigned ncomp, const uint8_t *rows, const uint8_t *cols, int16_t *out);
#endif
