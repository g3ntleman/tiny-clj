#include "tests_common.h"
#include "../vector_scene_graph.h"
#include "../scene.h"

#define TEST_W 64
#define TEST_H 48

static size_t count_color(const uint16_t *pixels, size_t n, uint16_t color) {
    size_t c = 0;
    for (size_t i = 0; i < n; i++) {
        if (pixels[i] == color) {
            c++;
        }
    }
    return c;
}

static size_t count_not_color(const uint16_t *pixels, size_t n, uint16_t color) {
    size_t c = 0;
    for (size_t i = 0; i < n; i++) {
        if (pixels[i] != color) {
            c++;
        }
    }
    return c;
}

static bool find_non_bg_bounds(const uint16_t *pixels, size_t w, size_t h, uint16_t bg,
                               size_t *out_min_x, size_t *out_min_y,
                               size_t *out_max_x, size_t *out_max_y) {
    bool found = false;
    size_t min_x = w;
    size_t min_y = h;
    size_t max_x = 0;
    size_t max_y = 0;
    for (size_t y = 0; y < h; y++) {
        for (size_t x = 0; x < w; x++) {
            if (pixels[y * w + x] == bg) {
                continue;
            }
            if (!found) {
                found = true;
                min_x = max_x = x;
                min_y = max_y = y;
            } else {
                if (x < min_x) min_x = x;
                if (x > max_x) max_x = x;
                if (y < min_y) min_y = y;
                if (y > max_y) max_y = y;
            }
        }
    }
    if (!found) {
        return false;
    }
    if (out_min_x) *out_min_x = min_x;
    if (out_min_y) *out_min_y = min_y;
    if (out_max_x) *out_max_x = max_x;
    if (out_max_y) *out_max_y = max_y;
    return true;
}

TEST(test_vector_scene_graph_nested_group_transform_affects_child_line) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_rgb565 = 0xffffu;

    VgNode line = {
        .id = 11,
        .type = VG_NODE_LINE,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.line = {.x1 = 0, .y1 = 0, .x2 = 10, .y2 = 0}
    };

    VgNode *children[] = {&line};
    VgTransform group_t = vg_transform_identity();
    group_t.tx = 7;
    group_t.ty = 5;
    VgNode group = {
        .id = 10,
        .type = VG_NODE_GROUP,
        .has_transform = true,
        .transform = group_t,
        .style = style,
        .data.group = {.children = children, .child_count = 1}
    };

    vg_render_scene(&group, &fb);

    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)5 * TEST_W + 7]);
    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)5 * TEST_W + 17]);
}

TEST(test_vector_scene_graph_group_visible_false_skips_children) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgStyle line_style = vg_style_default();
    line_style.stroke_rgb565 = 0xffffu;
    VgNode line = {
        .id = 111,
        .type = VG_NODE_LINE,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = line_style,
        .data.line = {.x1 = 4, .y1 = 6, .x2 = 18, .y2 = 6}
    };

    VgStyle hidden_group_style = vg_style_default();
    hidden_group_style.visible = false;
    VgNode *children[] = {&line};
    VgNode group = {
        .id = 110,
        .type = VG_NODE_GROUP,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = hidden_group_style,
        .data.group = {.children = children, .child_count = 1}
    };

    vg_render_scene(&group, &fb);

    TEST_ASSERT_EQUAL_HEX16(0x0000u, pixels[(size_t)6 * TEST_W + 4]);
    TEST_ASSERT_EQUAL_HEX16(0x0000u, pixels[(size_t)6 * TEST_W + 18]);
}

TEST(test_vector_scene_graph_renders_line_directly_from_clojure_records) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID scene = eval_string(
        "(do (require 'tiny-gfx.scene) "
        "  (defrecord Transform [tx ty sx sy rot]) "
        "  (defrecord Style [stroke_rgb565 stroke_width visible has_fill fill_rgb565 has_bg_rgb565 bg_rgb565]) "
        "  (defrecord Line [id t style visible x1 y1 x2 y2]) "
        "  (defrecord Group [id t style visible children]) "
        "  (defrecord Scene [root clip-rect erase-rgb565 collision-rules]) "
        "  (->Scene "
        "    (->Group 100 nil nil true "
        "             [(->Line 101 nil (->Style 65535 1 true false 0 false 0) true 4 6 18 6)]) "
        "    nil nil nil))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(scene);

    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    TEST_ASSERT_TRUE(vg_render_scene_record(scene, &fb));
    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)6 * TEST_W + 4]);
    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)6 * TEST_W + 18]);
}

TEST(test_vector_scene_graph_record_group_visible_false_skips_children) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID scene = eval_string(
        "(do (require 'tiny-gfx.scene) "
        "  (defrecord Transform [tx ty sx sy rot]) "
        "  (defrecord Style [stroke_rgb565 stroke_width visible has_fill fill_rgb565 has_bg_rgb565 bg_rgb565]) "
        "  (defrecord Line [id t style visible x1 y1 x2 y2]) "
        "  (defrecord Group [id t style visible children]) "
        "  (defrecord Scene [root clip-rect erase-rgb565 collision-rules]) "
        "  (->Scene "
        "    (->Group 120 nil nil false "
        "             [(->Line 121 nil (->Style 65535 1 true false 0 false 0) true 4 6 18 6)]) "
        "    nil nil nil))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(scene);

    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    TEST_ASSERT_TRUE(vg_render_scene_record(scene, &fb));
    TEST_ASSERT_EQUAL_HEX16(0x0000u, pixels[(size_t)6 * TEST_W + 4]);
    TEST_ASSERT_EQUAL_HEX16(0x0000u, pixels[(size_t)6 * TEST_W + 18]);
}

TEST(test_vector_scene_graph_renders_nested_record_transform_inheritance) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID scene = eval_string(
        "(do (require 'tiny-gfx.scene) "
        "  (defrecord Transform [tx ty sx sy rot]) "
        "  (defrecord Style [stroke_rgb565 stroke_width visible has_fill fill_rgb565 has_bg_rgb565 bg_rgb565]) "
        "  (defrecord Line [id t style visible x1 y1 x2 y2]) "
        "  (defrecord Group [id t style visible children]) "
        "  (defrecord Scene [root clip-rect erase-rgb565 collision-rules]) "
        "  (->Scene "
        "    (->Group 200 (->Transform 7 5 1 1 0) nil true "
        "             [(->Line 201 nil (->Style 65535 1 true false 0 false 0) true 0 0 10 0)]) "
        "    nil nil nil))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(scene);

    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    TEST_ASSERT_TRUE(vg_render_scene_record(scene, &fb));
    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)5 * TEST_W + 7]);
    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)5 * TEST_W + 17]);
}

TEST(test_vector_scene_graph_scene_record_applies_shared_clip_and_erase_rect) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID scene = eval_string(
        "(do (require 'tiny-gfx.scene) "
        "  (defrecord Transform [tx ty sx sy rot]) "
        "  (defrecord Style [stroke_rgb565 stroke_width visible has_fill fill_rgb565 has_bg_rgb565 bg_rgb565]) "
        "  (defrecord Line [id t style visible x1 y1 x2 y2]) "
        "  (defrecord Group [id t style visible children]) "
        "  (defrecord Scene [root clip-rect erase-rgb565 collision-rules]) "
        "  (->Scene "
        "    (->Group 300 nil nil true "
        "             [(->Line 301 nil (->Style 65535 1 true false 0 false 0) true 0 10 63 10)]) "
        "    [20 8 10 6] "
        "    63488 nil))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(scene);

    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x1234u);

    TEST_ASSERT_TRUE(vg_render_scene_record(scene, &fb));

    /* Shared clip/erase rect: inside area gets erase color first. */
    TEST_ASSERT_EQUAL_HEX16(0xf800u, pixels[(size_t)9 * TEST_W + 22]);
    /* Clip-Rect begrenzt die Linie */
    TEST_ASSERT_EQUAL_HEX16(0x1234u, pixels[(size_t)10 * TEST_W + 10]);
    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)10 * TEST_W + 24]);
    TEST_ASSERT_EQUAL_HEX16(0x1234u, pixels[(size_t)10 * TEST_W + 35]);
    /* Outside shared clip/erase area remains untouched. */
    TEST_ASSERT_EQUAL_HEX16(0x1234u, pixels[(size_t)2 * TEST_W + 2]);
}

TEST(test_vector_scene_graph_decode_frame_scene_slot_record) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID scene = eval_string(
        "(do (require 'tiny-gfx.scene) "
        "  (defrecord Transform [tx ty sx sy rot]) "
        "  (defrecord Style [stroke_rgb565 stroke_width visible has_fill fill_rgb565 has_bg_rgb565 bg_rgb565]) "
        "  (defrecord Line [id t style visible x1 y1 x2 y2]) "
        "  (defrecord FrameScene [root clip-rect z visible opaque erase-rgb565 guard-px collision-rules]) "
        "  (->FrameScene "
        "    (->Line 401 nil (->Style 65535 1 true false 0 false 0) true 2 3 8 3) "
        "    [1 2 10 12] "
        "    3 true true 0 1 nil))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(scene);

    VgRenderSlot slot;
    TEST_ASSERT_TRUE(vg_decode_frame_slot_record(scene, &slot));
    TEST_ASSERT_EQUAL_INT(1, slot.clip_rect.x);
    TEST_ASSERT_EQUAL_INT(2, slot.clip_rect.y);
    TEST_ASSERT_EQUAL_INT(10, slot.clip_rect.w);
    TEST_ASSERT_EQUAL_INT(12, slot.clip_rect.h);
    TEST_ASSERT_EQUAL_INT(3, slot.z);
    TEST_ASSERT_TRUE(slot.visible);
    TEST_ASSERT_TRUE(slot.opaque);
    TEST_ASSERT_EQUAL_HEX16(0x0000u, slot.clear_rgb565);
    TEST_ASSERT_EQUAL_INT(1, slot.guard_px);
    TEST_ASSERT_NOT_NULL(slot.root);
}

TEST(test_vector_scene_graph_render_frame_scene_slot_record_if_changed) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID scene = eval_string(
        "(do (require 'tiny-gfx.scene) "
        "  (defrecord Transform [tx ty sx sy rot]) "
        "  (defrecord Style [stroke_rgb565 stroke_width visible has_fill fill_rgb565 has_bg_rgb565 bg_rgb565]) "
        "  (defrecord Line [id t style visible x1 y1 x2 y2]) "
        "  (defrecord FrameScene [root clip-rect z visible opaque erase-rgb565 guard-px collision-rules]) "
        "  (->FrameScene "
        "    (->Line 501 nil (->Style 65535 1 true false 0 false 0) true 0 10 63 10) "
        "    [20 8 10 6] "
        "    0 true true 0 0 nil))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(scene);

    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x1234u);

    VgRenderSlotState state = {0};
    TEST_ASSERT_TRUE(vg_render_frame_slot_record_if_changed(scene, &state, &fb, 1u));
    TEST_ASSERT_EQUAL_HEX16(0x1234u, pixels[(size_t)10 * TEST_W + 10]);
    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)10 * TEST_W + 24]);
    TEST_ASSERT_EQUAL_HEX16(0x1234u, pixels[(size_t)10 * TEST_W + 35]);

    TEST_ASSERT_FALSE(vg_render_frame_slot_record_if_changed(scene, &state, &fb, 1u));
}

TEST(test_vector_scene_graph_render_frame_scene_slot_record_if_changed_skips_when_slot_invisible) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID scene = eval_string(
        "(do (require 'tiny-gfx.scene) "
        "  (defrecord Transform [tx ty sx sy rot]) "
        "  (defrecord Style [stroke_rgb565 stroke_width visible has_fill fill_rgb565 has_bg_rgb565 bg_rgb565]) "
        "  (defrecord Line [id t style visible x1 y1 x2 y2]) "
        "  (defrecord FrameScene [root clip-rect z visible opaque erase-rgb565 guard-px collision-rules]) "
        "  (->FrameScene "
        "    (->Line 511 nil (->Style 65535 1 true false 0 false 0) true 0 10 63 10) "
        "    [20 8 10 6] "
        "    0 false true 0 0 nil))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(scene);

    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x1234u);

    VgRenderSlotState state = {0};
    TEST_ASSERT_TRUE(vg_render_frame_slot_record_if_changed(scene, &state, &fb, 1u));
    TEST_ASSERT_EQUAL_HEX16(0x0000u, pixels[(size_t)9 * TEST_W + 22]);
    TEST_ASSERT_EQUAL_HEX16(0x0000u, pixels[(size_t)10 * TEST_W + 24]);
    TEST_ASSERT_EQUAL_HEX16(0x1234u, pixels[(size_t)2 * TEST_W + 2]);
}

TEST(test_vector_scene_graph_fixed_transform_compose_apply_px_scale_translate_exact) {
    VgTransform parent = vg_transform_identity();
    parent.tx = 10;
    parent.ty = -2;
    parent.sx = 2 * VG_SCALE_ONE;
    parent.sy = 3 * VG_SCALE_ONE;

    VgTransform local = vg_transform_identity();
    local.tx = 4;
    local.ty = 5;

    VgTransformFixed parent_f = vg_transform_fixed_from_transform(parent);
    VgTransformFixed local_f = vg_transform_fixed_from_transform(local);
    VgTransformFixed composed = vg_transform_fixed_compose(parent_f, local_f);

    int out_x = 0;
    int out_y = 0;
    vg_transform_fixed_apply_px(composed, 7, 8, &out_x, &out_y);

    TEST_ASSERT_EQUAL_INT(32, out_x);
    TEST_ASSERT_EQUAL_INT(37, out_y);
}

TEST(test_vector_scene_graph_fixed_transform_cardinal_rotation_is_exact) {
    int one_fp = vg_transform_fixed_identity().m00;
    VgTransform t = vg_transform_identity();
    t.tx = 10;
    t.ty = 20;
    t.sx = VG_SCALE_ONE;
    t.sy = VG_SCALE_ONE;
    t.rot_deg = -90;

    VgTransformFixed tf = vg_transform_fixed_from_transform(t);
    TEST_ASSERT_EQUAL_INT(0, tf.m00);
    TEST_ASSERT_EQUAL_INT(one_fp, tf.m01);
    TEST_ASSERT_EQUAL_INT(-one_fp, tf.m10);
    TEST_ASSERT_EQUAL_INT(0, tf.m11);

    int out_x = 0;
    int out_y = 0;
    vg_transform_fixed_apply_px(tf, 3, 4, &out_x, &out_y);
    TEST_ASSERT_EQUAL_INT(14, out_x);
    TEST_ASSERT_EQUAL_INT(17, out_y);
}

TEST(test_vector_scene_graph_anim_fixed_progress_ease_and_lerp_are_deterministic) {
    int32_t p0 = vg_anim_progress_q13(0u, 1000u);
    int32_t p_half = vg_anim_progress_q13(500u, 1000u);
    int32_t p_end = vg_anim_progress_q13(1000u, 1000u);
    int32_t p_over = vg_anim_progress_q13(1200u, 1000u);
    int32_t p_zero_dur = vg_anim_progress_q13(1u, 0u);

    TEST_ASSERT_EQUAL_INT32(0, p0);
    TEST_ASSERT_EQUAL_INT32(VG_SCALE_ONE / 2, p_half);
    TEST_ASSERT_EQUAL_INT32(VG_SCALE_ONE, p_end);
    TEST_ASSERT_EQUAL_INT32(VG_SCALE_ONE, p_over);
    TEST_ASSERT_EQUAL_INT32(VG_SCALE_ONE, p_zero_dur);

    TEST_ASSERT_EQUAL_INT32(0, vg_anim_ease_q13(VG_ANIM_EASE_LINEAR, -1));
    TEST_ASSERT_EQUAL_INT32(VG_SCALE_ONE, vg_anim_ease_q13(VG_ANIM_EASE_LINEAR, VG_SCALE_ONE + 1));
    TEST_ASSERT_EQUAL_INT32(p_half, vg_anim_ease_q13(VG_ANIM_EASE_LINEAR, p_half));

    int32_t eased_half = vg_anim_ease_q13(VG_ANIM_EASE_OUT_CUBIC, p_half);
    TEST_ASSERT_TRUE(eased_half > p_half); /* ease-out should advance faster in first half */
    TEST_ASSERT_EQUAL_INT32(0, vg_anim_ease_q13(VG_ANIM_EASE_OUT_CUBIC, 0));
    TEST_ASSERT_EQUAL_INT32(VG_SCALE_ONE, vg_anim_ease_q13(VG_ANIM_EASE_OUT_CUBIC, VG_SCALE_ONE));

    int32_t from = 10 * VG_SCALE_ONE;
    int32_t to = 20 * VG_SCALE_ONE;
    int32_t mid_lin = vg_anim_lerp_q13(from, to, p_half);
    int32_t mid_eased = vg_anim_lerp_q13(from, to, eased_half);
    TEST_ASSERT_EQUAL_INT32(15 * VG_SCALE_ONE, mid_lin);
    TEST_ASSERT_TRUE(mid_eased > mid_lin);

    /* Determinism: same input -> same result. */
    TEST_ASSERT_EQUAL_INT32(eased_half, vg_anim_ease_q13(VG_ANIM_EASE_OUT_CUBIC, p_half));
    TEST_ASSERT_EQUAL_INT32(mid_eased, vg_anim_lerp_q13(from, to, eased_half));
}

TEST(test_vector_scene_graph_anim_fixed_in_out_quad_symmetry_samples) {
    int32_t t1 = VG_SCALE_ONE / 4;
    int32_t t2 = VG_SCALE_ONE - t1;
    int32_t y1 = vg_anim_ease_q13(VG_ANIM_EASE_IN_OUT_QUAD, t1);
    int32_t y2 = vg_anim_ease_q13(VG_ANIM_EASE_IN_OUT_QUAD, t2);

    /* Symmetry around 0.5: y(1-t) ~= 1-y(t) in fixed arithmetic. */
    TEST_ASSERT_INT_WITHIN(1, VG_SCALE_ONE - y1, y2);
}

TEST(test_vector_scene_graph_deterministic_frame_checksum_for_mixed_scene) {
    uint16_t pixels_a[TEST_W * TEST_H];
    uint16_t pixels_b[TEST_W * TEST_H];
    VgFrameBuffer fb_a;
    VgFrameBuffer fb_b;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb_a, TEST_W, TEST_H, pixels_a, TEST_W * TEST_H));
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb_b, TEST_W, TEST_H, pixels_b, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb_a, 0x0000u);
    vg_framebuffer_clear(&fb_b, 0x0000u);

    VgStyle white = vg_style_default();
    white.stroke_rgb565 = 0xffffu;
    VgStyle green = vg_style_default();
    green.stroke_rgb565 = 0x07e0u;
    green.stroke_width = 2;
    VgStyle red = vg_style_default();
    red.stroke_rgb565 = 0xf800u;

    VgPoint pts[] = {{10, 25}, {20, 30}, {30, 24}, {40, 30}};

    VgNode line = {
        .id = 21,
        .type = VG_NODE_LINE,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = white,
        .data.line = {.x1 = 2, .y1 = 2, .x2 = 30, .y2 = 12}
    };
    VgNode rect = {
        .id = 22,
        .type = VG_NODE_RECT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = green,
        .data.rect = {.x = 6, .y = 18, .w = 20, .h = 10}
    };
    VgNode tri = {
        .id = 23,
        .type = VG_NODE_TRI,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = red,
        .data.tri = {.x1 = 45, .y1 = 6, .x2 = 52, .y2 = 20, .x3 = 37, .y3 = 20}
    };
    VgNode poly = {
        .id = 24,
        .type = VG_NODE_POLYLINE,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = white,
        .data.polyline = {.points = pts, .point_count = 4, .closed = false}
    };
    VgNode text = {
        .id = 25,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = white,
        .data.text = {.x = 34, .y = 30, .scale = VG_SCALE_ONE, .rot_deg = 0, .text = "HI"}
    };

    VgNode *children[] = {&line, &rect, &tri, &poly, &text};
    VgNode root = {
        .id = 20,
        .type = VG_NODE_GROUP,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = vg_style_default(),
        .data.group = {.children = children, .child_count = 5}
    };

    vg_render_scene(&root, &fb_a);
    vg_render_scene(&root, &fb_b);

    uint32_t checksum_a = vg_framebuffer_checksum(&fb_a);
    uint32_t checksum_b = vg_framebuffer_checksum(&fb_b);
    TEST_ASSERT_EQUAL_HEX32(checksum_a, checksum_b);
}

TEST(test_vector_scene_graph_clipped_render_limits_written_pixels) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_rgb565 = 0xffffu;
    style.stroke_width = 1;

    VgNode line = {
        .id = 26,
        .type = VG_NODE_LINE,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.line = {.x1 = 0, .y1 = 10, .x2 = 63, .y2 = 10}
    };

    VgClipRect clip = {.x = 20, .y = 8, .w = 10, .h = 6};
    vg_render_scene_clipped(&line, &fb, clip);

    TEST_ASSERT_EQUAL_HEX16(0x0000u, pixels[(size_t)10 * TEST_W + 10]);
    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)10 * TEST_W + 24]);
    TEST_ASSERT_EQUAL_HEX16(0x0000u, pixels[(size_t)10 * TEST_W + 35]);
}

TEST(test_vector_scene_graph_render_slot_if_changed_skips_unchanged_and_clears_moved_union) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x1234u);

    VgStyle style = vg_style_default();
    style.stroke_rgb565 = 0xffffu;
    style.stroke_width = 1;

    VgNode line = {
        .id = 27,
        .type = VG_NODE_LINE,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.line = {.x1 = 2, .y1 = 2, .x2 = 8, .y2 = 2}
    };

    VgRenderSlot slot = {
        .root = &line,
        .clip_rect = {.x = 0, .y = 0, .w = 16, .h = 16},
        .z = 0,
        .visible = true,
        .opaque = true,
        .clear_rgb565 = 0x0000u,
        .guard_px = 0
    };
    VgRenderSlotState state = {0};

    TEST_ASSERT_TRUE(vg_render_slot_if_changed(&slot, &state, &fb, 1u));
    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)2 * TEST_W + 4]);
    uint32_t after_first = vg_framebuffer_checksum(&fb);

    TEST_ASSERT_FALSE(vg_render_slot_if_changed(&slot, &state, &fb, 1u));
    uint32_t after_second = vg_framebuffer_checksum(&fb);
    TEST_ASSERT_EQUAL_HEX32(after_first, after_second);

    line.has_transform = true;
    line.transform = vg_transform_identity();
    line.transform.tx = 20;
    slot.clip_rect.x = 20;
    slot.clip_rect.w = 16;

    TEST_ASSERT_TRUE(vg_render_slot_if_changed(&slot, &state, &fb, 2u));
    TEST_ASSERT_EQUAL_HEX16(0x0000u, pixels[(size_t)2 * TEST_W + 4]);
    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)2 * TEST_W + 24]);
}

TEST(test_vector_scene_graph_thick_line_changes_coverage) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgStyle thin = vg_style_default();
    thin.stroke_rgb565 = 0xffffu;
    thin.stroke_width = 1;
    VgStyle thick = thin;
    thick.stroke_width = 4;

    VgNode line_thin = {
        .id = 31,
        .type = VG_NODE_LINE,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = thin,
        .data.line = {.x1 = 4, .y1 = 10, .x2 = 58, .y2 = 10}
    };
    VgNode line_thick = line_thin;
    line_thick.id = 32;
    line_thick.style = thick;
    line_thick.data.line.y1 = 26;
    line_thick.data.line.y2 = 26;

    VgNode *children[] = {&line_thin, &line_thick};
    VgNode root = {
        .id = 30,
        .type = VG_NODE_GROUP,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = vg_style_default(),
        .data.group = {.children = children, .child_count = 2}
    };

    vg_render_scene(&root, &fb);

    size_t colored = count_color(pixels, TEST_W * TEST_H, 0xffffu);
    TEST_ASSERT_TRUE(colored > 160);
}

TEST(test_vector_scene_graph_patch_updates_transform_visibility_style_and_text) {
    uint16_t pixels_before[TEST_W * TEST_H];
    uint16_t pixels_after[TEST_W * TEST_H];
    VgFrameBuffer fb_before;
    VgFrameBuffer fb_after;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb_before, TEST_W, TEST_H, pixels_before, TEST_W * TEST_H));
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb_after, TEST_W, TEST_H, pixels_after, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb_before, 0x0000u);
    vg_framebuffer_clear(&fb_after, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_rgb565 = 0xffffu;
    VgNode text = {
        .id = 41,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.text = {.x = 2, .y = 4, .scale = VG_SCALE_ONE, .rot_deg = 0, .text = "A"}
    };

    VgNode line = {
        .id = 42,
        .type = VG_NODE_LINE,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.line = {.x1 = 2, .y1 = 34, .x2 = 18, .y2 = 34}
    };

    VgNode *children[] = {&text, &line};
    VgNode root = {
        .id = 40,
        .type = VG_NODE_GROUP,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = vg_style_default(),
        .data.group = {.children = children, .child_count = 2}
    };

    vg_render_scene(&root, &fb_before);
    uint32_t before = vg_framebuffer_checksum(&fb_before);

    VgPatch tpatch = {.id = 42, .type = VG_PATCH_TRANSFORM};
    tpatch.value.transform = vg_transform_identity();
    tpatch.value.transform.tx = 20;
    tpatch.value.transform.ty = -8;
    TEST_ASSERT_TRUE(vg_scene_apply_patch(&root, &tpatch));

    VgPatch spatch = {.id = 42, .type = VG_PATCH_STYLE};
    spatch.value.style = style;
    spatch.value.style.stroke_rgb565 = 0xf800u;
    spatch.value.style.stroke_width = 3;
    TEST_ASSERT_TRUE(vg_scene_apply_patch(&root, &spatch));

    VgPatch vpatch = {.id = 41, .type = VG_PATCH_VISIBILITY};
    vpatch.value.visible = false;
    TEST_ASSERT_TRUE(vg_scene_apply_patch(&root, &vpatch));

    VgPatch txpatch = {.id = 41, .type = VG_PATCH_TEXT};
    txpatch.value.text = "CHANGED";
    TEST_ASSERT_TRUE(vg_scene_apply_patch(&root, &txpatch));

    vg_render_scene(&root, &fb_after);
    uint32_t after = vg_framebuffer_checksum(&fb_after);
    TEST_ASSERT_NOT_EQUAL(before, after);
}

TEST(test_vector_scene_graph_aa_only_when_bg_given_for_1px_lines) {
    uint16_t pixels_noaa[TEST_W * TEST_H];
    uint16_t pixels_aa[TEST_W * TEST_H];
    VgFrameBuffer fb_noaa;
    VgFrameBuffer fb_aa;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb_noaa, TEST_W, TEST_H, pixels_noaa, TEST_W * TEST_H));
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb_aa, TEST_W, TEST_H, pixels_aa, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb_noaa, 0x0000u);
    vg_framebuffer_clear(&fb_aa, 0x0000u);

    VgStyle noaa = vg_style_default();
    noaa.stroke_rgb565 = 0xffffu;
    noaa.stroke_width = 1;
    noaa.has_bg_rgb565 = false;

    VgStyle aa = noaa;
    aa.has_bg_rgb565 = true;
    aa.bg_rgb565 = 0x0000u;

    VgNode line_noaa = {
        .id = 51,
        .type = VG_NODE_LINE,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = noaa,
        .data.line = {.x1 = 5, .y1 = 5, .x2 = 58, .y2 = 37}
    };

    VgNode line_aa = line_noaa;
    line_aa.id = 52;
    line_aa.style = aa;

    vg_render_scene(&line_noaa, &fb_noaa);
    vg_render_scene(&line_aa, &fb_aa);

    size_t aa_intermediate = 0;
    for (size_t i = 0; i < TEST_W * TEST_H; i++) {
        uint16_t p = pixels_aa[i];
        if (p != 0x0000u && p != 0xffffu) {
            aa_intermediate++;
        }
    }
    TEST_ASSERT_TRUE(aa_intermediate > 0);

    size_t noaa_intermediate = 0;
    for (size_t i = 0; i < TEST_W * TEST_H; i++) {
        uint16_t p = pixels_noaa[i];
        if (p != 0x0000u && p != 0xffffu) {
            noaa_intermediate++;
        }
    }
    TEST_ASSERT_EQUAL_size_t(0, noaa_intermediate);

    size_t aa_ink = count_not_color(pixels_aa, TEST_W * TEST_H, 0x0000u);
    size_t noaa_ink = count_not_color(pixels_noaa, TEST_W * TEST_H, 0x0000u);
    TEST_ASSERT_TRUE(aa_ink > noaa_ink);
}

TEST(test_vector_scene_graph_hv_mono_has_inter_glyph_gap) {
    uint16_t pixels_a[TEST_W * TEST_H];
    uint16_t pixels_aa[TEST_W * TEST_H];
    VgFrameBuffer fb_a;
    VgFrameBuffer fb_aa;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb_a, TEST_W, TEST_H, pixels_a, TEST_W * TEST_H));
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb_aa, TEST_W, TEST_H, pixels_aa, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb_a, 0x0000u);
    vg_framebuffer_clear(&fb_aa, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_rgb565 = 0xffffu;
    style.stroke_width = 1;

    VgNode text_a = {
        .id = 61,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.text = {.x = 0, .y = 0, .scale = VG_SCALE_ONE, .rot_deg = 0, .text = "A"}
    };
    VgNode text_aa = text_a;
    text_aa.id = 62;
    text_aa.data.text.text = "AA";

    vg_render_scene(&text_a, &fb_a);
    vg_render_scene(&text_aa, &fb_aa);

    size_t min_x_a = 0, min_y_a = 0, max_x_a = 0, max_y_a = 0;
    size_t min_x_aa = 0, min_y_aa = 0, max_x_aa = 0, max_y_aa = 0;
    TEST_ASSERT_TRUE(find_non_bg_bounds(pixels_a, TEST_W, TEST_H, 0x0000u, &min_x_a, &min_y_a, &max_x_a, &max_y_a));
    TEST_ASSERT_TRUE(find_non_bg_bounds(pixels_aa, TEST_W, TEST_H, 0x0000u, &min_x_aa, &min_y_aa, &max_x_aa, &max_y_aa));

    size_t width_a = max_x_a - min_x_a + 1;
    size_t width_aa = max_x_aa - min_x_aa + 1;
    TEST_ASSERT_TRUE(width_aa > width_a + 2);
}

TEST(test_vector_scene_graph_hv_mono_problem_glyphs_not_half_height) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_rgb565 = 0xffffu;
    style.stroke_width = 1;

    VgNode text = {
        .id = 62,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.text = {.x = 2, .y = 2, .scale = VG_SCALE_ONE, .rot_deg = 0, .text = "MWXYR"}
    };

    vg_render_scene(&text, &fb);

    size_t min_x = 0, min_y = 0, max_x = 0, max_y = 0;
    TEST_ASSERT_TRUE(find_non_bg_bounds(pixels, TEST_W, TEST_H, 0x0000u, &min_x, &min_y, &max_x, &max_y));

    size_t height = max_y - min_y + 1;
    // Regression intent: avoid accidentally squashing glyphs to half height.
    TEST_ASSERT_TRUE(height >= 7);
}

TEST(test_vector_scene_graph_punctuation_dot_and_colon_are_compact_blobs) {
    uint16_t pixels_dot[TEST_W * TEST_H];
    uint16_t pixels_colon[TEST_W * TEST_H];
    VgFrameBuffer fb_dot;
    VgFrameBuffer fb_colon;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb_dot, TEST_W, TEST_H, pixels_dot, TEST_W * TEST_H));
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb_colon, TEST_W, TEST_H, pixels_colon, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb_dot, 0x0000u);
    vg_framebuffer_clear(&fb_colon, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_rgb565 = 0xffffu;
    style.stroke_width = 1;

    VgNode dot = {
        .id = 71,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.text = {.x = 2, .y = 2, .scale = VG_SCALE_ONE, .rot_deg = 0, .text = "."}
    };
    VgNode colon = dot;
    colon.id = 72;
    colon.data.text.text = ":";

    vg_render_scene(&dot, &fb_dot);
    vg_render_scene(&colon, &fb_colon);

    size_t min_x = 0, min_y = 0, max_x = 0, max_y = 0;
    TEST_ASSERT_TRUE(find_non_bg_bounds(pixels_dot, TEST_W, TEST_H, 0x0000u, &min_x, &min_y, &max_x, &max_y));
    size_t dot_w = max_x - min_x + 1;
    size_t dot_h = max_y - min_y + 1;
    /* Arcade period cell is rendered as a compact blob at the cell center. */
    TEST_ASSERT_TRUE(min_x >= 3);
    TEST_ASSERT_TRUE(dot_w <= 8);
    TEST_ASSERT_TRUE(dot_h >= 1);
    TEST_ASSERT_TRUE(count_not_color(pixels_dot, TEST_W * TEST_H, 0x0000u) >= 1);

    TEST_ASSERT_TRUE(find_non_bg_bounds(pixels_colon, TEST_W, TEST_H, 0x0000u, &min_x, &min_y, &max_x, &max_y));
    size_t colon_w = max_x - min_x + 1;
    size_t colon_h = max_y - min_y + 1;
    /* Arcade colon: two compact blobs (one per filled cell). */
    TEST_ASSERT_TRUE(colon_w <= 10);
    TEST_ASSERT_TRUE(colon_h >= 4);
    TEST_ASSERT_TRUE(count_not_color(pixels_colon, TEST_W * TEST_H, 0x0000u) >= 2);
}

TEST(test_vector_scene_graph_comma_sits_lower_than_period_and_not_left) {
    uint16_t pixels_dot[TEST_W * TEST_H];
    uint16_t pixels_comma[TEST_W * TEST_H];
    VgFrameBuffer fb_dot;
    VgFrameBuffer fb_comma;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb_dot, TEST_W, TEST_H, pixels_dot, TEST_W * TEST_H));
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb_comma, TEST_W, TEST_H, pixels_comma, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb_dot, 0x0000u);
    vg_framebuffer_clear(&fb_comma, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_rgb565 = 0xffffu;
    style.stroke_width = 1;

    VgNode dot = {
        .id = 73,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.text = {.x = 2, .y = 2, .scale = VG_SCALE_ONE, .rot_deg = 0, .text = "."}
    };
    VgNode comma = dot;
    comma.id = 74;
    comma.data.text.text = ",";

    vg_render_scene(&dot, &fb_dot);
    vg_render_scene(&comma, &fb_comma);

    size_t dot_min_x = 0, dot_min_y = 0, dot_max_x = 0, dot_max_y = 0;
    size_t comma_min_x = 0, comma_min_y = 0, comma_max_x = 0, comma_max_y = 0;
    TEST_ASSERT_TRUE(find_non_bg_bounds(pixels_dot, TEST_W, TEST_H, 0x0000u,
                                        &dot_min_x, &dot_min_y, &dot_max_x, &dot_max_y));
    TEST_ASSERT_TRUE(find_non_bg_bounds(pixels_comma, TEST_W, TEST_H, 0x0000u,
                                        &comma_min_x, &comma_min_y, &comma_max_x, &comma_max_y));

    TEST_ASSERT_TRUE(comma_max_y > dot_max_y);
    TEST_ASSERT_TRUE(comma_min_x >= dot_min_x);
}

TEST(test_vector_scene_graph_additional_ascii_punctuation_avoids_box_fallback) {
    typedef struct {
        const char *text;
        size_t max_width;
        size_t min_height;
        size_t min_ink;
    } GlyphExpect;

    const GlyphExpect cases[] = {
        {";", 10, 6, 5},
        {"!", 4, 8, 6},
        {"?", 6, 8, 9},
        {"(", 4, 9, 8},
        {")", 4, 9, 8},
    };

    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));

    VgStyle style = vg_style_default();
    style.stroke_rgb565 = 0xffffu;
    style.stroke_width = 1;

    VgNode text = {
        .id = 81,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.text = {.x = 2, .y = 2, .scale = VG_SCALE_ONE, .rot_deg = 0, .text = NULL}
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        vg_framebuffer_clear(&fb, 0x0000u);
        text.id = 81u + (uint32_t)i;
        text.data.text.text = cases[i].text;
        vg_render_scene(&text, &fb);

        size_t min_x = 0, min_y = 0, max_x = 0, max_y = 0;
        TEST_ASSERT_TRUE(find_non_bg_bounds(pixels, TEST_W, TEST_H, 0x0000u, &min_x, &min_y, &max_x, &max_y));
        size_t w = max_x - min_x + 1;
        size_t h = max_y - min_y + 1;
        size_t ink = count_not_color(pixels, TEST_W * TEST_H, 0x0000u);

        // Default fallback is a 6x11 box; these glyphs should be narrower and shaped.
        TEST_ASSERT_TRUE(w <= cases[i].max_width);
        TEST_ASSERT_TRUE(h >= cases[i].min_height);
        TEST_ASSERT_TRUE(ink >= cases[i].min_ink);
    }
}

TEST(test_vector_scene_graph_compact_punctuation_disables_aa_fringe_even_with_bg) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));

    VgStyle style = vg_style_default();
    style.stroke_rgb565 = 0xffffu;
    style.stroke_width = 1;
    style.has_bg_rgb565 = true;
    style.bg_rgb565 = 0x0000u;

    VgNode text = {
        .id = 91,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.text = {.x = 2, .y = 2, .scale = VG_SCALE_ONE, .rot_deg = 0, .text = NULL}
    };

    const char *cases[] = {".", ":"};
    for (size_t ci = 0; ci < sizeof(cases) / sizeof(cases[0]); ci++) {
        vg_framebuffer_clear(&fb, 0x0000u);
        text.id = 91u + (uint32_t)ci;
        text.data.text.text = cases[ci];
        vg_render_scene(&text, &fb);

        size_t intermediate = 0;
        size_t ink = 0;
        for (size_t i = 0; i < TEST_W * TEST_H; i++) {
            uint16_t p = pixels[i];
            if (p == 0x0000u) continue;
            ink++;
            if (p != 0xffffu) {
                intermediate++;
            }
        }
        TEST_ASSERT_TRUE(ink > 0);
        TEST_ASSERT_EQUAL_size_t(0, intermediate);
    }
}

TEST(test_vector_scene_graph_arcade_text_disables_aa_fringe_even_with_bg) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));

    VgStyle style = vg_style_default();
    style.stroke_rgb565 = 0xffffu;
    style.stroke_width = 1;
    style.has_bg_rgb565 = true;
    style.bg_rgb565 = 0x0000u;

    VgNode text = {
        .id = 95,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.text = {.x = 2, .y = 2, .scale = VG_SCALE_ONE, .rot_deg = 0, .text = "FPS: 59.9"}
    };

    vg_framebuffer_clear(&fb, 0x0000u);
    vg_render_scene(&text, &fb);

    size_t intermediate = 0;
    size_t ink = 0;
    for (size_t i = 0; i < TEST_W * TEST_H; i++) {
        uint16_t p = pixels[i];
        if (p == 0x0000u) continue;
        ink++;
        if (p != 0xffffu) {
            intermediate++;
        }
    }

    TEST_ASSERT_TRUE(ink > 0);
    TEST_ASSERT_EQUAL_size_t(0, intermediate);
}

TEST(test_vector_scene_graph_arcade_z_scale_1_5_diagonal_stays_within_expected_bounds) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_rgb565 = 0xffffu;
    style.stroke_width = 1;
    style.has_bg_rgb565 = true;
    style.bg_rgb565 = 0x0000u;

    VgNode text = {
        .id = 96,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.text = {.x = 2, .y = 2, .scale = VG_SCALE_ONE * 3 / 2, .rot_deg = 0, .text = "Z"}
    };

    vg_render_scene(&text, &fb);

    size_t min_x = 0, min_y = 0, max_x = 0, max_y = 0;
    TEST_ASSERT_TRUE(find_non_bg_bounds(pixels, TEST_W, TEST_H, 0x0000u, &min_x, &min_y, &max_x, &max_y));

    TEST_ASSERT_EQUAL_size_t(13, (max_x - min_x + 1));
    TEST_ASSERT_EQUAL_size_t(13, (max_y - min_y + 1));
}

TEST(test_vector_scene_graph_slash_backslash_scale_1_5_are_not_one_pixel_too_low) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));

    VgStyle style = vg_style_default();
    style.stroke_rgb565 = 0xffffu;
    style.stroke_width = 1;
    style.has_bg_rgb565 = true;
    style.bg_rgb565 = 0x0000u;

    VgNode text = {
        .id = 97,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.text = {.x = 2, .y = 2, .scale = VG_SCALE_ONE * 3 / 2, .rot_deg = 0, .text = NULL}
    };

    const char *cases[] = {"/", "\\"};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        vg_framebuffer_clear(&fb, 0x0000u);
        text.id = 97u + (uint32_t)i;
        text.data.text.text = cases[i];
        vg_render_scene(&text, &fb);

        size_t min_x = 0, min_y = 0, max_x = 0, max_y = 0;
        TEST_ASSERT_TRUE(find_non_bg_bounds(pixels, TEST_W, TEST_H, 0x0000u, &min_x, &min_y, &max_x, &max_y));
        TEST_ASSERT_TRUE(min_x >= 3);
        TEST_ASSERT_EQUAL_size_t(16, max_y);
    }
}

TEST(test_vector_scene_graph_arcade_exclamation_has_detached_dot) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_rgb565 = 0xffffu;
    style.stroke_width = 1;
    style.has_bg_rgb565 = true;
    style.bg_rgb565 = 0x0000u;

    VgNode text = {
        .id = 98,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.text = {.x = 2, .y = 2, .scale = VG_SCALE_ONE, .rot_deg = 0, .text = "!"}
    };

    vg_render_scene(&text, &fb);

    size_t min_x = 0, min_y = 0, max_x = 0, max_y = 0;
    TEST_ASSERT_TRUE(find_non_bg_bounds(pixels, TEST_W, TEST_H, 0x0000u, &min_x, &min_y, &max_x, &max_y));

    bool seen_ink = false;
    bool seen_gap_after_ink = false;
    bool seen_ink_after_gap = false;
    for (size_t y = min_y; y <= max_y; y++) {
        bool row_has_ink = false;
        for (size_t x = min_x; x <= max_x; x++) {
            if (pixels[y * TEST_W + x] != 0x0000u) {
                row_has_ink = true;
                break;
            }
        }
        if (row_has_ink) {
            if (seen_gap_after_ink) {
                seen_ink_after_gap = true;
            }
            seen_ink = true;
        } else if (seen_ink && !seen_ink_after_gap) {
            seen_gap_after_ink = true;
        }
    }

    TEST_ASSERT_TRUE(seen_gap_after_ink);
    TEST_ASSERT_TRUE(seen_ink_after_gap);
}

TEST(test_vector_scene_graph_colon_blobs_are_vertically_aligned) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_rgb565 = 0xffffu;
    style.stroke_width = 1;
    style.has_bg_rgb565 = true;
    style.bg_rgb565 = 0x0000u;

    VgNode text = {
        .id = 92,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.text = {.x = 2, .y = 2, .scale = VG_SCALE_ONE, .rot_deg = 0, .text = ":"}
    };

    vg_render_scene(&text, &fb);

    size_t min_x = 0, min_y = 0, max_x = 0, max_y = 0;
    TEST_ASSERT_TRUE(find_non_bg_bounds(pixels, TEST_W, TEST_H, 0x0000u, &min_x, &min_y, &max_x, &max_y));

    size_t mid_y = (min_y + max_y) / 2;
    size_t top_sum_x = 0, top_count = 0;
    size_t bot_sum_x = 0, bot_count = 0;
    for (size_t y = min_y; y <= max_y; y++) {
        for (size_t x = min_x; x <= max_x; x++) {
            if (pixels[y * TEST_W + x] == 0x0000u) continue;
            if (y <= mid_y) {
                top_sum_x += x;
                top_count++;
            } else {
                bot_sum_x += x;
                bot_count++;
            }
        }
    }

    TEST_ASSERT_TRUE(top_count > 0);
    TEST_ASSERT_TRUE(bot_count > 0);
    int top_cx = (int)((top_sum_x + (top_count / 2)) / top_count);
    int bot_cx = (int)((bot_sum_x + (bot_count / 2)) / bot_count);
    int dx = top_cx - bot_cx;
    if (dx < 0) dx = -dx;
    TEST_ASSERT_TRUE(dx <= 1);
}

TEST(test_vector_scene_graph_arcade_j_has_bottom_bar_shape) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_rgb565 = 0xffffu;
    style.stroke_width = 1;

    VgNode text = {
        .id = 93,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.text = {.x = 2, .y = 2, .scale = VG_SCALE_ONE, .rot_deg = 0, .text = "J"}
    };

    vg_render_scene(&text, &fb);

    size_t min_x = 0, min_y = 0, max_x = 0, max_y = 0;
    TEST_ASSERT_TRUE(find_non_bg_bounds(pixels, TEST_W, TEST_H, 0x0000u, &min_x, &min_y, &max_x, &max_y));

    size_t top_row_ink = 0;
    size_t bottom_row_ink = 0;
    for (size_t x = min_x; x <= max_x; x++) {
        if (pixels[min_y * TEST_W + x] != 0x0000u) top_row_ink++;
        if (pixels[max_y * TEST_W + x] != 0x0000u) bottom_row_ink++;
    }

    // Arcade J has the horizontal bar at the bottom (renderer y grows downward).
    TEST_ASSERT_TRUE(bottom_row_ink > top_row_ink);
}

/* ---------- M4b: Solid Fill Color Tests ---------- */

TEST(test_vector_scene_graph_filled_rect_produces_interior_pixels) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_rgb565 = 0xffffu;
    style.stroke_width = 1;
    style.has_fill = true;
    style.fill_rgb565 = 0x07e0u; /* green */

    VgNode rect = {
        .id = 1001,
        .type = VG_NODE_RECT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.rect = {.x = 10, .y = 10, .w = 20, .h = 12}
    };

    vg_render_scene(&rect, &fb);

    size_t fill_count = count_color(pixels, TEST_W * TEST_H, 0x07e0u);
    size_t stroke_count = count_color(pixels, TEST_W * TEST_H, 0xffffu);
    TEST_ASSERT_TRUE(fill_count > 0);
    TEST_ASSERT_TRUE(stroke_count > 0);
    TEST_ASSERT_TRUE(fill_count > stroke_count);
}

TEST(test_vector_scene_graph_filled_tri_produces_interior_pixels) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_rgb565 = 0xffffu;
    style.stroke_width = 1;
    style.has_fill = true;
    style.fill_rgb565 = 0xf800u; /* red */

    VgNode tri = {
        .id = 1002,
        .type = VG_NODE_TRI,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.tri = {.x1 = 32, .y1 = 4, .x2 = 50, .y2 = 40, .x3 = 14, .y3 = 40}
    };

    vg_render_scene(&tri, &fb);

    size_t fill_count = count_color(pixels, TEST_W * TEST_H, 0xf800u);
    size_t stroke_count = count_color(pixels, TEST_W * TEST_H, 0xffffu);
    TEST_ASSERT_TRUE(fill_count > 0);
    TEST_ASSERT_TRUE(stroke_count > 0);
    TEST_ASSERT_TRUE(fill_count > stroke_count);
}

TEST(test_vector_scene_graph_filled_closed_polyline_produces_interior_pixels) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_rgb565 = 0xffffu;
    style.stroke_width = 1;
    style.has_fill = true;
    style.fill_rgb565 = 0x001fu; /* blue */

    VgPoint pts[] = {{10, 5}, {50, 5}, {50, 35}, {10, 35}};

    VgNode poly = {
        .id = 1003,
        .type = VG_NODE_POLYLINE,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.polyline = {.points = pts, .point_count = 4, .closed = true}
    };

    vg_render_scene(&poly, &fb);

    size_t fill_count = count_color(pixels, TEST_W * TEST_H, 0x001fu);
    size_t stroke_count = count_color(pixels, TEST_W * TEST_H, 0xffffu);
    TEST_ASSERT_TRUE(fill_count > 0);
    TEST_ASSERT_TRUE(stroke_count > 0);
    TEST_ASSERT_TRUE(fill_count > stroke_count);
}

TEST(test_vector_scene_graph_open_polyline_does_not_fill) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_rgb565 = 0xffffu;
    style.stroke_width = 1;
    style.has_fill = true;
    style.fill_rgb565 = 0x001fu;

    VgPoint pts[] = {{10, 5}, {50, 5}, {50, 35}, {10, 35}};

    VgNode poly = {
        .id = 1004,
        .type = VG_NODE_POLYLINE,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.polyline = {.points = pts, .point_count = 4, .closed = false}
    };

    vg_render_scene(&poly, &fb);

    size_t fill_count = count_color(pixels, TEST_W * TEST_H, 0x001fu);
    TEST_ASSERT_EQUAL_size_t(0, fill_count);
}

TEST(test_vector_scene_graph_fill_paint_order_fill_under_stroke) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_rgb565 = 0xffffu;
    style.stroke_width = 2;
    style.has_fill = true;
    style.fill_rgb565 = 0xf800u; /* red fill */

    VgNode rect = {
        .id = 1005,
        .type = VG_NODE_RECT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.rect = {.x = 10, .y = 10, .w = 30, .h = 20}
    };

    vg_render_scene(&rect, &fb);

    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)10 * TEST_W + 10]);
    size_t fill_count = count_color(pixels, TEST_W * TEST_H, 0xf800u);
    size_t stroke_count = count_color(pixels, TEST_W * TEST_H, 0xffffu);
    TEST_ASSERT_TRUE(fill_count > 0);
    TEST_ASSERT_TRUE(stroke_count > 0);
}

TEST(test_vector_scene_graph_no_fill_when_has_fill_false) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_rgb565 = 0xffffu;
    style.stroke_width = 1;
    style.has_fill = false;
    style.fill_rgb565 = 0xf800u;

    VgNode rect = {
        .id = 1006,
        .type = VG_NODE_RECT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.rect = {.x = 10, .y = 10, .w = 20, .h = 12}
    };

    vg_render_scene(&rect, &fb);

    size_t fill_count = count_color(pixels, TEST_W * TEST_H, 0xf800u);
    TEST_ASSERT_EQUAL_size_t(0, fill_count);
}

TEST(test_vector_scene_graph_filled_rect_clipped) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_rgb565 = 0xffffu;
    style.stroke_width = 1;
    style.has_fill = true;
    style.fill_rgb565 = 0x07e0u; /* green */

    VgNode rect = {
        .id = 1007,
        .type = VG_NODE_RECT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.rect = {.x = 5, .y = 5, .w = 50, .h = 35}
    };

    VgClipRect clip = {.x = 20, .y = 15, .w = 10, .h = 10};
    vg_render_scene_clipped(&rect, &fb, clip);

    TEST_ASSERT_EQUAL_HEX16(0x0000u, pixels[(size_t)10 * TEST_W + 10]);
    size_t fill_in_clip = 0;
    for (int y = 15; y < 25; y++) {
        for (int x = 20; x < 30; x++) {
            if (pixels[y * TEST_W + x] != 0x0000u) fill_in_clip++;
        }
    }
    TEST_ASSERT_TRUE(fill_in_clip > 0);
    size_t outside_clip = count_not_color(pixels, TEST_W * TEST_H, 0x0000u) - fill_in_clip;
    TEST_ASSERT_EQUAL_size_t(0, outside_clip);
}

TEST(test_vector_scene_graph_filled_rect_deterministic_checksum) {
    uint16_t pixels_a[TEST_W * TEST_H];
    uint16_t pixels_b[TEST_W * TEST_H];
    VgFrameBuffer fb_a, fb_b;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb_a, TEST_W, TEST_H, pixels_a, TEST_W * TEST_H));
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb_b, TEST_W, TEST_H, pixels_b, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb_a, 0x0000u);
    vg_framebuffer_clear(&fb_b, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_rgb565 = 0xffffu;
    style.stroke_width = 1;
    style.has_fill = true;
    style.fill_rgb565 = 0x07e0u;

    VgNode rect = {
        .id = 1008,
        .type = VG_NODE_RECT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.rect = {.x = 8, .y = 6, .w = 25, .h = 18}
    };

    vg_render_scene(&rect, &fb_a);
    vg_render_scene(&rect, &fb_b);

    TEST_ASSERT_EQUAL_HEX32(vg_framebuffer_checksum(&fb_a), vg_framebuffer_checksum(&fb_b));
}

TEST(test_vector_scene_graph_filled_rect_from_clojure_records) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID scene = eval_string(
        "(do (require 'tiny-gfx.scene) "
        "  (defrecord Transform [tx ty sx sy rot]) "
        "  (defrecord Style [stroke_rgb565 stroke_width visible has_fill fill_rgb565 has_bg_rgb565 bg_rgb565]) "
        "  (defrecord Rect [id t style visible x y w h]) "
        "  (defrecord Group [id t style visible children]) "
        "  (defrecord Scene [root clip-rect erase-rgb565 collision-rules]) "
        "  (->Scene "
        "    (->Rect 1009 nil (->Style 65535 1 true true 2016 false 0) true 10 10 20 12) "
        "    nil nil nil))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(scene);

    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    TEST_ASSERT_TRUE(vg_render_scene_record(scene, &fb));

    size_t fill_count = count_color(pixels, TEST_W * TEST_H, 0x07e0u);
    size_t stroke_count = count_color(pixels, TEST_W * TEST_H, 0xffffu);
    TEST_ASSERT_TRUE(fill_count > 0);
    TEST_ASSERT_TRUE(stroke_count > 0);
}
