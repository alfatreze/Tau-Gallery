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
            if (z.layout == 1u) printf("zoom %u rc 0 layout 1 w %u h %u off %u len %u stride %u ntx %u nty %u scale %u first %u last %u right %u outside %u\n", k, z.w, z.h, z.off, z.len, z.stride, z.ntx, z.nty, z.scale_q16, gt_tile_off(&z, 0, 0), gt_tile_off(&z, z.ntx - 1u, z.nty - 1u), gt_tile_off(&z, z.ntx - 1u, 0), gt_tile_off(&z, z.ntx, 0));
            else printf("zoom %u rc 0 layout 0 w %u h %u off %u len %u stride %u rect %u %u %u %u scale %u first %u last %u outside %u tileoff %u\n", k, z.w, z.h, z.off, z.len, z.stride, z.rx, z.ry, z.rw, z.rh, z.scale_q16, gt_row_off(&z, 0), gt_row_off(&z, z.h - 1u), gt_row_off(&z, z.h), gt_tile_off(&z, 0, 0));
        }
        return 0;
    }
    if (!strcmp(argv[1], "tgy")) {
        gt_info_t t; memset(&t, 0, sizeof t); int r = gt_parse(buf, (uint32_t)n, &t);
        printf("rc %d\n", r); if (r) return 0;
        printf("w %u h %u fsize %u canvas_off %u canvas_len %u thumb_off %u rect %u %u %u %u\n", t.width, t.height, t.file_size, t.canvas_off, t.canvas_len, t.thumb_off, t.rx, t.ry, t.rw, t.rh);
    } else {
        gx_head_t h; int r = gx_parse_head(buf, (uint32_t)n, &h); printf("rc %d\n", r); if (r) return 0;
        fseek(f, 0, SEEK_END); long fsz = ftell(f); printf("count %u entries_off %u atlas_off %u\n", h.count, h.entries_off, h.atlas_off);
        if (h.version == 1u) {
            printf("head1 folders %u folders_off %u details_off %u details_len %u sort %u size_rc %d\n", h.folder_count, h.folders_off, h.details_off, h.details_len, h.sort, gx_check_size(&h, (uint32_t)fsz));
            for (uint32_t k = 0; k < h.folder_count; k++) {
                uint8_t rec[GX1_FOLDER_BYTES]; fseek(f, (long)gx_folder_off(&h, k), SEEK_SET); if (fread(rec, 1, sizeof rec, f) != sizeof rec) { printf("short folder %u\n", k); return 0; }
                gx_folder_t fo; r = gx_parse_folder(&h, rec, sizeof rec, &fo); printf("folder %u rc %d name %s first %u count %u cover %u\n", k, r, fo.name, fo.first, fo.count, fo.cover);
            }
        }
        for (uint32_t i = 0; i < h.count; i++) {
            uint8_t rec[GX1_ENTRY_BYTES]; fseek(f, (long)gx_entry_off(&h, i), SEEK_SET); if (fread(rec, 1, h.entry_bytes, f) != h.entry_bytes) { printf("short %u\n", i); return 0; }
            gx_entry_t e; r = gx_parse_entry_h(&h, rec, h.entry_bytes, &e);
            printf("entry %u rc %d path %s name %s size %u w %u h %u thumb %u thumb_off %u", i, r, e.path, e.name, e.size, e.width, e.height, e.thumb, gx_thumb_off(&h, e.thumb));
            if (h.version == 1u) {
                printf(" folder %u flags %u dlen %u taken %u", e.folder, e.flags, e.details_len, e.taken);
                if (!r && e.details_len) {
                    uint8_t d[GX_MAX_DETAILS]; fseek(f, (long)gx_details_abs(&h, &e), SEEK_SET); size_t got = fread(d, 1, e.details_len, f);
                    printf(" dok %d dabs %u d ", got == e.details_len && gx_details_ok(d, e.details_len), gx_details_abs(&h, &e)); for (size_t j = 0; j < got; j++) printf("%02x", d[j]);
                    const uint8_t *v; uint32_t vl; printf(" iso %d camera %d tag99 %d", gx_details_find(d, got, GD_ISO, &v, &vl), gx_details_find(d, got, GD_CAMERA, &v, &vl), gx_details_find(d, got, 99, &v, &vl));
                }
            }
            printf("\n");
        }
    }
    return 0;
}
