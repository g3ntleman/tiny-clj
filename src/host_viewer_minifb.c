#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(TINYCLJ_WITH_MINIFB)
#include "vector_scene_graph.h"
#include <math.h>
#include "MiniFB.h"
#if defined(__APPLE__)
#include "host_viewer_macos_menu.h"
#endif

#define VIEW_W 320
#define VIEW_H 240
#define VIEW_DEFAULT_WINDOW_SCALE 2u

/** Letterbox viewport in window coordinates (avoids MiniFB's scale division which breaks on Retina). */
static void set_letterbox_viewport(struct mfb_window *window, unsigned win_w, unsigned win_h) {
    if (win_w == 0 || win_h == 0) return;
    float scale = (float)win_w / (float)VIEW_W;
    float scale_y = (float)win_h / (float)VIEW_H;
    if (scale_y < scale) scale = scale_y;
    unsigned draw_w = (unsigned)((float)VIEW_W * scale + 0.5f);
    unsigned draw_h = (unsigned)((float)VIEW_H * scale + 0.5f);
    unsigned offset_x = (win_w - draw_w) >> 1;
    unsigned offset_y = (win_h - draw_h) >> 1;
    (void)mfb_set_viewport(window, offset_x, offset_y, draw_w, draw_h);
}

static void on_window_resize(struct mfb_window *window, int width, int height) {
    set_letterbox_viewport(window, (unsigned)width, (unsigned)height);
}

static uint32_t rgb565_to_xrgb8888(uint16_t c) {
    uint32_t r = (uint32_t)((((c >> 11) & 0x1f) * 255) / 31);
    uint32_t g = (uint32_t)((((c >> 5) & 0x3f) * 255) / 63);
    uint32_t b = (uint32_t)(((c & 0x1f) * 255) / 31);
    return 0xff000000u | (r << 16) | (g << 8) | b;
}
#endif

int main(void) {
#if !defined(TINYCLJ_WITH_MINIFB)
    fprintf(stderr, "MiniFB support is disabled for this build.\n");
    return 1;
#else
    uint16_t fb_pixels[VIEW_W * VIEW_H];
    uint32_t window_pixels[VIEW_W * VIEW_H];

    VgFrameBuffer fb;
    if (!vg_framebuffer_init(&fb, VIEW_W, VIEW_H, fb_pixels, VIEW_W * VIEW_H)) {
        fprintf(stderr, "Failed to initialize framebuffer\n");
        return 1;
    }

#if defined(__APPLE__)
    tinyclj_host_viewer_install_macos_menu();
#endif
    const unsigned default_win_w = VIEW_W * VIEW_DEFAULT_WINDOW_SCALE;
    const unsigned default_win_h = VIEW_H * VIEW_DEFAULT_WINDOW_SCALE;
    struct mfb_window *window = mfb_open_ex("tiny-clj vector host viewer (MiniFB)", default_win_w, default_win_h, WF_RESIZABLE);
    if (!window) {
        fprintf(stderr, "Failed to open MiniFB window\n");
        return 1;
    }
    mfb_show_cursor(window, true);
    mfb_set_resize_callback(window, on_window_resize);
    set_letterbox_viewport(window, default_win_w, default_win_h);
    struct mfb_timer *timer = mfb_timer_create();
    if (!timer) {
        fprintf(stderr, "Failed to create MiniFB timer\n");
        mfb_close(window);
        return 1;
    }
    double fps_window_start_s = mfb_timer_now(timer);
    unsigned fps_frame_count = 0;
    char fps_label[32];
    (void)snprintf(fps_label, sizeof(fps_label), "FPS: --.-");

    VgStyle w1 = vg_style_default();
    w1.stroke_rgb565 = 0xffffu;
    w1.stroke_width = 1;
    w1.has_bg_rgb565 = true;
    w1.bg_rgb565 = 0x0000u;
    VgStyle w1_noaa = vg_style_default();
    w1_noaa.stroke_rgb565 = 0xffffu;
    w1_noaa.stroke_width = 1;
    w1_noaa.has_bg_rgb565 = false;
    w1_noaa.bg_rgb565 = 0x0000u;
    VgStyle w2 = vg_style_default();
    w2.stroke_rgb565 = 0x07ffu;
    w2.stroke_width = 2;
    VgStyle w4 = vg_style_default();
    w4.stroke_rgb565 = 0x07e0u;
    w4.stroke_width = 4;
    VgStyle w6 = vg_style_default();
    w6.stroke_rgb565 = 0xf81fu;
    w6.stroke_width = 6;
    VgStyle w9 = vg_style_default();
    w9.stroke_rgb565 = 0xffe0u;
    w9.stroke_width = 9;
    VgStyle text_white = vg_style_default();
    text_white.stroke_rgb565 = 0xffffu;
    text_white.stroke_width = 1;
    text_white.has_bg_rgb565 = false;
    text_white.bg_rgb565 = 0x0000u;
    VgStyle text_white_aa = vg_style_default();
    text_white_aa.stroke_rgb565 = 0xffffu;
    text_white_aa.stroke_width = 1;
    text_white_aa.has_bg_rgb565 = true;
    text_white_aa.bg_rgb565 = 0x0000u;
    VgStyle text_cyan = vg_style_default();
    text_cyan.stroke_rgb565 = 0x07ffu;
    text_cyan.stroke_width = 1;
    text_cyan.has_bg_rgb565 = false;
    text_cyan.bg_rgb565 = 0x0000u;
    VgStyle text_cyan_aa = vg_style_default();
    text_cyan_aa.stroke_rgb565 = 0x07ffu;
    text_cyan_aa.stroke_width = 1;
    text_cyan_aa.has_bg_rgb565 = true;
    text_cyan_aa.bg_rgb565 = 0x0000u;

    VgPoint poly_pts[] = {
        {0, 0}, {24, -18}, {54, 6}, {90, -22}, {126, 8}, {156, -8}
    };

    VgNode line_w1 = {
        .id = 101,
        .type = VG_NODE_LINE,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = w1_noaa,
        .data.line = {.x1 = 16, .y1 = 24, .x2 = 304, .y2 = 24}
    };
    VgNode line_w2 = {
        .id = 102,
        .type = VG_NODE_LINE,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = w2,
        .data.line = {.x1 = 16, .y1 = 44, .x2 = 304, .y2 = 44}
    };
    VgNode line_w1_aa = {
        .id = 113,
        .type = VG_NODE_LINE,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = w1,
        .data.line = {.x1 = 16, .y1 = 56, .x2 = 304, .y2 = 56}
    };
    VgNode line_w4 = {
        .id = 103,
        .type = VG_NODE_LINE,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = w4,
        .data.line = {.x1 = 16, .y1 = 70, .x2 = 304, .y2 = 70}
    };
    VgNode line_diag_w6 = {
        .id = 104,
        .type = VG_NODE_LINE,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = w6,
        .data.line = {.x1 = 24, .y1 = 118, .x2 = 136, .y2 = 200}
    };
    VgNode line_diag_w9 = {
        .id = 105,
        .type = VG_NODE_LINE,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = w9,
        .data.line = {.x1 = 184, .y1 = 198, .x2 = 300, .y2 = 110}
    };
    VgNode poly = {
        .id = 106,
        .type = VG_NODE_POLYLINE,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = w4,
        .data.polyline = {.points = poly_pts, .point_count = sizeof(poly_pts) / sizeof(poly_pts[0]), .closed = false}
    };
    VgNode text = {
        .id = 107,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = text_white_aa,
        .data.text = {.x = 0, .y = 0, .scale = 1.0f, .rot_deg = 0.0f, .text = "THE QUICK, BROWN FOX!"}
    };
    VgNode text_large = {
        .id = 108,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = text_cyan_aa,
        .data.text = {.x = 0, .y = 0, .scale = 1.0f, .rot_deg = 0.0f, .text = "JUMPS OVER THE LAZY DOG?"}
    };
    VgNode text_small = {
        .id = 109,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = text_white_aa,
        .data.text = {.x = 0, .y = 0, .scale = 1.0f, .rot_deg = 0.0f, .text = "MONOSPACE_UPPERCASE"}
    };
    VgNode text_digits = {
        .id = 114,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = text_white_aa,
        .data.text = {.x = 0, .y = 0, .scale = 1.0f, .rot_deg = 0.0f, .text = "0123/45\\67-89"}
    };
    VgNode text_rot = {
        .id = 111,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = text_cyan_aa,
        .data.text = {.x = 0, .y = 0, .scale = 1.0f, .rot_deg = 0.0f, .text = "MOVE?"}
    };
    VgNode fps_text = {
        .id = 112,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = text_white_aa,
        .data.text = {.x = 0, .y = 0, .scale = 1.0f, .rot_deg = 0.0f, .text = fps_label}
    };
    VgNode text_size_small = {
        .id = 115,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = text_white_aa,
        .data.text = {.x = 0, .y = 0, .scale = 0.5f, .rot_deg = 0.0f, .text = "SIZE; 0.5"}
    };
    VgNode text_size_base = {
        .id = 116,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = text_white_aa,
        .data.text = {.x = 0, .y = 0, .scale = 1.0f, .rot_deg = 0.0f, .text = "SIZE 1.0"}
    };
    VgNode text_size_large = {
        .id = 117,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = text_white_aa,
        .data.text = {.x = 0, .y = 0, .scale = 1.5f, .rot_deg = 0.0f, .text = "SIZE 1.5"}
    };
    VgNode text_zoom = {
        .id = 118,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = text_cyan_aa,
        .data.text = {.x = 0, .y = 0, .scale = 1.0f, .rot_deg = 0.0f, .text = "ZOOM_(%)"}
    };

    VgNode *static_children[] = {
        &line_w1, &line_w2, &line_w1_aa, &line_w4
    };
    VgNode *moving_children[] = {
        &line_diag_w6, &line_diag_w9, &poly
    };
    VgNode *ui_children[] = {
        &text, &text_large, &text_small, &text_digits, &text_rot, &fps_text,
        &text_size_small, &text_size_base, &text_size_large, &text_zoom
    };

    VgNode static_group = {
        .id = 110,
        .type = VG_NODE_GROUP,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = vg_style_default(),
        .data.group = {.children = static_children, .child_count = sizeof(static_children) / sizeof(static_children[0])}
    };
    VgNode moving_group = {
        .id = 120,
        .type = VG_NODE_GROUP,
        .has_transform = true,
        .transform = vg_transform_identity(),
        .style = vg_style_default(),
        .data.group = {.children = moving_children, .child_count = sizeof(moving_children) / sizeof(moving_children[0])}
    };
    VgNode ui_group = {
        .id = 130,
        .type = VG_NODE_GROUP,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = vg_style_default(),
        .data.group = {.children = ui_children, .child_count = sizeof(ui_children) / sizeof(ui_children[0])}
    };
    VgNode *root_children[] = {
        &static_group, &moving_group, &ui_group
    };

    VgNode root = {
        .id = 100,
        .type = VG_NODE_GROUP,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = vg_style_default(),
        .data.group = {.children = root_children, .child_count = sizeof(root_children) / sizeof(root_children[0])}
    };

    VgTransform poly_local = vg_transform_identity();
    poly_local.tx = 76.0f;
    poly_local.ty = 96.0f;
    poly_local.rot_deg = 6.0f;
    poly.has_transform = true;
    poly.transform = poly_local;

    VgTransform text_local = vg_transform_identity();
    text_local.tx = 12.0f;
    text_local.ty = 172.0f;
    text_local.rot_deg = 0.0f;
    text.has_transform = true;
    text.transform = text_local;
    VgTransform text_large_local = vg_transform_identity();
    text_large_local.tx = 12.0f;
    text_large_local.ty = 188.0f;
    text_large_local.rot_deg = 0.0f;
    text_large.has_transform = true;
    text_large.transform = text_large_local;
    VgTransform text_small_local = vg_transform_identity();
    text_small_local.tx = 12.0f;
    text_small_local.ty = 206.0f;
    text_small_local.rot_deg = 0.0f;
    text_small.has_transform = true;
    text_small.transform = text_small_local;
    VgTransform text_digits_local = vg_transform_identity();
    text_digits_local.tx = 12.0f;
    text_digits_local.ty = 222.0f;
    text_digits_local.rot_deg = 0.0f;
    text_digits.has_transform = true;
    text_digits.transform = text_digits_local;
    VgTransform text_rot_local = vg_transform_identity();
    text_rot_local.tx = 236.0f;
    text_rot_local.ty = 184.0f;
    text_rot_local.rot_deg = 0.0f;
    text_rot.has_transform = true;
    text_rot.transform = text_rot_local;
    VgTransform fps_text_local = vg_transform_identity();
    fps_text_local.tx = 160.0f;
    fps_text_local.ty = 222.0f;
    fps_text_local.rot_deg = 0.0f;
    fps_text.has_transform = true;
    fps_text.transform = fps_text_local;
    VgTransform text_size_small_local = vg_transform_identity();
    text_size_small_local.tx = 180.0f;
    text_size_small_local.ty = 110.0f;
    text_size_small_local.rot_deg = 0.0f;
    text_size_small.has_transform = true;
    text_size_small.transform = text_size_small_local;
    VgTransform text_size_base_local = vg_transform_identity();
    text_size_base_local.tx = 180.0f;
    text_size_base_local.ty = 126.0f;
    text_size_base_local.rot_deg = 0.0f;
    text_size_base.has_transform = true;
    text_size_base.transform = text_size_base_local;
    VgTransform text_size_large_local = vg_transform_identity();
    text_size_large_local.tx = 180.0f;
    text_size_large_local.ty = 146.0f;
    text_size_large_local.rot_deg = 0.0f;
    text_size_large.has_transform = true;
    text_size_large.transform = text_size_large_local;
    VgTransform text_zoom_local = vg_transform_identity();
    text_zoom_local.tx = 180.0f;
    text_zoom_local.ty = 192.0f;
    text_zoom_local.rot_deg = 0.0f;
    text_zoom.has_transform = true;
    text_zoom.transform = text_zoom_local;

    while (true) {
        float time_s = (float)mfb_timer_now(timer);
        fps_frame_count++;
        double fps_elapsed_s = (double)time_s - fps_window_start_s;
#if defined(__APPLE__)
        if (fps_elapsed_s >= 1.0) {
            double fps = (double)fps_frame_count / fps_elapsed_s;
            (void)snprintf(fps_label, sizeof(fps_label), "FPS: %.1f", fps);
            char title[96];
            (void)snprintf(title, sizeof(title), "tiny-clj vector host viewer (MiniFB) - %.1f FPS", fps);
            tinyclj_host_viewer_set_macos_window_title(title);
            VgPatch fps_text_patch = {.id = 112, .type = VG_PATCH_TEXT};
            fps_text_patch.value.text = fps_label;
            (void)vg_scene_apply_patch(&root, &fps_text_patch);
            fps_window_start_s = (double)time_s;
            fps_frame_count = 0;
        }
#endif
        if (!mfb_wait_sync(window)) {
            break;
        }
        const uint8_t *keys = mfb_get_key_buffer(window);
        if (keys) {
            bool esc = keys[KB_KEY_ESCAPE] != 0;
            bool cmd_q = (keys[KB_KEY_Q] != 0) &&
                         ((keys[KB_KEY_LEFT_SUPER] != 0) || (keys[KB_KEY_RIGHT_SUPER] != 0));
            if (esc || cmd_q) {
                break;
            }
        }

        VgPatch moving_patch = {.id = 120, .type = VG_PATCH_TRANSFORM};
        moving_patch.value.transform = vg_transform_identity();
        moving_patch.value.transform.tx = sinf(time_s * 0.7f) * 20.0f;
        moving_patch.value.transform.ty = 0.0f;
        (void)vg_scene_apply_patch(&root, &moving_patch);
        VgPatch text_rot_patch = {.id = 111, .type = VG_PATCH_TRANSFORM};
        text_rot_patch.value.transform = text_rot_local;
        text_rot_patch.value.transform.tx = text_rot_local.tx + sinf(time_s * 1.1f) * 10.0f;
        text_rot_patch.value.transform.rot_deg = sinf(time_s * 1.6f) * 10.0f;
        (void)vg_scene_apply_patch(&root, &text_rot_patch);
        VgPatch text_zoom_patch = {.id = 118, .type = VG_PATCH_TRANSFORM};
        text_zoom_patch.value.transform = text_zoom_local;
        text_zoom_patch.value.transform.sx = 0.8f + (sinf(time_s * 1.8f) + 1.0f) * 0.5f * 0.8f;
        text_zoom_patch.value.transform.sy = text_zoom_patch.value.transform.sx;
        (void)vg_scene_apply_patch(&root, &text_zoom_patch);

        vg_framebuffer_clear(&fb, 0x0000u);
        vg_render_scene(&root, &fb);

        for (size_t i = 0; i < (size_t)VIEW_W * (size_t)VIEW_H; i++) {
            window_pixels[i] = rgb565_to_xrgb8888(fb_pixels[i]);
        }

        mfb_update_state state = mfb_update_ex(window, window_pixels, VIEW_W, VIEW_H);
        if (state != STATE_OK) {
            break;
        }
    }

    mfb_timer_destroy(timer);
    mfb_close(window);
    return 0;
#endif
}
