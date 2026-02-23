#include "tests_common.h"
#include "../vector_scene_graph.h"

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
    group_t.tx = 7.0f;
    group_t.ty = 5.0f;
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
        .data.text = {.x = 34, .y = 30, .scale = 1.0f, .rot_deg = 0.0f, .text = "HI"}
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
        .data.text = {.x = 2, .y = 4, .scale = 1.0f, .rot_deg = 0.0f, .text = "A"}
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
    tpatch.value.transform.tx = 20.0f;
    tpatch.value.transform.ty = -8.0f;
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
        .data.text = {.x = 0, .y = 0, .scale = 1.0f, .rot_deg = 0.0f, .text = "A"}
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
        .data.text = {.x = 2, .y = 2, .scale = 1.0f, .rot_deg = 0.0f, .text = "MWXYR"}
    };

    vg_render_scene(&text, &fb);

    size_t min_x = 0, min_y = 0, max_x = 0, max_y = 0;
    TEST_ASSERT_TRUE(find_non_bg_bounds(pixels, TEST_W, TEST_H, 0x0000u, &min_x, &min_y, &max_x, &max_y));

    size_t height = max_y - min_y + 1;
    // Regression intent: avoid accidentally squashing glyphs to half height.
    TEST_ASSERT_TRUE(height >= 7);
}
