#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(TINYCLJ_WITH_MINIFB)
#include "vector_scene_graph.h"
#include "scene.h"
#include "tiny_gfx.h"
#include "builtins.h"
#include "value.h"
#include "runtime.h"
#include "record.h"
#include "event_loop.h"
#include "atom.h"
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
#define PLAYER_JUMP_HEIGHT_PX      10u  /* double previous bob peak (5px -> 10px) */
#define PLAYER_JUMP_DURATION_FRAMES 16u
#define PLAYER_JUMP_PERIOD_FRAMES   48u

#define TERRAIN_PPF  (TERRAIN_SPEED_PXS / TARGET_FPS)   /* 2 */
#define OBSTACLE_PPF (OBSTACLE_SPEED_PXS / TARGET_FPS)  /* 2 */

static int q13_to_int_round(int32_t v_q13) {
    if (v_q13 >= 0) {
        return (int)((v_q13 + (VG_SCALE_ONE / 2)) / VG_SCALE_ONE);
    }
    return (int)((v_q13 - (VG_SCALE_ONE / 2)) / VG_SCALE_ONE);
}

static int compute_player_jump_y(unsigned frame_count) {
    uint32_t jump_half_frames = (PLAYER_JUMP_DURATION_FRAMES > 1u) ? (PLAYER_JUMP_DURATION_FRAMES / 2u) : 1u;
    uint32_t jump_phase = frame_count % PLAYER_JUMP_PERIOD_FRAMES;
    int32_t jump_y_q13 = 0;

    if (jump_phase < PLAYER_JUMP_DURATION_FRAMES) {
        int32_t jump_t = 0;
        int32_t jump_eased = 0;
        if (jump_phase < jump_half_frames) {
            jump_t = vg_anim_progress_q13(jump_phase, jump_half_frames);
            jump_eased = vg_anim_ease_q13(VG_ANIM_EASE_OUT_QUAD, jump_t);
            jump_y_q13 = vg_anim_lerp_q13(0,
                                          -(int32_t)(PLAYER_JUMP_HEIGHT_PX * VG_SCALE_ONE),
                                          jump_eased);
        } else {
            jump_t = vg_anim_progress_q13(jump_phase - jump_half_frames, jump_half_frames);
            jump_eased = vg_anim_ease_q13(VG_ANIM_EASE_IN_QUAD, jump_t);
            jump_y_q13 = vg_anim_lerp_q13(-(int32_t)(PLAYER_JUMP_HEIGHT_PX * VG_SCALE_ONE),
                                          0,
                                          jump_eased);
        }
    }

    return q13_to_int_round(jump_y_q13);
}

static int compute_obstacle_x(unsigned frame_count) {
    uint32_t obstacle_phase_px = (frame_count * OBSTACLE_PPF) % 360u;
    int32_t obstacle_t = vg_anim_progress_q13(obstacle_phase_px, 360u);
    /* Keep behavior equivalent to the old constant-speed motion; linear easing is intentional. */
    int32_t obstacle_x_q13 = vg_anim_lerp_q13(319 * VG_SCALE_ONE,
                                              (319 - 360) * VG_SCALE_ONE,
                                              vg_anim_ease_q13(VG_ANIM_EASE_LINEAR, obstacle_t));
    return q13_to_int_round(obstacle_x_q13);
}

static bool viewer_should_exit_for_keys(const uint8_t *keys) {
    if (!keys) {
        return false;
    }
    bool esc = keys[KB_KEY_ESCAPE] != 0;
    bool cmd_q = (keys[KB_KEY_Q] != 0) &&
                 ((keys[KB_KEY_LEFT_SUPER] != 0) || (keys[KB_KEY_RIGHT_SUPER] != 0));
    return esc || cmd_q;
}

static void set_player_geometry(VgNode *game_player, bool player_small) {
    if (!game_player) {
        return;
    }
    if (player_small) {
        game_player->data.tri.x1 = 60;
        game_player->data.tri.y1 = 146;
        game_player->data.tri.x2 = 72;
        game_player->data.tri.y2 = 126;
        game_player->data.tri.x3 = 84;
        game_player->data.tri.y3 = 146;
    } else {
        game_player->data.tri.x1 = 56;
        game_player->data.tri.y1 = 146;
        game_player->data.tri.x2 = 72;
        game_player->data.tri.y2 = 118;
        game_player->data.tri.x3 = 88;
        game_player->data.tri.y3 = 146;
    }
}

static void set_obstacle_transforms(VgNode *game_obstacle_body, VgNode *game_obstacle_nose, int obstacle_x) {
    if (!game_obstacle_body || !game_obstacle_nose) {
        return;
    }
    game_obstacle_body->transform = vg_transform_identity();
    game_obstacle_body->transform.tx = (int16_t)(obstacle_x + 20);
    game_obstacle_body->transform.ty = 126;
    game_obstacle_body->transform.rot_deg = -90;

    game_obstacle_nose->transform = vg_transform_identity();
    game_obstacle_nose->transform.tx = (int16_t)(obstacle_x + 20);
    game_obstacle_nose->transform.ty = 126;
    game_obstacle_nose->transform.rot_deg = -90;
}

static bool detect_player_obstacle_collision(int player_jump_y, int obstacle_x) {
    // Collision uses a stable hitbox to avoid size-toggle feedback jitter.
    int player_min_x = 58;
    int player_max_x = 86;
    int player_min_y = 124 + player_jump_y;
    int player_max_y = 146 + player_jump_y;
    int obstacle_min_x_i = 13 + obstacle_x;
    int obstacle_max_x_i = 27 + obstacle_x;
    int obstacle_min_y_i = 106;
    int obstacle_max_y_i = 146;
    return (player_max_x >= obstacle_min_x_i) && (player_min_x <= obstacle_max_x_i) &&
           (player_max_y >= obstacle_min_y_i) && (player_min_y <= obstacle_max_y_i);
}

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

#define g_record_schema (*tiny_gfx_schema())

static CljPersistentRecord *make_record_with_descriptor(CljRecordDescriptor *desc) {
    CljPersistentRecord *record = record_create_with_descriptor(desc, NULL);
    if (!record) {
        return NULL;
    }
    return AUTORELEASE(record);
}

static ID make_transform_record(const VgNode *node) {
    if (!node || !node->has_transform) {
        return NULL;
    }
    Transform *record = (Transform *)make_record_with_descriptor(g_record_schema.d_transform);
    if (!record) {
        return NULL;
    }
    ASSIGN(record->tx, fixnum(node->transform.tx));
    ASSIGN(record->ty, fixnum(node->transform.ty));
    ASSIGN(record->sx, (ID)(((uintptr_t)node->transform.sx << TAG_BITS) | TAG_FIXED));
    ASSIGN(record->sy, (ID)(((uintptr_t)node->transform.sy << TAG_BITS) | TAG_FIXED));
    ASSIGN(record->rot, fixnum(node->transform.rot_deg));
    return record;
}

static ID make_style_record(VgStyle style) {
    Style *record = (Style *)make_record_with_descriptor(g_record_schema.d_style);
    if (!record) {
        return NULL;
    }
    ASSIGN(record->stroke_rgb565, fixnum((int)style.stroke_rgb565));
    ASSIGN(record->stroke_width, fixnum((int)style.stroke_width));
    ASSIGN(record->visible, style.visible ? clj_true : clj_false);
    ASSIGN(record->has_fill, style.has_fill ? clj_true : clj_false);
    ASSIGN(record->fill_rgb565, fixnum((int)style.fill_rgb565));
    ASSIGN(record->has_bg_rgb565, style.has_bg_rgb565 ? clj_true : clj_false);
    ASSIGN(record->bg_rgb565, fixnum((int)style.bg_rgb565));
    return record;
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
            Group *record = (Group *)make_record_with_descriptor(g_record_schema.d_group);
            if (!record) return NULL;
            ASSIGN(record->id, fixnum((int)node->id));
            ASSIGN(record->t, t_rec);
            ASSIGN(record->style, s_rec);
            ASSIGN(record->visible, visible);
            ASSIGN(record->children, children);
            return record;
        }
        case VG_NODE_LINE: {
            Line *record = (Line *)make_record_with_descriptor(g_record_schema.d_line);
            if (!record) return NULL;
            ASSIGN(record->id, fixnum((int)node->id));
            ASSIGN(record->t, t_rec);
            ASSIGN(record->style, s_rec);
            ASSIGN(record->visible, visible);
            ASSIGN(record->x1, fixnum((int)node->data.line.x1));
            ASSIGN(record->y1, fixnum((int)node->data.line.y1));
            ASSIGN(record->x2, fixnum((int)node->data.line.x2));
            ASSIGN(record->y2, fixnum((int)node->data.line.y2));
            return record;
        }
        case VG_NODE_POLYLINE: {
            ID pts = make_polyline_points_vector(node);
            Polyline *record = (Polyline *)make_record_with_descriptor(g_record_schema.d_polyline);
            if (!record) return NULL;
            ASSIGN(record->id, fixnum((int)node->id));
            ASSIGN(record->t, t_rec);
            ASSIGN(record->style, s_rec);
            ASSIGN(record->visible, visible);
            ASSIGN(record->pts, pts);
            ASSIGN(record->closed, node->data.polyline.closed ? clj_true : clj_false);
            return record;
        }
        case VG_NODE_RECT: {
            Rect *record = (Rect *)make_record_with_descriptor(g_record_schema.d_rect);
            if (!record) return NULL;
            ASSIGN(record->id, fixnum((int)node->id));
            ASSIGN(record->t, t_rec);
            ASSIGN(record->style, s_rec);
            ASSIGN(record->visible, visible);
            ASSIGN(record->x, fixnum((int)node->data.rect.x));
            ASSIGN(record->y, fixnum((int)node->data.rect.y));
            ASSIGN(record->w, fixnum((int)node->data.rect.w));
            ASSIGN(record->h, fixnum((int)node->data.rect.h));
            return record;
        }
        case VG_NODE_TRI: {
            Tri *record = (Tri *)make_record_with_descriptor(g_record_schema.d_tri);
            if (!record) return NULL;
            ASSIGN(record->id, fixnum((int)node->id));
            ASSIGN(record->t, t_rec);
            ASSIGN(record->style, s_rec);
            ASSIGN(record->visible, visible);
            ASSIGN(record->x1, fixnum((int)node->data.tri.x1));
            ASSIGN(record->y1, fixnum((int)node->data.tri.y1));
            ASSIGN(record->x2, fixnum((int)node->data.tri.x2));
            ASSIGN(record->y2, fixnum((int)node->data.tri.y2));
            ASSIGN(record->x3, fixnum((int)node->data.tri.x3));
            ASSIGN(record->y3, fixnum((int)node->data.tri.y3));
            return record;
        }
        case VG_NODE_VTEXT: {
            ID text = AUTORELEASE(make_string(node->data.text.text ? node->data.text.text : ""));
            VText *record = (VText *)make_record_with_descriptor(g_record_schema.d_vtext);
            if (!record) return NULL;
            ASSIGN(record->id, fixnum((int)node->id));
            ASSIGN(record->t, t_rec);
            ASSIGN(record->style, s_rec);
            ASSIGN(record->visible, visible);
            ASSIGN(record->x, fixnum((int)node->data.text.x));
            ASSIGN(record->y, fixnum((int)node->data.text.y));
            ASSIGN(record->scale, (ID)(((uintptr_t)node->data.text.scale << TAG_BITS) | TAG_FIXED));
            ASSIGN(record->rot, fixnum(node->data.text.rot_deg));
            ASSIGN(record->text, text);
            return record;
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

    FrameScene *scene = (FrameScene *)make_record_with_descriptor(g_record_schema.d_frame_scene);
    if (!scene) {
        RELEASE(clip_vec);
        return NULL;
    }
    ASSIGN(scene->root, root_rec);
    ASSIGN(scene->clip_rect, clip_vec);
    ASSIGN(scene->z, fixnum(z));
    ASSIGN(scene->visible, visible ? clj_true : clj_false);
    ASSIGN(scene->opaque, opaque ? clj_true : clj_false);
    ASSIGN(scene->erase_rgb565, fixnum((int)erase_rgb565));
    ASSIGN(scene->guard_px, fixnum((int)guard_px));
    RELEASE(clip_vec);
    return scene;
}

static CljAtom *g_scene_slot_atoms[VIEWER_SLOT_COUNT] = {0};
static VgSlotChangeTracker g_slot_change_tracker;

static bool init_scene_slot_atoms(void) {
    for (size_t i = 0; i < VIEWER_SLOT_COUNT; i++) {
        g_scene_slot_atoms[i] = make_atom(NULL);
        if (!g_scene_slot_atoms[i]) {
            for (size_t j = 0; j < i; j++) {
                RELEASE(g_scene_slot_atoms[j]);
                g_scene_slot_atoms[j] = NULL;
            }
            return false;
        }
    }
    return true;
}

static void destroy_scene_slot_atoms(void) {
    for (size_t i = 0; i < VIEWER_SLOT_COUNT; i++) {
        RELEASE(g_scene_slot_atoms[i]);
        g_scene_slot_atoms[i] = NULL;
    }
}

static ID slot_atom_snapshot_owned(size_t slot_index) {
    if (slot_index >= VIEWER_SLOT_COUNT || !g_scene_slot_atoms[slot_index]) {
        return NULL;
    }
    return RETAIN(g_scene_slot_atoms[slot_index]->value);
}

static void render_changed_slot_records(VgFrameBuffer *fb,
                                        VgRenderSlotState *slot_states,
                                        uint32_t *slot_seen_generations) {
    if (!fb || !slot_states || !slot_seen_generations) {
        return;
    }
    uint32_t slot_generations[VIEWER_SLOT_COUNT] = {0};
    uint32_t changed_mask = vg_slot_change_tracker_wait_for_changes(&g_slot_change_tracker,
                                                                    slot_seen_generations,
                                                                    slot_generations,
                                                                    0u);
    for (size_t i = 0; i < VIEWER_SLOT_COUNT; i++) {
        if ((changed_mask & (1u << i)) == 0u) {
            continue;
        }
        ID snapshot = slot_atom_snapshot_owned(i);
        if (snapshot) {
            (void)vg_render_frame_slot_record_if_changed(snapshot, &slot_states[i], fb, slot_generations[i]);
            RELEASE(snapshot);
        }
    }
    memcpy(slot_seen_generations, slot_generations, sizeof(slot_generations));
}

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
            (void)atom_reset(g_scene_slot_atoms[slot_index], scene);
            (void)vg_slot_change_tracker_publish(&g_slot_change_tracker, (uint8_t)slot_index, NULL);
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
    struct mfb_window *window = NULL;
    struct mfb_timer *timer = NULL;
    bool slot_atoms_initialized = false;
    bool slot_tracker_initialized = false;
    int exit_code = 1;

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
        goto cleanup;
    }
    evalstate_set_ns(viewer_eval_state, "user");
    if (!tiny_gfx_ensure_schema(viewer_eval_state)) {
        fprintf(stderr, "Failed to initialize vector scene record schema via tiny-gfx.scene\n");
        goto cleanup;
    }
    if (!init_scene_slot_atoms()) {
        fprintf(stderr, "Failed to initialize scene slot atoms\n");
        goto cleanup;
    }
    slot_atoms_initialized = true;
    if (!vg_slot_change_tracker_init(&g_slot_change_tracker, VIEWER_SLOT_COUNT)) {
        fprintf(stderr, "Failed to initialize slot change tracker\n");
        goto cleanup;
    }
    slot_tracker_initialized = true;

#if defined(__APPLE__)
    macos_viewer_install_menu();
#endif
    const unsigned default_win_w = VIEW_W * VIEW_DEFAULT_WINDOW_SCALE;
    const unsigned default_win_h = VIEW_H * VIEW_DEFAULT_WINDOW_SCALE;
    window = mfb_open_ex("tiny-clj host viewer", default_win_w, default_win_h, WF_RESIZABLE);
    if (!window) {
        fprintf(stderr, "Failed to open MiniFB window\n");
        goto cleanup;
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
    timer = mfb_timer_create();
    if (!timer) {
        fprintf(stderr, "Failed to create MiniFB timer\n");
        goto cleanup;
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
    uint32_t slot_seen_generations[VIEWER_SLOT_COUNT] = {0};
    bool player_small = false;
    bool collision_latched = false;
    float collision_cooldown_end_s = 0.0f;
    unsigned frame_count = 0;
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
        if (viewer_should_exit_for_keys(mfb_get_key_buffer(window))) {
            break;
        }

        frame_count++;
        bool collision_cooldown_active = (time_s < collision_cooldown_end_s);
        int terrain_scroll_px = (int)((frame_count * TERRAIN_PPF) % 320u);
        int player_jump_y = compute_player_jump_y(frame_count);
        int obstacle_x = compute_obstacle_x(frame_count);

        game_terrain.transform = vg_transform_identity();
        game_terrain.transform.tx = (int16_t)(-terrain_scroll_px);
        game_player.transform = vg_transform_identity();
        game_player.transform.ty = (int16_t)player_jump_y;
        set_obstacle_transforms(&game_obstacle_body, &game_obstacle_nose, obstacle_x);
        set_player_geometry(&game_player, player_small);

        {
            bool colliding = detect_player_obstacle_collision(player_jump_y, obstacle_x);

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
        render_changed_slot_records(&fb, slot_states, slot_seen_generations);

        for (size_t i = 0; i < (size_t)VIEW_W * (size_t)VIEW_H; i++) {
            window_pixels[i] = rgb565_to_xrgb8888(fb_pixels[i]);
        }

        mfb_update_state state = mfb_update_ex(window, window_pixels, VIEW_W, VIEW_H);
        if (state != STATE_OK) {
            break;
        }
    }

    exit_code = 0;

cleanup:
    if (timer) {
        mfb_timer_destroy(timer);
    }
#if defined(__APPLE__)
    if (window) {
        macos_viewer_save_window_position();
    }
#endif
    if (window) {
        mfb_close(window);
    }
    if (slot_atoms_initialized) {
        destroy_scene_slot_atoms();
    }
    if (slot_tracker_initialized) {
        vg_slot_change_tracker_destroy(&g_slot_change_tracker);
    }
    runtime_reset(&g_runtime);
    return exit_code;
#endif
}
