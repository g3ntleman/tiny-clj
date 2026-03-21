#include "panel_esp_lcd.h"

static bool vg_esp_lcd_reset(void *ctx) {
    VgEspLcdPanel *esp_panel = (VgEspLcdPanel *)ctx;
    return esp_panel && esp_panel->ops && esp_panel->ops->reset && esp_panel->ops->reset(esp_panel->ctx);
}

static bool vg_esp_lcd_init(void *ctx) {
    VgEspLcdPanel *esp_panel = (VgEspLcdPanel *)ctx;
    return esp_panel && esp_panel->ops && esp_panel->ops->init && esp_panel->ops->init(esp_panel->ctx);
}

static bool vg_esp_lcd_set_orientation(void *ctx, bool mirror_x, bool mirror_y, bool swap_xy) {
    VgEspLcdPanel *esp_panel = (VgEspLcdPanel *)ctx;
    if (!esp_panel || !esp_panel->ops || !esp_panel->ops->swap_xy || !esp_panel->ops->mirror) {
        return false;
    }
    if (!esp_panel->ops->swap_xy(esp_panel->ctx, swap_xy)) {
        return false;
    }
    return esp_panel->ops->mirror(esp_panel->ctx, mirror_x, mirror_y);
}

static bool vg_esp_lcd_set_gap(void *ctx, int16_t x_gap, int16_t y_gap) {
    VgEspLcdPanel *esp_panel = (VgEspLcdPanel *)ctx;
    if (!esp_panel || !esp_panel->ops) {
        return false;
    }
    if (!esp_panel->ops->set_gap) {
        return true;
    }
    return esp_panel->ops->set_gap(esp_panel->ctx, x_gap, y_gap);
}

static bool vg_esp_lcd_write_bitmap(void *ctx,
                                    int16_t x_start,
                                    int16_t y_start,
                                    int16_t x_end,
                                    int16_t y_end,
                                    const uint16_t *rgb565_pixels) {
    VgEspLcdPanel *esp_panel = (VgEspLcdPanel *)ctx;
    return esp_panel && esp_panel->ops && esp_panel->ops->draw_bitmap &&
           esp_panel->ops->draw_bitmap(esp_panel->ctx, x_start, y_start, x_end, y_end, rgb565_pixels);
}

static bool vg_esp_lcd_set_display_enabled(void *ctx, bool enabled) {
    VgEspLcdPanel *esp_panel = (VgEspLcdPanel *)ctx;
    return esp_panel && esp_panel->ops && esp_panel->ops->disp_on_off &&
           esp_panel->ops->disp_on_off(esp_panel->ctx, enabled);
}

static bool vg_esp_lcd_set_sleep(void *ctx, bool sleep) {
    VgEspLcdPanel *esp_panel = (VgEspLcdPanel *)ctx;
    return esp_panel && esp_panel->ops && esp_panel->ops->disp_sleep &&
           esp_panel->ops->disp_sleep(esp_panel->ctx, sleep);
}

static const VgPanelOps vg_esp_lcd_panel_ops = {
    .reset = vg_esp_lcd_reset,
    .init = vg_esp_lcd_init,
    .set_orientation = vg_esp_lcd_set_orientation,
    .set_gap = vg_esp_lcd_set_gap,
    .write_bitmap = vg_esp_lcd_write_bitmap,
    .set_display_enabled = vg_esp_lcd_set_display_enabled,
    .set_sleep = vg_esp_lcd_set_sleep,
};

void vg_esp_lcd_panel_init(VgEspLcdPanel *esp_panel, const VgEspLcdOps *ops, void *ctx) {
    if (!esp_panel) {
        return;
    }
    esp_panel->panel.ops = &vg_esp_lcd_panel_ops;
    esp_panel->panel.ctx = esp_panel;
    esp_panel->panel.initialized = false;
    esp_panel->ops = ops;
    esp_panel->ctx = ctx;
}

#ifdef ESP32_BUILD
#include "esp_lcd_panel_ops.h"

static bool vg_esp_lcd_native_reset(void *ctx) {
    return esp_lcd_panel_reset((esp_lcd_panel_handle_t)ctx) == ESP_OK;
}

static bool vg_esp_lcd_native_init(void *ctx) {
    return esp_lcd_panel_init((esp_lcd_panel_handle_t)ctx) == ESP_OK;
}

static bool vg_esp_lcd_native_draw_bitmap(void *ctx,
                                          int x_start,
                                          int y_start,
                                          int x_end,
                                          int y_end,
                                          const void *color_data) {
    return esp_lcd_panel_draw_bitmap((esp_lcd_panel_handle_t)ctx, x_start, y_start, x_end, y_end, color_data) == ESP_OK;
}

static bool vg_esp_lcd_native_mirror(void *ctx, bool mirror_x, bool mirror_y) {
    return esp_lcd_panel_mirror((esp_lcd_panel_handle_t)ctx, mirror_x, mirror_y) == ESP_OK;
}

static bool vg_esp_lcd_native_swap_xy(void *ctx, bool swap_xy) {
    return esp_lcd_panel_swap_xy((esp_lcd_panel_handle_t)ctx, swap_xy) == ESP_OK;
}

static bool vg_esp_lcd_native_set_gap(void *ctx, int x_gap, int y_gap) {
    return esp_lcd_panel_set_gap((esp_lcd_panel_handle_t)ctx, x_gap, y_gap) == ESP_OK;
}

static bool vg_esp_lcd_native_disp_on_off(void *ctx, bool on_off) {
    return esp_lcd_panel_disp_on_off((esp_lcd_panel_handle_t)ctx, on_off) == ESP_OK;
}

static bool vg_esp_lcd_native_disp_sleep(void *ctx, bool sleep) {
    return esp_lcd_panel_disp_sleep((esp_lcd_panel_handle_t)ctx, sleep) == ESP_OK;
}

void vg_esp_lcd_panel_init_handle(VgEspLcdPanel *esp_panel, esp_lcd_panel_handle_t panel_handle) {
    static const VgEspLcdOps ops = {
        .reset = vg_esp_lcd_native_reset,
        .init = vg_esp_lcd_native_init,
        .draw_bitmap = vg_esp_lcd_native_draw_bitmap,
        .mirror = vg_esp_lcd_native_mirror,
        .swap_xy = vg_esp_lcd_native_swap_xy,
        .set_gap = vg_esp_lcd_native_set_gap,
        .disp_on_off = vg_esp_lcd_native_disp_on_off,
        .disp_sleep = vg_esp_lcd_native_disp_sleep,
    };
    vg_esp_lcd_panel_init(esp_panel, &ops, panel_handle);
}
#endif
