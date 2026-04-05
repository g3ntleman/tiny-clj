#include "tinyclj_idf_renderer.h"

#if defined(TINY_FX_ENABLED) && TINY_FX_ENABLED

#include <string.h>

#include "mini_format.h"
#include "platform.h"
#include "render_driver.h"
#include "renderer_lifecycle.h"
#include "tinyclj_idf_display.h"
#include "vector_handheld_config.h"

#ifndef TINYCLJ_RENDER_STRIPE_ROWS
#define TINYCLJ_RENDER_STRIPE_ROWS 30
#endif

#ifndef TINYCLJ_RENDER_THREAD_STACK_BYTES
#define TINYCLJ_RENDER_THREAD_STACK_BYTES 4096u
#endif

#ifndef TINYCLJ_RENDER_THREAD_PRIORITY
#define TINYCLJ_RENDER_THREAD_PRIORITY 5
#endif

#ifndef TINYCLJ_RENDER_REPORT_STACK_WATERMARK
#define TINYCLJ_RENDER_REPORT_STACK_WATERMARK 1
#endif

static VgRenderDriver g_render_driver = {0};
static uint16_t g_render_stripe_pixels[VG_DISP_WIDTH * TINYCLJ_RENDER_STRIPE_ROWS]
    __attribute__((aligned(4)));

static bool tinyclj_idf_renderer_start_cb(ID slot_atoms, void *user_data) {
    (void)slot_atoms;
    VgRenderDriver *driver = (VgRenderDriver *)user_data;
    if (!driver) {
        return false;
    }
    return vg_render_driver_start(driver);
}

static bool tinyclj_idf_renderer_stop_cb(void *user_data) {
    VgRenderDriver *driver = (VgRenderDriver *)user_data;
    if (!driver) {
        return false;
    }
    bool stopped = vg_render_driver_stop(driver);
#if TINYCLJ_RENDER_REPORT_STACK_WATERMARK
    if (stopped && driver->stack_high_water_mark_bytes > 0u) {
        char line[128];
        (void)mini_snprintf(line,
                            sizeof(line),
                            "[esp-render] stack high-water=%u bytes\n",
                            (unsigned int)driver->stack_high_water_mark_bytes);
        platform_put_string(NULL, line);
    }
#endif
    return stopped;
}

bool tinyclj_idf_renderer_show_boot_screen(void) {
    if (!g_render_driver.panel || !g_render_driver.stripe_pixels) {
        return false;
    }

    VgNode title = {
        .id = 2u,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = {
            .stroke_color = 0xFFFFu,
            .stroke_width = 1u,
            .visible = true,
            .has_fill = false,
            .fill_color = 0u,
            .has_bg_color = false,
            .bg_color = 0u,
        },
        .data.text = {
            .x = 72,
            .y = 108,
            .scale = 2 * VG_SCALE_ONE,
            .rot_deg = 0,
            .text = "TINY-CLJ",
        },
    };

    VgNode frame = {
        .id = 1u,
        .type = VG_NODE_RECT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = {
            .stroke_color = 0x7BEFu,
            .stroke_width = 2u,
            .visible = true,
            .has_fill = false,
            .fill_color = 0u,
            .has_bg_color = false,
            .bg_color = 0u,
        },
        .data.rect = {
            .x = 20,
            .y = 20,
            .w = VG_DISP_WIDTH - 40,
            .h = VG_DISP_HEIGHT - 40,
        },
    };

    VgNode *children[] = {&frame, &title};
    VgNode root = {
        .id = 0u,
        .type = VG_NODE_GROUP,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = vg_style_default(),
        .data.group = {
            .children = children,
            .child_count = sizeof(children) / sizeof(children[0]),
        },
    };

    VgClipRect full = {
        .x = 0,
        .y = 0,
        .w = VG_DISP_WIDTH,
        .h = VG_DISP_HEIGHT,
    };
    return vg_render_driver_stripe_push(&g_render_driver,
                                        &root,
                                        full,
                                        0u,
                                        0x000Fu);
}

bool tinyclj_idf_renderer_init(TinycljIdfDisplay *display) {
    if (!display || !display->initialized) {
        return false;
    }

    memset(&g_render_driver, 0, sizeof(g_render_driver));
    g_render_driver.panel = &display->panel.panel;
    g_render_driver.stripe_pixels = g_render_stripe_pixels;
    g_render_driver.display_width = VG_DISP_WIDTH;
    g_render_driver.display_height = VG_DISP_HEIGHT;
    g_render_driver.stripe_rows = TINYCLJ_RENDER_STRIPE_ROWS;
    g_render_driver.thread_name = "render";
    g_render_driver.thread_stack_bytes = TINYCLJ_RENDER_THREAD_STACK_BYTES;
    g_render_driver.thread_priority = TINYCLJ_RENDER_THREAD_PRIORITY;

    (void)vg_render_driver_bind_runtime(&g_render_driver, NULL, NULL, 0x000Fu);
    tiny_renderer_lifecycle_set_callbacks(tinyclj_idf_renderer_start_cb,
                                          tinyclj_idf_renderer_stop_cb,
                                          &g_render_driver);
    return true;
}

#else

bool tinyclj_idf_renderer_show_boot_screen(void) {
    return false;
}

bool tinyclj_idf_renderer_init(void *display) {
    (void)display;
    return false;
}

#endif
