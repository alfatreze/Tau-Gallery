/* Colour presets of the viewer (K5): one 256-entry table a colour channel, loaded into the scan-out's tables through R_PIX_LUT.
 * Preset 0 is NORMAL (tables off, nothing loaded). Integer maths only (no libm); every value is clamped to 0..255. The presets are numbered as stored in the settings word (3 bits). */
#ifndef GALLERY_LUT_H
#define GALLERY_LUT_H
#include <stdint.h>
enum { GL_NORMAL = 0, GL_BRIGHT, GL_CONTRAST, GL_MIDTONES, GL_FADED, GL_WARM, GL_COOL, GL_INVERT, GL_PRESETS };
static const char *const gl_names[GL_PRESETS] = { "NORMAL", "BRIGHT", "CONTRAST", "MIDTONES", "FADED", "WARM", "COOL", "INVERT" };
static inline uint32_t gl_clamp(int32_t v) { return v < 0 ? 0u : v > 255 ? 255u : (uint32_t)v; }
/* The table value of `preset` for channel ch (0 R, 1 G, 2 B) at input i (0..255). */
static inline uint32_t gl_value(uint32_t preset, uint32_t ch, uint32_t i)
{
    const int32_t x = (int32_t)(i & 255u);
    switch (preset % GL_PRESETS) {
    case GL_BRIGHT:   return gl_clamp(x + (32 * (255 - x) + 127) / 255);                      /* lifts the dark end by up to 32, white stays white */
    case GL_CONTRAST: return gl_clamp(((x - 128) * 5) / 4 + 128);                              /* steeper around mid grey */
    case GL_MIDTONES: return gl_clamp(x + (x * (255 - x)) / 512);                              /* lifts the middle by up to 31, both ends stay */
    case GL_FADED:    return gl_clamp(24 + (x * 231) / 255);                                   /* blacks lifted to 24 */
    case GL_WARM:     return ch == 0u ? gl_clamp((x * 17) / 16) : ch == 2u ? gl_clamp((x * 7) / 8) : (uint32_t)x;
    case GL_COOL:     return ch == 2u ? gl_clamp((x * 17) / 16) : ch == 0u ? gl_clamp((x * 7) / 8) : (uint32_t)x;
    case GL_INVERT:   return (uint32_t)(255 - x);
    default:          return (uint32_t)x;
    }
}
/* The R_PIX_LUT word for one entry. */
static inline uint32_t gl_word(uint32_t ch, uint32_t i, uint32_t v) { return ((ch & 3u) << 16) | ((i & 255u) << 8) | (v & 255u); }
#endif
