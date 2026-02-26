#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(TINYCLJ_WITH_MINIFB)
#include "vector_scene_graph.h"
#include "vector_scene_graph_records.h"
#include "tiny_gfx.h"
#include "builtins.h"
#include "value.h"
#include "runtime.h"
#include "record.h"
#include "event_loop.h"
#include "MiniFB.h"
#if defined(__APPLE__)
#include "host_viewer_macos_menu.h"
#endif

#define VIEW_W 320
#define VIEW_H 240
#define VIEW_DEFAULT_WINDOW_SCALE 2u
#define VIEWER_SLOT_COUNT 3
#define TARGET_FPS           60u

#define TERRAIN_SPEED_PXS  120    /* px/s  → 2 px/frame @60 */
#define OBSTACLE_SPEED_PXS 120    /* px/s  → 2 px/frame @60 */
#define PLAYER_BOB_HZ        5    /* Hz    → 1 LUT step/frame @60 (12 entries) */

#define TERRAIN_PPF  (TERRAIN_SPEED_PXS / TARGET_FPS)   /* 2 */
#define OBSTACLE_PPF (OBSTACLE_SPEED_PXS / TARGET_FPS)  /* 1 */

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
#if defined(__APPLE__)
    unsigned content_w = 0, content_h = 0;
    if (macos_viewer_get_content_size(&content_w, &content_h)) {
        set_letterbox_viewport(window, content_w, content_h);
        return;
    }
#endif
    set_letterbox_viewport(window, (unsigned)width, (unsigned)height);
}

static uint32_t rgb565_to_xrgb8888(uint16_t c) {
    uint32_t r = (uint32_t)((((c >> 11) & 0x1f) * 255) / 31);
    uint32_t g = (uint32_t)((((c >> 5) & 0x3f) * 255) / 63);
    uint32_t b = (uint32_t)(((c & 0x1f) * 255) / 31);
    return 0xff000000u | (r << 16) | (g << 8) | b;
}

static bool ensure_vector_scene_record_schema(EvalState *st) {
    return tiny_gfx_ensure_schema(st);
}

#define g_record_schema (*tiny_gfx_schema())

static ID create_record_from_slots(ID type_symbol, unsigned int field_count, ID *slots) {
    return tiny_gfx_create_record_from_slots(type_symbol, field_count, slots);
}

static ID make_transform_record(const VgNode *node) {
    if (!node || !node->has_transform) {
        return NULL;
    }
    ID *slots = STACK_ALLOC(ID, g_record_schema.n_transform);
    for (unsigned int i = 0; i < g_record_schema.n_transform; i++) slots[i] = NULL;
    slots[g_record_schema.transform_tx] = fixnum(node->transform.tx);
    slots[g_record_schema.transform_ty] = fixnum(node->transform.ty);
    slots[g_record_schema.transform_sx] = (ID)(((uintptr_t)node->transform.sx << TAG_BITS) | TAG_FIXED);
    slots[g_record_schema.transform_sy] = (ID)(((uintptr_t)node->transform.sy << TAG_BITS) | TAG_FIXED);
    slots[g_record_schema.transform_rot] = fixnum(node->transform.rot_deg);
    return create_record_from_slots(g_record_schema.t_transform, g_record_schema.n_transform, slots);
}

static ID make_style_record(VgStyle style) {
    ID *slots = STACK_ALLOC(ID, g_record_schema.n_style);
    for (unsigned int i = 0; i < g_record_schema.n_style; i++) slots[i] = NULL;
    slots[g_record_schema.style_stroke_rgb565] = fixnum((int)style.stroke_rgb565);
    slots[g_record_schema.style_stroke_width] = fixnum((int)style.stroke_width);
    slots[g_record_schema.style_visible] = style.visible ? clj_true : clj_false;
    slots[g_record_schema.style_has_fill] = style.has_fill ? clj_true : clj_false;
    slots[g_record_schema.style_fill_rgb565] = fixnum((int)style.fill_rgb565);
    slots[g_record_schema.style_has_bg_rgb565] = style.has_bg_rgb565 ? clj_true : clj_false;
    slots[g_record_schema.style_bg_rgb565] = fixnum((int)style.bg_rgb565);
    return create_record_from_slots(g_record_schema.t_style, g_record_schema.n_style, slots);
}

static ID make_node_record(const VgNode *node);

static ID make_group_children_vector(const VgNode *node) {
    CljPersistentVector *children_vec = make_vector((unsigned int)node->data.group.child_count, STRONG);
    if (!children_vec) return NULL;
    for (size_t i = 0; i < node->data.group.child_count; i++) {
        ID child = make_node_record(node->data.group.children[i]);
        if (!child) {
            RELEASE(children_vec);
            return NULL;
        }
        vector_conj_inplace(&children_vec, child);
    }
    return AUTORELEASE(children_vec);
}

static ID make_polyline_points_vector(const VgNode *node) {
    CljPersistentVector *pts = make_vector((unsigned int)node->data.polyline.point_count, STRONG);
    if (!pts) return NULL;
    for (size_t i = 0; i < node->data.polyline.point_count; i++) {
        CljPersistentVector *xy = make_vector(2, STRONG);
        if (!xy) {
            RELEASE(pts);
            return NULL;
        }
        vector_conj_inplace(&xy, fixnum((int)node->data.polyline.points[i].x));
        vector_conj_inplace(&xy, fixnum((int)node->data.polyline.points[i].y));
        vector_conj_inplace(&pts, xy);
        RELEASE(xy);
    }
    return AUTORELEASE(pts);
}

static ID make_node_record(const VgNode *node) {
    if (!node) return NULL;
    ID t_rec = make_transform_record(node);
    ID s_rec = make_style_record(node->style);
    ID visible = node->style.visible ? clj_true : clj_false;

    switch (node->type) {
        case VG_NODE_GROUP: {
            ID children = make_group_children_vector(node);
            ID *slots = STACK_ALLOC(ID, g_record_schema.n_group);
            for (unsigned int i = 0; i < g_record_schema.n_group; i++) slots[i] = NULL;
            slots[g_record_schema.group_id] = fixnum((int)node->id);
            slots[g_record_schema.group_t] = t_rec;
            slots[g_record_schema.group_style] = s_rec;
            slots[g_record_schema.group_visible] = visible;
            slots[g_record_schema.group_children] = children;
            return create_record_from_slots(g_record_schema.t_group, g_record_schema.n_group, slots);
        }
        case VG_NODE_LINE: {
            ID *slots = STACK_ALLOC(ID, g_record_schema.n_line);
            for (unsigned int i = 0; i < g_record_schema.n_line; i++) slots[i] = NULL;
            slots[g_record_schema.line_id] = fixnum((int)node->id);
            slots[g_record_schema.line_t] = t_rec;
            slots[g_record_schema.line_style] = s_rec;
            slots[g_record_schema.line_visible] = visible;
            slots[g_record_schema.line_x1] = fixnum((int)node->data.line.x1);
            slots[g_record_schema.line_y1] = fixnum((int)node->data.line.y1);
            slots[g_record_schema.line_x2] = fixnum((int)node->data.line.x2);
            slots[g_record_schema.line_y2] = fixnum((int)node->data.line.y2);
            return create_record_from_slots(g_record_schema.t_line, g_record_schema.n_line, slots);
        }
        case VG_NODE_POLYLINE: {
            ID pts = make_polyline_points_vector(node);
            ID *slots = STACK_ALLOC(ID, g_record_schema.n_polyline);
            for (unsigned int i = 0; i < g_record_schema.n_polyline; i++) slots[i] = NULL;
            slots[g_record_schema.poly_id] = fixnum((int)node->id);
            slots[g_record_schema.poly_t] = t_rec;
            slots[g_record_schema.poly_style] = s_rec;
            slots[g_record_schema.poly_visible] = visible;
            slots[g_record_schema.poly_pts] = pts;
            slots[g_record_schema.poly_closed] = node->data.polyline.closed ? clj_true : clj_false;
            return create_record_from_slots(g_record_schema.t_polyline, g_record_schema.n_polyline, slots);
        }
        case VG_NODE_RECT: {
            ID *slots = STACK_ALLOC(ID, g_record_schema.n_rect);
            for (unsigned int i = 0; i < g_record_schema.n_rect; i++) slots[i] = NULL;
            slots[g_record_schema.rect_id] = fixnum((int)node->id);
            slots[g_record_schema.rect_t] = t_rec;
            slots[g_record_schema.rect_style] = s_rec;
            slots[g_record_schema.rect_visible] = visible;
            slots[g_record_schema.rect_x] = fixnum((int)node->data.rect.x);
            slots[g_record_schema.rect_y] = fixnum((int)node->data.rect.y);
            slots[g_record_schema.rect_w] = fixnum((int)node->data.rect.w);
            slots[g_record_schema.rect_h] = fixnum((int)node->data.rect.h);
            return create_record_from_slots(g_record_schema.t_rect, g_record_schema.n_rect, slots);
        }
        case VG_NODE_TRI: {
            ID *slots = STACK_ALLOC(ID, g_record_schema.n_tri);
            for (unsigned int i = 0; i < g_record_schema.n_tri; i++) slots[i] = NULL;
            slots[g_record_schema.tri_id] = fixnum((int)node->id);
            slots[g_record_schema.tri_t] = t_rec;
            slots[g_record_schema.tri_style] = s_rec;
            slots[g_record_schema.tri_visible] = visible;
            slots[g_record_schema.tri_x1] = fixnum((int)node->data.tri.x1);
            slots[g_record_schema.tri_y1] = fixnum((int)node->data.tri.y1);
            slots[g_record_schema.tri_x2] = fixnum((int)node->data.tri.x2);
            slots[g_record_schema.tri_y2] = fixnum((int)node->data.tri.y2);
            slots[g_record_schema.tri_x3] = fixnum((int)node->data.tri.x3);
            slots[g_record_schema.tri_y3] = fixnum((int)node->data.tri.y3);
            return create_record_from_slots(g_record_schema.t_tri, g_record_schema.n_tri, slots);
        }
        case VG_NODE_VTEXT: {
            ID text = AUTORELEASE(make_string(node->data.text.text ? node->data.text.text : ""));
            ID *slots = STACK_ALLOC(ID, g_record_schema.n_vtext);
            for (unsigned int i = 0; i < g_record_schema.n_vtext; i++) slots[i] = NULL;
            slots[g_record_schema.text_id] = fixnum((int)node->id);
            slots[g_record_schema.text_t] = t_rec;
            slots[g_record_schema.text_style] = s_rec;
            slots[g_record_schema.text_visible] = visible;
            slots[g_record_schema.text_x] = fixnum((int)node->data.text.x);
            slots[g_record_schema.text_y] = fixnum((int)node->data.text.y);
            slots[g_record_schema.text_scale] = (ID)(((uintptr_t)node->data.text.scale << TAG_BITS) | TAG_FIXED);
            slots[g_record_schema.text_rot] = fixnum(node->data.text.rot_deg);
            slots[g_record_schema.text_text] = text;
            return create_record_from_slots(g_record_schema.t_vtext, g_record_schema.n_vtext, slots);
        }
        default:
            return NULL;
    }
}

static ID make_frame_scene_record(const VgNode *root,
                                   VgClipRect clip_rect,
                                   int z,
                                   bool visible,
                                   bool opaque,
                                   uint16_t erase_rgb565,
                                   uint8_t guard_px) {
    ID root_rec = make_node_record(root);
    if (!root_rec) return NULL;

    CljPersistentVector *clip_vec = make_vector(4, STRONG);
    if (!clip_vec) return NULL;
    vector_conj_inplace(&clip_vec, fixnum(clip_rect.x));
    vector_conj_inplace(&clip_vec, fixnum(clip_rect.y));
    vector_conj_inplace(&clip_vec, fixnum(clip_rect.w));
    vector_conj_inplace(&clip_vec, fixnum(clip_rect.h));

    ID *slots = STACK_ALLOC(ID, g_record_schema.n_frame_scene);
    for (unsigned int i = 0; i < g_record_schema.n_frame_scene; i++) slots[i] = NULL;
    slots[g_record_schema.frame_root] = root_rec;
    slots[g_record_schema.frame_clip_rect] = clip_vec;
    slots[g_record_schema.frame_z] = fixnum(z);
    slots[g_record_schema.frame_visible] = visible ? clj_true : clj_false;
    slots[g_record_schema.frame_opaque] = opaque ? clj_true : clj_false;
    slots[g_record_schema.frame_erase_rgb565] = fixnum((int)erase_rgb565);
    slots[g_record_schema.frame_guard_px] = fixnum((int)guard_px);
    ID scene = create_record_from_slots(g_record_schema.t_frame_scene, g_record_schema.n_frame_scene, slots);
    RELEASE(clip_vec);
    return scene;
}

static ID g_published_slots[VIEWER_SLOT_COUNT] = {0};
static uint32_t g_published_slot_generation[VIEWER_SLOT_COUNT] = {0};

static void publish_frame_scene_slot(size_t slot_index,
                                     const VgNode *root,
                                     VgClipRect clip_rect,
                                     int z,
                                     bool visible,
                                     bool opaque,
                                     uint16_t erase_rgb565,
                                     uint8_t guard_px) {
    if (slot_index >= VIEWER_SLOT_COUNT) {
        return;
    }
    WITH_AUTORELEASE_POOL({
        ID scene = make_frame_scene_record(root, clip_rect, z, visible, opaque, erase_rgb565, guard_px);
        if (scene) {
            RETAIN(scene);
            ASSIGN(g_published_slots[slot_index], scene);
            g_published_slot_generation[slot_index]++;
            RELEASE(scene);
        }
    });
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
    runtime_init(&g_runtime);
    event_loop_init();
    EvalState *viewer_eval_state = evalstate_new(true);
    if (!viewer_eval_state) {
        fprintf(stderr, "Failed to initialize eval state\n");
        return 1;
    }
    evalstate_set_ns(viewer_eval_state, "user");
    if (!ensure_vector_scene_record_schema(viewer_eval_state)) {
        fprintf(stderr, "Failed to initialize vector scene record schema via tiny-gfx.scene\n");
        return 1;
    }

#if defined(__APPLE__)
    macos_viewer_install_menu();
#endif
    const unsigned default_win_w = VIEW_W * VIEW_DEFAULT_WINDOW_SCALE;
    const unsigned default_win_h = VIEW_H * VIEW_DEFAULT_WINDOW_SCALE;
    struct mfb_window *window = mfb_open_ex("tiny-clj host viewer", default_win_w, default_win_h, WF_RESIZABLE);
    if (!window) {
        fprintf(stderr, "Failed to open MiniFB window\n");
        return 1;
    }
#if defined(__APPLE__)
    macos_viewer_register_window_callbacks();
    macos_viewer_restore_window_position();
#endif
    mfb_show_cursor(window, true);
    mfb_set_resize_callback(window, on_window_resize);
#if defined(__APPLE__)
    {
        unsigned content_w = 0, content_h = 0;
        if (macos_viewer_get_content_size(&content_w, &content_h)) {
            set_letterbox_viewport(window, content_w, content_h);
        } else {
            set_letterbox_viewport(window, default_win_w, default_win_h);
        }
    }
#else
    set_letterbox_viewport(window, default_win_w, default_win_h);
#endif
    struct mfb_timer *timer = mfb_timer_create();
    if (!timer) {
        fprintf(stderr, "Failed to create MiniFB timer\n");
        mfb_close(window);
        return 1;
    }
    double fps_window_start_s = mfb_timer_now(timer);
    unsigned fps_frame_count = 0;

    VgStyle deco_style = vg_style_default();
    deco_style.stroke_rgb565 = 0x07ffu;
    deco_style.stroke_width = 2;
    deco_style.has_bg_rgb565 = false;

    VgStyle score_style = vg_style_default();
    score_style.stroke_rgb565 = 0xffffu;
    score_style.stroke_width = 1;
    score_style.has_bg_rgb565 = false;

    VgStyle game_line_style = vg_style_default();
    game_line_style.stroke_rgb565 = 0x07e0u;
    game_line_style.stroke_width = 2;
    game_line_style.has_bg_rgb565 = false;

    VgStyle game_player_style = vg_style_default();
    game_player_style.stroke_rgb565 = 0xf81fu;
    game_player_style.stroke_width = 3;
    game_player_style.has_bg_rgb565 = false;

    VgStyle game_obstacle_style = vg_style_default();
    game_obstacle_style.stroke_rgb565 = 0xffe0u;
    game_obstacle_style.stroke_width = 2;
    game_obstacle_style.has_fill = true;
    game_obstacle_style.fill_rgb565 = 0xffe0u;
    game_obstacle_style.has_bg_rgb565 = false;
    VgStyle game_obstacle_nose_style = vg_style_default();
    game_obstacle_nose_style.stroke_rgb565 = 0xf800u;
    game_obstacle_nose_style.stroke_width = 2;
    game_obstacle_nose_style.has_fill = true;
    game_obstacle_nose_style.fill_rgb565 = 0xf800u;
    game_obstacle_nose_style.has_bg_rgb565 = false;

    VgClipRect score_clip = {.x = 0, .y = 0, .w = VIEW_W, .h = 32};
    VgClipRect game_clip = {.x = 0, .y = 40, .w = VIEW_W, .h = 136};
    VgClipRect deco_clip = {.x = 0, .y = 184, .w = VIEW_W, .h = 56};

    VgPoint mountain_pts[] = {
        {0, 228}, {26, 206}, {58, 226}, {92, 196}, {126, 225},
        {162, 202}, {198, 230}, {238, 192}, {276, 226}, {319, 204}
    };
    VgNode mountains = {
        .id = 1001,
        .type = VG_NODE_POLYLINE,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = deco_style,
        .data.polyline = {.points = mountain_pts, .point_count = sizeof(mountain_pts) / sizeof(mountain_pts[0]), .closed = false}
    };
    VgNode deco_horizon = {
        .id = 1002,
        .type = VG_NODE_LINE,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = deco_style,
        .data.line = {.x1 = 0, .y1 = 236, .x2 = 319, .y2 = 236}
    };
    VgNode *deco_children[] = {&mountains, &deco_horizon};
    VgNode deco_root = {
        .id = 1000,
        .type = VG_NODE_GROUP,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = vg_style_default(),
        .data.group = {.children = deco_children, .child_count = sizeof(deco_children) / sizeof(deco_children[0])}
    };

    char score_line[64];
    (void)snprintf(score_line, sizeof(score_line), "SCORE 0000    LIFES 3");
    VgNode score_text = {
        .id = 2001,
        .type = VG_NODE_VTEXT,
        .has_transform = true,
        .transform = vg_transform_identity(),
        .style = score_style,
        .data.text = {.x = 0, .y = 0, .scale = VG_SCALE_ONE, .rot_deg = 0, .text = score_line}
    };
    score_text.transform.tx = 6;
    score_text.transform.ty = 10;
    VgNode *score_children[] = {&score_text};
    VgNode score_root = {
        .id = 2000,
        .type = VG_NODE_GROUP,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = vg_style_default(),
        .data.group = {.children = score_children, .child_count = 1}
    };

    VgPoint terrain_pts[] = {
        {0, 156}, {60, 156}, {100, 144}, {150, 156}, {220, 156}, {260, 146}, {319, 156},
        {380, 156}, {420, 146}, {480, 156}, {560, 156}, {620, 144}
    };
    VgNode game_terrain = {
        .id = 3001,
        .type = VG_NODE_POLYLINE,
        .has_transform = true,
        .transform = vg_transform_identity(),
        .style = game_line_style,
        .data.polyline = {.points = terrain_pts, .point_count = sizeof(terrain_pts) / sizeof(terrain_pts[0]), .closed = false}
    };
    VgNode game_player = {
        .id = 3002,
        .type = VG_NODE_TRI,
        .has_transform = true,
        .transform = vg_transform_identity(),
        .style = game_player_style,
        .data.tri = {.x1 = 56, .y1 = 146, .x2 = 72, .y2 = 118, .x3 = 88, .y3 = 146}
    };
    /* Missile silhouette: original right-facing geometry, oriented upward via transform rotation. */
    VgPoint missile_body_pts[] = {
        {-12, 3}, {8, 3}, {8, -3}, {-12, -3},
        {-16, -7}, {-20, -7}, {-20, -2}, {-13, -2},
        {-13, -1}, {-20, -1}, {-20, 1}, {-13, 1},
        {-13, 2}, {-20, 2}, {-20, 7}, {-16, 7}
    };
    VgNode game_obstacle_body = {
        .id = 3003,
        .type = VG_NODE_POLYLINE,
        .has_transform = true,
        .transform = vg_transform_identity(),
        .style = game_obstacle_style,
        .data.polyline = {.points = missile_body_pts, .point_count = sizeof(missile_body_pts) / sizeof(missile_body_pts[0]), .closed = true}
    };
    VgNode game_obstacle_nose = {
        .id = 3005,
        .type = VG_NODE_TRI,
        .has_transform = true,
        .transform = vg_transform_identity(),
        .style = game_obstacle_nose_style,
        .data.tri = {.x1 = 20, .y1 = 0, .x2 = 10, .y2 = -3, .x3 = 10, .y3 = 3}
    };
    VgNode game_caption = {
        .id = 3004,
        .type = VG_NODE_VTEXT,
        .has_transform = true,
        .transform = vg_transform_identity(),
        .style = score_style,
        .data.text = {.x = 0, .y = 0, .scale = VG_SCALE_ONE, .rot_deg = 0, .text = "GAME SCENE"}
    };
    game_caption.transform.tx = 96;
    game_caption.transform.ty = 52;
    VgNode *game_children[] = {&game_terrain, &game_player, &game_obstacle_body, &game_obstacle_nose, &game_caption};
    VgNode game_root = {
        .id = 3000,
        .type = VG_NODE_GROUP,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = vg_style_default(),
        .data.group = {.children = game_children, .child_count = sizeof(game_children) / sizeof(game_children[0])}
    };

    VgRenderSlotState slot_states[VIEWER_SLOT_COUNT] = {0};
    bool player_small = false;
    bool collision_latched = false;
    float collision_cooldown_end_s = 0.0f;
    unsigned frame_count = 0;
    static const int player_bob_lut[] = {0, -1, -3, -5, -4, -2, 0, 1, 0, -1, -2, -1};
    vg_framebuffer_clear(&fb, 0x0000u);
    publish_frame_scene_slot(0, &deco_root, deco_clip, 0, true, true, 0x0000u, 1);
    publish_frame_scene_slot(1, &score_root, score_clip, 1, true, true, 0x0000u, 1);
    publish_frame_scene_slot(2, &game_root, game_clip, 2, true, true, 0x0000u, 1);

    while (true) {
        float time_s = (float)mfb_timer_now(timer);
        fps_frame_count++;
        double fps_elapsed_s = (double)time_s - fps_window_start_s;
        bool score_changed = false;
#if defined(__APPLE__)
        if (fps_elapsed_s >= 1.0) {
            double fps = (double)fps_frame_count / fps_elapsed_s;
            int score = (int)(time_s * 120.0f);
            (void)snprintf(score_line, sizeof(score_line), "SCORE %04d    LIFES 3", score % 10000);
            char title[96];
            (void)snprintf(title, sizeof(title), "tiny-clj host viewer - %.1f FPS", fps);
            macos_viewer_set_window_title(title);
            fps_window_start_s = (double)time_s;
            fps_frame_count = 0;
            score_changed = true;
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

        frame_count++;
        bool collision_cooldown_active = (time_s < collision_cooldown_end_s);
        unsigned bob_lut_len = (unsigned)(sizeof(player_bob_lut) / sizeof(player_bob_lut[0]));
        int terrain_scroll_px = (int)((frame_count * TERRAIN_PPF) % 320u);
        int player_bob_y = player_bob_lut[frame_count % bob_lut_len];
        int obstacle_x = 319 - (int)((frame_count * OBSTACLE_PPF) % 360u);

        game_terrain.transform = vg_transform_identity();
        game_terrain.transform.tx = (int16_t)(-terrain_scroll_px);
        game_player.transform = vg_transform_identity();
        game_player.transform.ty = (int16_t)player_bob_y;
        game_obstacle_body.transform = vg_transform_identity();
        game_obstacle_body.transform.tx = (int16_t)(obstacle_x + 20);
        game_obstacle_body.transform.ty = 126;
        game_obstacle_body.transform.rot_deg = -90;
        game_obstacle_nose.transform = vg_transform_identity();
        game_obstacle_nose.transform.tx = (int16_t)(obstacle_x + 20);
        game_obstacle_nose.transform.ty = 126;
        game_obstacle_nose.transform.rot_deg = -90;

        if (player_small) {
            game_player.data.tri.x1 = 60;
            game_player.data.tri.y1 = 146;
            game_player.data.tri.x2 = 72;
            game_player.data.tri.y2 = 126;
            game_player.data.tri.x3 = 84;
            game_player.data.tri.y3 = 146;
        } else {
            game_player.data.tri.x1 = 56;
            game_player.data.tri.y1 = 146;
            game_player.data.tri.x2 = 72;
            game_player.data.tri.y2 = 118;
            game_player.data.tri.x3 = 88;
            game_player.data.tri.y3 = 146;
        }

        {
            // Collision uses a stable hitbox to avoid size-toggle feedback jitter.
            int player_min_x = 58;
            int player_max_x = 86;
            int player_min_y = 124 + player_bob_y;
            int player_max_y = 146 + player_bob_y;
            int obstacle_min_x_i = 13 + obstacle_x;
            int obstacle_max_x_i = 27 + obstacle_x;
            int obstacle_min_y_i = 106;
            int obstacle_max_y_i = 146;
            bool colliding = (player_max_x >= obstacle_min_x_i) && (player_min_x <= obstacle_max_x_i) &&
                             (player_max_y >= obstacle_min_y_i) && (player_min_y <= obstacle_max_y_i);

            if (colliding && !collision_latched && !collision_cooldown_active) {
                player_small = !player_small;
                collision_latched = true;
                collision_cooldown_end_s = time_s + 0.3f;
            } else if (!colliding) {
                collision_latched = false;
            }
        }

        if (score_changed) {
            publish_frame_scene_slot(1, &score_root, score_clip, 1, true, true, 0x0000u, 1);
        }
        publish_frame_scene_slot(2, &game_root, game_clip, 2, true, true, 0x0000u, 1);

        for (size_t i = 0; i < VIEWER_SLOT_COUNT; i++) {
            if (g_published_slots[i]) {
                (void)vg_render_frame_slot_record_if_changed(g_published_slots[i], &slot_states[i], &fb, g_published_slot_generation[i]);
            }
        }

        for (size_t i = 0; i < (size_t)VIEW_W * (size_t)VIEW_H; i++) {
            window_pixels[i] = rgb565_to_xrgb8888(fb_pixels[i]);
        }

        mfb_update_state state = mfb_update_ex(window, window_pixels, VIEW_W, VIEW_H);
        if (state != STATE_OK) {
            break;
        }
    }

    mfb_timer_destroy(timer);
#if defined(__APPLE__)
    macos_viewer_save_window_position();
#endif
    mfb_close(window);
    for (size_t i = 0; i < VIEWER_SLOT_COUNT; i++) {
        RELEASE(g_published_slots[i]);
        g_published_slots[i] = NULL;
    }
    runtime_reset(&g_runtime);
    return 0;
#endif
}
