#include "panel_backend.h"

/**
 * @brief Submits one clipped framebuffer rect through the panel bitmap API.
 *
 * The source pointer may describe a clipped window inside a wider framebuffer
 * row stride. The adapter therefore uses the 2D panel write entry so the panel
 * layer can decide whether a native 2D callback exists or whether it should
 * fall back to per-row contiguous writes.
 *
 * @param panel Target panel.
 * @param fb Source framebuffer that owns the submitted pixels.
 * @param rect Dirty rect in framebuffer coordinates.
 * @return true when the dirty rect is valid and the panel accepts the write.
 */
bool vg_panel_backend_submit_clip_rect(VgPanel *panel,
                                       const VgFrameBuffer *fb,
                                       VgClipRect rect) {
    VgPanelBitmapView view = {0};
    if (!panel || !fb || !fb->pixels) {
        return false;
    }
    VgClipRect fb_rect = {0, 0, (int16_t)fb->width, (int16_t)fb->height};
    VgClipRect clipped = {0};
    if (!vg_clip_rect_intersect(rect, fb_rect, &clipped)) {
        return true;
    }
    if (!vg_panel_bitmap_view_init(fb->pixels,
                                   (uint16_t)fb->width,
                                   (uint16_t)fb->height,
                                   clipped.x,
                                   clipped.y,
                                   (int16_t)(clipped.x + clipped.w),
                                   (int16_t)(clipped.y + clipped.h),
                                   &view)) {
        return false;
    }
    if (view.stride_px == (uint16_t)view.width) {
        return vg_panel_write_bitmap(panel,
                                     clipped.x,
                                     clipped.y,
                                     (int16_t)(clipped.x + clipped.w),
                                     (int16_t)(clipped.y + clipped.h),
                                     view.pixels);
    }
    return vg_panel_write_bitmap_2d(panel,
                                    clipped.x,
                                    clipped.y,
                                    (int16_t)(clipped.x + clipped.w),
                                    (int16_t)(clipped.y + clipped.h),
                                    view.pixels,
                                    view.stride_px,
                                    (uint16_t)view.height,
                                    0,
                                    0,
                                    view.width,
                                    view.height);
}
