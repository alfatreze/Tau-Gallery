/* Software PNG on the real VexRiscv RTL: inflate and the row filters on three 128 x 128 crops (docs/M3_STREAMING_NOTE.md). Build and run as README.md describes, with bench3.c and inflate.c. */
#include <stdint.h>
#include "pngdata.h"
#include "inflate.c"
#define CON (*(volatile uint32_t*)0x80000000u)
static void ps(const char*s){while(*s)CON=*s++;}
static void pd(uint32_t v){char t[12];int n=0;do{t[n++]='0'+v%10;v/=10;}while(v);while(n)CON=t[--n];}
static inline uint32_t cyc(void){uint32_t c;__asm__ volatile("rdcycle %0":"=r"(c));return c;}
static inline uint32_t ins(void){uint32_t c;__asm__ volatile("rdinstret %0":"=r"(c));return c;}
static uint8_t buf[50000];
int main(void)
{
    const unsigned char *d[] = { png0_data, png1_data, png2_data }; const char *nm[] = { PNG0_NAME, PNG1_NAME, PNG2_NAME };
    const uint32_t len[] = { PNG0_LEN, PNG1_LEN, PNG2_LEN }, w[] = { PNG0_W, PNG1_W, PNG2_W }, h[] = { PNG0_H, PNG1_H, PNG2_H }, want[] = { PNG0_CS, PNG1_CS, PNG2_CS };
    for (int i = 0; i < PNG_COUNT; i++) {
        uint32_t c0 = cyc(), i0 = ins(); uint32_t n = inflate_raw(d[i], len[i], buf); uint32_t c1 = cyc(), i1 = ins();
        uint32_t m = png_unfilter(buf, w[i], h[i], 3u); uint32_t c2 = cyc(), i2 = ins();
        uint32_t cs = 0; for (uint32_t k = 0; k < m; k++) cs = cs * 31u + buf[k] + k;
        ps(nm[i]); ps(cs == want[i] ? " checksum OK" : " checksum BAD"); ps(" compressed "); pd(len[i]); ps(" raw "); pd(m); ps(" inflated "); pd(n); ps("\n");
        ps("  inflate: cycles "); pd(c1 - c0); ps(" instr "); pd(i1 - i0); ps(" cycles*100/raw-byte "); pd((c1 - c0) * 100u / m); ps("\n");
        ps("  unfilter: cycles "); pd(c2 - c1); ps(" instr "); pd(i2 - i1); ps(" cycles*100/raw-byte "); pd((c2 - c1) * 100u / m); ps("\n");
    }
    return 0;
}
