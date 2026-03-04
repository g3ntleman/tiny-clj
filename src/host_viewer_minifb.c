#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <time.h>
#include <sched.h>
#if defined(__APPLE__)
#include <pthread/qos.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <mach/thread_act.h>
#include <mach/mach_init.h>
#endif

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
#define VIEWER_SLOT_DECO  0u
#define VIEWER_SLOT_SCORE 1u
#define VIEWER_SLOT_GAME  2u
#define TARGET_FPS           60u
#define SCENE_TARGET_FPS     TARGET_FPS
#define SCENE_ERASE_RGB565   0x0000u
#define RGB565_BYTES_PER_PIXEL 2u

#if (TARGET_FPS % SCENE_TARGET_FPS) != 0
#error "SCENE_TARGET_FPS must divide TARGET_FPS"
#endif
#define SCENE_FRAME_DIVIDER (TARGET_FPS / SCENE_TARGET_FPS)

#define TERRAIN_SPEED_PXS  120    /* px/s  → 2 px/frame @60 */
#define OBSTACLE_SPEED_PXS 120    /* px/s  → 2 px/frame @60 */
#define PLAYER_JUMP_HEIGHT_PX      10u  /* double previous bob peak (5px -> 10px) */
#define PLAYER_JUMP_DURATION_FRAMES 16u
#define PLAYER_JUMP_PERIOD_FRAMES   48u

#define TERRAIN_PPF  (TERRAIN_SPEED_PXS / TARGET_FPS)   /* 2 */
#define OBSTACLE_PPF (OBSTACLE_SPEED_PXS / TARGET_FPS)  /* 2 */

/* Convert engine q13 fixed-point to nearest integer for pixel-space updates. */
static int q13_to_int_round(int32_t v_q13) {
    if (v_q13 >= 0) {
        return (int)((v_q13 + (VG_SCALE_ONE / 2)) / VG_SCALE_ONE);
    }
    return (int)((v_q13 - (VG_SCALE_ONE / 2)) / VG_SCALE_ONE);
}

/* Runs one jump cycle with fixed-point easing and returns y offset in pixels. */
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

/* Scrolls the obstacle from right to left at constant speed. */
static int compute_obstacle_x(unsigned frame_count) {
    uint32_t obstacle_phase_px = (frame_count * OBSTACLE_PPF) % 360u;
    int32_t obstacle_t = vg_anim_progress_q13(obstacle_phase_px, 360u);
    /* Keep behavior equivalent to the old constant-speed motion; linear easing is intentional. */
    int32_t obstacle_x_q13 = vg_anim_lerp_q13(319 * VG_SCALE_ONE,
                                              (319 - 360) * VG_SCALE_ONE,
                                              vg_anim_ease_q13(VG_ANIM_EASE_LINEAR, obstacle_t));
    return q13_to_int_round(obstacle_x_q13);
}

/* Handles immediate viewer exit shortcuts. */
static bool viewer_should_exit_for_keys(const uint8_t *keys) {
    if (!keys) {
        return false;
    }
    bool esc = keys[KB_KEY_ESCAPE] != 0;
    bool cmd_q = (keys[KB_KEY_Q] != 0) &&
                 ((keys[KB_KEY_LEFT_SUPER] != 0) || (keys[KB_KEY_RIGHT_SUPER] != 0));
    return esc || cmd_q;
}

/* Reset transform and apply translation/rotation in one place. */
static void set_node_transform(VgNode *node, int16_t tx, int16_t ty, int16_t rot_deg) {
    if (!node) {
        return;
    }
    node->transform = vg_transform_identity();
    node->transform.tx = tx;
    node->transform.ty = ty;
    node->transform.rot_deg = rot_deg;
}

/* Build common style presets without repeating manual field writes. */
static VgStyle make_style(uint16_t stroke_rgb565, uint8_t stroke_width, bool has_fill, uint16_t fill_rgb565) {
    VgStyle style = vg_style_default();
    style.stroke_rgb565 = stroke_rgb565;
    style.stroke_width = stroke_width;
    style.has_bg_rgb565 = false;
    style.has_fill = has_fill;
    style.fill_rgb565 = fill_rgb565;
    return style;
}

/* Switches player triangle geometry between normal and small hit profile. */
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

/* Place both obstacle parts using one shared transform. */
static void set_obstacle_transforms(VgNode *game_obstacle_body, VgNode *game_obstacle_nose, int obstacle_x) {
    if (!game_obstacle_body || !game_obstacle_nose) {
        return;
    }
    int16_t tx = (int16_t)(obstacle_x + 20);
    set_node_transform(game_obstacle_body, tx, 126, -90);
    set_node_transform(game_obstacle_nose, tx, 126, -90);
}

/* Simple AABB overlap test in world-space pixel coordinates. */
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


/* Expand RGB565 framebuffer pixels to MiniFB's XRGB8888 format. */
static uint32_t rgb565_to_xrgb8888(uint16_t c) {
    uint32_t r = (uint32_t)((((c >> 11) & 0x1f) * 255) / 31);
    uint32_t g = (uint32_t)((((c >> 5) & 0x3f) * 255) / 63);
    uint32_t b = (uint32_t)(((c & 0x1f) * 255) / 31);
    return 0xff000000u | (r << 16) | (g << 8) | b;
}

typedef struct {
    double window_start_s;
    uint64_t window_frames;
    uint64_t window_dirty_pixels;
    uint64_t window_changed_slots;
} ViewerPerfWindow;

typedef struct {
    double fps;
    double avg_dirty_px_per_frame;
    double dirty_ratio;
    double dirty_bytes_per_s;
    double full_bytes_per_s;
    double avg_changed_slots;
    double avg_main_lock_wait_us;
    double avg_main_lock_hold_us;
    double avg_render_lock_hold_us;
    double max_render_lock_hold_us;
    uint64_t skipped_generations;
    uint64_t skipped_max_frame;
    uint64_t skipped_max_slot;
} ViewerPerfSnapshot;

/* Initialize rolling perf window counters for throughput estimation. */
static void perf_window_init(ViewerPerfWindow *perf, double start_s) {
    if (!perf) {
        return;
    }
    memset(perf, 0, sizeof(*perf));
    perf->window_start_s = start_s;
}

/* Accumulate dirty-area and slot-change stats for one rendered frame. */
static void perf_window_record_frame(ViewerPerfWindow *perf, uint32_t dirty_pixels, uint32_t changed_slots) {
    if (!perf) {
        return;
    }
    perf->window_frames++;
    perf->window_dirty_pixels += dirty_pixels;
    perf->window_changed_slots += changed_slots;
}

/* Emit one-second rolling perf snapshot and reset counters. */
static bool perf_window_take_snapshot_if_due(ViewerPerfWindow *perf,
                                              double now_s,
                                              ViewerPerfSnapshot *out_snapshot) {
    if (out_snapshot) {
        memset(out_snapshot, 0, sizeof(*out_snapshot));
    }
    if (!perf) {
        return false;
    }
    double elapsed_s = now_s - perf->window_start_s;
    if (elapsed_s < 1.0 || perf->window_frames == 0u) {
        return false;
    }
    if (out_snapshot) {
        out_snapshot->fps = (double)perf->window_frames / elapsed_s;
        out_snapshot->avg_dirty_px_per_frame = (double)perf->window_dirty_pixels / (double)perf->window_frames;
        out_snapshot->dirty_ratio = out_snapshot->avg_dirty_px_per_frame / (double)(VIEW_W * VIEW_H);
        out_snapshot->dirty_bytes_per_s =
            ((double)perf->window_dirty_pixels * (double)RGB565_BYTES_PER_PIXEL) / elapsed_s;
        out_snapshot->full_bytes_per_s = out_snapshot->fps * (double)(VIEW_W * VIEW_H * RGB565_BYTES_PER_PIXEL);
        out_snapshot->avg_changed_slots = (double)perf->window_changed_slots / (double)perf->window_frames;
    }
    perf->window_start_s = now_s;
    perf->window_frames = 0u;
    perf->window_dirty_pixels = 0u;
    perf->window_changed_slots = 0u;
    return true;
}

#define g_record_schema (*tiny_gfx_schema())

/* Allocate a record instance from an already resolved descriptor. */
static CljPersistentRecord *make_record_with_descriptor(CljRecordDescriptor *desc) {
    CljPersistentRecord *record = record_create_with_descriptor(desc, NULL);
    return record ? AUTORELEASE(record) : NULL;
}

/* Convert node transform into tiny-gfx Transform record (or nil when absent). */
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

/* Convert runtime style struct to tiny-gfx Style record. */
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

/* Convert group children recursively. */
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

/* Convert polyline points into [[x y] ...] vector form expected by tiny-gfx. */
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

#define ASSIGN_NODE_COMMON_FIELDS(record_ptr, node_ptr, t_rec, s_rec, visible_rec) \
    do {                                                                             \
        ASSIGN((record_ptr)->id, fixnum((int)(node_ptr)->id));                      \
        ASSIGN((record_ptr)->t, (t_rec));                                            \
        ASSIGN((record_ptr)->style, (s_rec));                                        \
        ASSIGN((record_ptr)->visible, (visible_rec));                                \
    } while (0)

/* Convert one VgNode tree node to its defrecord representation. */
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
            ASSIGN_NODE_COMMON_FIELDS(record, node, t_rec, s_rec, visible);
            ASSIGN(record->children, children);
            return record;
        }
        case VG_NODE_LINE: {
            Line *record = (Line *)make_record_with_descriptor(g_record_schema.d_line);
            if (!record) return NULL;
            ASSIGN_NODE_COMMON_FIELDS(record, node, t_rec, s_rec, visible);
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
            ASSIGN_NODE_COMMON_FIELDS(record, node, t_rec, s_rec, visible);
            ASSIGN(record->pts, pts);
            ASSIGN(record->closed, node->data.polyline.closed ? clj_true : clj_false);
            return record;
        }
        case VG_NODE_RECT: {
            Rect *record = (Rect *)make_record_with_descriptor(g_record_schema.d_rect);
            if (!record) return NULL;
            ASSIGN_NODE_COMMON_FIELDS(record, node, t_rec, s_rec, visible);
            ASSIGN(record->x, fixnum((int)node->data.rect.x));
            ASSIGN(record->y, fixnum((int)node->data.rect.y));
            ASSIGN(record->w, fixnum((int)node->data.rect.w));
            ASSIGN(record->h, fixnum((int)node->data.rect.h));
            return record;
        }
        case VG_NODE_TRI: {
            Tri *record = (Tri *)make_record_with_descriptor(g_record_schema.d_tri);
            if (!record) return NULL;
            ASSIGN_NODE_COMMON_FIELDS(record, node, t_rec, s_rec, visible);
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
            ASSIGN_NODE_COMMON_FIELDS(record, node, t_rec, s_rec, visible);
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

/* Build one FrameScene record with clip/z/erase metadata for slot rendering. */
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
static uint16_t g_present_copy_rgb565[VIEW_W * VIEW_H];
static const uint8_t g_slot_render_priority[VIEWER_SLOT_COUNT] = {
    VIEWER_SLOT_GAME,
    VIEWER_SLOT_SCORE,
    VIEWER_SLOT_DECO
};
typedef struct {
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_mutex_t render_done_mutex;
    pthread_cond_t render_done_cond;
    atomic_bool running;
    bool started;
    VgRenderSlotState slot_states[VIEWER_SLOT_COUNT];
    uint32_t slot_seen_generations[VIEWER_SLOT_COUNT];
    atomic_uint_fast32_t rendered_frame_serial;
    uint32_t last_dirty_pixels;
    uint32_t last_changed_slots;
    uint32_t last_rendered_generation[VIEWER_SLOT_COUNT];
    atomic_uint_fast64_t main_lock_wait_ns_total;
    atomic_uint_fast64_t main_lock_hold_ns_total;
    atomic_uint_fast64_t main_lock_samples;
    atomic_uint_fast64_t render_lock_hold_ns_total;
    atomic_uint_fast64_t render_lock_hold_ns_max;
    atomic_uint_fast64_t render_lock_samples;
    atomic_uint_fast64_t skipped_generations_total;
    atomic_uint_fast64_t skipped_max_frame;
    atomic_uint_fast64_t skipped_max_slot;
} ViewerRenderThread;
static ViewerRenderThread g_render_thread = {0};

static uint64_t monotonic_now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0u;
    }
    return ((uint64_t)ts.tv_sec * 1000000000ull) + (uint64_t)ts.tv_nsec;
}

#if defined(__APPLE__)
static mach_timebase_info_data_t g_mach_timebase = {0};

static void viewer_init_mach_timebase(void) {
    if (g_mach_timebase.denom == 0) {
        (void)mach_timebase_info(&g_mach_timebase);
    }
}

static uint64_t ns_to_mach_abs(uint64_t ns) {
    return (ns * g_mach_timebase.denom) / g_mach_timebase.numer;
}
#endif

static void viewer_set_realtime_thread_policy(void) {
#if defined(__APPLE__)
    viewer_init_mach_timebase();
    thread_time_constraint_policy_data_t policy;
    policy.period      = (uint32_t)ns_to_mach_abs(16666667u);
    policy.computation = (uint32_t)ns_to_mach_abs(2000000u);
    policy.constraint  = (uint32_t)ns_to_mach_abs(16666667u);
    policy.preemptible = true;
    (void)thread_policy_set(
        mach_thread_self(),
        THREAD_TIME_CONSTRAINT_POLICY,
        (thread_policy_t)&policy,
        THREAD_TIME_CONSTRAINT_POLICY_COUNT);
#endif
}

static void collect_thread_lock_metrics(ViewerPerfSnapshot *out_snapshot) {
    if (!out_snapshot) {
        return;
    }
    uint64_t main_samples = atomic_exchange_explicit(&g_render_thread.main_lock_samples,
                                                     0u,
                                                     memory_order_acq_rel);
    uint64_t main_wait_ns = atomic_exchange_explicit(&g_render_thread.main_lock_wait_ns_total,
                                                     0u,
                                                     memory_order_acq_rel);
    uint64_t main_hold_ns = atomic_exchange_explicit(&g_render_thread.main_lock_hold_ns_total,
                                                     0u,
                                                     memory_order_acq_rel);
    uint64_t render_samples = atomic_exchange_explicit(&g_render_thread.render_lock_samples,
                                                       0u,
                                                       memory_order_acq_rel);
    uint64_t render_hold_ns = atomic_exchange_explicit(&g_render_thread.render_lock_hold_ns_total,
                                                       0u,
                                                       memory_order_acq_rel);
    uint64_t render_hold_max_ns = atomic_exchange_explicit(&g_render_thread.render_lock_hold_ns_max,
                                                           0u,
                                                           memory_order_acq_rel);
    out_snapshot->skipped_generations =
        atomic_exchange_explicit(&g_render_thread.skipped_generations_total, 0u, memory_order_acq_rel);
    out_snapshot->skipped_max_frame =
        atomic_exchange_explicit(&g_render_thread.skipped_max_frame, 0u, memory_order_acq_rel);
    out_snapshot->skipped_max_slot =
        atomic_exchange_explicit(&g_render_thread.skipped_max_slot, 0u, memory_order_acq_rel);
    out_snapshot->avg_main_lock_wait_us =
        main_samples ? ((double)main_wait_ns / (double)main_samples) / 1000.0 : 0.0;
    out_snapshot->avg_main_lock_hold_us =
        main_samples ? ((double)main_hold_ns / (double)main_samples) / 1000.0 : 0.0;
    out_snapshot->avg_render_lock_hold_us =
        render_samples ? ((double)render_hold_ns / (double)render_samples) / 1000.0 : 0.0;
    out_snapshot->max_render_lock_hold_us = (double)render_hold_max_ns / 1000.0;
}

/* Allocate one atom per slot used by the renderer. */
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

/* Release all slot atoms and clear globals. */
static void destroy_scene_slot_atoms(void) {
    for (size_t i = 0; i < VIEWER_SLOT_COUNT; i++) {
        RELEASE(g_scene_slot_atoms[i]);
        g_scene_slot_atoms[i] = NULL;
    }
}

/* Render-thread loop: blocks for changed slots and renders into shared framebuffer. */
static void *viewer_render_thread_main(void *arg) {
    VgFrameBuffer *fb = (VgFrameBuffer *)arg;
    if (!fb) {
        return NULL;
    }
    viewer_set_realtime_thread_policy();
    while (atomic_load_explicit(&g_render_thread.running, memory_order_acquire)) {
        uint32_t slot_generations[VIEWER_SLOT_COUNT] = {0};
        uint32_t changed_mask = vg_slot_change_tracker_wait_for_changes(&g_slot_change_tracker,
                                                                        g_render_thread.slot_seen_generations,
                                                                        slot_generations,
                                                                        UINT32_MAX);
        if (!atomic_load_explicit(&g_render_thread.running, memory_order_acquire)) {
            break;
        }
        if (changed_mask == 0u) {
            continue;
        }

        uint32_t frame_dirty_pixels = 0u;
        uint32_t frame_changed_slots = 0u;
        uint64_t frame_skipped_total = 0u;
        uint64_t lock_begin_ns = monotonic_now_ns();
        if (pthread_mutex_lock(&g_render_thread.mutex) != 0) {
            continue;
        }
        uint64_t lock_acquired_ns = monotonic_now_ns();
        for (size_t p = 0; p < VIEWER_SLOT_COUNT; p++) {
            uint8_t i = g_slot_render_priority[p];
            if ((changed_mask & (1u << i)) == 0u) {
                continue;
            }
            if (!g_scene_slot_atoms[i]) {
                continue;
            }
            ID snapshot = g_scene_slot_atoms[i]->value;
            if (!snapshot) {
                continue;
            }
            uint32_t dirty_pixels = 0u;
            bool rendered = vg_render_frame_slot_record_if_changed(snapshot,
                                                                   &g_render_thread.slot_states[i],
                                                                   fb,
                                                                   slot_generations[i],
                                                                   &dirty_pixels);
            if (rendered) {
                uint32_t prev_gen = g_render_thread.last_rendered_generation[i];
                uint32_t curr_gen = slot_generations[i];
                if (curr_gen > (prev_gen + 1u)) {
                    uint64_t skipped = (uint64_t)(curr_gen - prev_gen - 1u);
                    frame_skipped_total += skipped;
                    atomic_fetch_add_explicit(&g_render_thread.skipped_generations_total,
                                              skipped,
                                              memory_order_relaxed);
                    uint64_t slot_max = atomic_load_explicit(&g_render_thread.skipped_max_slot, memory_order_relaxed);
                    while (skipped > slot_max &&
                           !atomic_compare_exchange_weak_explicit(&g_render_thread.skipped_max_slot,
                                                                  &slot_max,
                                                                  skipped,
                                                                  memory_order_relaxed,
                                                                  memory_order_relaxed)) {
                    }
                }
                g_render_thread.last_rendered_generation[i] = curr_gen;
                frame_changed_slots++;
                frame_dirty_pixels += dirty_pixels;
            }
        }
        if (frame_skipped_total > 0u) {
            uint64_t frame_max = atomic_load_explicit(&g_render_thread.skipped_max_frame, memory_order_relaxed);
            while (frame_skipped_total > frame_max &&
                   !atomic_compare_exchange_weak_explicit(&g_render_thread.skipped_max_frame,
                                                          &frame_max,
                                                          frame_skipped_total,
                                                          memory_order_relaxed,
                                                          memory_order_relaxed)) {
            }
        }
        memcpy(g_render_thread.slot_seen_generations, slot_generations, sizeof(slot_generations));
        g_render_thread.last_dirty_pixels = frame_dirty_pixels;
        g_render_thread.last_changed_slots = frame_changed_slots;
        uint64_t lock_release_ns = monotonic_now_ns();
        uint64_t hold_ns = (lock_release_ns > lock_acquired_ns)
                               ? (lock_release_ns - lock_acquired_ns)
                               : 0u;
        atomic_fetch_add_explicit(&g_render_thread.render_lock_hold_ns_total, hold_ns, memory_order_relaxed);
        atomic_fetch_add_explicit(&g_render_thread.render_lock_samples, 1u, memory_order_relaxed);
        uint64_t max_hold = atomic_load_explicit(&g_render_thread.render_lock_hold_ns_max, memory_order_relaxed);
        while (hold_ns > max_hold &&
               !atomic_compare_exchange_weak_explicit(&g_render_thread.render_lock_hold_ns_max,
                                                      &max_hold,
                                                      hold_ns,
                                                      memory_order_relaxed,
                                                      memory_order_relaxed)) {
        }
        (void)pthread_mutex_unlock(&g_render_thread.mutex);
        atomic_fetch_add_explicit(&g_render_thread.rendered_frame_serial, 1u, memory_order_release);
        (void)pthread_mutex_lock(&g_render_thread.render_done_mutex);
        (void)pthread_cond_signal(&g_render_thread.render_done_cond);
        (void)pthread_mutex_unlock(&g_render_thread.render_done_mutex);
        (void)lock_begin_ns;
    }
    return NULL;
}

/* Start dedicated render thread that owns slot rendering. */
static bool start_render_thread(VgFrameBuffer *fb) {
    if (!fb) {
        return false;
    }
    memset(&g_render_thread, 0, sizeof(g_render_thread));
    if (pthread_mutex_init(&g_render_thread.mutex, NULL) != 0) {
        return false;
    }
    if (pthread_mutex_init(&g_render_thread.render_done_mutex, NULL) != 0) {
        (void)pthread_mutex_destroy(&g_render_thread.mutex);
        return false;
    }
    if (pthread_cond_init(&g_render_thread.render_done_cond, NULL) != 0) {
        (void)pthread_mutex_destroy(&g_render_thread.mutex);
        (void)pthread_mutex_destroy(&g_render_thread.render_done_mutex);
        return false;
    }
    atomic_store_explicit(&g_render_thread.running, true, memory_order_release);
    if (pthread_create(&g_render_thread.thread, NULL, viewer_render_thread_main, fb) != 0) {
        atomic_store_explicit(&g_render_thread.running, false, memory_order_release);
        (void)pthread_mutex_destroy(&g_render_thread.mutex);
        (void)pthread_mutex_destroy(&g_render_thread.render_done_mutex);
        (void)pthread_cond_destroy(&g_render_thread.render_done_cond);
        return false;
    }
    g_render_thread.started = true;
    return true;
}

/* Stop render thread and free synchronization primitives. */
static void stop_render_thread(void) {
    if (!g_render_thread.started) {
        return;
    }
    atomic_store_explicit(&g_render_thread.running, false, memory_order_release);
    /* Wake blocked render wait to allow clean shutdown. */
    (void)vg_slot_change_tracker_publish(&g_slot_change_tracker, 0u, NULL);
    (void)pthread_cond_signal(&g_render_thread.render_done_cond);
    (void)pthread_join(g_render_thread.thread, NULL);
    (void)pthread_mutex_destroy(&g_render_thread.mutex);
    (void)pthread_mutex_destroy(&g_render_thread.render_done_mutex);
    (void)pthread_cond_destroy(&g_render_thread.render_done_cond);
    memset(&g_render_thread, 0, sizeof(g_render_thread));
}

/* Publish a freshly built FrameScene into one slot atom and mark it dirty. */
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
            if (pthread_mutex_lock(&g_render_thread.mutex) != 0) {
                return;
            }
            (void)atom_reset(g_scene_slot_atoms[slot_index], scene);
            (void)pthread_mutex_unlock(&g_render_thread.mutex);
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
    bool render_thread_started = false;
    int exit_code = 1;
    viewer_set_realtime_thread_policy();

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
    if (!start_render_thread(&fb)) {
        fprintf(stderr, "Failed to start render thread\n");
        goto cleanup;
    }
    render_thread_started = true;

#if defined(__APPLE__)
    macos_viewer_install_menu();
    macos_viewer_begin_performance_activity();
#endif
    const unsigned default_win_w = VIEW_W * VIEW_DEFAULT_WINDOW_SCALE;
    const unsigned default_win_h = VIEW_H * VIEW_DEFAULT_WINDOW_SCALE;
    window = mfb_open_ex("tiny-clj host viewer", default_win_w, default_win_h, 0);
    if (!window) {
        fprintf(stderr, "Failed to open MiniFB window\n");
        goto cleanup;
    }
#if defined(__APPLE__)
    macos_viewer_register_window_callbacks();
    macos_viewer_restore_window_position();
#endif
    mfb_show_cursor(window, true);
    (void)mfb_set_viewport(window, 0, 0, default_win_w, default_win_h);
    timer = mfb_timer_create();
    if (!timer) {
        fprintf(stderr, "Failed to create MiniFB timer\n");
        goto cleanup;
    }
    ViewerPerfWindow perf_window;
    perf_window_init(&perf_window, mfb_timer_now(timer));

    VgStyle deco_style = make_style(0x07ffu, 2, false, 0u);
    VgStyle score_style = make_style(0xffffu, 1, false, 0u);
    VgStyle game_line_style = make_style(0x07e0u, 2, false, 0u);
    VgStyle game_player_style = make_style(0xf81fu, 3, false, 0u);
    VgStyle game_obstacle_style = make_style(0xffe0u, 2, true, 0xffe0u);
    VgStyle game_obstacle_nose_style = make_style(0xf800u, 2, true, 0xf800u);

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
    set_node_transform(&score_text, 6, 10, 0);
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
    set_node_transform(&game_caption, 96, 52, 0);

    VgStyle hbar_style = make_style(0xffffu, 1, true, 0xffffu);
    VgNode game_hbar = {
        .id = 3010,
        .type = VG_NODE_TRI,
        .has_transform = true,
        .transform = vg_transform_identity(),
        .style = hbar_style,
        .data.tri = {.x1 = 0, .y1 = -4, .x2 = 20, .y2 = 0, .x3 = 0, .y3 = 4}
    };
    set_node_transform(&game_hbar, 0, 80, 0);

    VgNode *game_children[] = {&game_terrain, &game_player, &game_obstacle_body, &game_obstacle_nose, &game_caption, &game_hbar};
    VgNode game_root = {
        .id = 3000,
        .type = VG_NODE_GROUP,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = vg_style_default(),
        .data.group = {.children = game_children, .child_count = sizeof(game_children) / sizeof(game_children[0])}
    };

    bool player_small = false;
    bool collision_latched = false;
    float collision_cooldown_end_s = 0.0f;
    unsigned frame_count = 0;
    uint_fast32_t last_presented_frame_serial = 0u;
    bool sync_mode = false;
    bool s_key_was_down = false;
    bool w_key_was_down = false;
    bool p_key_was_down = false;
    bool use_mfb_waitsync = false;
    bool periodic_score_updates_enabled = true;
    VgRenderSlotState sync_slot_states[VIEWER_SLOT_COUNT] = {0};
    uint32_t sync_slot_generation = 1u;
    const uint64_t target_frame_ns = 1000000000ull / TARGET_FPS;
    uint64_t next_frame_deadline_ns = monotonic_now_ns() + target_frame_ns;
    uint64_t last_present_ns = 0u;
    uint64_t frame_dt_min_ns = UINT64_MAX;
    uint64_t frame_dt_max_ns = 0u;
    uint64_t frame_dt_sum_ns = 0u;
    uint32_t frame_dt_count = 0u;
    uint64_t waitsync_min_ns = UINT64_MAX;
    uint64_t waitsync_max_ns = 0u;
    uint64_t waitsync_sum_ns = 0u;
    uint32_t waitsync_count = 0u;
    uint64_t update_min_ns = UINT64_MAX;
    uint64_t update_max_ns = 0u;
    uint64_t update_sum_ns = 0u;
    uint32_t update_count = 0u;
    vg_framebuffer_clear(&fb, SCENE_ERASE_RGB565);
    publish_frame_scene_slot(VIEWER_SLOT_DECO, &deco_root, deco_clip, 0, true, true, SCENE_ERASE_RGB565, 1);
    publish_frame_scene_slot(VIEWER_SLOT_SCORE, &score_root, score_clip, 1, true, true, SCENE_ERASE_RGB565, 1);
    publish_frame_scene_slot(VIEWER_SLOT_GAME, &game_root, game_clip, 2, true, true, SCENE_ERASE_RGB565, 1);

    while (true) {
        float time_s = (float)mfb_timer_now(timer);
        uint64_t waitsync_begin_ns = monotonic_now_ns();
        bool waitsync_ok = true;
        if (use_mfb_waitsync) {
            waitsync_ok = mfb_wait_sync(window);
        } else {
#if defined(__APPLE__)
            uint64_t deadline_mach = mach_absolute_time() +
                ns_to_mach_abs((next_frame_deadline_ns > waitsync_begin_ns)
                               ? (next_frame_deadline_ns - waitsync_begin_ns)
                               : 0u);
            (void)mach_wait_until(deadline_mach);
#else
            {
                uint64_t t = waitsync_begin_ns;
                while (t < next_frame_deadline_ns) {
                    uint64_t remaining_ns = next_frame_deadline_ns - t;
                    if (remaining_ns > 1500000u) {
                        struct timespec ts;
                        ts.tv_sec = 0;
                        ts.tv_nsec = (long)(remaining_ns - 800000u);
                        (void)nanosleep(&ts, NULL);
                    } else {
                        sched_yield();
                    }
                    t = monotonic_now_ns();
                }
            }
#endif
            uint64_t now_ns = monotonic_now_ns();
            next_frame_deadline_ns += target_frame_ns;
            if (now_ns > next_frame_deadline_ns + (target_frame_ns * 3u)) {
                next_frame_deadline_ns = now_ns + target_frame_ns;
            }
        }
        uint64_t waitsync_end_ns = monotonic_now_ns();
        uint64_t waitsync_ns = (waitsync_end_ns > waitsync_begin_ns)
                                   ? (waitsync_end_ns - waitsync_begin_ns)
                                   : 0u;
        waitsync_sum_ns += waitsync_ns;
        waitsync_count++;
        if (waitsync_ns < waitsync_min_ns) waitsync_min_ns = waitsync_ns;
        if (waitsync_ns > waitsync_max_ns) waitsync_max_ns = waitsync_ns;
        if (!waitsync_ok) {
            break;
        }
        const uint8_t *keys = mfb_get_key_buffer(window);
        if (viewer_should_exit_for_keys(keys)) {
            break;
        }
        {
            bool s_down = keys && keys[KB_KEY_S] != 0;
            if (s_down && !s_key_was_down) {
                sync_mode = !sync_mode;
            }
            s_key_was_down = s_down;
            bool w_down = keys && keys[KB_KEY_W] != 0;
            if (w_down && !w_key_was_down) {
                use_mfb_waitsync = !use_mfb_waitsync;
                next_frame_deadline_ns = monotonic_now_ns() + target_frame_ns;
            }
            w_key_was_down = w_down;
            bool p_down = keys && keys[KB_KEY_P] != 0;
            if (p_down && !p_key_was_down) {
                periodic_score_updates_enabled = !periodic_score_updates_enabled;
            }
            p_key_was_down = p_down;
        }

        frame_count++;
        bool scene_tick = (frame_count % SCENE_FRAME_DIVIDER) == 0u;
        bool collision_cooldown_active = (time_s < collision_cooldown_end_s);
        int terrain_scroll_px = (int)((frame_count * TERRAIN_PPF) % 320u);
        int player_jump_y = compute_player_jump_y(frame_count);
        int obstacle_x = compute_obstacle_x(frame_count);

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

        if (scene_tick) {
            set_node_transform(&game_terrain, (int16_t)(-terrain_scroll_px), 0, 0);
            set_node_transform(&game_player, 0, (int16_t)player_jump_y, 0);
            set_obstacle_transforms(&game_obstacle_body, &game_obstacle_nose, obstacle_x);
            set_player_geometry(&game_player, player_small);
            int hbar_x = (int)((frame_count * 2u) % 340u) - 10;
            set_node_transform(&game_hbar, (int16_t)hbar_x, 80, 0);
        }

        uint32_t dirty_pixels = 0u;
        uint32_t changed_slots = 0u;
        uint_fast32_t frame_serial = 0u;

        if (sync_mode) {
            if (scene_tick) {
                WITH_AUTORELEASE_POOL({
                    ID scene = make_frame_scene_record(&game_root, game_clip, 2, true, true, SCENE_ERASE_RGB565, 1);
                    if (scene) {
                        sync_slot_generation++;
                        uint32_t dp = 0u;
                        if (vg_render_frame_slot_record_if_changed(scene,
                                                                    &sync_slot_states[VIEWER_SLOT_GAME],
                                                                    &fb,
                                                                    sync_slot_generation,
                                                                    &dp)) {
                            dirty_pixels += dp;
                            changed_slots++;
                        }
                    }
                });
                frame_serial = sync_slot_generation;
            }
        } else {
            if (scene_tick) {
                uint_fast32_t pre_serial = atomic_load_explicit(&g_render_thread.rendered_frame_serial, memory_order_acquire);
                publish_frame_scene_slot(VIEWER_SLOT_GAME, &game_root, game_clip, 2, true, true, SCENE_ERASE_RGB565, 1);
                (void)pthread_mutex_lock(&g_render_thread.render_done_mutex);
                while (atomic_load_explicit(&g_render_thread.rendered_frame_serial, memory_order_acquire) == pre_serial) {
                    (void)pthread_cond_wait(&g_render_thread.render_done_cond, &g_render_thread.render_done_mutex);
                }
                (void)pthread_mutex_unlock(&g_render_thread.render_done_mutex);
            }
            uint64_t main_lock_begin_ns = monotonic_now_ns();
            if (pthread_mutex_lock(&g_render_thread.mutex) == 0) {
                uint64_t main_lock_acquired_ns = monotonic_now_ns();
                frame_serial = atomic_load_explicit(&g_render_thread.rendered_frame_serial, memory_order_acquire);
                dirty_pixels = g_render_thread.last_dirty_pixels;
                changed_slots = g_render_thread.last_changed_slots;
                memcpy(g_present_copy_rgb565, fb_pixels, sizeof(g_present_copy_rgb565));
                uint64_t main_lock_release_ns = monotonic_now_ns();
                (void)pthread_mutex_unlock(&g_render_thread.mutex);
                uint64_t wait_ns = (main_lock_acquired_ns > main_lock_begin_ns)
                                       ? (main_lock_acquired_ns - main_lock_begin_ns)
                                       : 0u;
                uint64_t hold_ns = (main_lock_release_ns > main_lock_acquired_ns)
                                       ? (main_lock_release_ns - main_lock_acquired_ns)
                                       : 0u;
                atomic_fetch_add_explicit(&g_render_thread.main_lock_wait_ns_total, wait_ns, memory_order_relaxed);
                atomic_fetch_add_explicit(&g_render_thread.main_lock_hold_ns_total, hold_ns, memory_order_relaxed);
                atomic_fetch_add_explicit(&g_render_thread.main_lock_samples, 1u, memory_order_relaxed);
            }
        }
        {
            const uint16_t *src = sync_mode ? fb_pixels : g_present_copy_rgb565;
            for (size_t i = 0; i < (size_t)VIEW_W * (size_t)VIEW_H; i++) {
                window_pixels[i] = rgb565_to_xrgb8888(src[i]);
            }
        }
        if (frame_serial != last_presented_frame_serial) {
            perf_window_record_frame(&perf_window, dirty_pixels, changed_slots);
            last_presented_frame_serial = frame_serial;
        }
        ViewerPerfSnapshot perf_snapshot;
        bool perf_ready = perf_window_take_snapshot_if_due(&perf_window, (double)time_s, &perf_snapshot);
        if (perf_ready) {
            collect_thread_lock_metrics(&perf_snapshot);
        }
#if defined(__APPLE__)
        if (perf_ready) {
            if (periodic_score_updates_enabled) {
                int score = (int)(time_s * 120.0f);
                (void)snprintf(score_line, sizeof(score_line), "SCORE %04d    LIFES 3", score % 10000);
            }
            double dt_avg_ms = (frame_dt_count > 0u) ? ((double)frame_dt_sum_ns / (double)frame_dt_count / 1e6) : 0.0;
            double dt_min_ms = (frame_dt_min_ns < UINT64_MAX) ? ((double)frame_dt_min_ns / 1e6) : 0.0;
            double dt_max_ms = (frame_dt_max_ns > 0u) ? ((double)frame_dt_max_ns / 1e6) : 0.0;
            double ws_avg_ms = (waitsync_count > 0u) ? ((double)waitsync_sum_ns / (double)waitsync_count / 1e6) : 0.0;
            double ws_min_ms = (waitsync_min_ns < UINT64_MAX) ? ((double)waitsync_min_ns / 1e6) : 0.0;
            double ws_max_ms = (waitsync_max_ns > 0u) ? ((double)waitsync_max_ns / 1e6) : 0.0;
            double up_avg_ms = (update_count > 0u) ? ((double)update_sum_ns / (double)update_count / 1e6) : 0.0;
            double up_min_ms = (update_min_ns < UINT64_MAX) ? ((double)update_min_ns / 1e6) : 0.0;
            double up_max_ms = (update_max_ns > 0u) ? ((double)update_max_ns / 1e6) : 0.0;
            frame_dt_min_ns = UINT64_MAX;
            frame_dt_max_ns = 0u;
            frame_dt_sum_ns = 0u;
            frame_dt_count = 0u;
            waitsync_min_ns = UINT64_MAX;
            waitsync_max_ns = 0u;
            waitsync_sum_ns = 0u;
            waitsync_count = 0u;
            update_min_ns = UINT64_MAX;
            update_max_ns = 0u;
            update_sum_ns = 0u;
            update_count = 0u;
            char title[192];
            (void)snprintf(title,
                           sizeof(title),
                           "tiny-clj [%s|%s|SC%s] | FPS %.1f | dt %.1f/%.1f/%.1fms | ws %.1f/%.1f/%.1fms | up %.1f/%.1f/%.1fms",
                           sync_mode ? "SYNC" : "ASYNC",
                           use_mfb_waitsync ? "WAITSYNC" : "CUSTOM",
                           periodic_score_updates_enabled ? "ON" : "OFF",
                           perf_snapshot.fps,
                           dt_min_ms, dt_avg_ms, dt_max_ms,
                           ws_min_ms, ws_avg_ms, ws_max_ms,
                           up_min_ms, up_avg_ms, up_max_ms);
            macos_viewer_set_window_title(title);
            if (periodic_score_updates_enabled) {
                publish_frame_scene_slot(VIEWER_SLOT_SCORE,
                                         &score_root,
                                         score_clip,
                                         1,
                                         true,
                                         true,
                                         SCENE_ERASE_RGB565,
                                         1);
            }
        }
#else
        (void)perf_snapshot;
        (void)perf_ready;
#endif

        {
            uint64_t now_ns = monotonic_now_ns();
            if (last_present_ns > 0u) {
                uint64_t dt = (now_ns > last_present_ns) ? (now_ns - last_present_ns) : 0u;
                frame_dt_sum_ns += dt;
                frame_dt_count++;
                if (dt < frame_dt_min_ns) frame_dt_min_ns = dt;
                if (dt > frame_dt_max_ns) frame_dt_max_ns = dt;
            }
            last_present_ns = now_ns;
        }
        uint64_t update_begin_ns = monotonic_now_ns();
        mfb_update_state state = mfb_update_ex(window, window_pixels, VIEW_W, VIEW_H);
        uint64_t update_end_ns = monotonic_now_ns();
        uint64_t update_ns = (update_end_ns > update_begin_ns)
                                 ? (update_end_ns - update_begin_ns)
                                 : 0u;
        update_sum_ns += update_ns;
        update_count++;
        if (update_ns < update_min_ns) update_min_ns = update_ns;
        if (update_ns > update_max_ns) update_max_ns = update_ns;
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
    macos_viewer_end_performance_activity();
#endif
    if (window) {
        mfb_close(window);
    }
    if (render_thread_started) {
        stop_render_thread();
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
