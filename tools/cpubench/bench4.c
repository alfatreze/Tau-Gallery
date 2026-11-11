/* TGY tile pipeline on the real VexRiscv RTL (docs/M3_STREAMING_NOTE.md section 20): the cycle cost of decoding one 128 x 128 4:2:0 tile (384 blocks) in C: the entropy decoder, the 384 dequantise + IDCT blocks, the chroma
 * upsampling and colour conversion of 16,384 pixels, and the whole tgy_tile_rgb. Two real tiles, a dense one and a sparse one; the RGB checksum is checked against the Python reconstruction. Build as README.md describes. */
#include <stdint.h>
#include "tgydata.h"
#include "../../fw2/gallery/tgy_dec.c"
#include "../../fw2/gallery/tgy_pix.c"
#define CON (*(volatile uint32_t*)0x80000000u)
static void ps(const char*s){while(*s)CON=*s++;}
static void pd(uint32_t v){char t[12];int n=0;do{t[n++]='0'+v%10;v/=10;}while(v);while(n)CON=t[--n];}
static inline uint32_t cyc(void){uint32_t c;__asm__ volatile("rdcycle %0":"=r"(c));return c;}
static tgy_dec_t dec; static tgy_planes_t planes; static int16_t coef[384 * 64];
static uint32_t cs, csn;
static void emit(void *ctx, unsigned row, const uint8_t *rgb){(void)ctx;(void)row;for(unsigned i=0;i<128*3;i++){cs=cs*31u+rgb[i]+csn;csn++;}}
static void line(const char *n, uint32_t c){ps("  ");ps(n);ps(": ");pd(c);ps(" cycles\n");}
int main(void)
{
    const uint8_t *st[2] = { tile0, tile1 }; const uint32_t len[2] = { TILE0_LEN, TILE1_LEN }, want[2] = { TILE0_CS, TILE1_CS }; const uint8_t rows[3] = { 16, 8, 8 }, cols[3] = { 16, 8, 8 };
#ifndef ONLY
#define ONLY -1
#endif
    for (int t = 0; t < 2; t++) {
        if (ONLY >= 0 && t != ONLY) continue;
        cs = 0; csn = 0; uint32_t c3 = cyc(); int e2 = tgy_tile_rgb(&dec, &planes, st[t], len[t], TGY_420, rows, cols, 128, 128, QY, QC, emit, 0); uint32_t c4 = cyc();       /* the whole pipeline, checked */
        uint32_t c0 = cyc(); int e = tgy_tile(&dec, st[t], len[t], 3, rows, cols, coef); uint32_t c1 = cyc();                                                                 /* the entropy decoder alone */
        uint8_t px[64]; for (unsigned b = 0; b < 384; b++) tgy_dequant_idct(coef + b * 64, b < 256 ? QY : QC, px); uint32_t c2 = cyc();                                        /* the 384 IDCTs alone */
        ps(t ? "sparse tile" : "dense tile"); ps(": stream "); pd(len[t]); ps(" bytes, error flags "); pd((uint32_t)(e | e2)); ps(cs == want[t] ? ", RGB checksum OK\n" : ", RGB checksum BAD\n");
        line("whole tgy_tile_rgb", c4 - c3); line("entropy decode (384 blocks)", c1 - c0); line("dequantise + IDCT (384 blocks)", c2 - c1);
        ps("  upsample + colour + glue (the rest): "); pd((c4 - c3) - (c1 - c0) - (c2 - c1)); ps(" cycles\n");
    }
    return 0;
}
