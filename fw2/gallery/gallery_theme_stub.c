/* The gallery reads no tau-assets.bin, so it has no file themes: ui/theme.c refers to these two accessors for a theme number past the built-in ones, a number theme_count() never allows here (th_file_n stays 0). */
#include "../ui/theme.h"
const char *theme_file_name(uint32_t i) { (void)i; return "?"; }
uint16_t theme_file_role(uint32_t i, uint32_t p, uint32_t r) { (void)i; return theme_builtin_role(p, r); }
