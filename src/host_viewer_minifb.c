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
#include "eval.h"
#include "value.h"
#include "callbacks.h"
#include "runtime.h"
#include "record.h"
#include "event_loop.h"
#include "renderer_lifecycle.h"
#include "rendered_state_snapshot.h"
#include "viewer_collision.h"
#include "platform.h"
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
#define TARGET_FPS              60u
#define SCENE_ERASE_COLOR       0x0000u
#define RGB565_BYTES_PER_PIXEL 2u
#define VIEWER_ANIMATED_WAIT_TIMEOUT_MS 8u

static uint64_t monotonic_now_ns(void);
typedef struct ViewerDemoBundle ViewerDemoBundle;
typedef struct ViewerCollisionPolicy ViewerCollisionPolicy;

static inline uint32_t viewer_record_type_hash(ID obj) {
    CljPersistentRecord *r = (CljPersistentRecord *)obj;
    return r->descriptor ? clj_hash(r->descriptor->type_symbol) : 0u;
}

static int32_t viewer_fixed_raw_to_int_trunc_zero(int32_t raw) {
    if (raw >= 0) {
        return raw >> CLJ_FIXED_FRAC_BITS;
    }
    return -(((-raw) >> CLJ_FIXED_FRAC_BITS));
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


typedef struct {
    bool use_mfb_waitsync;
    bool w_key_was_down;
} ViewerRuntimeFlags;

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
    if (viewer_key_pressed_once(keys, KB_KEY_W, &flags->w_key_was_down)) {
        flags->use_mfb_waitsync = !flags->use_mfb_waitsync;
        *next_frame_deadline_ns = monotonic_now_ns() + target_frame_ns;
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
    uint32_t max_dirty_px_frame;
} ViewerPerfWindow;

typedef struct {
    double fps;
    double avg_dirty_px_per_frame;
    double dirty_ratio;
    double dirty_bytes_per_s;
    double full_bytes_per_s;
    double avg_changed_slots;
    double max_dirty_bytes_per_frame;
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
    DEMO_BUNDLE_COUNT = 3
};

struct ViewerDemoBundle {
    ID bundle_root;
    FrameScene *deco_scene;
    FrameScene *score_scene;
    FrameScene *game_scene;
};

struct ViewerCollisionPolicy {
    int player_entity_id;
    int obstacle_entity_id;
    int player_min_x;
    int player_max_x;
    int player_min_y_base;
    int player_max_y_base;
    int obstacle_min_x_base;
    int obstacle_max_x_base;
    int obstacle_min_y;
    int obstacle_max_y;
    int obstacle_anchor_x;
    uint32_t cooldown_ms;
};

static void destroy_demo_bundle(ViewerDemoBundle *bundle) {
    if (!bundle) {
        return;
    }
    RELEASE(bundle->bundle_root);
    memset(bundle, 0, sizeof(*bundle));
}

static bool viewer_extract_demo_bundle(ID bundle, ViewerDemoBundle *out_bundle) {
    if (!bundle || !out_bundle || !is_vector(bundle)) {
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
    return out_bundle->deco_scene && out_bundle->score_scene && out_bundle->game_scene;
}

static bool viewer_policy_int_at(CljPersistentVector *vec, uint32_t idx, int *out_value) {
    if (!vec || !out_value) {
        return false;
    }
    ID v = vector_nth(vec, idx);
    if (!v || !is_fixnum(v)) {
        return false;
    }
    *out_value = AS_FIXNUM(v);
    return true;
}

static bool viewer_load_host_viewer_config(EvalState *st,
                                           ViewerDemoBundle *out_bundle,
                                           ViewerCollisionPolicy *out_policy) {
    if (!st || !out_bundle || !out_policy) {
        return false;
    }
    memset(out_bundle, 0, sizeof(*out_bundle));
    memset(out_policy, 0, sizeof(*out_policy));
    if (!require_namespace_by_name(st, "tiny-gfx.runtime")) {
        return false;
    }
    ID cfg = eval_string("(tiny-gfx.runtime/host-viewer-config)", st);
    if (!cfg || !is_map(cfg)) {
        return false;
    }
    static CljSymbol *k_bundle = NULL;
    static CljSymbol *k_collision_policy = NULL;
    static CljSymbol *k_collision_entity_ids = NULL;
    if (!k_bundle) {
        k_bundle = intern_symbol_global(":bundle");
        k_collision_policy = intern_symbol_global(":collision-policy");
        k_collision_entity_ids = intern_symbol_global(":collision-entity-ids");
    }
    if (!k_bundle || !k_collision_policy || !k_collision_entity_ids) {
        return false;
    }
    ID bundle = map_get_sentinel(cfg, k_bundle, NULL);
    ID policy = map_get_sentinel(cfg, k_collision_policy, NULL);
    ID ids = map_get_sentinel(cfg, k_collision_entity_ids, NULL);
    if (!viewer_extract_demo_bundle(bundle, out_bundle)) {
        return false;
    }
    if (!policy || !is_vector(policy) || !ids || !is_vector(ids)) {
        destroy_demo_bundle(out_bundle);
        return false;
    }
    CljPersistentVector *policy_vec = as_vector(policy);
    CljPersistentVector *ids_vec = as_vector(ids);
    if (!policy_vec || !ids_vec) {
        destroy_demo_bundle(out_bundle);
        return false;
    }
    int vals[9] = {0};
    for (uint32_t i = 0; i < 9u; i++) {
        if (!viewer_policy_int_at(policy_vec, i, &vals[i])) {
            destroy_demo_bundle(out_bundle);
            return false;
        }
    }
    int cooldown_val = 0;
    int player_id = 0;
    int obstacle_id = 0;
    if (!viewer_policy_int_at(policy_vec, 9u, &cooldown_val) ||
        cooldown_val < 0 ||
        !viewer_policy_int_at(ids_vec, 0u, &player_id) ||
        !viewer_policy_int_at(ids_vec, 1u, &obstacle_id)) {
        destroy_demo_bundle(out_bundle);
        return false;
    }
    out_policy->player_entity_id = player_id;
    out_policy->obstacle_entity_id = obstacle_id;
    out_policy->player_min_x = vals[0];
    out_policy->player_max_x = vals[1];
    out_policy->player_min_y_base = vals[2];
    out_policy->player_max_y_base = vals[3];
    out_policy->obstacle_min_x_base = vals[4];
    out_policy->obstacle_max_x_base = vals[5];
    out_policy->obstacle_min_y = vals[6];
    out_policy->obstacle_max_y = vals[7];
    out_policy->obstacle_anchor_x = vals[8];
    out_policy->cooldown_ms = (uint32_t)cooldown_val;
    return true;
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
    if (dirty_pixels > perf->max_dirty_px_frame) {
        perf->max_dirty_px_frame = dirty_pixels;
    }
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
        out_snapshot->max_dirty_bytes_per_frame = (double)perf->max_dirty_px_frame * (double)RGB565_BYTES_PER_PIXEL;
    }
    perf->window_start_s = now_s;
    perf->window_frames = 0u;
    perf->window_dirty_pixels = 0u;
    perf->window_changed_slots = 0u;
    perf->max_dirty_px_frame = 0u;
    return true;
}

static CljAtom *g_scene_slot_atoms[VIEWER_SLOT_COUNT] = {0};
static VgSlotChangeTracker g_slot_change_tracker;

/*
 * Two-buffer model matching ESP32 SPI/I80 hardware:
 *
 *   g_render_buffer  = MCU-local render target (private to render thread)
 *   g_gram_pixels    = display GRAM (read by UI thread for presentation)
 *
 * The render thread erases + draws into g_render_buffer. After rendering is
 * complete, the dirty region is copied to g_gram_pixels — simulating the
 * SPI/DMA transfer from MCU RAM to the display's internal GRAM.
 *
 * The UI thread only reads g_gram_pixels, so it never sees the intermediate
 * erased state of g_render_buffer. Each pixel in the GRAM transitions
 * directly from old → new, matching real SPI display behavior.
 */
static uint16_t g_render_buffer[VIEW_W * VIEW_H];
static uint16_t *g_gram_pixels = NULL;
static const uint8_t g_slot_render_priority[VIEWER_SLOT_COUNT] = {
    VIEWER_SLOT_GAME,
    VIEWER_SLOT_SCORE,
    VIEWER_SLOT_DECO
};
typedef struct {
    pthread_t thread;
    pthread_mutex_t mutex;
    atomic_bool running;
    bool started;
    VgRenderSlotState slot_states[VIEWER_SLOT_COUNT];
    uint32_t slot_seen_generations[VIEWER_SLOT_COUNT];
    atomic_uint_fast32_t rendered_frame_serial;
    atomic_uint_fast32_t last_dirty_pixels;
    atomic_uint_fast32_t last_changed_slots;
    uint32_t last_rendered_generation[VIEWER_SLOT_COUNT];
    atomic_uint_fast64_t render_lock_hold_ns_total;
    atomic_uint_fast64_t render_lock_hold_ns_max;
    atomic_uint_fast64_t render_lock_samples;
    atomic_uint_fast64_t skipped_generations_total;
    atomic_uint_fast64_t skipped_max_frame;
    atomic_uint_fast64_t skipped_max_slot;
    atomic_uint_fast32_t animated_slots_mask;
} ViewerRenderThread;
static ViewerRenderThread g_render_thread = {0};

static uint32_t viewer_compute_animated_slots_mask(const VgRenderSlotState *slot_states) {
    if (!slot_states) {
        return 0u;
    }
    uint32_t mask = 0u;
    for (uint8_t i = 0; i < VIEWER_SLOT_COUNT; i++) {
        if (slot_states[i].initialized && slot_states[i].has_animation) {
            mask |= (1u << i);
        }
    }
    return mask;
}

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

static void collect_render_thread_metrics(ViewerPerfSnapshot *out_snapshot) {
    if (!out_snapshot) {
        return;
    }
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

/* Render-thread loop: changed slots render immediately; animated slots tick continuously. */
static void *viewer_render_thread_main(void *arg) {
    VgFrameBuffer *fb = (VgFrameBuffer *)arg;
    if (!fb) {
        return NULL;
    }
    viewer_set_realtime_thread_policy();
    while (atomic_load_explicit(&g_render_thread.running, memory_order_acquire)) {
        uint32_t animated_mask = (uint32_t)atomic_load_explicit(&g_render_thread.animated_slots_mask,
                                                                 memory_order_acquire);
        uint32_t wait_timeout_ms = (animated_mask == 0u) ? UINT32_MAX : VIEWER_ANIMATED_WAIT_TIMEOUT_MS;
        uint32_t slot_generations[VIEWER_SLOT_COUNT] = {0};
        uint32_t changed_mask = vg_slot_change_tracker_wait_for_changes(&g_slot_change_tracker,
                                                                        g_render_thread.slot_seen_generations,
                                                                        slot_generations,
                                                                        wait_timeout_ms);
        if (!atomic_load_explicit(&g_render_thread.running, memory_order_acquire)) {
            break;
        }
        if (changed_mask == 0u && animated_mask == 0u) {
            continue;
        }

        uint32_t frame_now_ms = platform_current_time_ms();
        uint32_t frame_dirty_pixels = 0u;
        uint32_t frame_changed_slots = 0u;
        uint64_t frame_skipped_total = 0u;
        if (pthread_mutex_lock(&g_render_thread.mutex) != 0) {
            continue;
        }
        uint64_t lock_acquired_ns = monotonic_now_ns();
        for (size_t p = 0; p < VIEWER_SLOT_COUNT; p++) {
            uint8_t i = g_slot_render_priority[p];
            bool slot_changed = (changed_mask & (1u << i)) != 0u;
            bool slot_animated_tick = !slot_changed && ((animated_mask & (1u << i)) != 0u);
            if (!slot_changed && !slot_animated_tick) {
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
            vg_rendered_state_capture_begin(i, slot_generations[i], frame_now_ms);
            bool rendered = vg_render_frame_slot_record_at_ms(snapshot,
                                                              &g_render_thread.slot_states[i],
                                                              fb,
                                                              slot_generations[i],
                                                              frame_now_ms,
                                                              slot_animated_tick,
                                                              &dirty_pixels);
            if (rendered) {
                vg_rendered_state_capture_commit();
            } else {
                vg_rendered_state_capture_discard();
            }
            if (rendered) {
                uint32_t prev_gen = g_render_thread.last_rendered_generation[i];
                uint32_t curr_gen = slot_generations[i];
                if (slot_changed && curr_gen > (prev_gen + 1u)) {
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
        atomic_store_explicit(&g_render_thread.animated_slots_mask,
                              viewer_compute_animated_slots_mask(g_render_thread.slot_states),
                              memory_order_release);
        atomic_store_explicit(&g_render_thread.last_dirty_pixels, frame_dirty_pixels, memory_order_relaxed);
        atomic_store_explicit(&g_render_thread.last_changed_slots, frame_changed_slots, memory_order_relaxed);
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
        /*
         * Simulate SPI/DMA transfer: copy finished render result to GRAM.
         * This runs outside the mutex — the render buffer is only written by
         * this thread, and g_gram_pixels is the "display bus" target.
         * The UI thread may read g_gram_pixels during this copy, seeing a mix
         * of old + new pixels (tearing) — same as real SPI display behavior.
         * Critically, it will NEVER see the intermediate erased (black) state,
         * because each pixel goes directly old → new.
         */
        if (g_gram_pixels) {
            memcpy(g_gram_pixels, g_render_buffer, sizeof(g_render_buffer));
        }
        atomic_fetch_add_explicit(&g_render_thread.rendered_frame_serial, 1u, memory_order_release);
    }
    return NULL;
}

/* Start dedicated render thread that owns slot rendering. */
static bool start_render_thread(VgFrameBuffer *fb) {
    if (!fb) {
        return false;
    }
    memset(&g_render_thread, 0, sizeof(g_render_thread));
    atomic_store_explicit(&g_render_thread.animated_slots_mask, 0u, memory_order_release);
    if (pthread_mutex_init(&g_render_thread.mutex, NULL) != 0) {
        return false;
    }
    atomic_store_explicit(&g_render_thread.running, true, memory_order_release);
    if (pthread_create(&g_render_thread.thread, NULL, viewer_render_thread_main, fb) != 0) {
        atomic_store_explicit(&g_render_thread.running, false, memory_order_release);
        (void)pthread_mutex_destroy(&g_render_thread.mutex);
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
    (void)pthread_join(g_render_thread.thread, NULL);
    (void)pthread_mutex_destroy(&g_render_thread.mutex);
    memset(&g_render_thread, 0, sizeof(g_render_thread));
}

static bool viewer_renderer_start_callback(ID slot_atoms, void *user_data) {
    (void)slot_atoms;
    VgFrameBuffer *fb = (VgFrameBuffer *)user_data;
    return start_render_thread(fb);
}

static bool viewer_renderer_stop_callback(void *user_data) {
    (void)user_data;
    stop_render_thread();
    return true;
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

static ID viewer_collision_dispatch_fn(EvalState *st) {
    if (!st) {
        return NULL;
    }
    static CljSymbol *invoke_callback_sym = NULL;
    if (!invoke_callback_sym) {
        CljSymbol *collision_ns = intern_symbol_global("tiny-gfx.collision");
        if (!collision_ns) {
            return NULL;
        }
        invoke_callback_sym = intern_symbol(collision_ns, "invoke-collision-callback!");
        if (!invoke_callback_sym) {
            return NULL;
        }
    }
    ID callback_dispatch_fn = ns_resolve(st, invoke_callback_sym);
    if (!callback_dispatch_fn || callback_dispatch_fn == NOT_FOUND) {
        return NULL;
    }
    unsigned char fn_tag = TAG(callback_dispatch_fn);
    if (fn_tag != CLJ_FUNC && fn_tag != CLJ_CLOSURE) {
        return NULL;
    }
    return callback_dispatch_fn;
}

static bool viewer_invoke_collision_callback(EvalState *st) {
    if (!st) {
        return false;
    }
    ID callback_dispatch_fn = NULL;
    bool success = false;
    TRY {
        callback_dispatch_fn = viewer_collision_dispatch_fn(st);
        if (!callback_dispatch_fn || callback_dispatch_fn == NOT_FOUND) {
            success = false;
        } else {
            /*
             * Dispatch callback through the event-loop API so execution happens
             * on the Clojure runloop path. Callback return values are intentionally
             * ignored by the C host bridge.
             */
            if (!event_loop_enqueue_ingress(callback_dispatch_fn)) {
                success = false;
            } else {
                success = event_loop_run_next(NULL, st);
            }
        }
    } CATCH(ex) {
        (void)ex;
        success = false;
    } END_TRY
    return success;
}

static FrameScene *viewer_current_game_scene_from_clojure(EvalState *st) {
    if (!st) {
        return NULL;
    }
    static CljSymbol *game_scene_state_sym = NULL;
    if (!game_scene_state_sym) {
        CljSymbol *demo_ns = intern_symbol_global("tiny-gfx.host-viewer-demo");
        if (!demo_ns) {
            return NULL;
        }
        game_scene_state_sym = intern_symbol(demo_ns, "game-scene-state");
        if (!game_scene_state_sym) {
            return NULL;
        }
    }
    ID state_atom = ns_resolve(st, game_scene_state_sym);
    if (!state_atom || state_atom == NOT_FOUND || TAG(state_atom) != CLJ_ATOM) {
        return NULL;
    }
    ID scene = ((CljAtom *)state_atom)->value;
    if (!scene || TAG(scene) != CLJ_RECORD) {
        return NULL;
    }
    const VgRecordSchema *schema = tiny_gfx_schema();
    if (!schema || viewer_record_type_hash(scene) != schema->h_frame_scene) {
        return NULL;
    }
    return (FrameScene *)scene;
}

static bool viewer_apply_collision_step(ViewerDemoBundle *bundle,
                                        VgCollisionState *state,
                                        const ViewerCollisionPolicy *policy,
                                        EvalState *st,
                                        uint32_t now_ms) {
    if (!bundle || !state || !policy || !st || !bundle->game_scene) {
        return false;
    }

    VgRenderedEntityState player_state;
    VgRenderedEntityState obstacle_state;
    bool have_player = vg_rendered_state_query_entity(VIEWER_SLOT_GAME,
                                                      (uintptr_t)fixnum(policy->player_entity_id),
                                                      &player_state);
    bool have_obstacle = vg_rendered_state_query_entity(VIEWER_SLOT_GAME,
                                                        (uintptr_t)fixnum(policy->obstacle_entity_id),
                                                        &obstacle_state);
    if (!have_player || !have_obstacle) {
        return false;
    }

    int player_tx = (int)viewer_fixed_raw_to_int_trunc_zero(player_state.world_t.m02);
    int player_ty = (int)viewer_fixed_raw_to_int_trunc_zero(player_state.world_t.m12);
    int obstacle_x = (int)viewer_fixed_raw_to_int_trunc_zero(obstacle_state.world_t.m02) -
                     policy->obstacle_anchor_x;
    VgAabb player_box = {
        .min_x = policy->player_min_x + player_tx,
        .max_x = policy->player_max_x + player_tx,
        .min_y = policy->player_min_y_base + player_ty,
        .max_y = policy->player_max_y_base + player_ty
    };
    VgAabb obstacle_box = {
        .min_x = policy->obstacle_min_x_base + obstacle_x,
        .max_x = policy->obstacle_max_x_base + obstacle_x,
        .min_y = policy->obstacle_min_y,
        .max_y = policy->obstacle_max_y
    };
    bool colliding = vg_collision_detect_aabb_overlap(&player_box, &obstacle_box);
    bool toggled = vg_collision_step_latched_cooldown(state,
                                                       now_ms,
                                                       policy->cooldown_ms,
                                                       colliding);
    if (!toggled) {
        return false;
    }

    if (!viewer_invoke_collision_callback(st)) {
        return false;
    }
    FrameScene *updated_game_scene = viewer_current_game_scene_from_clojure(st);
    if (!updated_game_scene) {
        return false;
    }
    bundle->game_scene = updated_game_scene;

    if (pthread_mutex_lock(&g_render_thread.mutex) != 0) {
        return false;
    }
    CljAtom *slot_atom = g_scene_slot_atoms[VIEWER_SLOT_GAME];
    if (slot_atom) {
        ASSIGN(slot_atom->value, updated_game_scene);
    }
    (void)pthread_mutex_unlock(&g_render_thread.mutex);
    (void)vg_slot_change_tracker_publish(&g_slot_change_tracker, VIEWER_SLOT_GAME, NULL);
    return true;
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

/*
 * Lock-free frame polling: reads atomic counters from the render thread.
 * The main thread reads fb_pixels directly (no copy), faithfully simulating
 * ESP32 SPI/I80 displays where the bus reads the live GRAM with no double-buffer.
 * Tearing is possible and accepted — same as on real hardware.
 */
static ViewerFrameRenderResult viewer_poll_render_frame(void) {
    ViewerFrameRenderResult result = {0};
    result.frame_serial = atomic_load_explicit(&g_render_thread.rendered_frame_serial, memory_order_acquire);
    result.dirty_pixels = atomic_load_explicit(&g_render_thread.last_dirty_pixels, memory_order_relaxed);
    result.changed_slots = atomic_load_explicit(&g_render_thread.last_changed_slots, memory_order_relaxed);
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
    ViewerCollisionPolicy collision_policy = {0};
    VgCollisionState collision_state = {0};
    int exit_code = 1;
    viewer_set_realtime_thread_policy();

    g_gram_pixels = fb_pixels;
    memset(g_render_buffer, 0, sizeof(g_render_buffer));
    memset(fb_pixels, 0, sizeof(uint16_t) * VIEW_W * VIEW_H);
    VgFrameBuffer fb;
    if (!vg_framebuffer_init(&fb, VIEW_W, VIEW_H, g_render_buffer, VIEW_W * VIEW_H)) {
        fprintf(stderr, "Failed to initialize framebuffer\n");
        return 1;
    }
    runtime_init(&g_runtime);
    event_loop_init();
    vg_rendered_state_reset_all();
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
    tiny_renderer_lifecycle_set_callbacks(viewer_renderer_start_callback,
                                          viewer_renderer_stop_callback,
                                          &fb);
    if (!tiny_renderer_lifecycle_start(NULL)) {
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

    if (!viewer_load_host_viewer_config(viewer_eval_state, &demo_bundle, &collision_policy)) {
        fprintf(stderr, "Failed to load host-viewer config from tiny-gfx.runtime/host-viewer-config\n");
        goto cleanup;
    }
    demo_bundle_initialized = true;

    uint_fast32_t last_presented_frame_serial = 0u;
    ViewerRuntimeFlags runtime_flags = {
        .use_mfb_waitsync = true,
        .w_key_was_down = false
    };
    const uint64_t target_frame_ns = 1000000000ull / TARGET_FPS;
    uint64_t next_frame_deadline_ns = monotonic_now_ns() + target_frame_ns;
    uint64_t last_present_ns = 0u;
    TimingAccumulator frame_dt_stats;
    TimingAccumulator waitsync_stats;
    TimingAccumulator update_stats;
    timing_accumulator_reset(&frame_dt_stats);
    timing_accumulator_reset(&waitsync_stats);
    timing_accumulator_reset(&update_stats);
    uint32_t long_frame_count = 0u;
    vg_framebuffer_clear(&fb, SCENE_ERASE_COLOR);
    memcpy(fb_pixels, g_render_buffer, sizeof(g_render_buffer));
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

        ViewerFrameRenderResult frame_result = viewer_poll_render_frame();
        (void)viewer_apply_collision_step(&demo_bundle,
                                          &collision_state,
                                          &collision_policy,
                                          viewer_eval_state,
                                          platform_current_time_ms());

        viewer_expand_rgb565_to_window(fb_pixels, window_pixels, (size_t)VIEW_W * (size_t)VIEW_H);

        if (frame_result.frame_serial != last_presented_frame_serial) {
            perf_window_record_frame(&perf_window, frame_result.dirty_pixels, frame_result.changed_slots);
            last_presented_frame_serial = frame_result.frame_serial;
        }

        ViewerPerfSnapshot perf_snapshot;
        bool perf_ready = perf_window_take_snapshot_if_due(&perf_window, (double)time_s, &perf_snapshot);
        if (perf_ready) {
            collect_render_thread_metrics(&perf_snapshot);
        }
#if defined(__APPLE__)
        if (perf_ready) {
            double dt_avg_ms = timing_accumulator_avg_ms(&frame_dt_stats);
            double dt_max_ms = timing_accumulator_max_ms(&frame_dt_stats);
            double ws_avg_ms = timing_accumulator_avg_ms(&waitsync_stats);
            double up_avg_ms = timing_accumulator_avg_ms(&update_stats);
            timing_accumulator_reset(&frame_dt_stats);
            timing_accumulator_reset(&waitsync_stats);
            timing_accumulator_reset(&update_stats);
            /*
             * Keep the title intentionally short: macOS truncates long titles,
             * and skip diagnostics should stay visible even in narrow windows.
             */
            double full_frame_kb = (double)(VIEW_W * VIEW_H * RGB565_BYTES_PER_PIXEL) / 1024.0;
            double max_bw_kb = perf_snapshot.max_dirty_bytes_per_frame / 1024.0;
            char title[200];
            (void)snprintf(title,
                           sizeof(title),
                           "[%s] FPS %.1f bw %.1f/%.0fKB sk %llu/%llu lf %u dmx %.1f lk %.0fus dt %.1f up %.1f",
                           runtime_flags.use_mfb_waitsync ? "WAITSYNC" : "CUSTOM",
                           perf_snapshot.fps,
                           max_bw_kb,
                           full_frame_kb,
                           (unsigned long long)perf_snapshot.skipped_generations,
                           (unsigned long long)perf_snapshot.skipped_max_frame,
                           long_frame_count,
                           dt_max_ms,
                           perf_snapshot.max_render_lock_hold_us,
                           dt_avg_ms,
                           up_avg_ms);
            macos_viewer_set_window_title(title);
            if (perf_snapshot.skipped_generations > 0u) {
                fprintf(stderr,
                        "[viewer] skip-diag: total=%llu frame-max=%llu slot-max=%llu "
                        "lock-max=%.0fus fps=%.1f dt=%.1f ws=%.1f up=%.1f\n",
                        (unsigned long long)perf_snapshot.skipped_generations,
                        (unsigned long long)perf_snapshot.skipped_max_frame,
                        (unsigned long long)perf_snapshot.skipped_max_slot,
                        perf_snapshot.max_render_lock_hold_us,
                        perf_snapshot.fps,
                        dt_avg_ms,
                        ws_avg_ms,
                        up_avg_ms);
            }
            if (long_frame_count > 0u) {
                fprintf(stderr,
                        "[viewer] stutter-diag: long=%u dt-max=%.1fms "
                        "fps=%.1f dt=%.1f ws=%.1f up=%.1f\n",
                        long_frame_count,
                        dt_max_ms,
                        perf_snapshot.fps,
                        dt_avg_ms,
                        ws_avg_ms,
                        up_avg_ms);
            }
            long_frame_count = 0u;
        }
#else
        (void)perf_snapshot;
        (void)perf_ready;
#endif

        uint64_t now_ns = monotonic_now_ns();
        if (last_present_ns > 0u) {
            uint64_t dt = (now_ns > last_present_ns) ? (now_ns - last_present_ns) : 0u;
            timing_accumulator_add(&frame_dt_stats, dt);
            if (dt > 20000000ull) {
                long_frame_count++;
            }
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
        (void)tiny_renderer_lifecycle_stop();
    }
    tiny_renderer_lifecycle_set_callbacks(NULL, NULL, NULL);
    if (slot_atoms_initialized) {
        destroy_scene_slot_atoms();
    }
    if (slot_tracker_initialized) {
        vg_slot_change_tracker_destroy(&g_slot_change_tracker);
    }
    if (demo_bundle_initialized) {
        destroy_demo_bundle(&demo_bundle);
    }
    g_gram_pixels = NULL;
    runtime_reset(&g_runtime);
    return exit_code;
#endif
}
