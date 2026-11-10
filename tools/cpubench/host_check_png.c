/* host check: the kernel inflates and unfilters the three crops to the checksum Pillow's own pixels give. cc -O1 host_check_png.c -o /tmp/hc && /tmp/hc */
#include <stdio.h>
#include <stdint.h>
#include "pngdata.h"
#include "inflate.c"
static uint8_t buf[200000];
int main(void)
{
    const unsigned char *d[] = { png0_data, png1_data, png2_data }; const uint32_t len[] = { PNG0_LEN, PNG1_LEN, PNG2_LEN }, w[] = { PNG0_W, PNG1_W, PNG2_W }, h[] = { PNG0_H, PNG1_H, PNG2_H }, want[] = { PNG0_CS, PNG1_CS, PNG2_CS };
    int bad = 0;
    for (int i = 0; i < PNG_COUNT; i++) {
        uint32_t n = inflate_raw(d[i], len[i], buf), exp = h[i] * (w[i] * 3u + 1u);
        uint32_t m = png_unfilter(buf, w[i], h[i], 3u), c = 0; for (uint32_t k = 0; k < m; k++) c = c * 31u + buf[k] + k;
        printf("crop %d inflated %u (want %u) unfiltered %u checksum %s\n", i, n, exp, m, c == want[i] ? "OK" : "BAD"); if (n != exp || c != want[i]) bad = 1;
    }
    return bad;
}
