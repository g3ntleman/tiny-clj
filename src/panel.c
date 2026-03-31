#include "panel.h"

#include <stddef.h>
#include <string.h>

/**
 * @brief Returns whether a panel has completed initialization.
 *
 * @param panel Panel descriptor.
 * @return true when the panel is ready for configuration or bitmap writes.
 */
static bool vg_panel_is_ready(const VgPanel *panel) {
    return panel && panel->initialized;
}

/**
 * @brief Validates one panel destination window expressed in end-exclusive coordinates.
 *
 * @param x_start Left edge, inclusive.
 * @param y_start Top edge, inclusive.
 * @param x_end Right edge, exclusive.
 * @param y_end Bottom edge, exclusive.
 * @return true when the rectangle spans at least one pixel.
 */
static bool vg_panel_bitmap_rect_is_valid(int16_t x_start, int16_t y_start, int16_t x_end, int16_t y_end) {
    return x_start >= 0 && y_start >= 0 && x_end > x_start && y_end > y_start;
}

static const uint16_t *vg_panel_bitmap_view_row(const VgPanelBitmapView *view, int16_t row) {
    return view->pixels + ((size_t)row * (size_t)view->stride_px);
}

static void vg_panel_record_transfer(VgPanel *panel, int16_t width_px, int16_t height_px) {
    if (!panel || width_px <= 0 || height_px <= 0) {
        return;
    }
    uint64_t pixels = (uint64_t)(uint16_t)width_px * (uint64_t)(uint16_t)height_px;
    panel->transfer_stats.transfer_count++;
    panel->transfer_stats.transferred_pixels += pixels;
    panel->transfer_stats.transferred_bytes += pixels * (uint64_t)sizeof(uint16_t);
}

/**
 * @brief Describes one cropped 2D RGB565 source view without allocating.
 *
 * The returned view points at the first pixel of the crop and keeps the source
 * row stride so callers can forward strided data or perform row copies without
 * building temporary buffers.
 *
 * @param src_pixels Full source bitmap pixels.
 * @param src_w Source bitmap width in pixels.
 * @param src_h Source bitmap height in pixels.
 * @param src_x_start Crop left edge, inclusive.
 * @param src_y_start Crop top edge, inclusive.
 * @param src_x_end Crop right edge, exclusive.
 * @param src_y_end Crop bottom edge, exclusive.
 * @param out_view Receives the cropped bitmap view.
 * @return true when the crop is valid and fully contained in the source bitmap.
 */
bool vg_panel_bitmap_view_init(const uint16_t *src_pixels,
                               uint16_t src_w,
                               uint16_t src_h,
                               int16_t src_x_start,
                               int16_t src_y_start,
                               int16_t src_x_end,
                               int16_t src_y_end,
                               VgPanelBitmapView *out_view) {
    if (!src_pixels || !out_view ||
        !vg_panel_bitmap_rect_is_valid(src_x_start, src_y_start, src_x_end, src_y_end)) {
        return false;
    }
    if ((uint16_t)src_x_end > src_w || (uint16_t)src_y_end > src_h) {
        return false;
    }
    out_view->pixels = src_pixels + ((size_t)src_y_start * (size_t)src_w) + (size_t)src_x_start;
    out_view->stride_px = src_w;
    out_view->width = (int16_t)(src_x_end - src_x_start);
    out_view->height = (int16_t)(src_y_end - src_y_start);
    return true;
}

/**
 * @brief Copies one RGB565 bitmap view into a destination surface without allocating.
 *
 * @param dst_pixels Destination surface pixels.
 * @param dst_w Destination width in pixels.
 * @param dst_h Destination height in pixels.
 * @param dst_x_start Destination left edge, inclusive.
 * @param dst_y_start Destination top edge, inclusive.
 * @param src_view Cropped source bitmap view.
 * @return true when source and destination bounds are valid and the copy succeeds.
 */
bool vg_panel_rgb565_blit(uint16_t *dst_pixels,
                          uint16_t dst_w,
                          uint16_t dst_h,
                          int16_t dst_x_start,
                          int16_t dst_y_start,
                          const VgPanelBitmapView *src_view) {
    if (!dst_pixels || !src_view || !src_view->pixels || src_view->stride_px < (uint16_t)src_view->width ||
        src_view->width <= 0 || src_view->height <= 0 || dst_x_start < 0 || dst_y_start < 0) {
        return false;
    }
    int16_t dst_x_end = (int16_t)(dst_x_start + src_view->width);
    int16_t dst_y_end = (int16_t)(dst_y_start + src_view->height);
    if (!vg_panel_bitmap_rect_is_valid(dst_x_start, dst_y_start, dst_x_end, dst_y_end)) {
        return false;
    }
    if ((uint16_t)dst_x_end > dst_w || (uint16_t)dst_y_end > dst_h) {
        return false;
    }
    if (dst_x_start == 0 && src_view->width == (int16_t)dst_w && src_view->stride_px == dst_w) {
        size_t dst_off = (size_t)dst_y_start * (size_t)dst_w;
        memcpy(&dst_pixels[dst_off],
               src_view->pixels,
               (size_t)src_view->width * (size_t)src_view->height * sizeof(uint16_t));
        return true;
    }
    for (int16_t row = 0; row < src_view->height; row++) {
        size_t dst_off = (size_t)(dst_y_start + row) * (size_t)dst_w + (size_t)dst_x_start;
        memcpy(&dst_pixels[dst_off],
               vg_panel_bitmap_view_row(src_view, row),
               (size_t)src_view->width * sizeof(uint16_t));
    }
    return true;
}

/**
 * @brief Resets one panel and clears the initialized state on success.
 *
 * @param panel Panel descriptor.
 * @return true when the reset callback succeeds.
 */
bool vg_panel_reset(VgPanel *panel) {
    if (!panel || !panel->ops || !panel->ops->reset) {
        return false;
    }
    if (!panel->ops->reset(panel->ctx)) {
        return false;
    }
    panel->initialized = false;
    return true;
}

/**
 * @brief Initializes one panel and marks it ready for subsequent commands.
 *
 * @param panel Panel descriptor.
 * @return true when the init callback succeeds.
 */
bool vg_panel_init(VgPanel *panel) {
    if (!panel || !panel->ops || !panel->ops->init) {
        return false;
    }
    if (!panel->ops->init(panel->ctx)) {
        return false;
    }
    panel->initialized = true;
    return true;
}

/**
 * @brief Configures the panel memory orientation.
 *
 * @param panel Panel descriptor.
 * @param mirror_x Mirror x axis.
 * @param mirror_y Mirror y axis.
 * @param swap_xy Swap x/y axes.
 * @return true when the command succeeds.
 */
bool vg_panel_set_orientation(VgPanel *panel, bool mirror_x, bool mirror_y, bool swap_xy) {
    if (!vg_panel_is_ready(panel) || !panel->ops || !panel->ops->set_orientation) {
        return false;
    }
    return panel->ops->set_orientation(panel->ctx, mirror_x, mirror_y, swap_xy);
}

/**
 * @brief Configures the panel coordinate gap when supported.
 *
 * Missing callbacks are treated as a successful no-op because some targets do
 * not need a gap command at all.
 *
 * @param panel Panel descriptor.
 * @param x_gap Horizontal gap in pixels.
 * @param y_gap Vertical gap in pixels.
 * @return true when the panel is ready and the optional command succeeds.
 */
bool vg_panel_set_gap(VgPanel *panel, int16_t x_gap, int16_t y_gap) {
    if (!vg_panel_is_ready(panel) || !panel->ops) {
        return false;
    }
    if (!panel->ops->set_gap) {
        return true;
    }
    return panel->ops->set_gap(panel->ctx, x_gap, y_gap);
}

/**
 * @brief Writes one contiguous RGB565 window to the panel.
 *
 * Coordinates follow the common panel convention `start` inclusive, `end`
 * exclusive, matching `esp_lcd_panel_draw_bitmap`.
 *
 * @param panel Panel descriptor.
 * @param x_start Left edge, inclusive.
 * @param y_start Top edge, inclusive.
 * @param x_end Right edge, exclusive.
 * @param y_end Bottom edge, exclusive.
 * @param rgb565_pixels Pointer to tightly packed RGB565 source pixels.
 * @return true when the bitmap window is valid and the callback succeeds.
 */
bool vg_panel_write_bitmap(VgPanel *panel,
                           int16_t x_start,
                           int16_t y_start,
                           int16_t x_end,
                           int16_t y_end,
                           const uint16_t *rgb565_pixels) {
    if (!vg_panel_is_ready(panel) || !panel->ops || !panel->ops->write_bitmap || !rgb565_pixels) {
        return false;
    }
    if (!vg_panel_bitmap_rect_is_valid(x_start, y_start, x_end, y_end)) {
        return false;
    }
    if (!panel->ops->write_bitmap(panel->ctx, x_start, y_start, x_end, y_end, rgb565_pixels)) {
        return false;
    }
    vg_panel_record_transfer(panel, (int16_t)(x_end - x_start), (int16_t)(y_end - y_start));
    return true;
}

/**
 * @brief Writes one RGB565 sub-rectangle from a 2D source bitmap to the panel.
 *
 * When the backend lacks a dedicated 2D callback, the helper falls back to the
 * contiguous `write_bitmap` callback. Full-width crops are forwarded as one
 * transfer, narrow crops are emitted row by row so no temporary heap buffer is
 * required.
 *
 * @param panel Panel descriptor.
 * @param x_start Destination left edge, inclusive.
 * @param y_start Destination top edge, inclusive.
 * @param x_end Destination right edge, exclusive.
 * @param y_end Destination bottom edge, exclusive.
 * @param src_pixels Full source bitmap pixels.
 * @param src_w Source bitmap width.
 * @param src_h Source bitmap height.
 * @param src_x_start Source crop left edge, inclusive.
 * @param src_y_start Source crop top edge, inclusive.
 * @param src_x_end Source crop right edge, exclusive.
 * @param src_y_end Source crop bottom edge, exclusive.
 * @return true when the crop is valid and all underlying writes succeed.
 */
bool vg_panel_write_bitmap_2d(VgPanel *panel,
                              int16_t x_start,
                              int16_t y_start,
                              int16_t x_end,
                              int16_t y_end,
                              const uint16_t *src_pixels,
                              uint16_t src_w,
                              uint16_t src_h,
                              int16_t src_x_start,
                              int16_t src_y_start,
                              int16_t src_x_end,
                              int16_t src_y_end) {
    VgPanelBitmapView view = {0};
    if (!vg_panel_is_ready(panel) || !panel->ops || !src_pixels) {
        return false;
    }
    if (!vg_panel_bitmap_rect_is_valid(x_start, y_start, x_end, y_end) ||
        !vg_panel_bitmap_rect_is_valid(src_x_start, src_y_start, src_x_end, src_y_end)) {
        return false;
    }
    int16_t dest_w = (int16_t)(x_end - x_start);
    int16_t dest_h = (int16_t)(y_end - y_start);
    int16_t src_rect_w = (int16_t)(src_x_end - src_x_start);
    int16_t src_rect_h = (int16_t)(src_y_end - src_y_start);
    if (dest_w != src_rect_w || dest_h != src_rect_h) {
        return false;
    }
    if (!vg_panel_bitmap_view_init(src_pixels,
                                   src_w,
                                   src_h,
                                   src_x_start,
                                   src_y_start,
                                   src_x_end,
                                   src_y_end,
                                   &view)) {
        return false;
    }
    if (panel->ops->write_bitmap_2d) {
        if (!panel->ops->write_bitmap_2d(panel->ctx,
                                         x_start,
                                         y_start,
                                         x_end,
                                         y_end,
                                         src_pixels,
                                         src_w,
                                         src_h,
                                         src_x_start,
                                         src_y_start,
                                         src_x_end,
                                         src_y_end)) {
            return false;
        }
        vg_panel_record_transfer(panel, (int16_t)(x_end - x_start), (int16_t)(y_end - y_start));
        return true;
    }
    if (!panel->ops->write_bitmap) {
        return false;
    }
    if (view.stride_px == (uint16_t)view.width) {
        if (!panel->ops->write_bitmap(panel->ctx,
                                      x_start,
                                      y_start,
                                      x_end,
                                      y_end,
                                      view.pixels)) {
            return false;
        }
        vg_panel_record_transfer(panel, view.width, view.height);
        return true;
    }

    for (int16_t row = 0; row < view.height; row++) {
        if (!panel->ops->write_bitmap(panel->ctx,
                                      x_start,
                                      (int16_t)(y_start + row),
                                      x_end,
                                      (int16_t)(y_start + row + 1),
                                      vg_panel_bitmap_view_row(&view, row))) {
            return false;
        }
        vg_panel_record_transfer(panel, view.width, 1);
    }
    return true;
}

/**
 * @brief Enables or disables panel output.
 *
 * @param panel Panel descriptor.
 * @param enabled Desired display state.
 * @return true when the command succeeds.
 */
bool vg_panel_set_display_enabled(VgPanel *panel, bool enabled) {
    if (!vg_panel_is_ready(panel) || !panel->ops || !panel->ops->set_display_enabled) {
        return false;
    }
    return panel->ops->set_display_enabled(panel->ctx, enabled);
}

/**
 * @brief Requests sleep mode when supported.
 *
 * Missing callbacks are treated as a successful no-op because not every panel
 * path exposes a sleep command.
 *
 * @param panel Panel descriptor.
 * @param sleep Desired sleep state.
 * @return true when the panel is ready and the optional command succeeds.
 */
bool vg_panel_set_sleep(VgPanel *panel, bool sleep) {
    if (!vg_panel_is_ready(panel) || !panel->ops) {
        return false;
    }
    if (!panel->ops->set_sleep) {
        return true;
    }
    return panel->ops->set_sleep(panel->ctx, sleep);
}

bool vg_panel_transfer_stats_snapshot(const VgPanel *panel, VgPanelTransferStats *out_stats) {
    if (!panel || !out_stats) {
        return false;
    }
    *out_stats = panel->transfer_stats;
    return true;
}

bool vg_panel_transfer_stats_reset(VgPanel *panel) {
    if (!panel) {
        return false;
    }
    memset(&panel->transfer_stats, 0, sizeof(panel->transfer_stats));
    return true;
}
