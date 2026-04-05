#ifndef TINYCLJ_IDF_RENDERER_H
#define TINYCLJ_IDF_RENDERER_H

#include <stdbool.h>

#if defined(TINY_FX_ENABLED) && TINY_FX_ENABLED
#include "tinyclj_idf_display.h"
bool tinyclj_idf_renderer_init(TinycljIdfDisplay *display);
#else
bool tinyclj_idf_renderer_init(void *display);
#endif

bool tinyclj_idf_renderer_show_boot_screen(void);

#endif /* TINYCLJ_IDF_RENDERER_H */
