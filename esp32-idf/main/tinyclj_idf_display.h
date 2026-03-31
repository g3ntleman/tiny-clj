#ifndef TINYCLJ_IDF_DISPLAY_H
#define TINYCLJ_IDF_DISPLAY_H

#include <stdbool.h>

#if defined(TINYCLJ_WITH_TINY_FX) && TINYCLJ_WITH_TINY_FX
#include "panel_esp_lcd.h"

typedef struct {
    VgEspLcdPanel panel;
    void *panel_io_handle;
    void *panel_handle;
    bool initialized;
} TinycljIdfDisplay;

bool tinyclj_idf_display_init(TinycljIdfDisplay *display);
bool tinyclj_idf_display_bootstrap(void);
TinycljIdfDisplay *tinyclj_idf_display_get(void);
#endif

#endif
