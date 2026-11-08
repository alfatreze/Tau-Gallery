/* Tau Gallery M0 test patterns (docs/M0_DESIGN.md 3 and 6c): pure functions, no hardware, so a host test can check them. A pattern is a function of (x, y) over the 800 x 720 viewer plane;
 * a line is packed in the frozen plane format: R, G, B bytes per pixel with no padding (2,400 bytes), 16-bit word i = {byte(2i+1), byte(2i)}, so a 32-bit load of four bytes is two words. */
#ifndef GALLERY_PATTERN_H
#define GALLERY_PATTERN_H
#include <stdint.h>
#define GAL_W 800u
#define GAL_H 720u
#define GAL_LINE_BYTES (GAL_W * 3u)                 /* 2,400 */
#define GAL_LINE_WORDS16 (GAL_LINE_BYTES / 2u)      /* 1,200 16-bit words: the plane stride */
#define GAL_LINE_WORDS32 (GAL_LINE_BYTES / 4u)      /* 600 mailbox words */
enum { GP_RAMP = 0, GP_GRADIENT, GP_CHECKER, GP_TEXTURE, GP_COUNT };
extern const char *const gal_pattern_name[GP_COUNT];
/* One pixel. All patterns but the texture have a one-pixel white frame on the outermost pixels (a cropped or shifted picture shows at once). */
void gal_pixel(uint32_t pat, uint32_t x, uint32_t y, uint8_t rgb[3]);
/* Plane line y as 2,400 bytes. */
void gal_line(uint32_t pat, uint32_t y, uint8_t *bytes);
/* The i-th 32-bit mailbox word of a packed line: the four bytes 4i..4i+3, byte 4i lowest (two plane words, the first in the low half). */
uint32_t gal_pack32(const uint8_t *bytes, uint32_t i);
#endif
