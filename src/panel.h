#ifndef TINY_CLJ_PANEL_H
#define TINY_CLJ_PANEL_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const uint16_t *pixels;
    uint16_t stride_px;
    int16_t width;
    int16_t height;
} VgPanelBitmapView;

typedef struct {
    bool (*reset)(void *ctx);
    bool (*init)(void *ctx);
    bool (*set_orientation)(void *ctx, bool mirror_x, bool mirror_y, bool swap_xy);
    bool (*set_gap)(void *ctx, int16_t x_gap, int16_t y_gap);
    bool (*write_bitmap)(void *ctx,
                         int16_t x_start,
                         int16_t y_start,
                         int16_t x_end,
                         int16_t y_end,
                         const uint16_t *rgb565_pixels);
    bool (*write_bitmap_2d)(void *ctx,
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
                            int16_t src_y_end);
    bool (*set_display_enabled)(void *ctx, bool enabled);
    bool (*set_sleep)(void *ctx, bool sleep);
} VgPanelOps;

typedef struct {
    uint64_t transfer_count;
    uint64_t transferred_pixels;
    uint64_t transferred_bytes;
} VgPanelTransferStats;

typedef struct {
    const VgPanelOps *ops;
    void *ctx;
    bool initialized;
    VgPanelTransferStats transfer_stats;
} VgPanel;

bool vg_panel_bitmap_view_init(const uint16_t *src_pixels,
                               uint16_t src_w,
                               uint16_t src_h,
                               int16_t src_x_start,
                               int16_t src_y_start,
                               int16_t src_x_end,
                               int16_t src_y_end,
                               VgPanelBitmapView *out_view);
bool vg_panel_rgb565_blit(uint16_t *dst_pixels,
                          uint16_t dst_w,
                          uint16_t dst_h,
                          int16_t dst_x_start,
                          int16_t dst_y_start,
                          const VgPanelBitmapView *src_view);
bool vg_panel_reset(VgPanel *panel);
bool vg_panel_init(VgPanel *panel);
bool vg_panel_set_orientation(VgPanel *panel, bool mirror_x, bool mirror_y, bool swap_xy);
bool vg_panel_set_gap(VgPanel *panel, int16_t x_gap, int16_t y_gap);
bool vg_panel_write_bitmap(VgPanel *panel,
                           int16_t x_start,
                           int16_t y_start,
                           int16_t x_end,
                           int16_t y_end,
                           const uint16_t *rgb565_pixels);
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
                              int16_t src_y_end);
bool vg_panel_set_display_enabled(VgPanel *panel, bool enabled);
bool vg_panel_set_sleep(VgPanel *panel, bool sleep);
bool vg_panel_transfer_stats_snapshot(const VgPanel *panel, VgPanelTransferStats *out_stats);
bool vg_panel_transfer_stats_reset(VgPanel *panel);

#endif
