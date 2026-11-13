/* Host harness of fw2/gallery/gallery_rot.h: prints, for an iw x ih picture and every orientation, the displayed size, the map of every source pixel, the strides for a pitch and the tile offsets, for sim/test_gallery_rot.py.
 *   gallery_rot_harness IW IH PITCH */
#include <stdio.h>
#include <stdlib.h>
#include "../fw2/gallery/gallery_rot.h"
int main(int argc, char **argv)
{
    if (argc < 4) return 2;
    const uint32_t iw = (uint32_t)atoi(argv[1]), ih = (uint32_t)atoi(argv[2]); const int32_t pitch = atoi(argv[3]);
    for (uint32_t o = 0; o < 8u; o++) {
        uint32_t dw, dh; gr_dims(o, iw, ih, &dw, &dh); printf("o %u dims %u %u stx %d sty %d turn_cw %u turn_ccw %u mirror %u flip %u\n", o, dw, dh, gr_stx(o, pitch), gr_sty(o, pitch), gr_turn(o, 1), gr_turn(o, 0), gr_mirror(o), gr_flip(o));
        for (uint32_t y = 0; y < ih; y++) for (uint32_t x = 0; x < iw; x++) { uint32_t X, Y; gr_map(o, iw, ih, x, y, &X, &Y); printf("m %u %u %u %u %u\n", o, x, y, X, Y); }
        for (uint32_t sy = 0; sy < ih; sy += 7) for (uint32_t sx = 0; sx < iw; sx += 5) printf("t %u %u %u %u\n", o, sx, sy, gr_tile_off(o, iw, ih, sx, sy, (uint32_t)pitch));
    }
    return 0;
}
