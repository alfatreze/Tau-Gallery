/* Host harness of fw2/gallery/gallery_mask.h: prints the crop of every mask in both orientations for a picture rectangle. gallery_mask_harness X Y W H */
#include <stdio.h>
#include <stdlib.h>
#include "../fw2/gallery/gallery_mask.h"
int main(int argc, char **argv)
{
    if (argc < 5) return 2;
    const uint32_t x = (uint32_t)atoi(argv[1]), y = (uint32_t)atoi(argv[2]), w = (uint32_t)atoi(argv[3]), h = (uint32_t)atoi(argv[4]);
    for (uint32_t i = 1; i < GM_N; i++) for (uint32_t p = 0; p < 2u; p++) {
        uint32_t x0, y0, x1, y1; gm_crop(i, p, x, y, w, h, &x0, &y0, &x1, &y1); printf("%u %u %u %u %u %u %d %u\n", i, p, x0, y0, x1, y1, gm_can_turn(i), gm_next(i));
    }
    printf("next0 %u nextlast %u names %s %s\n", gm_next(0), gm_next(GM_N - 1u), gm_ratio[1].name, gm_ratio[6].name);
    return 0;
}
