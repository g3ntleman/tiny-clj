#ifndef TINY_CLJ_PANEL_ESP_LCD_H
#define TINY_CLJ_PANEL_ESP_LCD_H

#include "panel.h"

typedef struct {
    bool (*reset)(void *ctx);
    bool (*init)(void *ctx);
    bool (*draw_bitmap)(void *ctx,
                        int x_start,
                        int y_start,
                        int x_end,
                        int y_end,
                        const void *color_data);
    bool (*mirror)(void *ctx, bool mirror_x, bool mirror_y);
    bool (*swap_xy)(void *ctx, bool swap_xy);
    bool (*set_gap)(void *ctx, int x_gap, int y_gap);
    bool (*disp_on_off)(void *ctx, bool on_off);
    bool (*disp_sleep)(void *ctx, bool sleep);
} VgEspLcdOps;

typedef struct {
    VgPanel panel;
    const VgEspLcdOps *ops;
    void *ctx;
} VgEspLcdPanel;

void vg_esp_lcd_panel_init(VgEspLcdPanel *esp_panel, const VgEspLcdOps *ops, void *ctx);

#ifdef ESP32_BUILD
#include "esp_lcd_panel_ops.h"
void vg_esp_lcd_panel_init_handle(VgEspLcdPanel *esp_panel, esp_lcd_panel_handle_t panel_handle);
#endif

#endif
