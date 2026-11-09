/* The register-file side of the gallery settings (gallery_settings.c). */
#ifndef GALLERY_SETTINGS_HW_H
#define GALLERY_SETTINGS_HW_H
#include "gallery_settings.h"
typedef struct { uint32_t loads, stores, menu_changes, layout; } gs_stats_t;   /* layout: the layout version the word 0 held at the last read or write (0 = nothing saved yet) */
extern gs_stats_t gs_stats;
void gs_hw_load(gs_vals_t *v);                                   /* once at boot: the saved values (the defaults when the file is not ours) */
/* Once per idle pass. A change in `v` is published after a quiet second since `last_activity` (cycles of the last key or card read); a change made in the Pocket's Core Settings is adopted into `v` (returns 1). */
int  gs_hw_service(gs_vals_t *v, uint32_t last_activity);
#endif
