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
#include "tiny_clj.h"
#include "builtins.h"
#include "value.h"
#include "runtime.h"
#include "record.h"
#include "event_loop.h"
#include "atom.h"
#include "vector.h"
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
#define SCENE_ERASE_COLOR   0x0000u
#define RGB565_BYTES_PER_PIXEL 2u

#define TERRAIN_SPEED_PXS  120    /* px/s  → 2 px/frame @60 */
#define OBSTACLE_SPEED_PXS 120    /* px/s  → 2 px/frame @60 */
#define PLAYER_JUMP_HEIGHT_PX      10u  /* double previous bob peak (5px -> 10px) */
#define PLAYER_JUMP_DURATION_FRAMES 16u
#define PLAYER_JUMP_PERIOD_FRAMES   48u
#define PLAYER_ANIM_RESPONSE_MS     70u
#define TERRAIN_ANIM_RESPONSE_MS    24u
#define OBSTACLE_ANIM_RESPONSE_MS   24u
#define COLLISION_COOLDOWN_MS      300u

#define TERRAIN_PPF  (TERRAIN_SPEED_PXS / TARGET_FPS)   /* 2 */
#define OBSTACLE_PPF (OBSTACLE_SPEED_PXS / TARGET_FPS)  /* 2 */

static uint64_t monotonic_now_ns(void);
typedef struct ViewerDemoBundle ViewerDemoBundle;

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

/* Writes tx/ty/rot while keeping sx/sy unchanged. */
static void set_transform_fields(Transform *transform, int tx, int ty, int rot_deg) {
    if (!transform) {
        return;
    }
    ASSIGN(transform->tx, fixnum(tx));
    ASSIGN(transform->ty, fixnum(ty));
    ASSIGN(transform->rot, fixnum(rot_deg));
}

/* Switches player triangle geometry between normal and small hit profile. */
static void set_player_geometry(Tri *game_player, bool player_small) {
    if (!game_player) {
        return;
    }
    if (player_small) {
        ASSIGN(game_player->x1, fixnum(60));
        ASSIGN(game_player->y1, fixnum(146));
        ASSIGN(game_player->x2, fixnum(72));
        ASSIGN(game_player->y2, fixnum(126));
        ASSIGN(game_player->x3, fixnum(84));
        ASSIGN(game_player->y3, fixnum(146));
    } else {
        ASSIGN(game_player->x1, fixnum(56));
        ASSIGN(game_player->y1, fixnum(146));
        ASSIGN(game_player->x2, fixnum(72));
        ASSIGN(game_player->y2, fixnum(118));
        ASSIGN(game_player->x3, fixnum(88));
        ASSIGN(game_player->y3, fixnum(146));
    }
}

/* Place both obstacle parts using one shared transform. */
static void set_obstacle_transforms(Transform *body_t, Transform *nose_t, int obstacle_x) {
    if (!body_t || !nose_t) {
        return;
    }
    int tx = obstacle_x + 20;
    set_transform_fields(body_t, tx, 126, -90);
    set_transform_fields(nose_t, tx, 126, -90);
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

static VgTransform make_transform_target(int tx, int ty, int rot_deg) {
    VgTransform t = vg_transform_identity();
    t.tx = (int16_t)tx;
    t.ty = (int16_t)ty;
    t.rot_deg = (int16_t)rot_deg;
    return t;
}

static bool has_wrap_jump_on_tx(const VgAnimTransformState *state, int target_tx, int wrap_period_px) {
    if (!state || !state->initialized || wrap_period_px <= 0) {
        return false;
    }
    int half_period = wrap_period_px / 2;
    VgTransform current = vg_anim_transform_state_current(state);
    int delta = target_tx - (int)current.tx;
    return (delta > half_period) || (delta < -half_period);
}

static void set_transform_anim_target_with_wrap_snap(VgAnimTransformState *state,
                                                     VgTransform target,
                                                     int wrap_period_px) {
    if (!state) {
        return;
    }
    if (has_wrap_jump_on_tx(state, (int)target.tx, wrap_period_px)) {
        vg_anim_transform_state_reset(state, target, state->response_ms, state->ease);
        return;
    }
    vg_anim_transform_state_set_target(state, target);
}

typedef struct {
    bool sync_mode;
    bool use_mfb_waitsync;
    bool periodic_score_updates_enabled;
    bool s_key_was_down;
    bool w_key_was_down;
    bool p_key_was_down;
} ViewerRuntimeFlags;

typedef struct {
    unsigned frame_count;
    bool player_small;
    bool collision_latched;
    uint32_t collision_cooldown_end_ms;
    bool anim_states_initialized;
    VgAnimTransformState terrain_anim;
    VgAnimTransformState player_anim;
    VgAnimTransformState obstacle_anim;
    uint32_t last_update_ms;
} ViewerGameplayState;

typedef struct {
    uint32_t dirty_pixels;
    uint32_t changed_slots;
    uint_fast32_t frame_serial;
} ViewerFrameRenderResult;

static bool viewer_key_pressed_once(const uint8_t *keys, int key, bool *was_down) {
    if (!was_down) {
        return false;
    }
    bool down = keys && keys[key] != 0;
    bool pressed = down && !(*was_down);
    *was_down = down;
    return pressed;
}

static void viewer_update_runtime_flags(const uint8_t *keys,
                                        ViewerRuntimeFlags *flags,
                                        uint64_t *next_frame_deadline_ns,
                                        uint64_t target_frame_ns) {
    if (!flags || !next_frame_deadline_ns) {
        return;
    }
    if (viewer_key_pressed_once(keys, KB_KEY_S, &flags->s_key_was_down)) {
        flags->sync_mode = !flags->sync_mode;
    }
    if (viewer_key_pressed_once(keys, KB_KEY_W, &flags->w_key_was_down)) {
        flags->use_mfb_waitsync = !flags->use_mfb_waitsync;
        *next_frame_deadline_ns = monotonic_now_ns() + target_frame_ns;
    }
    if (viewer_key_pressed_once(keys, KB_KEY_P, &flags->p_key_was_down)) {
        flags->periodic_score_updates_enabled = !flags->periodic_score_updates_enabled;
    }
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

typedef struct {
    uint64_t min_ns;
    uint64_t max_ns;
    uint64_t sum_ns;
    uint32_t count;
} TimingAccumulator;

enum {
    DEMO_BUNDLE_DECO_SCENE = 0,
    DEMO_BUNDLE_SCORE_SCENE = 1,
    DEMO_BUNDLE_GAME_SCENE = 2,
    DEMO_BUNDLE_TERRAIN_T = 3,
    DEMO_BUNDLE_PLAYER_T = 4,
    DEMO_BUNDLE_OBSTACLE_BODY_T = 5,
    DEMO_BUNDLE_OBSTACLE_NOSE_T = 6,
    DEMO_BUNDLE_GAME_PLAYER = 7,
    DEMO_BUNDLE_SCORE_TEXT = 8,
    DEMO_BUNDLE_COUNT = 9
};

struct ViewerDemoBundle {
    ID bundle_root;
    FrameScene *deco_scene;
    FrameScene *score_scene;
    FrameScene *game_scene;
    Transform *terrain_t;
    Transform *player_t;
    Transform *obstacle_body_t;
    Transform *obstacle_nose_t;
    Tri *game_player;
    VText *score_text;
};

static bool init_demo_bundle(EvalState *st, ViewerDemoBundle *out_bundle) {
    if (!st || !out_bundle) {
        return false;
    }
    memset(out_bundle, 0, sizeof(*out_bundle));
    if (!require_namespace_by_name(st, "tiny-gfx.host-viewer-demo")) {
        return false;
    }
    ID bundle = eval_string("(tiny-gfx.host-viewer-demo/create-demo-bundle)", st);
    if (!bundle || !is_vector(bundle)) {
        return false;
    }
    CljPersistentVector *vec = as_vector(bundle);
    if (!vec || vector_count(vec) < DEMO_BUNDLE_COUNT) {
        return false;
    }
    RETAIN(bundle);
    out_bundle->bundle_root = bundle;
    out_bundle->deco_scene = (FrameScene *)vector_nth(vec, DEMO_BUNDLE_DECO_SCENE);
    out_bundle->score_scene = (FrameScene *)vector_nth(vec, DEMO_BUNDLE_SCORE_SCENE);
    out_bundle->game_scene = (FrameScene *)vector_nth(vec, DEMO_BUNDLE_GAME_SCENE);
    out_bundle->terrain_t = (Transform *)vector_nth(vec, DEMO_BUNDLE_TERRAIN_T);
    out_bundle->player_t = (Transform *)vector_nth(vec, DEMO_BUNDLE_PLAYER_T);
    out_bundle->obstacle_body_t = (Transform *)vector_nth(vec, DEMO_BUNDLE_OBSTACLE_BODY_T);
    out_bundle->obstacle_nose_t = (Transform *)vector_nth(vec, DEMO_BUNDLE_OBSTACLE_NOSE_T);
    out_bundle->game_player = (Tri *)vector_nth(vec, DEMO_BUNDLE_GAME_PLAYER);
    out_bundle->score_text = (VText *)vector_nth(vec, DEMO_BUNDLE_SCORE_TEXT);
    return out_bundle->deco_scene && out_bundle->score_scene && out_bundle->game_scene &&
           out_bundle->terrain_t && out_bundle->player_t && out_bundle->obstacle_body_t &&
           out_bundle->obstacle_nose_t &&
           out_bundle->game_player && out_bundle->score_text;
}

static void destroy_demo_bundle(ViewerDemoBundle *bundle) {
    if (!bundle) {
        return;
    }
    RELEASE(bundle->bundle_root);
    memset(bundle, 0, sizeof(*bundle));
}

static void viewer_apply_gameplay_step(const ViewerDemoBundle *bundle,
                                       ViewerGameplayState *state,
                                       uint32_t now_ms,
                                       uint32_t dt_ms) {
    if (!bundle || !state) {
        return;
    }

    state->frame_count++;
    int terrain_scroll_px = (int)((state->frame_count * TERRAIN_PPF) % 320u);
    int player_jump_target_y = compute_player_jump_y(state->frame_count);
    int obstacle_target_x = compute_obstacle_x(state->frame_count);

    VgTransform terrain_target = make_transform_target(-terrain_scroll_px, 0, 0);
    VgTransform player_target = make_transform_target(0, player_jump_target_y, 0);
    VgTransform obstacle_target = make_transform_target(obstacle_target_x + 20, 126, -90);

    if (!state->anim_states_initialized) {
        vg_anim_transform_state_reset(&state->terrain_anim,
                                      terrain_target,
                                      TERRAIN_ANIM_RESPONSE_MS,
                                      VG_ANIM_EASE_LINEAR);
        vg_anim_transform_state_reset(&state->player_anim,
                                      player_target,
                                      PLAYER_ANIM_RESPONSE_MS,
                                      VG_ANIM_EASE_OUT_CUBIC);
        vg_anim_transform_state_reset(&state->obstacle_anim,
                                      obstacle_target,
                                      OBSTACLE_ANIM_RESPONSE_MS,
                                      VG_ANIM_EASE_LINEAR);
        state->anim_states_initialized = true;
    }

    set_transform_anim_target_with_wrap_snap(&state->terrain_anim, terrain_target, 320);
    vg_anim_transform_state_set_target(&state->player_anim, player_target);
    set_transform_anim_target_with_wrap_snap(&state->obstacle_anim, obstacle_target, 360);

    VgTransform terrain_visual = vg_anim_transform_state_step(&state->terrain_anim, dt_ms);
    VgTransform player_visual = vg_anim_transform_state_step(&state->player_anim, dt_ms);
    VgTransform obstacle_visual = vg_anim_transform_state_step(&state->obstacle_anim, dt_ms);

    set_transform_fields(bundle->terrain_t, terrain_visual.tx, terrain_visual.ty, terrain_visual.rot_deg);
    set_transform_fields(bundle->player_t, player_visual.tx, player_visual.ty, player_visual.rot_deg);
    set_obstacle_transforms(bundle->obstacle_body_t, bundle->obstacle_nose_t, obstacle_visual.tx - 20);

    bool collision_cooldown_active = (now_ms < state->collision_cooldown_end_ms);
    bool colliding = detect_player_obstacle_collision(player_visual.ty, obstacle_visual.tx - 20);
    if (colliding && !state->collision_latched && !collision_cooldown_active) {
        state->player_small = !state->player_small;
        state->collision_latched = true;
        state->collision_cooldown_end_ms = now_ms + COLLISION_COOLDOWN_MS;
    } else if (!colliding) {
        state->collision_latched = false;
    }
    set_player_geometry(bundle->game_player, state->player_small);
}

static void timing_accumulator_reset(TimingAccumulator *acc) {
    if (!acc) {
        return;
    }
    acc->min_ns = UINT64_MAX;
    acc->max_ns = 0u;
    acc->sum_ns = 0u;
    acc->count = 0u;
}

static void timing_accumulator_add(TimingAccumulator *acc, uint64_t sample_ns) {
    if (!acc) {
        return;
    }
    acc->sum_ns += sample_ns;
    acc->count++;
    if (sample_ns < acc->min_ns) {
        acc->min_ns = sample_ns;
    }
    if (sample_ns > acc->max_ns) {
        acc->max_ns = sample_ns;
    }
}

static double timing_accumulator_avg_ms(const TimingAccumulator *acc) {
    if (!acc || acc->count == 0u) {
        return 0.0;
    }
    return (double)acc->sum_ns / (double)acc->count / 1e6;
}

static double timing_accumulator_min_ms(const TimingAccumulator *acc) {
    if (!acc || acc->min_ns == UINT64_MAX) {
        return 0.0;
    }
    return (double)acc->min_ns / 1e6;
}

static double timing_accumulator_max_ms(const TimingAccumulator *acc) {
    if (!acc || acc->max_ns == 0u) {
        return 0.0;
    }
    return (double)acc->max_ns / 1e6;
}

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

/* Publish one already-built FrameScene record into one slot atom and mark it dirty. */
static void publish_frame_scene_slot_record(size_t slot_index, ID scene, uint32_t *out_generation) {
    if (slot_index >= VIEWER_SLOT_COUNT) {
        return;
    }
    if (!scene) {
        return;
    }
    if (pthread_mutex_lock(&g_render_thread.mutex) != 0) {
        return;
    }
    CljAtom *slot_atom = g_scene_slot_atoms[slot_index];
    if (slot_atom) {
        /*
         * Do not use atom_reset() here: its usable/pool-safe return value adds
         * an extra RETAIN+AUTORELEASE that is unnecessary in this hot publish path.
         */
        ASSIGN(slot_atom->value, scene);
    }
    (void)pthread_mutex_unlock(&g_render_thread.mutex);
    (void)vg_slot_change_tracker_publish(&g_slot_change_tracker, (uint8_t)slot_index, out_generation);
}

static bool viewer_wait_for_frame_pacing(struct mfb_window *window,
                                         bool use_mfb_waitsync,
                                         uint64_t target_frame_ns,
                                         uint64_t *next_frame_deadline_ns,
                                         TimingAccumulator *waitsync_stats) {
    uint64_t waitsync_begin_ns = monotonic_now_ns();
    bool waitsync_ok = true;
    if (use_mfb_waitsync) {
        waitsync_ok = mfb_wait_sync(window);
    } else {
#if defined(__APPLE__)
        uint64_t deadline_mach = mach_absolute_time() +
            ns_to_mach_abs((*next_frame_deadline_ns > waitsync_begin_ns)
                           ? (*next_frame_deadline_ns - waitsync_begin_ns)
                           : 0u);
        (void)mach_wait_until(deadline_mach);
#else
        uint64_t t = waitsync_begin_ns;
        while (t < *next_frame_deadline_ns) {
            uint64_t remaining_ns = *next_frame_deadline_ns - t;
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
#endif
        uint64_t now_ns = monotonic_now_ns();
        *next_frame_deadline_ns += target_frame_ns;
        if (now_ns > *next_frame_deadline_ns + (target_frame_ns * 3u)) {
            *next_frame_deadline_ns = now_ns + target_frame_ns;
        }
    }
    uint64_t waitsync_end_ns = monotonic_now_ns();
    uint64_t waitsync_ns = (waitsync_end_ns > waitsync_begin_ns)
                               ? (waitsync_end_ns - waitsync_begin_ns)
                               : 0u;
    timing_accumulator_add(waitsync_stats, waitsync_ns);
    return waitsync_ok;
}

static ViewerFrameRenderResult viewer_render_game_frame_sync(FrameScene *game_scene,
                                                             VgRenderSlotState *sync_slot_states,
                                                             uint32_t *sync_slot_generation,
                                                             VgFrameBuffer *fb) {
    ViewerFrameRenderResult result = {0};
    if (!game_scene || !sync_slot_states || !sync_slot_generation || !fb) {
        return result;
    }
    (*sync_slot_generation)++;
    uint32_t dirty_pixels = 0u;
    if (vg_render_frame_slot_record_if_changed(game_scene,
                                               &sync_slot_states[VIEWER_SLOT_GAME],
                                               fb,
                                               *sync_slot_generation,
                                               &dirty_pixels)) {
        result.dirty_pixels = dirty_pixels;
        result.changed_slots = 1u;
    }
    result.frame_serial = *sync_slot_generation;
    return result;
}

static ViewerFrameRenderResult viewer_render_game_frame_async(FrameScene *game_scene,
                                                              const uint16_t *fb_pixels) {
    ViewerFrameRenderResult result = {0};
    if (!game_scene || !fb_pixels) {
        return result;
    }
    uint_fast32_t pre_serial = atomic_load_explicit(&g_render_thread.rendered_frame_serial, memory_order_acquire);
    uint32_t expected_game_generation = 0u;
    publish_frame_scene_slot_record(VIEWER_SLOT_GAME, game_scene, &expected_game_generation);
    (void)pthread_mutex_lock(&g_render_thread.render_done_mutex);
    while (true) {
        uint_fast32_t serial = atomic_load_explicit(&g_render_thread.rendered_frame_serial, memory_order_acquire);
        uint32_t rendered_game_generation = 0u;
        if (pthread_mutex_lock(&g_render_thread.mutex) == 0) {
            rendered_game_generation = g_render_thread.last_rendered_generation[VIEWER_SLOT_GAME];
            (void)pthread_mutex_unlock(&g_render_thread.mutex);
        }
        if (serial != pre_serial && rendered_game_generation >= expected_game_generation) {
            break;
        }
        (void)pthread_cond_wait(&g_render_thread.render_done_cond, &g_render_thread.render_done_mutex);
    }
    (void)pthread_mutex_unlock(&g_render_thread.render_done_mutex);

    uint64_t main_lock_begin_ns = monotonic_now_ns();
    if (pthread_mutex_lock(&g_render_thread.mutex) == 0) {
        uint64_t main_lock_acquired_ns = monotonic_now_ns();
        result.frame_serial = atomic_load_explicit(&g_render_thread.rendered_frame_serial, memory_order_acquire);
        result.dirty_pixels = g_render_thread.last_dirty_pixels;
        result.changed_slots = g_render_thread.last_changed_slots;
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
    return result;
}

static void viewer_expand_rgb565_to_window(const uint16_t *src, uint32_t *dst, size_t count) {
    if (!src || !dst) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        dst[i] = rgb565_to_xrgb8888(src[i]);
    }
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
    bool demo_bundle_initialized = false;
    ViewerDemoBundle demo_bundle = {0};
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

    if (!init_demo_bundle(viewer_eval_state, &demo_bundle)) {
        fprintf(stderr, "Failed to create host-viewer demo bundle from tiny-gfx.host-viewer-demo\n");
        goto cleanup;
    }
    demo_bundle_initialized = true;

    char score_line[64];
    (void)snprintf(score_line, sizeof(score_line), "SCORE 0000    LIFES 3");

    ViewerGameplayState gameplay_state = {0};
    uint_fast32_t last_presented_frame_serial = 0u;
    ViewerRuntimeFlags runtime_flags = {
        .periodic_score_updates_enabled = true
    };
    VgRenderSlotState sync_slot_states[VIEWER_SLOT_COUNT] = {0};
    uint32_t sync_slot_generation = 1u;
    const uint64_t target_frame_ns = 1000000000ull / TARGET_FPS;
    uint64_t next_frame_deadline_ns = monotonic_now_ns() + target_frame_ns;
    uint64_t last_present_ns = 0u;
    TimingAccumulator frame_dt_stats;
    TimingAccumulator waitsync_stats;
    TimingAccumulator update_stats;
    timing_accumulator_reset(&frame_dt_stats);
    timing_accumulator_reset(&waitsync_stats);
    timing_accumulator_reset(&update_stats);
    vg_framebuffer_clear(&fb, SCENE_ERASE_COLOR);
    publish_frame_scene_slot_record(VIEWER_SLOT_DECO, demo_bundle.deco_scene, NULL);
    publish_frame_scene_slot_record(VIEWER_SLOT_SCORE, demo_bundle.score_scene, NULL);
    publish_frame_scene_slot_record(VIEWER_SLOT_GAME, demo_bundle.game_scene, NULL);

    while (true) {
        float time_s = (float)mfb_timer_now(timer);
        if (!viewer_wait_for_frame_pacing(window,
                                          runtime_flags.use_mfb_waitsync,
                                          target_frame_ns,
                                          &next_frame_deadline_ns,
                                          &waitsync_stats)) {
            break;
        }

        const uint8_t *keys = mfb_get_key_buffer(window);
        if (viewer_should_exit_for_keys(keys)) {
            break;
        }
        viewer_update_runtime_flags(keys, &runtime_flags, &next_frame_deadline_ns, target_frame_ns);
        uint64_t gameplay_now_ns = monotonic_now_ns();
        uint32_t gameplay_now_ms = (uint32_t)(gameplay_now_ns / 1000000u);
        uint32_t gameplay_dt_ms = 1000u / TARGET_FPS;
        if (gameplay_state.last_update_ms != 0u) {
            uint32_t raw_dt_ms = gameplay_now_ms - gameplay_state.last_update_ms;
            if (raw_dt_ms == 0u) {
                raw_dt_ms = 1u;
            }
            if (raw_dt_ms > 250u) {
                raw_dt_ms = 250u;
            }
            gameplay_dt_ms = raw_dt_ms;
        }
        gameplay_state.last_update_ms = gameplay_now_ms;
        viewer_apply_gameplay_step(&demo_bundle, &gameplay_state, gameplay_now_ms, gameplay_dt_ms);

        ViewerFrameRenderResult frame_result = runtime_flags.sync_mode
                                                   ? viewer_render_game_frame_sync(demo_bundle.game_scene,
                                                                                   sync_slot_states,
                                                                                   &sync_slot_generation,
                                                                                   &fb)
                                                   : viewer_render_game_frame_async(demo_bundle.game_scene, fb_pixels);

        const uint16_t *src = runtime_flags.sync_mode ? fb_pixels : g_present_copy_rgb565;
        viewer_expand_rgb565_to_window(src, window_pixels, (size_t)VIEW_W * (size_t)VIEW_H);

        if (frame_result.frame_serial != last_presented_frame_serial) {
            perf_window_record_frame(&perf_window, frame_result.dirty_pixels, frame_result.changed_slots);
            last_presented_frame_serial = frame_result.frame_serial;
        }

        ViewerPerfSnapshot perf_snapshot;
        bool perf_ready = perf_window_take_snapshot_if_due(&perf_window, (double)time_s, &perf_snapshot);
        if (perf_ready) {
            collect_thread_lock_metrics(&perf_snapshot);
        }
#if defined(__APPLE__)
        if (perf_ready) {
            if (runtime_flags.periodic_score_updates_enabled) {
                int score = (int)(time_s * 120.0f);
                (void)snprintf(score_line, sizeof(score_line), "SCORE %04d    LIFES 3", score % 10000);
                ASSIGN(demo_bundle.score_text->text, AUTORELEASE(make_string(score_line)));
            }
            double dt_avg_ms = timing_accumulator_avg_ms(&frame_dt_stats);
            double dt_min_ms = timing_accumulator_min_ms(&frame_dt_stats);
            double dt_max_ms = timing_accumulator_max_ms(&frame_dt_stats);
            double ws_avg_ms = timing_accumulator_avg_ms(&waitsync_stats);
            double ws_min_ms = timing_accumulator_min_ms(&waitsync_stats);
            double ws_max_ms = timing_accumulator_max_ms(&waitsync_stats);
            double up_avg_ms = timing_accumulator_avg_ms(&update_stats);
            double up_min_ms = timing_accumulator_min_ms(&update_stats);
            double up_max_ms = timing_accumulator_max_ms(&update_stats);
            timing_accumulator_reset(&frame_dt_stats);
            timing_accumulator_reset(&waitsync_stats);
            timing_accumulator_reset(&update_stats);
            char title[192];
            (void)snprintf(title,
                           sizeof(title),
                           "[%s|%s|SC%s] | FPS %.1f | dt %.1f/%.1f/%.1fms | ws %.1f/%.1f/%.1fms | up %.1f/%.1f/%.1fms",
                           runtime_flags.sync_mode ? "SYNC" : "ASYNC",
                           runtime_flags.use_mfb_waitsync ? "WAITSYNC" : "CUSTOM",
                           runtime_flags.periodic_score_updates_enabled ? "ON" : "OFF",
                           perf_snapshot.fps,
                           dt_min_ms, dt_avg_ms, dt_max_ms,
                           ws_min_ms, ws_avg_ms, ws_max_ms,
                           up_min_ms, up_avg_ms, up_max_ms);
            macos_viewer_set_window_title(title);
            if (runtime_flags.periodic_score_updates_enabled) {
                publish_frame_scene_slot_record(VIEWER_SLOT_SCORE, demo_bundle.score_scene, NULL);
            }
        }
#else
        (void)perf_snapshot;
        (void)perf_ready;
#endif

        uint64_t now_ns = monotonic_now_ns();
        if (last_present_ns > 0u) {
            uint64_t dt = (now_ns > last_present_ns) ? (now_ns - last_present_ns) : 0u;
            timing_accumulator_add(&frame_dt_stats, dt);
        }
        last_present_ns = now_ns;

        uint64_t update_begin_ns = monotonic_now_ns();
        mfb_update_state state = mfb_update_ex(window, window_pixels, VIEW_W, VIEW_H);
        uint64_t update_end_ns = monotonic_now_ns();
        uint64_t update_ns = (update_end_ns > update_begin_ns)
                                 ? (update_end_ns - update_begin_ns)
                                 : 0u;
        timing_accumulator_add(&update_stats, update_ns);
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
    if (demo_bundle_initialized) {
        destroy_demo_bundle(&demo_bundle);
    }
    runtime_reset(&g_runtime);
    return exit_code;
#endif
}
