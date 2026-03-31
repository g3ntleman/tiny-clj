#include "tests_common.h"
#include "panel.h"
#include "panel_esp_lcd.h"
#include "panel_backend.h"

typedef struct {
    uint32_t reset_calls;
    uint32_t init_calls;
    uint32_t orientation_calls;
    uint32_t gap_calls;
    uint32_t write_bitmap_calls;
    uint32_t write_bitmap_2d_calls;
    uint32_t display_enabled_calls;
    uint32_t sleep_calls;
    bool last_mirror_x;
    bool last_mirror_y;
    bool last_swap_xy;
    int16_t last_x_gap;
    int16_t last_y_gap;
    int16_t last_x_start;
    int16_t last_y_start;
    int16_t last_x_end;
    int16_t last_y_end;
    int16_t last_src_x_start;
    int16_t last_src_y_start;
    int16_t last_src_x_end;
    int16_t last_src_y_end;
    uint16_t last_src_w;
    uint16_t last_src_h;
    bool last_display_enabled;
    bool last_sleep;
    const uint16_t *last_pixels;
    bool fail_reset;
    bool fail_init;
    bool fail_orientation;
    bool fail_gap;
    bool fail_write_bitmap;
    bool fail_write_bitmap_2d;
    bool fail_display_enabled;
    bool fail_sleep;
} TestPanelCapture;

typedef struct {
    uint32_t reset_calls;
    uint32_t init_calls;
    uint32_t draw_bitmap_calls;
    uint32_t mirror_calls;
    uint32_t swap_xy_calls;
    uint32_t set_gap_calls;
    uint32_t disp_on_off_calls;
    uint32_t disp_sleep_calls;
    int last_x_start;
    int last_y_start;
    int last_x_end;
    int last_y_end;
    int last_x_gap;
    int last_y_gap;
    bool last_mirror_x;
    bool last_mirror_y;
    bool last_swap_xy;
    bool last_disp_on_off;
    bool last_disp_sleep;
    const void *last_color_data;
    bool fail_reset;
    bool fail_init;
    bool fail_draw_bitmap;
    bool fail_mirror;
    bool fail_swap_xy;
    bool fail_set_gap;
    bool fail_disp_on_off;
    bool fail_disp_sleep;
} TestEspLcdCapture;

static bool test_panel_reset(void *ctx) {
    TestPanelCapture *capture = (TestPanelCapture *)ctx;
    TEST_ASSERT_NOT_NULL(capture);
    capture->reset_calls++;
    return !capture->fail_reset;
}

static bool test_panel_init(void *ctx) {
    TestPanelCapture *capture = (TestPanelCapture *)ctx;
    TEST_ASSERT_NOT_NULL(capture);
    capture->init_calls++;
    return !capture->fail_init;
}

static bool test_panel_set_orientation(void *ctx, bool mirror_x, bool mirror_y, bool swap_xy) {
    TestPanelCapture *capture = (TestPanelCapture *)ctx;
    TEST_ASSERT_NOT_NULL(capture);
    capture->orientation_calls++;
    capture->last_mirror_x = mirror_x;
    capture->last_mirror_y = mirror_y;
    capture->last_swap_xy = swap_xy;
    return !capture->fail_orientation;
}

static bool test_panel_set_gap(void *ctx, int16_t x_gap, int16_t y_gap) {
    TestPanelCapture *capture = (TestPanelCapture *)ctx;
    TEST_ASSERT_NOT_NULL(capture);
    capture->gap_calls++;
    capture->last_x_gap = x_gap;
    capture->last_y_gap = y_gap;
    return !capture->fail_gap;
}

static bool test_panel_write_bitmap(void *ctx,
                                    int16_t x_start,
                                    int16_t y_start,
                                    int16_t x_end,
                                    int16_t y_end,
                                    const uint16_t *rgb565_pixels) {
    TestPanelCapture *capture = (TestPanelCapture *)ctx;
    TEST_ASSERT_NOT_NULL(capture);
    TEST_ASSERT_NOT_NULL(rgb565_pixels);
    capture->write_bitmap_calls++;
    capture->last_x_start = x_start;
    capture->last_y_start = y_start;
    capture->last_x_end = x_end;
    capture->last_y_end = y_end;
    capture->last_pixels = rgb565_pixels;
    return !capture->fail_write_bitmap;
}

static bool test_panel_write_bitmap_2d(void *ctx,
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
    TestPanelCapture *capture = (TestPanelCapture *)ctx;
    TEST_ASSERT_NOT_NULL(capture);
    TEST_ASSERT_NOT_NULL(src_pixels);
    capture->write_bitmap_2d_calls++;
    capture->last_x_start = x_start;
    capture->last_y_start = y_start;
    capture->last_x_end = x_end;
    capture->last_y_end = y_end;
    capture->last_src_x_start = src_x_start;
    capture->last_src_y_start = src_y_start;
    capture->last_src_x_end = src_x_end;
    capture->last_src_y_end = src_y_end;
    capture->last_src_w = src_w;
    capture->last_src_h = src_h;
    capture->last_pixels = src_pixels;
    return !capture->fail_write_bitmap_2d;
}

static bool test_panel_set_display_enabled(void *ctx, bool enabled) {
    TestPanelCapture *capture = (TestPanelCapture *)ctx;
    TEST_ASSERT_NOT_NULL(capture);
    capture->display_enabled_calls++;
    capture->last_display_enabled = enabled;
    return !capture->fail_display_enabled;
}

static bool test_panel_set_sleep(void *ctx, bool sleep) {
    TestPanelCapture *capture = (TestPanelCapture *)ctx;
    TEST_ASSERT_NOT_NULL(capture);
    capture->sleep_calls++;
    capture->last_sleep = sleep;
    return !capture->fail_sleep;
}

static bool test_esp_lcd_reset(void *ctx) {
    TestEspLcdCapture *capture = (TestEspLcdCapture *)ctx;
    TEST_ASSERT_NOT_NULL(capture);
    capture->reset_calls++;
    return !capture->fail_reset;
}

static bool test_esp_lcd_init(void *ctx) {
    TestEspLcdCapture *capture = (TestEspLcdCapture *)ctx;
    TEST_ASSERT_NOT_NULL(capture);
    capture->init_calls++;
    return !capture->fail_init;
}

static bool test_esp_lcd_draw_bitmap(void *ctx,
                                     int x_start,
                                     int y_start,
                                     int x_end,
                                     int y_end,
                                     const void *color_data) {
    TestEspLcdCapture *capture = (TestEspLcdCapture *)ctx;
    TEST_ASSERT_NOT_NULL(capture);
    TEST_ASSERT_NOT_NULL(color_data);
    capture->draw_bitmap_calls++;
    capture->last_x_start = x_start;
    capture->last_y_start = y_start;
    capture->last_x_end = x_end;
    capture->last_y_end = y_end;
    capture->last_color_data = color_data;
    return !capture->fail_draw_bitmap;
}

static bool test_esp_lcd_mirror(void *ctx, bool mirror_x, bool mirror_y) {
    TestEspLcdCapture *capture = (TestEspLcdCapture *)ctx;
    TEST_ASSERT_NOT_NULL(capture);
    capture->mirror_calls++;
    capture->last_mirror_x = mirror_x;
    capture->last_mirror_y = mirror_y;
    return !capture->fail_mirror;
}

static bool test_esp_lcd_swap_xy(void *ctx, bool swap_xy) {
    TestEspLcdCapture *capture = (TestEspLcdCapture *)ctx;
    TEST_ASSERT_NOT_NULL(capture);
    capture->swap_xy_calls++;
    capture->last_swap_xy = swap_xy;
    return !capture->fail_swap_xy;
}

static bool test_esp_lcd_set_gap(void *ctx, int x_gap, int y_gap) {
    TestEspLcdCapture *capture = (TestEspLcdCapture *)ctx;
    TEST_ASSERT_NOT_NULL(capture);
    capture->set_gap_calls++;
    capture->last_x_gap = x_gap;
    capture->last_y_gap = y_gap;
    return !capture->fail_set_gap;
}

static bool test_esp_lcd_disp_on_off(void *ctx, bool on_off) {
    TestEspLcdCapture *capture = (TestEspLcdCapture *)ctx;
    TEST_ASSERT_NOT_NULL(capture);
    capture->disp_on_off_calls++;
    capture->last_disp_on_off = on_off;
    return !capture->fail_disp_on_off;
}

static bool test_esp_lcd_disp_sleep(void *ctx, bool sleep) {
    TestEspLcdCapture *capture = (TestEspLcdCapture *)ctx;
    TEST_ASSERT_NOT_NULL(capture);
    capture->disp_sleep_calls++;
    capture->last_disp_sleep = sleep;
    return !capture->fail_disp_sleep;
}

TEST(test_panel_api_bitmap_view_tracks_cropped_stride_without_copy) {
    uint16_t pixels[5 * 4];
    for (size_t i = 0; i < (sizeof(pixels) / sizeof(pixels[0])); i++) {
        pixels[i] = (uint16_t)(300u + i);
    }

    VgPanelBitmapView view = {0};
    TEST_ASSERT_TRUE(vg_panel_bitmap_view_init(pixels, 5, 4, 1, 1, 4, 3, &view));

    TEST_ASSERT_TRUE(view.pixels == &pixels[6]);
    TEST_ASSERT_EQUAL_UINT16(5u, view.stride_px);
    TEST_ASSERT_EQUAL_INT(3, view.width);
    TEST_ASSERT_EQUAL_INT(2, view.height);
}

TEST(test_panel_api_rgb565_blit_copies_strided_rows_without_allocation) {
    uint16_t src_pixels[5 * 4];
    for (size_t i = 0; i < (sizeof(src_pixels) / sizeof(src_pixels[0])); i++) {
        src_pixels[i] = (uint16_t)(400u + i);
    }
    uint16_t dst_pixels[6 * 5] = {0};

    VgPanelBitmapView view = {0};
    TEST_ASSERT_TRUE(vg_panel_bitmap_view_init(src_pixels, 5, 4, 1, 1, 4, 3, &view));
    TEST_ASSERT_TRUE(vg_panel_rgb565_blit(dst_pixels, 6, 5, 2, 1, &view));

    TEST_ASSERT_EQUAL_UINT16(src_pixels[6], dst_pixels[8]);
    TEST_ASSERT_EQUAL_UINT16(src_pixels[8], dst_pixels[10]);
    TEST_ASSERT_EQUAL_UINT16(src_pixels[11], dst_pixels[14]);
    TEST_ASSERT_EQUAL_UINT16(src_pixels[13], dst_pixels[16]);
}

TEST(test_panel_api_requires_init_before_configuration_and_write) {
    TestPanelCapture capture = {0};
    const VgPanelOps ops = {
        .reset = test_panel_reset,
        .init = test_panel_init,
        .set_orientation = test_panel_set_orientation,
        .write_bitmap = test_panel_write_bitmap,
        .set_display_enabled = test_panel_set_display_enabled,
    };
    VgPanel panel = {.ops = &ops, .ctx = &capture};
    uint16_t pixels[4] = {1u, 2u, 3u, 4u};

    TEST_ASSERT_FALSE(vg_panel_set_orientation(&panel, true, false, true));
    TEST_ASSERT_FALSE(vg_panel_write_bitmap(&panel, 0, 0, 2, 2, pixels));
    TEST_ASSERT_FALSE(vg_panel_set_display_enabled(&panel, true));

    TEST_ASSERT_TRUE(vg_panel_init(&panel));
    TEST_ASSERT_TRUE(vg_panel_set_orientation(&panel, true, false, true));
    TEST_ASSERT_TRUE(vg_panel_write_bitmap(&panel, 0, 0, 2, 2, pixels));
    TEST_ASSERT_TRUE(vg_panel_set_display_enabled(&panel, true));

    TEST_ASSERT_EQUAL_UINT32(1u, capture.init_calls);
    TEST_ASSERT_EQUAL_UINT32(1u, capture.orientation_calls);
    TEST_ASSERT_EQUAL_UINT32(1u, capture.write_bitmap_calls);
    TEST_ASSERT_EQUAL_UINT32(1u, capture.display_enabled_calls);
}

TEST(test_panel_api_reset_clears_initialized_state) {
    TestPanelCapture capture = {0};
    const VgPanelOps ops = {
        .reset = test_panel_reset,
        .init = test_panel_init,
        .set_orientation = test_panel_set_orientation,
    };
    VgPanel panel = {.ops = &ops, .ctx = &capture};

    TEST_ASSERT_TRUE(vg_panel_init(&panel));
    TEST_ASSERT_TRUE(vg_panel_set_orientation(&panel, false, true, false));
    TEST_ASSERT_TRUE(vg_panel_reset(&panel));
    TEST_ASSERT_FALSE(vg_panel_set_orientation(&panel, false, false, false));

    TEST_ASSERT_EQUAL_UINT32(1u, capture.reset_calls);
    TEST_ASSERT_EQUAL_UINT32(1u, capture.init_calls);
    TEST_ASSERT_EQUAL_UINT32(1u, capture.orientation_calls);
}

TEST(test_panel_api_forwards_optional_gap_and_sleep_commands) {
    TestPanelCapture capture = {0};
    const VgPanelOps ops = {
        .reset = test_panel_reset,
        .init = test_panel_init,
        .set_gap = test_panel_set_gap,
        .set_sleep = test_panel_set_sleep,
    };
    VgPanel panel = {.ops = &ops, .ctx = &capture};

    TEST_ASSERT_TRUE(vg_panel_init(&panel));
    TEST_ASSERT_TRUE(vg_panel_set_gap(&panel, 3, 5));
    TEST_ASSERT_TRUE(vg_panel_set_sleep(&panel, true));

    TEST_ASSERT_EQUAL_UINT32(1u, capture.gap_calls);
    TEST_ASSERT_EQUAL_INT(3, capture.last_x_gap);
    TEST_ASSERT_EQUAL_INT(5, capture.last_y_gap);
    TEST_ASSERT_EQUAL_UINT32(1u, capture.sleep_calls);
    TEST_ASSERT_TRUE(capture.last_sleep);
}

TEST(test_panel_api_write_bitmap_validates_bounds_and_pixels) {
    TestPanelCapture capture = {0};
    const VgPanelOps ops = {
        .reset = test_panel_reset,
        .init = test_panel_init,
        .write_bitmap = test_panel_write_bitmap,
    };
    VgPanel panel = {.ops = &ops, .ctx = &capture};
    uint16_t pixels[4] = {11u, 12u, 13u, 14u};

    TEST_ASSERT_TRUE(vg_panel_init(&panel));
    TEST_ASSERT_FALSE(vg_panel_write_bitmap(&panel, 2, 1, 2, 3, pixels));
    TEST_ASSERT_FALSE(vg_panel_write_bitmap(&panel, -1, 1, 2, 3, pixels));
    TEST_ASSERT_FALSE(vg_panel_write_bitmap(&panel, 0, 0, 2, 2, NULL));

    TEST_ASSERT_TRUE(vg_panel_write_bitmap(&panel, 4, 5, 6, 7, pixels));
    TEST_ASSERT_EQUAL_UINT32(1u, capture.write_bitmap_calls);
    TEST_ASSERT_EQUAL_INT(4, capture.last_x_start);
    TEST_ASSERT_EQUAL_INT(5, capture.last_y_start);
    TEST_ASSERT_EQUAL_INT(6, capture.last_x_end);
    TEST_ASSERT_EQUAL_INT(7, capture.last_y_end);
    TEST_ASSERT_TRUE(capture.last_pixels == pixels);
}

TEST(test_panel_api_transfer_stats_track_bitmap_writes_and_can_reset) {
    TestPanelCapture capture = {0};
    const VgPanelOps ops = {
        .reset = test_panel_reset,
        .init = test_panel_init,
        .write_bitmap = test_panel_write_bitmap,
    };
    VgPanel panel = {.ops = &ops, .ctx = &capture};
    uint16_t pixels[6] = {11u, 12u, 13u, 14u, 15u, 16u};
    VgPanelTransferStats stats = {0};

    TEST_ASSERT_TRUE(vg_panel_init(&panel));
    TEST_ASSERT_TRUE(vg_panel_write_bitmap(&panel, 4, 5, 7, 7, pixels));
    TEST_ASSERT_TRUE(vg_panel_transfer_stats_snapshot(&panel, &stats));
    TEST_ASSERT_EQUAL_UINT64(1u, stats.transfer_count);
    TEST_ASSERT_EQUAL_UINT64(6u, stats.transferred_pixels);
    TEST_ASSERT_EQUAL_UINT64(12u, stats.transferred_bytes);

    TEST_ASSERT_TRUE(vg_panel_transfer_stats_reset(&panel));
    TEST_ASSERT_TRUE(vg_panel_transfer_stats_snapshot(&panel, &stats));
    TEST_ASSERT_EQUAL_UINT64(0u, stats.transfer_count);
    TEST_ASSERT_EQUAL_UINT64(0u, stats.transferred_pixels);
    TEST_ASSERT_EQUAL_UINT64(0u, stats.transferred_bytes);
}

TEST(test_panel_api_write_bitmap_2d_uses_native_callback_when_available) {
    TestPanelCapture capture = {0};
    const VgPanelOps ops = {
        .reset = test_panel_reset,
        .init = test_panel_init,
        .write_bitmap = test_panel_write_bitmap,
        .write_bitmap_2d = test_panel_write_bitmap_2d,
    };
    VgPanel panel = {.ops = &ops, .ctx = &capture};
    uint16_t pixels[16] = {0};

    TEST_ASSERT_TRUE(vg_panel_init(&panel));
    TEST_ASSERT_TRUE(vg_panel_write_bitmap_2d(&panel, 10, 12, 13, 14, pixels, 4, 4, 1, 2, 4, 4));

    TEST_ASSERT_EQUAL_UINT32(0u, capture.write_bitmap_calls);
    TEST_ASSERT_EQUAL_UINT32(1u, capture.write_bitmap_2d_calls);
    TEST_ASSERT_EQUAL_INT(10, capture.last_x_start);
    TEST_ASSERT_EQUAL_INT(12, capture.last_y_start);
    TEST_ASSERT_EQUAL_INT(13, capture.last_x_end);
    TEST_ASSERT_EQUAL_INT(14, capture.last_y_end);
    TEST_ASSERT_EQUAL_UINT16(4u, capture.last_src_w);
    TEST_ASSERT_EQUAL_UINT16(4u, capture.last_src_h);
    TEST_ASSERT_EQUAL_INT(1, capture.last_src_x_start);
    TEST_ASSERT_EQUAL_INT(2, capture.last_src_y_start);
    TEST_ASSERT_EQUAL_INT(4, capture.last_src_x_end);
    TEST_ASSERT_EQUAL_INT(4, capture.last_src_y_end);
}

TEST(test_panel_api_write_bitmap_2d_falls_back_to_row_writes_without_temp_buffer) {
    TestPanelCapture capture = {0};
    const VgPanelOps ops = {
        .reset = test_panel_reset,
        .init = test_panel_init,
        .write_bitmap = test_panel_write_bitmap,
    };
    VgPanel panel = {.ops = &ops, .ctx = &capture};
    uint16_t pixels[12] = {
        10u, 11u, 12u, 13u,
        20u, 21u, 22u, 23u,
        30u, 31u, 32u, 33u,
    };

    TEST_ASSERT_TRUE(vg_panel_init(&panel));
    TEST_ASSERT_TRUE(vg_panel_write_bitmap_2d(&panel, 7, 8, 9, 10, pixels, 4, 3, 1, 1, 3, 3));

    TEST_ASSERT_EQUAL_UINT32(2u, capture.write_bitmap_calls);
    TEST_ASSERT_EQUAL_INT(7, capture.last_x_start);
    TEST_ASSERT_EQUAL_INT(9, capture.last_y_start);
    TEST_ASSERT_EQUAL_INT(9, capture.last_x_end);
    TEST_ASSERT_EQUAL_INT(10, capture.last_y_end);
    TEST_ASSERT_TRUE(capture.last_pixels == &pixels[9]);

    VgPanelTransferStats stats = {0};
    TEST_ASSERT_TRUE(vg_panel_transfer_stats_snapshot(&panel, &stats));
    TEST_ASSERT_EQUAL_UINT64(2u, stats.transfer_count);
    TEST_ASSERT_EQUAL_UINT64(4u, stats.transferred_pixels);
    TEST_ASSERT_EQUAL_UINT64(8u, stats.transferred_bytes);
}

TEST(test_panel_api_write_bitmap_2d_rejects_mismatched_or_out_of_bounds_rects) {
    TestPanelCapture capture = {0};
    const VgPanelOps ops = {
        .reset = test_panel_reset,
        .init = test_panel_init,
        .write_bitmap = test_panel_write_bitmap,
    };
    VgPanel panel = {.ops = &ops, .ctx = &capture};
    uint16_t pixels[16] = {0};

    TEST_ASSERT_TRUE(vg_panel_init(&panel));
    TEST_ASSERT_FALSE(vg_panel_write_bitmap_2d(&panel, 0, 0, 3, 2, pixels, 4, 4, 0, 0, 2, 2));
    TEST_ASSERT_FALSE(vg_panel_write_bitmap_2d(&panel, 0, 0, 2, 2, pixels, 4, 4, 3, 0, 5, 2));
    TEST_ASSERT_FALSE(vg_panel_write_bitmap_2d(&panel, 0, 0, 2, 2, NULL, 4, 4, 0, 0, 2, 2));
    TEST_ASSERT_EQUAL_UINT32(0u, capture.write_bitmap_calls);
}

TEST(test_panel_api_backend_adapter_maps_dirty_rect_to_panel_write_bitmap_2d) {
    uint16_t src_pixels[6 * 5];
    for (size_t i = 0; i < (sizeof(src_pixels) / sizeof(src_pixels[0])); i++) {
        src_pixels[i] = (uint16_t)(100u + i);
    }

    TestPanelCapture capture = {0};
    const VgPanelOps panel_ops = {
        .reset = test_panel_reset,
        .init = test_panel_init,
        .write_bitmap = test_panel_write_bitmap,
        .write_bitmap_2d = test_panel_write_bitmap_2d,
    };
    VgPanel panel = {.ops = &panel_ops, .ctx = &capture};
    TEST_ASSERT_TRUE(vg_panel_init(&panel));

    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, 6, 5, src_pixels, 6 * 5));

    TEST_ASSERT_TRUE(vg_panel_backend_submit_clip_rect(&panel, &fb, (VgClipRect){2, 1, 3, 2}));

    TEST_ASSERT_EQUAL_UINT32(1u, capture.write_bitmap_2d_calls);
    TEST_ASSERT_EQUAL_UINT32(0u, capture.write_bitmap_calls);
    TEST_ASSERT_EQUAL_INT(2, capture.last_x_start);
    TEST_ASSERT_EQUAL_INT(1, capture.last_y_start);
    TEST_ASSERT_EQUAL_INT(5, capture.last_x_end);
    TEST_ASSERT_EQUAL_INT(3, capture.last_y_end);
    TEST_ASSERT_EQUAL_UINT16(6u, capture.last_src_w);
    TEST_ASSERT_EQUAL_UINT16(2u, capture.last_src_h);
    TEST_ASSERT_EQUAL_INT(0, capture.last_src_x_start);
    TEST_ASSERT_EQUAL_INT(0, capture.last_src_y_start);
    TEST_ASSERT_EQUAL_INT(3, capture.last_src_x_end);
    TEST_ASSERT_EQUAL_INT(2, capture.last_src_y_end);
    TEST_ASSERT_TRUE(capture.last_pixels == &src_pixels[8]);
}

TEST(test_panel_api_backend_adapter_uses_panel_row_fallback_for_wide_stride_sources) {
    uint16_t src_pixels[5 * 4];
    for (size_t i = 0; i < (sizeof(src_pixels) / sizeof(src_pixels[0])); i++) {
        src_pixels[i] = (uint16_t)(200u + i);
    }

    TestPanelCapture capture = {0};
    const VgPanelOps panel_ops = {
        .reset = test_panel_reset,
        .init = test_panel_init,
        .write_bitmap = test_panel_write_bitmap,
    };
    VgPanel panel = {.ops = &panel_ops, .ctx = &capture};
    TEST_ASSERT_TRUE(vg_panel_init(&panel));

    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, 5, 4, src_pixels, 5 * 4));

    TEST_ASSERT_TRUE(vg_panel_backend_submit_clip_rect(&panel, &fb, (VgClipRect){1, 1, 2, 2}));

    TEST_ASSERT_EQUAL_UINT32(2u, capture.write_bitmap_calls);
    TEST_ASSERT_EQUAL_INT(1, capture.last_x_start);
    TEST_ASSERT_EQUAL_INT(2, capture.last_y_start);
    TEST_ASSERT_EQUAL_INT(3, capture.last_x_end);
    TEST_ASSERT_EQUAL_INT(3, capture.last_y_end);
    TEST_ASSERT_TRUE(capture.last_pixels == &src_pixels[11]);
}

TEST(test_panel_api_backend_adapter_uses_contiguous_write_for_full_width_rects) {
    uint16_t src_pixels[4 * 3];
    for (size_t i = 0; i < (sizeof(src_pixels) / sizeof(src_pixels[0])); i++) {
        src_pixels[i] = (uint16_t)(500u + i);
    }

    TestPanelCapture capture = {0};
    const VgPanelOps panel_ops = {
        .reset = test_panel_reset,
        .init = test_panel_init,
        .write_bitmap = test_panel_write_bitmap,
        .write_bitmap_2d = test_panel_write_bitmap_2d,
    };
    VgPanel panel = {.ops = &panel_ops, .ctx = &capture};
    TEST_ASSERT_TRUE(vg_panel_init(&panel));

    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, 4, 3, src_pixels, 4 * 3));

    TEST_ASSERT_TRUE(vg_panel_backend_submit_clip_rect(&panel, &fb, (VgClipRect){0, 1, 4, 1}));

    TEST_ASSERT_EQUAL_UINT32(1u, capture.write_bitmap_calls);
    TEST_ASSERT_EQUAL_UINT32(0u, capture.write_bitmap_2d_calls);
    TEST_ASSERT_EQUAL_INT(0, capture.last_x_start);
    TEST_ASSERT_EQUAL_INT(1, capture.last_y_start);
    TEST_ASSERT_EQUAL_INT(4, capture.last_x_end);
    TEST_ASSERT_EQUAL_INT(2, capture.last_y_end);
    TEST_ASSERT_TRUE(capture.last_pixels == &src_pixels[4]);
}

TEST(test_panel_api_esp_lcd_adapter_maps_core_panel_commands) {
    TestEspLcdCapture capture = {0};
    const VgEspLcdOps ops = {
        .reset = test_esp_lcd_reset,
        .init = test_esp_lcd_init,
        .draw_bitmap = test_esp_lcd_draw_bitmap,
        .mirror = test_esp_lcd_mirror,
        .swap_xy = test_esp_lcd_swap_xy,
        .set_gap = test_esp_lcd_set_gap,
        .disp_on_off = test_esp_lcd_disp_on_off,
        .disp_sleep = test_esp_lcd_disp_sleep,
    };
    VgEspLcdPanel esp_panel = {0};
    uint16_t pixels[4] = {1u, 2u, 3u, 4u};

    vg_esp_lcd_panel_init(&esp_panel, &ops, &capture);

    TEST_ASSERT_TRUE(vg_panel_reset(&esp_panel.panel));
    TEST_ASSERT_TRUE(vg_panel_init(&esp_panel.panel));
    TEST_ASSERT_TRUE(vg_panel_set_orientation(&esp_panel.panel, true, false, true));
    TEST_ASSERT_TRUE(vg_panel_set_gap(&esp_panel.panel, 7, 9));
    TEST_ASSERT_TRUE(vg_panel_write_bitmap(&esp_panel.panel, 3, 4, 5, 6, pixels));
    TEST_ASSERT_TRUE(vg_panel_set_display_enabled(&esp_panel.panel, true));
    TEST_ASSERT_TRUE(vg_panel_set_sleep(&esp_panel.panel, false));

    TEST_ASSERT_EQUAL_UINT32(1u, capture.reset_calls);
    TEST_ASSERT_EQUAL_UINT32(1u, capture.init_calls);
    TEST_ASSERT_EQUAL_UINT32(1u, capture.swap_xy_calls);
    TEST_ASSERT_EQUAL_UINT32(1u, capture.mirror_calls);
    TEST_ASSERT_EQUAL_UINT32(1u, capture.set_gap_calls);
    TEST_ASSERT_EQUAL_UINT32(1u, capture.draw_bitmap_calls);
    TEST_ASSERT_EQUAL_UINT32(1u, capture.disp_on_off_calls);
    TEST_ASSERT_EQUAL_UINT32(1u, capture.disp_sleep_calls);
    TEST_ASSERT_TRUE(capture.last_swap_xy);
    TEST_ASSERT_TRUE(capture.last_mirror_x);
    TEST_ASSERT_FALSE(capture.last_mirror_y);
    TEST_ASSERT_EQUAL_INT(7, capture.last_x_gap);
    TEST_ASSERT_EQUAL_INT(9, capture.last_y_gap);
    TEST_ASSERT_EQUAL_INT(3, capture.last_x_start);
    TEST_ASSERT_EQUAL_INT(4, capture.last_y_start);
    TEST_ASSERT_EQUAL_INT(5, capture.last_x_end);
    TEST_ASSERT_EQUAL_INT(6, capture.last_y_end);
    TEST_ASSERT_TRUE(capture.last_color_data == pixels);
    TEST_ASSERT_TRUE(capture.last_disp_on_off);
    TEST_ASSERT_FALSE(capture.last_disp_sleep);
}

TEST(test_panel_api_esp_lcd_adapter_propagates_callback_failures) {
    TestEspLcdCapture capture = {.fail_swap_xy = true};
    const VgEspLcdOps ops = {
        .reset = test_esp_lcd_reset,
        .init = test_esp_lcd_init,
        .draw_bitmap = test_esp_lcd_draw_bitmap,
        .mirror = test_esp_lcd_mirror,
        .swap_xy = test_esp_lcd_swap_xy,
    };
    VgEspLcdPanel esp_panel = {0};

    vg_esp_lcd_panel_init(&esp_panel, &ops, &capture);

    TEST_ASSERT_TRUE(vg_panel_init(&esp_panel.panel));
    TEST_ASSERT_FALSE(vg_panel_set_orientation(&esp_panel.panel, false, false, true));
    TEST_ASSERT_EQUAL_UINT32(1u, capture.swap_xy_calls);
    TEST_ASSERT_EQUAL_UINT32(0u, capture.mirror_calls);
}
