/* Host harness of fw2/gallery/gallery_fmt.c: reads a file the way the firmware does (only the bytes it asks for) and prints what the parsers found, for sim/test_gallery_fmt.py to compare with the Python reader.
 *   gallery_fmt_harness idx FILE   : head, then every entry
 *   gallery_fmt_harness tgy FILE   : the header with only the first 4096 bytes, as one data-slot read gives them
 *   gallery_fmt_harness zoom FILE  : the tile levels (M2) of a .tgy */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../fw2/gallery/gallery_fmt.c"
int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    FILE *f = fopen(argv[2], "rb"); if (!f) return 2;
    static uint8_t buf[4096]; size_t n = fread(buf, 1, sizeof buf, f);
    if (!strcmp(argv[1], "zoom")) {                                  /* every tile level, then a probe past the last one and tile offsets */
        for (uint32_t k = 0; k < 8u; k++) {
            gt_zoom_t z; memset(&z, 0, sizeof z); int r = gt_zoom_level(buf, (uint32_t)n, k, &z);
            if (r) { printf("zoom %u rc %d\n", k, r); break; }
            printf("zoom %u rc 0 w %u h %u off %u len %u ntx %u nty %u scale %u first %u last %u right %u outside %u\n", k, z.w, z.h, z.off, z.len, z.ntx, z.nty, z.scale_q16, gt_tile_off(&z, 0, 0), gt_tile_off(&z, z.ntx - 1u, z.nty - 1u), gt_tile_off(&z, z.ntx - 1u, 0), gt_tile_off(&z, z.ntx, 0));
        }
        return 0;
    }
    if (!strcmp(argv[1], "tgy")) {
        gt_info_t t; memset(&t, 0, sizeof t); int r = gt_parse(buf, (uint32_t)n, &t);
        printf("rc %d\n", r); if (r) return 0;
        printf("w %u h %u fsize %u canvas_off %u canvas_len %u thumb_off %u rect %u %u %u %u\n", t.width, t.height, t.file_size, t.canvas_off, t.canvas_len, t.thumb_off, t.rx, t.ry, t.rw, t.rh);
    } else {
        gx_head_t h; int r = gx_parse_head(buf, (uint32_t)n, &h); printf("rc %d\n", r); if (r) return 0;
        printf("count %u entries_off %u atlas_off %u\n", h.count, h.entries_off, h.atlas_off);
        for (uint32_t i = 0; i < h.count; i++) {
            uint8_t rec[GX_ENTRY_BYTES]; fseek(f, (long)gx_entry_off(&h, i), SEEK_SET); if (fread(rec, 1, sizeof rec, f) != sizeof rec) { printf("short %u\n", i); return 0; }
            gx_entry_t e; r = gx_parse_entry(rec, sizeof rec, &e);
            printf("entry %u rc %d path %s name %s size %u w %u h %u thumb %u thumb_off %u\n", i, r, e.path, e.name, e.size, e.width, e.height, e.thumb, gx_thumb_off(&h, e.thumb));
        }
    }
    return 0;
}
