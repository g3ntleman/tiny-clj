#include "tests_common.h"

#include "render_driver.h"

#define TEST_RENDER_W 32
#define TEST_RENDER_H 16
#define TEST_RENDER_MAX_WRITES 128

typedef struct {
    int16_t width;
    int16_t height;
    const uint16_t *stripe_pixels;
    size_t stripe_pixel_count;
    uint32_t write_count;
    VgClipRect writes[TEST_RENDER_MAX_WRITES];
    bool out_of_bounds;
    bool pointer_outside_stripe;
} MockRenderPanel;

static bool mock_panel_noop(void *ctx) {
    (void)ctx;
    return true;
}

static bool mock_panel_set_orientation(void *ctx, bool mirror_x, bool mirror_y, bool swap_xy) {
    (void)ctx;
    (void)mirror_x;
    (void)mirror_y;
    (void)swap_xy;
    return true;
}

static bool mock_panel_set_gap(void *ctx, int16_t x_gap, int16_t y_gap) {
    (void)ctx;
    (void)x_gap;
    (void)y_gap;
    return true;
}

static bool mock_panel_set_display_enabled(void *ctx, bool enabled) {
    (void)ctx;
    (void)enabled;
    return true;
}

static bool mock_panel_set_sleep(void *ctx, bool sleep) {
    (void)ctx;
    (void)sleep;
    return true;
}

static bool mock_panel_write_bitmap(void *ctx,
                                    int16_t x_start,
                                    int16_t y_start,
                                    int16_t x_end,
                                    int16_t y_end,
                                    const uint16_t *rgb565_pixels) {
    MockRenderPanel *panel = (MockRenderPanel *)ctx;
    if (!panel || !rgb565_pixels) {
        return false;
    }
    if (x_start < 0 || y_start < 0 || x_end <= x_start || y_end <= y_start ||
        x_end > panel->width || y_end > panel->height) {
        panel->out_of_bounds = true;
        return false;
    }
    if (panel->write_count < TEST_RENDER_MAX_WRITES) {
        panel->writes[panel->write_count++] = (VgClipRect){
            .x = x_start,
            .y = y_start,
            .w = (int16_t)(x_end - x_start),
            .h = (int16_t)(y_end - y_start),
        };
    }
    return true;
}

static bool mock_panel_write_bitmap_2d(void *ctx,
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
    MockRenderPanel *panel = (MockRenderPanel *)ctx;
    if (!panel || !src_pixels) {
        return false;
    }

    int16_t dst_w = (int16_t)(x_end - x_start);
    int16_t dst_h = (int16_t)(y_end - y_start);
    int16_t src_rect_w = (int16_t)(src_x_end - src_x_start);
    int16_t src_rect_h = (int16_t)(src_y_end - src_y_start);
    if (dst_w != src_rect_w || dst_h != src_rect_h) {
        return false;
    }
    if (x_start < 0 || y_start < 0 || x_end <= x_start || y_end <= y_start ||
        x_end > panel->width || y_end > panel->height) {
        panel->out_of_bounds = true;
        return false;
    }
    if (src_x_start < 0 || src_y_start < 0 || src_x_end > (int16_t)src_w || src_y_end > (int16_t)src_h) {
        panel->out_of_bounds = true;
        return false;
    }

    if (panel->stripe_pixels && panel->stripe_pixel_count > 0u) {
        const uint16_t *start = panel->stripe_pixels;
        const uint16_t *end = panel->stripe_pixels + panel->stripe_pixel_count;
        if (src_pixels < start || src_pixels >= end) {
            panel->pointer_outside_stripe = true;
            return false;
        }
        size_t start_offset = (size_t)(src_pixels - start);
        size_t last_index = start_offset +
                            (size_t)(src_h - 1u) * (size_t)src_w +
                            (size_t)(src_x_end - 1);
        if (last_index >= panel->stripe_pixel_count) {
            panel->pointer_outside_stripe = true;
            return false;
        }
    }

    if (panel->write_count < TEST_RENDER_MAX_WRITES) {
        panel->writes[panel->write_count++] = (VgClipRect){
            .x = x_start,
            .y = y_start,
            .w = dst_w,
            .h = dst_h,
        };
    }
    return true;
}

static const VgPanelOps g_mock_panel_ops = {
    .reset = mock_panel_noop,
    .init = mock_panel_noop,
    .set_orientation = mock_panel_set_orientation,
    .set_gap = mock_panel_set_gap,
    .write_bitmap = mock_panel_write_bitmap,
    .write_bitmap_2d = mock_panel_write_bitmap_2d,
    .set_display_enabled = mock_panel_set_display_enabled,
    .set_sleep = mock_panel_set_sleep,
};

static VgNode make_test_scene_root(VgNode *out_rect, VgNode **out_child) {
    *out_rect = (VgNode){
        .id = 1u,
        .type = VG_NODE_RECT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = {
            .stroke_color = 0xFFFFu,
            .stroke_width = 1u,
            .visible = true,
            .has_fill = true,
            .fill_color = 0x07E0u,
            .has_bg_color = false,
            .bg_color = 0u,
        },
        .data.rect = {
            .x = 0,
            .y = 0,
            .w = TEST_RENDER_W,
            .h = TEST_RENDER_H,
        },
    };
    *out_child = out_rect;

    return (VgNode){
        .id = 0u,
        .type = VG_NODE_GROUP,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = vg_style_default(),
        .data.group = {
            .children = out_child,
            .child_count = 1u,
        },
    };
}

static void setup_driver(VgRenderDriver *out_driver,
                         VgPanel *out_panel,
                         MockRenderPanel *out_mock,
                         uint16_t *stripe_pixels,
                         int16_t stripe_rows) {
    memset(out_mock, 0, sizeof(*out_mock));
    out_mock->width = TEST_RENDER_W;
    out_mock->height = TEST_RENDER_H;
    out_mock->stripe_pixels = stripe_pixels;
    out_mock->stripe_pixel_count = (size_t)TEST_RENDER_W * (size_t)stripe_rows;

    memset(out_panel, 0, sizeof(*out_panel));
    out_panel->ops = &g_mock_panel_ops;
    out_panel->ctx = out_mock;
    out_panel->initialized = true;

    memset(out_driver, 0, sizeof(*out_driver));
    out_driver->panel = out_panel;
    out_driver->stripe_pixels = stripe_pixels;
    out_driver->display_width = TEST_RENDER_W;
    out_driver->display_height = TEST_RENDER_H;
    out_driver->stripe_rows = stripe_rows;
    out_driver->thread_name = "test-render";
}

TEST(test_render_driver_stripe_push_single_stripe) {
    uint16_t stripe_pixels[TEST_RENDER_W * 4] = {0};
    VgRenderDriver driver;
    VgPanel panel;
    MockRenderPanel mock;
    setup_driver(&driver, &panel, &mock, stripe_pixels, 4);

    VgNode rect;
    VgNode *child = NULL;
    VgNode root = make_test_scene_root(&rect, &child);

    TEST_ASSERT_TRUE(vg_render_driver_stripe_push(&driver,
                                                  &root,
                                                  (VgClipRect){.x = 0, .y = 4, .w = TEST_RENDER_W, .h = 4},
                                                  0u,
                                                  0x0000u));
    TEST_ASSERT_EQUAL_UINT32(1u, mock.write_count);
    TEST_ASSERT_EQUAL_INT(4, mock.writes[0].y);
    TEST_ASSERT_EQUAL_INT(4, mock.writes[0].h);
    TEST_ASSERT_FALSE(mock.out_of_bounds);
}

TEST(test_render_driver_stripe_push_full_display) {
    uint16_t stripe_pixels[TEST_RENDER_W * 4] = {0};
    VgRenderDriver driver;
    VgPanel panel;
    MockRenderPanel mock;
    setup_driver(&driver, &panel, &mock, stripe_pixels, 4);

    VgNode rect;
    VgNode *child = NULL;
    VgNode root = make_test_scene_root(&rect, &child);

    TEST_ASSERT_TRUE(vg_render_driver_stripe_push(&driver,
                                                  &root,
                                                  (VgClipRect){.x = 0, .y = 0, .w = TEST_RENDER_W, .h = TEST_RENDER_H},
                                                  0u,
                                                  0x0000u));
    TEST_ASSERT_EQUAL_UINT32(4u, mock.write_count);
    TEST_ASSERT_FALSE(mock.out_of_bounds);
}

TEST(test_render_driver_stripe_push_partial_rect) {
    uint16_t stripe_pixels[TEST_RENDER_W * 4] = {0};
    VgRenderDriver driver;
    VgPanel panel;
    MockRenderPanel mock;
    setup_driver(&driver, &panel, &mock, stripe_pixels, 4);

    VgNode rect;
    VgNode *child = NULL;
    VgNode root = make_test_scene_root(&rect, &child);

    TEST_ASSERT_TRUE(vg_render_driver_stripe_push(&driver,
                                                  &root,
                                                  (VgClipRect){.x = 3, .y = 2, .w = 7, .h = 5},
                                                  0u,
                                                  0x0000u));
    TEST_ASSERT_EQUAL_UINT32(2u, mock.write_count);
    for (uint32_t i = 0u; i < mock.write_count; i++) {
        TEST_ASSERT_EQUAL_INT(3, mock.writes[i].x);
        TEST_ASSERT_EQUAL_INT(7, mock.writes[i].w);
    }
    TEST_ASSERT_FALSE(mock.out_of_bounds);
}

TEST(test_render_driver_stripe_push_unaligned) {
    uint16_t stripe_pixels[TEST_RENDER_W * 3] = {0};
    VgRenderDriver driver;
    VgPanel panel;
    MockRenderPanel mock;
    setup_driver(&driver, &panel, &mock, stripe_pixels, 3);

    VgNode rect;
    VgNode *child = NULL;
    VgNode root = make_test_scene_root(&rect, &child);

    TEST_ASSERT_TRUE(vg_render_driver_stripe_push(&driver,
                                                  &root,
                                                  (VgClipRect){.x = 1, .y = 1, .w = 5, .h = 7},
                                                  0u,
                                                  0x0000u));
    TEST_ASSERT_EQUAL_UINT32(3u, mock.write_count);
    TEST_ASSERT_FALSE(mock.out_of_bounds);
}

TEST(test_render_driver_stripe_push_full_height_no_striping) {
    uint16_t stripe_pixels[TEST_RENDER_W * TEST_RENDER_H] = {0};
    VgRenderDriver driver;
    VgPanel panel;
    MockRenderPanel mock;
    setup_driver(&driver, &panel, &mock, stripe_pixels, TEST_RENDER_H);

    VgNode rect;
    VgNode *child = NULL;
    VgNode root = make_test_scene_root(&rect, &child);

    TEST_ASSERT_TRUE(vg_render_driver_stripe_push(&driver,
                                                  &root,
                                                  (VgClipRect){.x = 0, .y = 0, .w = TEST_RENDER_W, .h = TEST_RENDER_H},
                                                  0u,
                                                  0x0000u));
    TEST_ASSERT_EQUAL_UINT32(1u, mock.write_count);
    TEST_ASSERT_FALSE(mock.out_of_bounds);
}

TEST(test_render_driver_stripe_push_pixels_within_buffer) {
    uint16_t stripe_pixels[TEST_RENDER_W * 4] = {0};
    VgRenderDriver driver;
    VgPanel panel;
    MockRenderPanel mock;
    setup_driver(&driver, &panel, &mock, stripe_pixels, 4);

    VgNode rect;
    VgNode *child = NULL;
    VgNode root = make_test_scene_root(&rect, &child);

    TEST_ASSERT_TRUE(vg_render_driver_stripe_push(&driver,
                                                  &root,
                                                  (VgClipRect){.x = 2, .y = 1, .w = 17, .h = 8},
                                                  0u,
                                                  0x0000u));
    TEST_ASSERT_FALSE(mock.pointer_outside_stripe);
    TEST_ASSERT_FALSE(mock.out_of_bounds);
}

TEST(test_render_driver_step_renders_changed_slot) {
    uint16_t stripe_pixels[TEST_RENDER_W * 4] = {0};
    VgRenderDriver driver;
    VgPanel panel;
    MockRenderPanel mock;
    setup_driver(&driver, &panel, &mock, stripe_pixels, 4);
    driver.slot_count = 1u;

    VgNode rect;
    VgNode *child = NULL;
    VgNode root = make_test_scene_root(&rect, &child);

    VgSlotChangeTracker tracker;
    TEST_ASSERT_TRUE(vg_slot_change_tracker_init(&tracker, 1u));

    ID scene_snapshots[1] = {(ID)&root};
    (void)vg_render_driver_bind_runtime(&driver, &tracker, scene_snapshots, 0x0000u);
    TEST_ASSERT_TRUE(vg_slot_change_tracker_publish(&tracker, 0u, NULL));

    int rendered = vg_render_driver_step(&driver, NULL, NULL);
    TEST_ASSERT_TRUE(rendered >= 1);
    TEST_ASSERT_TRUE(mock.write_count >= 1u);

    TEST_ASSERT_TRUE(vg_render_driver_stop(&driver));
    vg_slot_change_tracker_destroy(&tracker);
}

TEST(test_render_driver_start_creates_thread) {
    uint16_t stripe_pixels[TEST_RENDER_W * 4] = {0};
    VgRenderDriver driver;
    VgPanel panel;
    MockRenderPanel mock;
    setup_driver(&driver, &panel, &mock, stripe_pixels, 4);

    TEST_ASSERT_TRUE(vg_render_driver_start(&driver));
    TEST_ASSERT_NOT_NULL(driver.thread);
    TEST_ASSERT_TRUE(vg_render_driver_stop(&driver));
    TEST_ASSERT_NULL(driver.thread);
}

TEST(test_render_driver_stop_idempotent) {
    uint16_t stripe_pixels[TEST_RENDER_W * 4] = {0};
    VgRenderDriver driver;
    VgPanel panel;
    MockRenderPanel mock;
    setup_driver(&driver, &panel, &mock, stripe_pixels, 4);

    TEST_ASSERT_TRUE(vg_render_driver_stop(&driver));
    TEST_ASSERT_NULL(driver.thread);
}

TEST(test_render_driver_start_stop_cycle) {
    uint16_t stripe_pixels[TEST_RENDER_W * 4] = {0};
    VgRenderDriver driver;
    VgPanel panel;
    MockRenderPanel mock;
    setup_driver(&driver, &panel, &mock, stripe_pixels, 4);

    TEST_ASSERT_TRUE(vg_render_driver_start(&driver));
    TEST_ASSERT_TRUE(vg_render_driver_stop(&driver));
    TEST_ASSERT_TRUE(vg_render_driver_start(&driver));
    TEST_ASSERT_TRUE(vg_render_driver_stop(&driver));
}
