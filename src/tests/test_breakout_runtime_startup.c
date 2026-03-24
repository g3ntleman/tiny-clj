#include <stdbool.h>
#include <unistd.h>

#ifndef TINYCLJ_WITH_MINIFB
#define TINYCLJ_WITH_MINIFB 1
#endif
#if defined(__APPLE__)
static int g_macos_runloop_watchdog_start_calls = 0;
static int g_macos_runloop_watchdog_stop_calls = 0;

static void breakout_reset_macos_watchdog_counters(void) {
    g_macos_runloop_watchdog_start_calls = 0;
    g_macos_runloop_watchdog_stop_calls = 0;
}

void macos_viewer_install_menu(void) {}
void macos_viewer_set_window_title(const char *title) { (void)title; }
void macos_viewer_register_window_callbacks(void) {}
void macos_viewer_restore_window_position(void) {}
void macos_viewer_save_window_position(void) {}
void macos_viewer_activate_app_window(void) {}
bool macos_viewer_get_content_size(unsigned *out_w, unsigned *out_h) {
    (void)out_w;
    (void)out_h;
    return false;
}
void macos_viewer_begin_performance_activity(void) {}
void macos_viewer_end_performance_activity(void) {}
void macos_viewer_start_runloop_watchdog(void) { g_macos_runloop_watchdog_start_calls++; }
void macos_viewer_stop_runloop_watchdog(void) { g_macos_runloop_watchdog_stop_calls++; }
#endif
#include "../viewer_spatial_bridge.h"
#define main tinyclj_viewer_host_app_test_main
#include "../viewer_host_app.c"
#undef main
#include "../tiny_clj.h"
#include "unity/src/unity.h"
#include "test_registry.h"

typedef struct {
    ViewerSceneBundle bundle;
    ViewerSpatialRuleSet spatial_rules;
    EvalState *st;
} BreakoutViewerTestContext;

static ViewerCollisionPolicy *breakout_find_policy_by_id(ViewerSpatialRuleSet *rules, ID rule_id) {
    if (!rules || !rule_id) {
        return NULL;
    }
    for (uint32_t i = 0; i < rules->count; i++) {
        if (rules->items[i].rule_id == rule_id) {
            return &rules->items[i];
        }
    }
    return NULL;
}

static bool breakout_viewer_test_context_init_with_heap_budget(BreakoutViewerTestContext *ctx,
                                                               bool apply_host_heap_budget) {
    if (!ctx) {
        return false;
    }
    memset(ctx, 0, sizeof(*ctx));
    stop_runloop_thread();

    ViewerConfigSource config_source = {
        .namespace_name = "tiny-clj.deployment",
        .config_expr = "(tiny-clj.deployment/breakout-host-config)",
        .display_name = "tiny-clj.deployment/breakout-host-config",
    };

    runtime_init(&g_runtime);
    event_loop_init();
    event_loop_clear();
    vg_rendered_state_reset_all();
    viewer_seed_gpio_key_levels();
    if (apply_host_heap_budget) {
        tiny_fx_host_apply_heap_limit();
    }

    ctx->st = evalstate_new(true);
    if (!ctx->st) {
        return false;
    }
    evalstate_set_ns(ctx->st, "user");
    if (!tiny_fx_gfx_require_records_namespace(ctx->st) ||
        !tiny_fx_gfx_ensure_schema(ctx->st)) {
        return false;
    }
    if (!viewer_load_deployment_config(ctx->st, config_source, &ctx->bundle, &ctx->spatial_rules)) {
        return false;
    }
    viewer_collision_set_dispatch_context(&ctx->bundle, &ctx->spatial_rules);
    return true;
}

static bool breakout_viewer_test_context_init_with_heap_limit(BreakoutViewerTestContext *ctx,
                                                              size_t heap_limit_bytes) {
    if (!ctx) {
        return false;
    }
    memset(ctx, 0, sizeof(*ctx));
    stop_runloop_thread();

    ViewerConfigSource config_source = {
        .namespace_name = "tiny-clj.deployment",
        .config_expr = "(tiny-clj.deployment/breakout-host-config)",
        .display_name = "tiny-clj.deployment/breakout-host-config",
    };

    runtime_init(&g_runtime);
    event_loop_init();
    event_loop_clear();
    vg_rendered_state_reset_all();
    viewer_seed_gpio_key_levels();
    memory_set_heap_limit_bytes(heap_limit_bytes);

    ctx->st = evalstate_new(true);
    if (!ctx->st) {
        return false;
    }
    evalstate_set_ns(ctx->st, "user");
    if (!tiny_fx_gfx_require_records_namespace(ctx->st) ||
        !tiny_fx_gfx_ensure_schema(ctx->st)) {
        return false;
    }
    if (!viewer_load_deployment_config(ctx->st, config_source, &ctx->bundle, &ctx->spatial_rules)) {
        return false;
    }
    viewer_collision_set_dispatch_context(&ctx->bundle, &ctx->spatial_rules);
    return true;
}

static bool breakout_viewer_test_context_init(BreakoutViewerTestContext *ctx) {
    return breakout_viewer_test_context_init_with_heap_budget(ctx, false);
}

static void breakout_viewer_test_context_destroy(BreakoutViewerTestContext *ctx) {
    if (!ctx) {
        return;
    }
    stop_runloop_thread();
    event_loop_clear();
    viewer_collision_reset_dispatch_state();
    destroy_scene_bundle(&ctx->bundle);
    destroy_spatial_rule_set(&ctx->spatial_rules);
    evalstate_free(ctx->st);
    memset(ctx, 0, sizeof(*ctx));
}

TEST(test_breakout_runtime_startup_host_app_fits_debug_heap_limit) {
    BreakoutViewerTestContext ctx = {0};
    size_t previous_limit = memory_get_heap_limit_bytes();
    size_t host_limit = tiny_fx_host_heap_limit_bytes();
    bool init_ok = false;
    bool caught = false;
    ID caught_ex = NULL;

    TEST_ASSERT_EQUAL_UINT64(614400u, host_limit);
    TRY {
        init_ok = breakout_viewer_test_context_init_with_heap_budget(&ctx, true);
    } CATCH(ex) {
        caught = true;
        caught_ex = ex;
    } END_TRY
    memory_set_heap_limit_bytes(previous_limit);

    if (init_ok) {
        breakout_viewer_test_context_destroy(&ctx);
    }
    TEST_ASSERT_FALSE_MESSAGE(caught, caught_ex ? "breakout host startup should not OOM under 640KB total heap budget" : "");
    TEST_ASSERT_TRUE_MESSAGE(init_ok, "breakout host startup should fit inside the tiny-fx debug heap limit");
}

TEST(test_breakout_runtime_startup_host_app_fits_400k_startup_budget) {
    BreakoutViewerTestContext ctx = {0};
    size_t previous_limit = memory_get_heap_limit_bytes();
    const size_t startup_budget = 400u * 1024u;
    const size_t startup_required_headroom = 2u * 1024u;
    const size_t startup_limit = startup_budget - startup_required_headroom;
    bool init_ok = false;
    bool caught = false;
    ID caught_ex = NULL;
    MemoryStats stats = {0};

    TRY {
        init_ok = breakout_viewer_test_context_init_with_heap_limit(&ctx, startup_limit);
    } CATCH(ex) {
        caught = true;
        caught_ex = ex;
    } END_TRY
    stats = memory_profiler_get_stats();
    memory_set_heap_limit_bytes(previous_limit);

    if (init_ok) {
        breakout_viewer_test_context_destroy(&ctx);
    }
    TEST_ASSERT_FALSE_MESSAGE(caught, caught_ex ? "breakout host startup should not OOM under a 400KB startup budget with 2KB reserved headroom" : "");
    TEST_ASSERT_TRUE_MESSAGE(init_ok, "breakout host startup should fit inside a 400KB startup budget while preserving 2KB free");
    TEST_ASSERT_TRUE_MESSAGE(stats.current_memory_usage + startup_required_headroom <= startup_budget,
                             "startup should retain at least 2KB free heap headroom inside the 400KB budget");
}

TEST(test_breakout_runtime_startup_first_launch_fits_debug_heap_limit) {
    BreakoutViewerTestContext ctx = {0};
    size_t previous_limit = memory_get_heap_limit_bytes();
    bool init_ok = false;
    bool caught = false;
    ID caught_ex = NULL;
    ID launched = NULL;

    TRY {
        init_ok = breakout_viewer_test_context_init_with_heap_budget(&ctx, true);
        if (init_ok) {
            launched = eval_string(
                "(do "
                "  (tiny-breakout.runtime/start-runtime! nil) "
                "  (tiny-breakout.runtime/apply-input! {:launch true}) "
                "  (= :play (:phase @tiny-breakout.runtime/state*)))",
                ctx.st);
        }
    } CATCH(ex) {
        caught = true;
        caught_ex = ex;
    } END_TRY
    memory_set_heap_limit_bytes(previous_limit);

    if (init_ok) {
        breakout_viewer_test_context_destroy(&ctx);
    }

    TEST_ASSERT_FALSE_MESSAGE(caught, caught_ex ? "first breakout launch should not OOM under 640KB total heap budget" : "");
    TEST_ASSERT_TRUE_MESSAGE(init_ok, "breakout host startup should initialize before first-launch heap assertion");
    TEST_ASSERT_EQUAL_PTR(clj_true, launched);
}

TEST(test_breakout_runtime_startup_first_launch_heap_profile_stays_bounded) {
    BreakoutViewerTestContext ctx = {0};
    size_t previous_limit = memory_get_heap_limit_bytes();
    ID stats = NULL;
    ID k_total = NULL;
    ID k_peak = NULL;
    ID total = NULL;
    ID peak = NULL;

    TEST_ASSERT_TRUE(breakout_viewer_test_context_init_with_heap_budget(&ctx, true));

    stats = eval_string(
        "(do "
        "  (require 'tiny-breakout.runtime) "
        "  (tiny-breakout.runtime/start-runtime! nil) "
        "  (heap "
        "    (do "
        "      (tiny-breakout.runtime/apply-input! {:launch true}) "
        "      (:phase @tiny-breakout.runtime/state*))))",
        ctx.st);
    TEST_ASSERT_NOT_NULL(stats);
    TEST_ASSERT_TRUE(is_map(stats));

    k_total = intern_symbol_global(":total");
    k_peak = intern_symbol_global(":peak");
    TEST_ASSERT_NOT_NULL(k_total);
    TEST_ASSERT_NOT_NULL(k_peak);

    total = map_get_sentinel((CljPersistentMap *)stats, k_total, NOT_FOUND);
    peak = map_get_sentinel((CljPersistentMap *)stats, k_peak, NOT_FOUND);
    TEST_ASSERT_NOT_EQUAL(NOT_FOUND, total);
    TEST_ASSERT_NOT_EQUAL(NOT_FOUND, peak);
    TEST_ASSERT_TRUE(is_fixnum(total));
    TEST_ASSERT_TRUE(is_fixnum(peak));

    fprintf(stderr,
            "[first-launch-heap] total=%d peak=%d current=%zu limit=%zu\n",
            as_fixnum(total),
            as_fixnum(peak),
            memory_current_usage_bytes(),
            memory_get_heap_limit_bytes());

    TEST_ASSERT_TRUE_MESSAGE(as_fixnum(peak) < 96 * 1024,
                             "direct first-launch apply-input path should stay below 96KB local peak");

    memory_set_heap_limit_bytes(previous_limit);
    breakout_viewer_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_applies_absolute_host_heap_limit_before_clojure_bootstrap) {
    size_t previous_limit = memory_get_heap_limit_bytes();
    size_t host_limit = tiny_fx_host_heap_limit_bytes();

    TEST_ASSERT_TRUE(memory_current_usage_bytes() > 0u);
    tiny_fx_host_apply_heap_limit();
    TEST_ASSERT_EQUAL_UINT64(host_limit, memory_get_heap_limit_bytes());

    memory_set_heap_limit_bytes(previous_limit);
}

TEST(test_breakout_runtime_startup_tolerates_host_sound_init_failure_during_audio_preload) {
    BreakoutViewerTestContext ctx = {0};
    const char *env_name = "TINYCLJ_SOUND_HOST_INIT_FAIL";
    const char *saved_env = getenv(env_name);
    char *saved_copy = saved_env ? strdup(saved_env) : NULL;
    bool init_ok = false;
    bool caught = false;
    ID caught_ex = NULL;

    setenv(env_name, "component-find", 1);

    TRY {
        init_ok = breakout_viewer_test_context_init(&ctx);
    } CATCH(ex) {
        caught = true;
        caught_ex = ex;
    } END_TRY

    if (saved_copy) {
        setenv(env_name, saved_copy, 1);
        free(saved_copy);
    } else {
        unsetenv(env_name);
    }

    if (init_ok) {
        breakout_viewer_test_context_destroy(&ctx);
    }

    TEST_ASSERT_FALSE_MESSAGE(caught, caught_ex ? "breakout host startup should tolerate host audio init failures" : "");
    TEST_ASSERT_TRUE_MESSAGE(init_ok, "breakout host startup should still load when host audio init fails");
}

TEST(test_breakout_runtime_startup_defaults_host_demo_selection_to_breakout) {
    const char *env_name = "TINYCLJ_HOST_DEMO";
    const char *saved_env = getenv(env_name);
    char *saved_copy = saved_env ? strdup(saved_env) : NULL;

    unsetenv(env_name);
    ViewerConfigSource config_source = viewer_default_config_source();

    if (saved_copy) {
        setenv(env_name, saved_copy, 1);
        free(saved_copy);
    } else {
        unsetenv(env_name);
    }

    TEST_ASSERT_EQUAL_STRING("tiny-clj.deployment", config_source.namespace_name);
    TEST_ASSERT_EQUAL_STRING("(tiny-clj.deployment/breakout-host-config)", config_source.config_expr);
    TEST_ASSERT_EQUAL_STRING("tiny-clj.deployment/breakout-host-config", config_source.display_name);
}

TEST(test_breakout_runtime_startup_runloop_thread_start_is_idempotent) {
    EvalState *st_primary = NULL;
    EvalState *st_secondary = NULL;
    pthread_t first_thread;

    runtime_init(&g_runtime);
    event_loop_init();

    st_primary = evalstate_new(true);
    TEST_ASSERT_NOT_NULL(st_primary);
    st_secondary = evalstate_new(true);
    TEST_ASSERT_NOT_NULL(st_secondary);

    TEST_ASSERT_TRUE(start_runloop_thread(st_primary));
    TEST_ASSERT_TRUE(g_runloop_thread.started);
    first_thread = g_runloop_thread.thread;
    TEST_ASSERT_EQUAL_PTR(st_primary, g_runloop_thread.eval_state);

    TEST_ASSERT_TRUE(start_runloop_thread(st_secondary));
    TEST_ASSERT_TRUE(g_runloop_thread.started);
    TEST_ASSERT_TRUE(pthread_equal(first_thread, g_runloop_thread.thread));
    TEST_ASSERT_EQUAL_PTR(st_primary, g_runloop_thread.eval_state);

    stop_runloop_thread();
    stop_runloop_thread();

    evalstate_free(st_secondary);
    evalstate_free(st_primary);
}

TEST(test_breakout_runtime_startup_runloop_thread_registers_interpreter_thread) {
    EvalState *st = NULL;

    runtime_init(&g_runtime);
    event_loop_init();
    event_loop_clear();
    subjective_c_clear_interpreter_thread();

    st = evalstate_new(true);
    TEST_ASSERT_NOT_NULL(st);

    TEST_ASSERT_FALSE(subjective_c_has_interpreter_thread());
    TEST_ASSERT_FALSE(subjective_c_is_interpreter_thread());
    TEST_ASSERT_TRUE(start_runloop_thread(st));

    for (int attempt = 0; attempt < 50 && !subjective_c_has_interpreter_thread(); attempt++) {
        usleep(1000);
    }

    TEST_ASSERT_TRUE(subjective_c_has_interpreter_thread());
    TEST_ASSERT_FALSE(subjective_c_is_interpreter_thread());

    stop_runloop_thread();
    TEST_ASSERT_FALSE(subjective_c_has_interpreter_thread());
    TEST_ASSERT_FALSE(subjective_c_is_interpreter_thread());

    evalstate_free(st);
}

TEST(test_breakout_runtime_startup_runloop_liveness_snapshot_resets_cleanly) {
    viewer_runloop_liveness_reset();

    ViewerRunloopLivenessSnapshot snapshot = viewer_runloop_liveness_snapshot(9ull * 1000ull * 1000ull * 1000ull);

    TEST_ASSERT_EQUAL_UINT64(0u, snapshot.last_tick_ns);
    TEST_ASSERT_EQUAL_UINT64(0u, snapshot.iteration_count);
    TEST_ASSERT_EQUAL_UINT64(0u, snapshot.age_ns);
    TEST_ASSERT_EQUAL_INT(VIEWER_RUNLOOP_LIVENESS_HEALTHY, snapshot.state);
}

TEST(test_breakout_runtime_startup_runloop_liveness_snapshot_tracks_progress) {
    viewer_runloop_liveness_reset();
    viewer_runloop_liveness_note_progress_for_tests(2ull * 1000ull * 1000ull * 1000ull);
    viewer_runloop_liveness_note_progress_for_tests(3ull * 1000ull * 1000ull * 1000ull);

    ViewerRunloopLivenessSnapshot snapshot = viewer_runloop_liveness_snapshot(4ull * 1000ull * 1000ull * 1000ull);

    TEST_ASSERT_EQUAL_UINT64(3ull * 1000ull * 1000ull * 1000ull, snapshot.last_tick_ns);
    TEST_ASSERT_EQUAL_UINT64(2u, snapshot.iteration_count);
    TEST_ASSERT_EQUAL_UINT64(1ull * 1000ull * 1000ull * 1000ull, snapshot.age_ns);
    TEST_ASSERT_EQUAL_INT(VIEWER_RUNLOOP_LIVENESS_HEALTHY, snapshot.state);
}

TEST(test_breakout_runtime_startup_runloop_liveness_snapshot_flags_stalls_after_threshold) {
    viewer_runloop_liveness_reset();
    viewer_runloop_liveness_note_progress_for_tests(1ull * 1000ull * 1000ull * 1000ull);

    ViewerRunloopLivenessSnapshot snapshot = viewer_runloop_liveness_snapshot(7ull * 1000ull * 1000ull * 1000ull);

    TEST_ASSERT_EQUAL_UINT64(6ull * 1000ull * 1000ull * 1000ull, snapshot.age_ns);
    TEST_ASSERT_EQUAL_INT(VIEWER_RUNLOOP_LIVENESS_STALLED, snapshot.state);
}

#if defined(__APPLE__)
static ViewerHostWindow *breakout_test_open_host_window(const char *title, unsigned width, unsigned height) {
    (void)title;
    (void)width;
    (void)height;
    return (ViewerHostWindow *)(uintptr_t)0x1;
}

static void breakout_test_close_host_window(ViewerHostWindow *window) {
    TEST_ASSERT_EQUAL_PTR((ViewerHostWindow *)(uintptr_t)0x1, window);
}

TEST(test_breakout_runtime_startup_host_window_open_starts_macos_runloop_watchdog) {
    breakout_reset_macos_watchdog_counters();

    ViewerHostWindow *window =
        viewer_host_window_open_with_backend(breakout_test_open_host_window, "tiny-fx", 640u, 480u);

    TEST_ASSERT_NOT_NULL(window);
    TEST_ASSERT_EQUAL_INT(1, g_macos_runloop_watchdog_start_calls);
    TEST_ASSERT_EQUAL_INT(0, g_macos_runloop_watchdog_stop_calls);
}

TEST(test_breakout_runtime_startup_host_window_close_stops_macos_runloop_watchdog) {
    breakout_reset_macos_watchdog_counters();

    viewer_host_window_close_with_backend((ViewerHostWindow *)(uintptr_t)0x1, breakout_test_close_host_window);

    TEST_ASSERT_EQUAL_INT(0, g_macos_runloop_watchdog_start_calls);
    TEST_ASSERT_EQUAL_INT(1, g_macos_runloop_watchdog_stop_calls);
}
#endif

TEST(test_breakout_runtime_startup_loads_breakout_host_config_into_generic_viewer_bundle) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_viewer_test_context_init(&ctx));

    TEST_ASSERT_TRUE(ctx.bundle.has_primary_slot);
    TEST_ASSERT_NOT_NULL(ctx.bundle.startup_callback);
    TEST_ASSERT_NOT_NULL(ctx.bundle.primary_scene_atom);
    TEST_ASSERT_NOT_NULL(ctx.bundle.primary_scene);
    TEST_ASSERT_NOT_NULL(ctx.bundle.spatial_callback);
    TEST_ASSERT_TRUE(ctx.bundle.primary_slot_index < ctx.bundle.slot_count);
    TEST_ASSERT_EQUAL_PTR(ctx.bundle.primary_scene, ctx.bundle.slots[ctx.bundle.primary_slot_index].scene);
    TEST_ASSERT_TRUE(ctx.spatial_rules.count >= 1u);

    breakout_viewer_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_retain_slot_scenes_independently_of_scene_atoms) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_viewer_test_context_init(&ctx));

    TEST_ASSERT_TRUE(ctx.bundle.has_primary_slot);
    TEST_ASSERT_NOT_NULL(ctx.bundle.primary_scene);
    TEST_ASSERT_TRUE(((CljObject *)ctx.bundle.primary_scene)->rc > 1);

    breakout_viewer_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_spatial_events_include_entity_snapshots) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_viewer_test_context_init(&ctx));
    TEST_ASSERT_TRUE(ctx.spatial_rules.count > 0u);

    FrameScene *scene = viewer_frame_scene_from_atom(ctx.bundle.primary_scene_atom);
    TEST_ASSERT_NOT_NULL(scene);

    ViewerCollisionPolicy *policy = &ctx.spatial_rules.items[0];
    ID expected_self = map_get_sentinel(viewer_collision_scene_entity_map(scene), policy->self_entity_id, NULL);
    ID expected_other = map_get_sentinel(viewer_collision_scene_entity_map(scene), policy->other_entity_id, NULL);
    TEST_ASSERT_NOT_NULL(expected_self);
    TEST_ASSERT_NOT_NULL(expected_other);

    VgAabb self_box = {.min_x = 1, .min_y = 2, .max_x = 3, .max_y = 4};
    VgAabb other_box = {.min_x = 5, .min_y = 6, .max_x = 7, .max_y = 8};
    ID event = viewer_collision_make_spatial_event(&ctx.bundle,
                                                   policy,
                                                   intern_symbol_global(":enter"),
                                                   17u,
                                                   &self_box,
                                                   &other_box);
    TEST_ASSERT_NOT_NULL(event);

    ID k_self_entity = intern_symbol_global(":self-entity");
    ID k_other_entity = intern_symbol_global(":other-entity");
    TEST_ASSERT_NOT_NULL(k_self_entity);
    TEST_ASSERT_NOT_NULL(k_other_entity);
    TEST_ASSERT_EQUAL_PTR(expected_self, tiny_fx_gfx_get_field(event, k_self_entity, NULL));
    TEST_ASSERT_EQUAL_PTR(expected_other, tiny_fx_gfx_get_field(event, k_other_entity, NULL));

    RELEASE(event);
    breakout_viewer_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_fire_button_seeded_inactive_before_breakout_watchers) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_viewer_test_context_init(&ctx));

    ID ok = eval_string(
        "(do "
        "  (require 'tiny-clj.gpio) "
        "  (require 'tiny-breakout.runtime) "
        "  (tiny-breakout.runtime/start-runtime! nil) "
        "  (tiny-clj.gpio/simulate! 13 0) "
        "  (Thread/sleep 30) "
        "  (dotimes [_ 8] (run-next-task)) "
        "  (tiny-clj.gpio/simulate! 13 1) "
        "  (Thread/sleep 30) "
        "  (dotimes [_ 8] (run-next-task)) "
        "  (= :play (:phase @tiny-breakout.runtime/state*)))",
        ctx.st);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);

    breakout_viewer_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_restarts_title_overlay_fade_on_runtime_start) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_viewer_test_context_init(&ctx));

    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.scene) "
        "  (require 'tiny-breakout.runtime) "
        "  (tiny-breakout.runtime/start-runtime! nil) "
        "  (let [overlay (get (:index @tiny-breakout.runtime/scene*) :overlay-text) "
        "        kf (:keyframes (:stroke-color (:style overlay)))] "
        "    (> (first (first kf)) 0)))",
        ctx.st);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);

    breakout_viewer_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_bootstrap_title_overlay_starts_fade_immediately) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_viewer_test_context_init(&ctx));

    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.scene) "
        "  (require 'tiny-breakout.runtime) "
        "  (tiny-breakout.runtime/bootstrap-runtime!) "
        "  (let [overlay (get (:index @tiny-breakout.runtime/scene*) :overlay-text) "
        "        kf (:keyframes (:stroke-color (:style overlay)))] "
        "    (and (vector? kf) "
        "         (> (count kf) 0) "
        "         (> (first (first kf)) 0))))",
        ctx.st);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);

    breakout_viewer_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_level_clear_fire_press_advances_once_to_serve) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_viewer_test_context_init(&ctx));

    ID state = eval_string(
        "(do "
        "  (require 'tiny-clj.gpio) "
        "  (require 'tiny-breakout.runtime) "
        "  (tiny-breakout.runtime/start-runtime! nil) "
        "  (tiny-breakout.runtime/publish-state! "
        "    (assoc @tiny-breakout.runtime/state* "
        "      :phase :level-clear "
        "      :level-index 0 "
        "      :events [] "
        "      :ball-segment nil)) "
        "  (tiny-clj.gpio/simulate! 13 0) "
        "  (Thread/sleep 30) "
        "  (dotimes [_ 8] (run-next-task)) "
        "  (tiny-clj.gpio/simulate! 13 1) "
        "  (Thread/sleep 30) "
        "  (dotimes [_ 8] (run-next-task)) "
        "  @tiny-breakout.runtime/state*)",
        ctx.st);
    TEST_ASSERT_NOT_NULL(state);
    TEST_ASSERT_TRUE(is_map(state));
    ID k_phase = intern_symbol_global(":phase");
    ID k_level_index = intern_symbol_global(":level-index");
    ID k_bricks = intern_symbol_global(":bricks");
    TEST_ASSERT_NOT_NULL(k_phase);
    TEST_ASSERT_NOT_NULL(k_level_index);
    TEST_ASSERT_NOT_NULL(k_bricks);

    ID phase = map_get_sentinel(state, k_phase, NULL);
    ID level_index = map_get_sentinel(state, k_level_index, NULL);
    ID bricks = map_get_sentinel(state, k_bricks, NULL);

    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":serve"), phase);
    TEST_ASSERT_TRUE(is_fixnum(level_index));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(level_index));
    TEST_ASSERT_TRUE(is_vector(bricks));
    TEST_ASSERT_TRUE(vector_count(as_vector(bricks)) > 0);

    breakout_viewer_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_fire_button_heap_profile_stays_bounded) {
    BreakoutViewerTestContext ctx = {0};
    size_t previous_limit = memory_get_heap_limit_bytes();
    TEST_ASSERT_TRUE(breakout_viewer_test_context_init_with_heap_budget(&ctx, true));

    ID stats = eval_string(
        "(do "
        "  (require 'tiny-clj.gpio) "
        "  (require 'tiny-breakout.runtime) "
        "  (require 'tiny-clj.event) "
        "  (require 'tiny-fx.gfx-timeline) "
        "  (tiny-breakout.runtime/start-runtime! nil) "
        "  (heap "
        "    (do "
        "      (tiny-clj.gpio/simulate! 13 0) "
        "      (Thread/sleep 30) "
        "      (dotimes [_ 8] (run-next-task)) "
        "      (tiny-clj.gpio/simulate! 13 1) "
        "      (Thread/sleep 30) "
        "      (dotimes [_ 8] (run-next-task)) "
        "      (:phase @tiny-breakout.runtime/state*))))",
        ctx.st);
    TEST_ASSERT_NOT_NULL(stats);
    TEST_ASSERT_TRUE(is_map(stats));

    ID k_total = intern_symbol_global(":total");
    ID k_peak = intern_symbol_global(":peak");
    TEST_ASSERT_NOT_NULL(k_total);
    TEST_ASSERT_NOT_NULL(k_peak);

    ID total = map_get_sentinel((CljPersistentMap *)stats, k_total, NOT_FOUND);
    ID peak = map_get_sentinel((CljPersistentMap *)stats, k_peak, NOT_FOUND);
    TEST_ASSERT_NOT_EQUAL(NOT_FOUND, total);
    TEST_ASSERT_NOT_EQUAL(NOT_FOUND, peak);
    TEST_ASSERT_TRUE(is_fixnum(total));
    TEST_ASSERT_TRUE(is_fixnum(peak));

    TEST_ASSERT_TRUE_MESSAGE(as_fixnum(peak) < 128 * 1024,
                             "fire button path should stay below 128KB local peak");

    memory_set_heap_limit_bytes(previous_limit);
    breakout_viewer_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_spatial_callback_scene_replacement_reloads_rules_safely) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_viewer_test_context_init(&ctx));
    TEST_ASSERT_TRUE(ctx.bundle.has_primary_slot);
    TEST_ASSERT_TRUE(ctx.spatial_rules.count > 0u);

    ID replacement_scene = eval_string(
        "(do "
        "  (require 'tiny-fx.gfx) "
        "  (require '[tiny-fx.gfx-scene :refer :all]) "
        "  (record-create (quote FrameScene) ["
        "    :tiny-fx.scene/root "
        "    {:tiny-fx.scene/root (->Line :tiny-fx.scene/root nil (->Style 65535 1 true false 0 false 0) true 0 0 1 1 nil)} "
        "    [0 0 320 240] "
        "    0 true true 0 0 nil]))",
        ctx.st);
    TEST_ASSERT_NOT_NULL(replacement_scene);

    ViewerCollisionPolicy *policy = &ctx.spatial_rules.items[0];
    VgTransformFixed world_t = {
        .m00 = VG_SCALE_ONE,
        .m01 = 0,
        .m02 = 0,
        .m10 = 0,
        .m11 = VG_SCALE_ONE,
        .m12 = 0,
    };
    VgAabb self_box = {.min_x = 10, .min_y = 10, .max_x = 20, .max_y = 20};
    VgAabb other_box = {.min_x = 12, .min_y = 12, .max_x = 18, .max_y = 18};

    vg_rendered_state_reset_all();
    vg_rendered_state_capture_begin(ctx.bundle.primary_slot_index, 1u, 0u);
    vg_rendered_state_capture_record_entity((uintptr_t)policy->self_entity_id, world_t);
    vg_rendered_state_capture_record_entity_aabb((uintptr_t)policy->self_entity_id, self_box);
    vg_rendered_state_capture_record_entity((uintptr_t)policy->other_entity_id, world_t);
    vg_rendered_state_capture_record_entity_aabb((uintptr_t)policy->other_entity_id, other_box);
    vg_rendered_state_capture_commit();

    TEST_ASSERT_TRUE(viewer_collision_detect_step(&ctx.bundle, &ctx.spatial_rules, 0u, NULL, 0u));
    TEST_ASSERT_EQUAL_PTR(replacement_scene, atom_reset(ctx.bundle.primary_scene_atom, replacement_scene));

    viewer_sync_configured_slots(&ctx.bundle, &ctx.spatial_rules, NULL, false);
    TEST_ASSERT_EQUAL_UINT32(0u, ctx.spatial_rules.count);

    breakout_viewer_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_collision_latch_recovers_after_missing_snapshot_entities) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_viewer_test_context_init(&ctx));
    TEST_ASSERT_TRUE(ctx.bundle.has_primary_slot);

    ID paddle_rule_id = intern_symbol_global(":ball-vs-paddle");
    ViewerCollisionPolicy *policy = breakout_find_policy_by_id(&ctx.spatial_rules, paddle_rule_id);
    TEST_ASSERT_NOT_NULL(policy);
    ViewerSpatialRuleSet single_rule_set = {0};
    single_rule_set.items[0] = *policy;
    single_rule_set.count = 1u;

    uint8_t slot = ctx.bundle.primary_slot_index;
    VgTransformFixed world_t = {
        .m00 = VG_SCALE_ONE,
        .m01 = 0,
        .m02 = 0,
        .m10 = 0,
        .m11 = VG_SCALE_ONE,
        .m12 = 0,
    };
    /*
     * Right-edge paddle case: paddle protrudes one pixel beyond playfield,
     * ball overlaps at the edge.
     */
    VgAabb ball_box = {.min_x = 317, .max_x = 320, .min_y = 223, .max_y = 226};
    VgAabb paddle_box = {.min_x = 281, .max_x = 320, .min_y = 224, .max_y = 227};

    vg_rendered_state_reset_all();

    vg_rendered_state_capture_begin(slot, 1u, 0u);
    vg_rendered_state_capture_record_entity((uintptr_t)policy->self_entity_id, world_t);
    vg_rendered_state_capture_record_entity_aabb((uintptr_t)policy->self_entity_id, ball_box);
    vg_rendered_state_capture_record_entity((uintptr_t)policy->other_entity_id, world_t);
    vg_rendered_state_capture_record_entity_aabb((uintptr_t)policy->other_entity_id, paddle_box);
    vg_rendered_state_capture_commit();
    TEST_ASSERT_TRUE(viewer_collision_detect_step(&ctx.bundle, &ctx.spatial_rules, 0u, NULL, 0u));

    vg_rendered_state_capture_begin(slot, 2u, 0u);
    vg_rendered_state_capture_commit();
    TEST_ASSERT_FALSE(viewer_collision_detect_step(&ctx.bundle, &ctx.spatial_rules, 0u, NULL, 0u));

    vg_rendered_state_capture_begin(slot, 3u, 0u);
    vg_rendered_state_capture_record_entity((uintptr_t)policy->self_entity_id, world_t);
    vg_rendered_state_capture_record_entity_aabb((uintptr_t)policy->self_entity_id, ball_box);
    vg_rendered_state_capture_record_entity((uintptr_t)policy->other_entity_id, world_t);
    vg_rendered_state_capture_record_entity_aabb((uintptr_t)policy->other_entity_id, paddle_box);
    vg_rendered_state_capture_commit();
    TEST_ASSERT_TRUE(viewer_collision_detect_step(&ctx.bundle, &ctx.spatial_rules, 0u, NULL, 0u));

    breakout_viewer_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_collision_step_filters_candidates_by_dirty_rects) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_viewer_test_context_init(&ctx));
    TEST_ASSERT_TRUE(ctx.bundle.has_primary_slot);

    ID paddle_rule_id = intern_symbol_global(":ball-vs-paddle");
    ViewerCollisionPolicy *policy = breakout_find_policy_by_id(&ctx.spatial_rules, paddle_rule_id);
    TEST_ASSERT_NOT_NULL(policy);
    ViewerSpatialRuleSet single_rule_set = {0};
    single_rule_set.items[0] = *policy;
    single_rule_set.count = 1u;

    uint8_t slot = ctx.bundle.primary_slot_index;
    VgTransformFixed world_t = {
        .m00 = VG_SCALE_ONE,
        .m01 = 0,
        .m02 = 0,
        .m10 = 0,
        .m11 = VG_SCALE_ONE,
        .m12 = 0,
    };
    VgAabb ball_box = {.min_x = 156, .max_x = 159, .min_y = 221, .max_y = 224};
    VgAabb paddle_box = {.min_x = 140, .max_x = 179, .min_y = 224, .max_y = 227};

    vg_rendered_state_reset_all();
    vg_rendered_state_capture_begin(slot, 1u, 0u);
    vg_rendered_state_capture_record_entity((uintptr_t)policy->self_entity_id, world_t);
    vg_rendered_state_capture_record_entity_aabb((uintptr_t)policy->self_entity_id, ball_box);
    vg_rendered_state_capture_record_entity((uintptr_t)policy->other_entity_id, world_t);
    vg_rendered_state_capture_record_entity_aabb((uintptr_t)policy->other_entity_id, paddle_box);
    vg_rendered_state_capture_commit();

    VgClipRect far_dirty = {.x = 0, .y = 0, .w = 16, .h = 16};
    TEST_ASSERT_FALSE(viewer_collision_detect_step(&ctx.bundle,
                                                   &single_rule_set,
                                                   0u,
                                                   &far_dirty,
                                                   1u));

    VgClipRect hit_dirty = {.x = 150, .y = 216, .w = 40, .h = 20};
    TEST_ASSERT_TRUE(viewer_collision_detect_step(&ctx.bundle,
                                                  &single_rule_set,
                                                  0u,
                                                  &hit_dirty,
                                                  1u));

    breakout_viewer_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_collision_step_defers_dispatch_until_collision_drain) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_viewer_test_context_init(&ctx));
    TEST_ASSERT_TRUE(ctx.bundle.has_primary_slot);

    ID paddle_rule_id = intern_symbol_global(":ball-vs-paddle");
    ViewerCollisionPolicy *policy = breakout_find_policy_by_id(&ctx.spatial_rules, paddle_rule_id);
    TEST_ASSERT_NOT_NULL(policy);
    ViewerSpatialRuleSet single_rule_set = {0};
    single_rule_set.items[0] = *policy;
    single_rule_set.count = 1u;

    ID callback = eval_string(
        "(do "
        "  (def breakout-collision-drain-marker (atom nil)) "
        "  (fn [event] "
        "    (reset! breakout-collision-drain-marker [(:phase event) (:id event)]) "
        "    nil))",
        ctx.st);
    TEST_ASSERT_NOT_NULL(callback);
    RELEASE(ctx.bundle.spatial_callback);
    ctx.bundle.spatial_callback = RETAIN(callback);

    viewer_collision_set_dispatch_context(&ctx.bundle, &single_rule_set);

    uint8_t slot = ctx.bundle.primary_slot_index;
    VgTransformFixed world_t = {
        .m00 = VG_SCALE_ONE,
        .m01 = 0,
        .m02 = 0,
        .m10 = 0,
        .m11 = VG_SCALE_ONE,
        .m12 = 0,
    };
    VgAabb ball_box = {.min_x = 156, .max_x = 159, .min_y = 221, .max_y = 224};
    VgAabb paddle_box = {.min_x = 140, .max_x = 179, .min_y = 224, .max_y = 227};

    vg_rendered_state_reset_all();
    vg_rendered_state_capture_begin(slot, 1u, 0u);
    vg_rendered_state_capture_record_entity((uintptr_t)policy->self_entity_id, world_t);
    vg_rendered_state_capture_record_entity_aabb((uintptr_t)policy->self_entity_id, ball_box);
    vg_rendered_state_capture_record_entity((uintptr_t)policy->other_entity_id, world_t);
    vg_rendered_state_capture_record_entity_aabb((uintptr_t)policy->other_entity_id, paddle_box);
    vg_rendered_state_capture_commit();

    TEST_ASSERT_TRUE(viewer_collision_detect_step(&ctx.bundle, &single_rule_set, 0u, NULL, 0u));
    TEST_ASSERT_FALSE(event_loop_has_pending_tasks());
    TEST_ASSERT_FALSE(event_loop_ingress_has_pending());
    TEST_ASSERT_NULL(eval_string("@breakout-collision-drain-marker", ctx.st));

    TEST_ASSERT_TRUE(viewer_collision_poll_drain());
    TEST_ASSERT_TRUE(event_loop_has_pending_tasks());
    TEST_ASSERT_TRUE(event_loop_ingress_has_pending());
    TEST_ASSERT_NULL(eval_string("@breakout-collision-drain-marker", ctx.st));

    TEST_ASSERT_TRUE(event_loop_run_next(NULL, ctx.st));
    ID marker = eval_string("@breakout-collision-drain-marker", ctx.st);
    TEST_ASSERT_NOT_NULL(marker);
    TEST_ASSERT_TRUE(TAG(marker) == CLJ_VECTOR_PERSISTENT);
    ID marker_ok = eval_string("(= @breakout-collision-drain-marker [:enter :ball-vs-paddle])", ctx.st);
    TEST_ASSERT_EQUAL_PTR(clj_true, marker_ok);

    viewer_collision_reset_dispatch_state();
    breakout_viewer_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_collision_step_drops_callback_under_tight_heap_limit_without_crashing) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_viewer_test_context_init(&ctx));
    TEST_ASSERT_TRUE(ctx.bundle.has_primary_slot);

    ID paddle_rule_id = intern_symbol_global(":ball-vs-paddle");
    ViewerCollisionPolicy *policy = breakout_find_policy_by_id(&ctx.spatial_rules, paddle_rule_id);
    TEST_ASSERT_NOT_NULL(policy);
    ViewerSpatialRuleSet single_rule_set = {0};
    single_rule_set.items[0] = *policy;
    single_rule_set.count = 1u;

    uint8_t slot = ctx.bundle.primary_slot_index;
    VgTransformFixed world_t = {
        .m00 = VG_SCALE_ONE,
        .m01 = 0,
        .m02 = 0,
        .m10 = 0,
        .m11 = VG_SCALE_ONE,
        .m12 = 0,
    };
    VgAabb ball_box = {.min_x = 156, .max_x = 159, .min_y = 221, .max_y = 224};
    VgAabb paddle_box = {.min_x = 140, .max_x = 179, .min_y = 224, .max_y = 227};

    vg_rendered_state_reset_all();
    vg_rendered_state_capture_begin(slot, 1u, 0u);
    vg_rendered_state_capture_record_entity((uintptr_t)policy->self_entity_id, world_t);
    vg_rendered_state_capture_record_entity_aabb((uintptr_t)policy->self_entity_id, ball_box);
    vg_rendered_state_capture_record_entity((uintptr_t)policy->other_entity_id, world_t);
    vg_rendered_state_capture_record_entity_aabb((uintptr_t)policy->other_entity_id, paddle_box);
    vg_rendered_state_capture_commit();

    size_t prev_limit = memory_get_heap_limit_bytes();
    bool caught = false;
    bool triggered = false;
    bool drained = false;
    TRY {
        memory_set_heap_limit_bytes(0u);
        triggered = viewer_collision_detect_step(&ctx.bundle, &single_rule_set, 0u, NULL, 0u);
        drained = viewer_collision_poll_drain();
    } CATCH(ex) {
        (void)ex;
        caught = true;
    } END_TRY
    memory_set_heap_limit_bytes(prev_limit);

    TEST_ASSERT_FALSE(caught);
    TEST_ASSERT_TRUE(triggered);
    TEST_ASSERT_FALSE(drained);
    TEST_ASSERT_FALSE(event_loop_ingress_has_pending());

    breakout_viewer_test_context_destroy(&ctx);
}

#if defined(__APPLE__)
TEST(test_breakout_runtime_startup_maps_macos_virtual_keys_to_runtime_keys) {
    TEST_ASSERT_EQUAL_INT(KB_KEY_SPACE, tinyfx_macos_key_from_virtual_key(0x31));
    TEST_ASSERT_EQUAL_INT(KB_KEY_ENTER, tinyfx_macos_key_from_virtual_key(0x24));
    TEST_ASSERT_EQUAL_INT(KB_KEY_Q, tinyfx_macos_key_from_virtual_key(0x0C));
    TEST_ASSERT_EQUAL_INT(KB_KEY_LEFT, tinyfx_macos_key_from_virtual_key(0x7B));
    TEST_ASSERT_EQUAL_INT(KB_KEY_RIGHT, tinyfx_macos_key_from_virtual_key(0x7C));
    TEST_ASSERT_EQUAL_INT(KB_KEY_LEFT_SUPER, tinyfx_macos_key_from_virtual_key(0x37));
    TEST_ASSERT_EQUAL_INT(KB_KEY_UNKNOWN, tinyfx_macos_key_from_virtual_key(0xFFFFu));
}
#endif

TEST(test_breakout_runtime_startup_perf_window_snapshot_reports_spi_throughput_metrics) {
    ViewerPerfWindow perf = {0};
    ViewerPerfSnapshot snapshot = {0};

    perf_window_init(&perf, 10.0);
    perf_window_record_frame(&perf, 100u, 2u, 1u, 2000000u);
    perf_window_record_frame(&perf, 300u, 4u, 3u, 6000000u);

    TEST_ASSERT_TRUE(perf_window_take_snapshot_if_due(&perf, 11.0, &snapshot));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.0f, (float)snapshot.fps);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 200.0f, (float)snapshot.avg_dirty_px_per_frame);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 800.0f, (float)snapshot.dirty_bytes_per_s);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 307200.0f, (float)snapshot.full_bytes_per_s);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.0f, (float)snapshot.avg_changed_slots);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 4.0f, (float)snapshot.transfer_rects_per_s);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.0f, (float)snapshot.avg_transfer_rects_per_frame);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 4.0f, (float)snapshot.avg_transfer_ms_per_frame);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 600.0f, (float)snapshot.max_dirty_bytes_per_frame);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 200.0f / 76800.0f, (float)snapshot.dirty_ratio);

    TEST_ASSERT_FALSE(perf_window_take_snapshot_if_due(&perf, 11.2, &snapshot));
}

TEST(test_breakout_runtime_startup_perf_window_snapshot_waits_for_full_second) {
    ViewerPerfWindow perf = {0};
    ViewerPerfSnapshot snapshot = {0};

    perf_window_init(&perf, 3.0);
    perf_window_record_frame(&perf, 256u, 1u, 2u, 1000000u);

    TEST_ASSERT_FALSE(perf_window_take_snapshot_if_due(&perf, 3.8, &snapshot));
    TEST_ASSERT_TRUE(perf_window_take_snapshot_if_due(&perf, 4.1, &snapshot));
}

TEST(test_breakout_runtime_startup_perf_window_snapshot_reports_zero_when_idle) {
    ViewerPerfWindow perf = {0};
    ViewerPerfSnapshot snapshot = {0};

    perf_window_init(&perf, 7.0);

    TEST_ASSERT_TRUE(perf_window_take_snapshot_if_due(&perf, 8.2, &snapshot));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, (float)snapshot.fps);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, (float)snapshot.dirty_bytes_per_s);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, (float)snapshot.transfer_rects_per_s);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, (float)snapshot.avg_transfer_ms_per_frame);
}

TEST(test_breakout_runtime_startup_spi_mib_conversion_matches_binary_units) {
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, (float)bytes_per_second_to_mib_per_second(0.0));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, (float)bytes_per_second_to_mib_per_second(-123.0));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, (float)bytes_per_second_to_mib_per_second(1024.0 * 1024.0));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 2.5f, (float)bytes_per_second_to_mib_per_second(2.5 * 1024.0 * 1024.0));
}

TEST(test_breakout_runtime_startup_collision_step_runs_once_per_rendered_frame) {
    uint_fast32_t last_collision_frame_serial = 0u;

    TEST_ASSERT_FALSE(viewer_should_run_collision_step(0u, &last_collision_frame_serial));
    TEST_ASSERT_EQUAL_UINT64(0u, (uint64_t)last_collision_frame_serial);

    TEST_ASSERT_TRUE(viewer_should_run_collision_step(1u, &last_collision_frame_serial));
    TEST_ASSERT_EQUAL_UINT64(1u, (uint64_t)last_collision_frame_serial);
    TEST_ASSERT_FALSE(viewer_should_run_collision_step(1u, &last_collision_frame_serial));

    TEST_ASSERT_TRUE(viewer_should_run_collision_step(2u, &last_collision_frame_serial));
    TEST_ASSERT_EQUAL_UINT64(2u, (uint64_t)last_collision_frame_serial);
}

TEST(test_breakout_runtime_startup_redraw_overlay_keeps_last_non_empty_transfer_until_presented) {
    memset(&g_render_thread, 0, sizeof(g_render_thread));
    TEST_ASSERT_EQUAL_INT(0, pthread_mutex_init(&g_render_thread.transfer_rects_mutex, NULL));

    VgClipRect transfer_rect = {.x = 10, .y = 20, .w = 30, .h = 40};
    viewer_store_last_transfer_result(5u, &transfer_rect, 1u, 123u);
    viewer_store_last_transfer_result(0u, NULL, 0u, 0u);

    VgClipRect overlay_rects[4] = {0};
    uint_fast32_t last_presented_overlay_frame_serial = 0u;
    uint_fast32_t overlay_frame_serial = 0u;
    size_t overlay_count = viewer_take_pending_overlay_rects(&last_presented_overlay_frame_serial,
                                                             overlay_rects,
                                                             4u,
                                                             &overlay_frame_serial);

    TEST_ASSERT_EQUAL_UINT(1u, overlay_count);
    TEST_ASSERT_EQUAL_UINT64(5u, (uint64_t)overlay_frame_serial);
    TEST_ASSERT_EQUAL_UINT64(5u, (uint64_t)last_presented_overlay_frame_serial);
    TEST_ASSERT_TRUE(vg_clip_rect_equal(transfer_rect, overlay_rects[0]));
    TEST_ASSERT_EQUAL_UINT(0u, viewer_copy_last_transfer_rects(overlay_rects, 4u));
    TEST_ASSERT_EQUAL_UINT(0u,
                           viewer_take_pending_overlay_rects(&last_presented_overlay_frame_serial,
                                                             overlay_rects,
                                                             4u,
                                                             &overlay_frame_serial));

    (void)pthread_mutex_destroy(&g_render_thread.transfer_rects_mutex);
    memset(&g_render_thread, 0, sizeof(g_render_thread));
}

TEST(test_breakout_runtime_startup_runloop_play_loop_survives_timeline_watch_driven_scene_updates) {
    BreakoutViewerTestContext ctx = {0};
    uint16_t pixels[320u * 240u] = {0};
    VgFrameBuffer fb = {0};
    VgRenderSlotState render_state = {0};

    TEST_ASSERT_TRUE(breakout_viewer_test_context_init(&ctx));
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, 320u, 240u, pixels, 320u * 240u));

    ID state_atom_id = eval_string(
        "(do "
        "  (require 'tiny-breakout.runtime) "
        "  (tiny-breakout.runtime/start-runtime!) "
        "  (tiny-breakout.runtime/apply-input! {:launch true}) "
        "  tiny-breakout.runtime/state*)",
        ctx.st);
    TEST_ASSERT_NOT_NULL(state_atom_id);
    TEST_ASSERT_EQUAL_UINT8(CLJ_ATOM, TAG(state_atom_id));

    vg_rendered_state_capture_begin(ctx.bundle.primary_slot_index, 1u, (uint32_t)platform_current_time_ms());
    VgRenderFrameSlotResult initial_slot_result = {0};
    bool initial_rendered = vg_render_frame_slot_record_result_at_ms(
        atom_deref(ctx.bundle.primary_scene_atom), &render_state, &fb,
        1u, (uint32_t)platform_current_time_ms(), false, &initial_slot_result);
    if (initial_rendered) {
        vg_rendered_state_capture_commit();
    } else {
        vg_rendered_state_capture_discard();
    }
    TEST_ASSERT_TRUE(initial_rendered);

    /* Build a brick-collision event so the loop exercises spatial dispatch too. */
    ID brick_event = eval_string(
        "(do "
        "  (let [b (first (:bricks @tiny-breakout.runtime/state*)) "
        "        bx (:x b) by (:y b) bw (:w b) bh (:h b)] "
        "    {:source :spatial "
        "     :id :ball-vs-brick "
        "     :rule {:id :ball-vs-brick} "
        "     :phase :enter "
        "     :other (:id b) "
        "     :self-aabb {:min-x bx :min-y (+ by bh) :max-x (+ bx 4) :max-y (+ by bh 4)} "
        "     :other-aabb {:min-x bx :min-y by :max-x (+ bx bw) :max-y (+ by bh)}}))",
        ctx.st);
    TEST_ASSERT_NOT_NULL(brick_event);
    RETAIN(brick_event);

    size_t mem_before = memory_current_usage_bytes();

    TEST_ASSERT_TRUE(start_runloop_thread(ctx.st));

    /* Dispatch one brick collision to exercise the spatial callback path. */
    TEST_ASSERT_TRUE(event_loop_enqueue_ingress_call((CljObject *)ctx.bundle.spatial_callback, brick_event));
    RELEASE(brick_event);

    for (int i = 0; i < 600; i++) {
        usleep(16000);
        viewer_sync_configured_slots(&ctx.bundle, &ctx.spatial_rules, NULL, false);
        ID snapshot = atom_deref_owned(ctx.bundle.primary_scene_atom);
        if (snapshot) {
            vg_rendered_state_capture_begin(ctx.bundle.primary_slot_index, (uint32_t)(i + 2), (uint32_t)platform_current_time_ms());
            VgRenderFrameSlotResult r = {0};
            if (vg_render_frame_slot_record_result_at_ms(snapshot, &render_state, &fb,
                    (uint32_t)(i + 2), (uint32_t)platform_current_time_ms(),
                    render_state.has_animation, &r)) {
                vg_rendered_state_capture_commit();
            } else {
                vg_rendered_state_capture_discard();
            }
            RELEASE(snapshot);
        }
    }
    stop_runloop_thread();

    size_t mem_after = memory_current_usage_bytes();
    size_t mem_growth = (mem_after > mem_before) ? (mem_after - mem_before) : 0u;

    ID state = atom_deref((CljAtom *)state_atom_id);
    TEST_ASSERT_NOT_NULL(state);
    TEST_ASSERT_TRUE(is_map(state));
    ID phase = map_get_sentinel(state, intern_symbol_global(":phase"), NULL);
    ID segment_seq = map_get_sentinel(state, intern_symbol_global(":segment-id-seq"), NULL);
    const char *phase_name = "<non-symbol>";
    if (phase == NULL) {
        phase_name = "<nil>";
    } else if (TAG(phase) == CLJ_SYMBOL) {
        CljSymbol *phase_sym = as_symbol(phase);
        if (phase_sym && phase_sym->cname) {
            phase_name = phase_sym->cname;
        }
    }
    ID play_kw = intern_symbol_global(":play");
    ID serve_kw = intern_symbol_global(":serve");
    bool phase_ok = (phase == play_kw) || (phase == serve_kw);
    char phase_msg[128];
    mini_snprintf(phase_msg, sizeof(phase_msg),
                  "expected phase :play or :serve after runloop exercise, got %s",
                  phase_name);
    TEST_ASSERT_TRUE_MESSAGE(phase_ok, phase_msg);
    char seq_msg[128];
    if (is_fixnum(segment_seq)) {
        mini_snprintf(seq_msg, sizeof(seq_msg), "segment-id-seq should advance beyond first segment, got %d",
                      as_fixnum(segment_seq));
    } else {
        mini_snprintf(seq_msg, sizeof(seq_msg), "segment-id-seq should be fixnum, got tag %u",
                      segment_seq ? (unsigned int)TAG(segment_seq) : 255u);
    }
    TEST_ASSERT_TRUE_MESSAGE(is_fixnum(segment_seq) && as_fixnum(segment_seq) > 1, seq_msg);

    /* Play loop memory must stay bounded. Allow up to 8KB growth for the last
       scene/state still in atoms plus one pending timer closure. Anything more
       indicates a leak that will eventually OOM under the 600KB host budget. */
    char growth_msg[128];
    mini_snprintf(growth_msg, sizeof(growth_msg),
                  "play loop leaked %zu bytes (limit 8192)", mem_growth);
    TEST_ASSERT_TRUE_MESSAGE(mem_growth <= 8192u, growth_msg);

    breakout_viewer_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_post_launch_runloop_frames_fit_debug_heap_limit) {
    BreakoutViewerTestContext ctx = {0};
    VgSlotChangeTracker slot_change_tracker = {0};
    size_t previous_limit = memory_get_heap_limit_bytes();
    const size_t stricter_limit = 592u * 1024u;
    bool caught = false;
    ID caught_ex = NULL;

    TRY {
        TEST_ASSERT_TRUE(breakout_viewer_test_context_init_with_heap_budget(&ctx, true));
        memory_set_heap_limit_bytes(stricter_limit);
        TEST_ASSERT_TRUE(vg_slot_change_tracker_init(&slot_change_tracker, ctx.bundle.slot_count));

        ID ok = eval_string(
            "(do "
            "  (require 'tiny-breakout.runtime) "
            "  (tiny-breakout.runtime/start-runtime! nil) "
            "  true)",
            ctx.st);
        TEST_ASSERT_EQUAL_PTR(clj_true, ok);

        ok = eval_string(
            "(do "
            "  (tiny-breakout.runtime/apply-input! {:launch true}) "
            "  true)",
            ctx.st);
        TEST_ASSERT_EQUAL_PTR(clj_true, ok);

        TEST_ASSERT_TRUE(start_runloop_thread(ctx.st));

        for (int i = 0; i < 90; i++) {
            usleep(16000);
            viewer_sync_configured_slots(&ctx.bundle, &ctx.spatial_rules, &slot_change_tracker, true);
            (void)viewer_collision_detect_step(&ctx.bundle,
                                               &ctx.spatial_rules,
                                               (uint32_t)platform_current_time_ms(),
                                               NULL,
                                               0u);
        }
    } CATCH(ex) {
        caught = true;
        caught_ex = ex;
    } END_TRY

    memory_set_heap_limit_bytes(previous_limit);
    stop_runloop_thread();
    vg_slot_change_tracker_destroy(&slot_change_tracker);
    breakout_viewer_test_context_destroy(&ctx);

    if (caught_ex) {
        print_exception(caught_ex);
    }

    TEST_ASSERT_FALSE_MESSAGE(caught,
                              caught_ex ? "post-launch runloop frames should fit inside the stricter 592KB debug heap budget"
                                        : "");
}

TEST(test_breakout_runtime_startup_brick_collision_runloop_path_survives_and_scores_once) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_viewer_test_context_init(&ctx));
    TEST_ASSERT_NOT_NULL(ctx.bundle.spatial_callback);

    ID state_atom_id = eval_string(
        "(do "
        "  (require 'tiny-breakout.runtime) "
        "  (tiny-breakout.runtime/start-runtime! nil) "
        "  (tiny-breakout.runtime/apply-input! {:launch true}) "
        "  tiny-breakout.runtime/state*)",
        ctx.st);
    TEST_ASSERT_NOT_NULL(state_atom_id);
    TEST_ASSERT_EQUAL_UINT8(CLJ_ATOM, TAG(state_atom_id));
    CljAtom *state_atom = as_atom(state_atom_id);
    TEST_ASSERT_NOT_NULL(state_atom);

    ID score_kw = intern_symbol_global(":score");
    TEST_ASSERT_NOT_NULL(score_kw);

    ID brick_event = eval_string(
        "(do "
        "  (require 'tiny-breakout.runtime) "
        "  (let [b (first (:bricks @tiny-breakout.runtime/state*)) "
        "        bx (:x b) by (:y b) bw (:w b) bh (:h b)] "
        "    {:source :spatial "
        "     :id :ball-vs-brick "
        "     :rule {:id :ball-vs-brick} "
        "     :phase :enter "
        "     :other (:id b) "
        "     :self-aabb {:min-x (+ bx bw) :min-y (+ by 2) :max-x (+ bx bw 4) :max-y (+ by 6)} "
        "     :other-aabb {:min-x bx :min-y by :max-x (+ bx bw) :max-y (+ by bh)}}))",
        ctx.st);
    TEST_ASSERT_NOT_NULL(brick_event);
    TEST_ASSERT_TRUE(is_map(brick_event));

    TEST_ASSERT_TRUE(start_runloop_thread(ctx.st));
    ID retained_event = RETAIN(brick_event);
    TEST_ASSERT_TRUE(event_loop_enqueue_ingress_call((CljObject *)ctx.bundle.spatial_callback, retained_event));
    RELEASE(retained_event);

    bool saw_brick_score = false;
    for (int i = 0; i < 120; i++) {
        usleep(10000);
        ID state = atom_deref_owned(state_atom);
        if (state && is_map(state)) {
            ID score = map_get_sentinel(state, score_kw, NULL);
            if (score && is_fixnum(score) && as_fixnum(score) > 0) {
                saw_brick_score = true;
            }
        }
        RELEASE(state);
        if (saw_brick_score) {
            break;
        }
    }

    stop_runloop_thread();
    breakout_viewer_test_context_destroy(&ctx);

    TEST_ASSERT_TRUE_MESSAGE(saw_brick_score,
                             "expected at least one brick-hit score update while runloop + collision dispatch are active");
}

TEST(test_breakout_runtime_startup_loads_collision_rules_for_all_launched_bricks) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_viewer_test_context_init(&ctx));

    ID counts = eval_string(
        "(do "
        "  (require 'tiny-breakout.runtime) "
        "  (tiny-breakout.runtime/start-runtime! nil) "
        "  (tiny-breakout.runtime/apply-input! {:launch true}) "
        "  [(count (:collision-rules @tiny-breakout.runtime/scene*)) "
        "   (:id (last (:bricks @tiny-breakout.runtime/state*)))])",
        ctx.st);
    TEST_ASSERT_NOT_NULL(counts);
    TEST_ASSERT_TRUE(TAG(counts) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *counts_vec = as_vector(counts);
    TEST_ASSERT_NOT_NULL(counts_vec);
    TEST_ASSERT_EQUAL_UINT(2u, vector_count(counts_vec));
    ID expected_rule_count = vector_nth(counts_vec, 0u);
    ID bottom_brick_id = vector_nth(counts_vec, 1u);
    TEST_ASSERT_TRUE(is_fixnum(expected_rule_count));
    TEST_ASSERT_TRUE(is_fixnum(bottom_brick_id));
    TEST_ASSERT_TRUE_MESSAGE((uint32_t)AS_FIXNUM(expected_rule_count) <= VIEWER_MAX_SPATIAL_RULES,
                             "test scene must fit inside VIEWER_MAX_SPATIAL_RULES");

    viewer_sync_configured_slots(&ctx.bundle, &ctx.spatial_rules, NULL, false);

    TEST_ASSERT_EQUAL_UINT32((uint32_t)AS_FIXNUM(expected_rule_count), ctx.spatial_rules.count);
    bool saw_bottom_brick_rule = false;
    for (uint32_t i = 0; i < ctx.spatial_rules.count; i++) {
        ViewerCollisionPolicy *policy = &ctx.spatial_rules.items[i];
        if (policy->rule_id == intern_symbol_global(":ball-vs-brick") &&
            is_fixnum(policy->other_entity_id) &&
            AS_FIXNUM(policy->other_entity_id) == AS_FIXNUM(bottom_brick_id)) {
            saw_bottom_brick_rule = true;
            break;
        }
    }

    breakout_viewer_test_context_destroy(&ctx);
    TEST_ASSERT_TRUE_MESSAGE(saw_bottom_brick_rule,
                             "expected collision policy set to include the last launched brick as a collision target");
}

TEST(test_breakout_runtime_startup_segment_rearm_ignores_stale_at_end_snapshot_until_new_frame) {
    BreakoutViewerTestContext ctx = {0};
    uint16_t pixels[320u * 240u] = {0};
    VgFrameBuffer fb = {0};
    VgRenderSlotState render_state = {0};
    TEST_ASSERT_TRUE(breakout_viewer_test_context_init(&ctx));
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, 320u, 240u, pixels, 320u * 240u));

    ID state_atom_id = eval_string(
        "(do "
        "  (require 'tiny-breakout.runtime) "
        "  (tiny-breakout.runtime/start-runtime! nil) "
        "  tiny-breakout.runtime/state*)",
        ctx.st);
    TEST_ASSERT_NOT_NULL(state_atom_id);
    TEST_ASSERT_EQUAL_UINT8(CLJ_ATOM, TAG(state_atom_id));
    CljAtom *state_atom = as_atom(state_atom_id);
    TEST_ASSERT_NOT_NULL(state_atom);

    ID seeded_old = eval_string(
        "(do "
        "  (require 'tiny-breakout.runtime) "
        "  (let [now-ms (current-time-ms) "
        "        s1 (-> @tiny-breakout.runtime/state* "
        "               (assoc :phase :play) "
        "               (assoc :levels [{:id :l1 :bricks []}]) "
        "               (assoc :bricks []) "
        "               (assoc :events []) "
        "               (assoc :ball-x 10 :ball-y 100) "
        "               (assoc :ball-vx 2 :ball-vy -2) "
        "               (assoc :segment-id-seq 1) "
        "               (assoc :ball-segment {:id 1 :start-ms (- now-ms 200) :end-ms now-ms :to-x 316 :to-y 75 :wall :right}))] "
        "    (tiny-breakout.runtime/publish-state! s1) "
        "    true))",
        ctx.st);
    TEST_ASSERT_EQUAL_PTR(clj_true, seeded_old);

    ID snapshot = atom_deref_owned(ctx.bundle.primary_scene_atom);
    TEST_ASSERT_NOT_NULL(snapshot);
    uint32_t t0 = (uint32_t)platform_current_time_ms();
    vg_rendered_state_capture_begin(ctx.bundle.primary_slot_index, 1u, t0);
    VgRenderFrameSlotResult slot_result0 = {0};
    bool rendered0 = vg_render_frame_slot_record_result_at_ms(snapshot,
                                                               &render_state,
                                                               &fb,
                                                               1u,
                                                               t0,
                                                               false,
                                                               &slot_result0);
    if (rendered0) {
        vg_rendered_state_capture_commit();
    } else {
        vg_rendered_state_capture_discard();
    }
    RELEASE(snapshot);
    TEST_ASSERT_TRUE(rendered0);

    ID seeded_new = eval_string(
        "(do "
        "  (require 'tiny-breakout.runtime) "
        "  (let [now-ms (current-time-ms) "
        "        s2 (-> @tiny-breakout.runtime/state* "
        "               (assoc :phase :play) "
        "               (assoc :levels [{:id :l1 :bricks []}]) "
        "               (assoc :bricks []) "
        "               (assoc :events []) "
        "               (assoc :ball-x 316 :ball-y 75) "
        "               (assoc :ball-vx -2 :ball-vy -2) "
        "               (assoc :segment-id-seq 2) "
        "               (assoc :ball-segment {:id 2 :start-ms now-ms :end-ms (+ now-ms 500) :to-x 0 :to-y 13 :wall :left}))] "
        "    (tiny-breakout.runtime/publish-state! s2) "
        "    true))",
        ctx.st);
    TEST_ASSERT_EQUAL_PTR(clj_true, seeded_new);

    ID segment_seq_kw = intern_symbol_global(":segment-id-seq");
    ID phase_kw = intern_symbol_global(":phase");
    ID play_kw = intern_symbol_global(":play");
    TEST_ASSERT_NOT_NULL(segment_seq_kw);
    TEST_ASSERT_NOT_NULL(phase_kw);
    TEST_ASSERT_NOT_NULL(play_kw);

    TEST_ASSERT_TRUE(start_runloop_thread(ctx.st));

    int seq_before_new_frames = -1;
    for (int i = 0; i < 25; i++) {
        usleep(10000);
        ID state = atom_deref_owned(state_atom);
        if (state && is_map(state)) {
            ID seq = map_get_sentinel(state, segment_seq_kw, NULL);
            if (seq && is_fixnum(seq)) {
                seq_before_new_frames = as_fixnum(seq);
            }
        }
        RELEASE(state);
    }

    int seq_after_frames = seq_before_new_frames;
    bool phase_after_frames_is_play = false;
    for (int i = 0; i < 120; i++) {
        usleep(10000);
        ID snap = atom_deref_owned(ctx.bundle.primary_scene_atom);
        if (snap) {
            uint32_t frame_serial = (uint32_t)(i + 2);
            uint32_t now_ms = (uint32_t)platform_current_time_ms();
            vg_rendered_state_capture_begin(ctx.bundle.primary_slot_index, frame_serial, now_ms);
            VgRenderFrameSlotResult slot_result = {0};
            if (vg_render_frame_slot_record_result_at_ms(snap,
                                                         &render_state,
                                                         &fb,
                                                         frame_serial,
                                                         now_ms,
                                                         render_state.has_animation,
                                                         &slot_result)) {
                vg_rendered_state_capture_commit();
            } else {
                vg_rendered_state_capture_discard();
            }
            RELEASE(snap);
        }

        ID state = atom_deref_owned(state_atom);
        if (state && is_map(state)) {
            ID seq = map_get_sentinel(state, segment_seq_kw, NULL);
            ID phase = map_get_sentinel(state, phase_kw, NULL);
            if (seq && is_fixnum(seq)) {
                seq_after_frames = as_fixnum(seq);
            }
            phase_after_frames_is_play = (phase == play_kw);
        }
        RELEASE(state);
    }

    stop_runloop_thread();
    breakout_viewer_test_context_destroy(&ctx);

    TEST_ASSERT_TRUE_MESSAGE(seq_before_new_frames == 2,
                             "stale at-end snapshot must not advance to a newer segment before renderer publishes a fresh frame");
    TEST_ASSERT_TRUE_MESSAGE(seq_after_frames > 2,
                             "after fresh frames arrive, segment progression should resume");
    TEST_ASSERT_TRUE(phase_after_frames_is_play);
}

TEST(test_breakout_runtime_startup_brick_hit_followed_by_wall_contact_keeps_segment_progressing) {
    BreakoutViewerTestContext ctx = {0};
    uint16_t pixels[320u * 240u] = {0};
    VgFrameBuffer fb = {0};
    VgRenderSlotState render_state = {0};
    TEST_ASSERT_TRUE(breakout_viewer_test_context_init(&ctx));
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, 320u, 240u, pixels, 320u * 240u));
    TEST_ASSERT_NOT_NULL(ctx.bundle.spatial_callback);

    ID state_atom_id = eval_string(
        "(do "
        "  (require 'tiny-breakout.runtime) "
        "  (tiny-breakout.runtime/start-runtime! nil) "
        "  (tiny-breakout.runtime/apply-input! {:launch true}) "
        "  tiny-breakout.runtime/state*)",
        ctx.st);
    TEST_ASSERT_NOT_NULL(state_atom_id);
    TEST_ASSERT_EQUAL_UINT8(CLJ_ATOM, TAG(state_atom_id));
    CljAtom *state_atom = as_atom(state_atom_id);
    TEST_ASSERT_NOT_NULL(state_atom);

    ID score_kw = intern_symbol_global(":score");
    ID segment_seq_kw = intern_symbol_global(":segment-id-seq");
    ID phase_kw = intern_symbol_global(":phase");
    ID serve_kw = intern_symbol_global(":serve");
    ID game_over_kw = intern_symbol_global(":game-over");
    TEST_ASSERT_NOT_NULL(score_kw);
    TEST_ASSERT_NOT_NULL(segment_seq_kw);
    TEST_ASSERT_NOT_NULL(phase_kw);
    TEST_ASSERT_NOT_NULL(serve_kw);
    TEST_ASSERT_NOT_NULL(game_over_kw);

    ID brick_event = eval_string(
        "(do "
        "  (require 'tiny-breakout.runtime) "
        "  (let [b (first (:bricks @tiny-breakout.runtime/state*)) "
        "        bx (:x b) by (:y b) bw (:w b) bh (:h b)] "
        "    {:source :spatial "
        "     :id :ball-vs-brick "
        "     :rule {:id :ball-vs-brick} "
        "     :phase :enter "
        "     :other (:id b) "
        "     :self-aabb {:min-x bx :min-y (+ by bh) :max-x (+ bx 4) :max-y (+ by bh 4)} "
        "     :other-aabb {:min-x bx :min-y by :max-x (+ bx bw) :max-y (+ by bh)}}))",
        ctx.st);
    TEST_ASSERT_NOT_NULL(brick_event);
    TEST_ASSERT_TRUE(is_map(brick_event));

    TEST_ASSERT_TRUE(start_runloop_thread(ctx.st));

    ID retained_event = RETAIN(brick_event);
    TEST_ASSERT_TRUE(event_loop_enqueue_ingress_call((CljObject *)ctx.bundle.spatial_callback, retained_event));
    RELEASE(retained_event);

    bool saw_brick_score = false;
    bool saw_segment_advance_after_brick = false;
    bool reached_serve_or_game_over_after_brick = false;
    int brick_segment_seq = -1;

    for (int i = 0; i < 450; i++) {
        usleep(10000);

        ID snapshot = atom_deref_owned(ctx.bundle.primary_scene_atom);
        if (snapshot) {
            uint32_t frame_serial = (uint32_t)(i + 1);
            uint32_t now_ms = (uint32_t)platform_current_time_ms();
            vg_rendered_state_capture_begin(ctx.bundle.primary_slot_index, frame_serial, now_ms);
            VgRenderFrameSlotResult slot_result = {0};
            if (vg_render_frame_slot_record_result_at_ms(snapshot,
                                                         &render_state,
                                                         &fb,
                                                         frame_serial,
                                                         now_ms,
                                                         render_state.has_animation,
                                                         &slot_result)) {
                vg_rendered_state_capture_commit();
            } else {
                vg_rendered_state_capture_discard();
            }
            RELEASE(snapshot);
        }

        ID state = atom_deref_owned(state_atom);
        if (state && is_map(state)) {
            ID score = map_get_sentinel(state, score_kw, NULL);
            ID segment_seq = map_get_sentinel(state, segment_seq_kw, NULL);
            ID phase = map_get_sentinel(state, phase_kw, NULL);
            if (!saw_brick_score && score && is_fixnum(score) && as_fixnum(score) > 0) {
                saw_brick_score = true;
                if (segment_seq && is_fixnum(segment_seq)) {
                    brick_segment_seq = as_fixnum(segment_seq);
                }
            } else if (saw_brick_score && segment_seq && is_fixnum(segment_seq) &&
                       brick_segment_seq >= 0 && as_fixnum(segment_seq) > brick_segment_seq) {
                saw_segment_advance_after_brick = true;
            } else if (saw_brick_score && (phase == serve_kw || phase == game_over_kw)) {
                reached_serve_or_game_over_after_brick = true;
            }
        }
        RELEASE(state);

        if (saw_segment_advance_after_brick || reached_serve_or_game_over_after_brick) {
            break;
        }
    }

    stop_runloop_thread();
    breakout_viewer_test_context_destroy(&ctx);

    TEST_ASSERT_TRUE_MESSAGE(saw_brick_score,
                             "expected to observe at least one scored brick hit");
    TEST_ASSERT_TRUE_MESSAGE(saw_segment_advance_after_brick || reached_serve_or_game_over_after_brick,
                             "after first brick hit, expected either further segment-id-seq advance or a clean serve/game-over transition");
}

TEST(test_breakout_runtime_startup_first_launch_with_render_thread_fits_debug_heap_limit) {
    BreakoutViewerTestContext ctx = {0};
    VgSlotChangeTracker slot_change_tracker = {0};
    VgFrameBuffer fb = {0};
    size_t previous_limit = memory_get_heap_limit_bytes();
    bool caught = false;
    ID caught_ex = NULL;
    uint_fast32_t last_collision_frame_serial = 0u;
    ID startup_fn = NULL;
    ID launch_fn = NULL;
    ID launch_arg = NULL;
    ID state_atom_id = NULL;
    ID play_phase = intern_symbol_global(":play");
    ID k_phase = intern_symbol_global(":phase");
    ID final_state = NULL;
    ID final_phase = NULL;

    TRY {
        TEST_ASSERT_TRUE(breakout_viewer_test_context_init_with_heap_budget(&ctx, true));
        TEST_ASSERT_TRUE(viewer_init_slot_runtime_buffers(&ctx.bundle));
        TEST_ASSERT_TRUE(vg_slot_change_tracker_init(&slot_change_tracker, ctx.bundle.slot_count));
        memset(g_render_buffer, 0, sizeof(g_render_buffer));
        TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, VIEW_W, VIEW_H, g_render_buffer, VIEW_W * VIEW_H));
        startup_fn = RETAIN(eval_string(
            "(do "
            "  (require 'tiny-breakout.runtime) "
            "  tiny-breakout.runtime/start-runtime!)",
            ctx.st));
        launch_fn = RETAIN(eval_string("tiny-breakout.runtime/apply-input!", ctx.st));
        launch_arg = RETAIN(eval_string("{:launch true}", ctx.st));
        state_atom_id = RETAIN(eval_string("tiny-breakout.runtime/state*", ctx.st));
        TEST_ASSERT_NOT_NULL(startup_fn);
        TEST_ASSERT_NOT_NULL(launch_fn);
        TEST_ASSERT_NOT_NULL(launch_arg);
        TEST_ASSERT_NOT_NULL(state_atom_id);
        TEST_ASSERT_EQUAL_UINT8(CLJ_ATOM, TAG(state_atom_id));
        tiny_renderer_lifecycle_set_callbacks(viewer_renderer_start_callback,
                                              viewer_renderer_stop_callback,
                                              &fb);
        TEST_ASSERT_TRUE(tiny_renderer_lifecycle_start(NULL));
        TEST_ASSERT_TRUE(start_runloop_thread(ctx.st));
        TEST_ASSERT_TRUE(event_loop_enqueue_ingress_call((CljObject *)startup_fn, NULL));
        TEST_ASSERT_TRUE(event_loop_enqueue_ingress_call((CljObject *)launch_fn, launch_arg));

        for (int i = 0; i < 90; i++) {
            usleep(16000);
            viewer_sync_configured_slots(&ctx.bundle, &ctx.spatial_rules, &slot_change_tracker, true);
            ViewerFrameRenderResult render_result = viewer_poll_render_frame();
            if (viewer_should_run_collision_step(render_result.frame_serial,
                                                 &last_collision_frame_serial)) {
                (void)viewer_collision_detect_step(&ctx.bundle,
                                                   &ctx.spatial_rules,
                                                   (uint32_t)platform_current_time_ms(),
                                                   NULL,
                                                   0u);
            }
        }
    } CATCH(ex) {
        caught = true;
        caught_ex = ex;
    } END_TRY

    memory_set_heap_limit_bytes(previous_limit);
    stop_runloop_thread();
    if (state_atom_id && TAG(state_atom_id) == CLJ_ATOM) {
        final_state = atom_deref((CljAtom *)state_atom_id);
        if (is_map(final_state) && k_phase) {
            final_phase = map_get_sentinel((CljPersistentMap *)final_state, k_phase, NULL);
        }
    }
    (void)tiny_renderer_lifecycle_stop();
    tiny_renderer_lifecycle_set_callbacks(NULL, NULL, NULL);
    vg_slot_change_tracker_destroy(&slot_change_tracker);
    viewer_destroy_slot_runtime_buffers();
    RELEASE(startup_fn);
    RELEASE(launch_fn);
    RELEASE(launch_arg);
    RELEASE(state_atom_id);
    breakout_viewer_test_context_destroy(&ctx);

    if (caught_ex) {
        print_exception(caught_ex);
    }

    TEST_ASSERT_FALSE_MESSAGE(caught,
                              caught_ex ? "first launch with render thread should fit inside the 640KB debug heap budget"
                                        : "");
    TEST_ASSERT_EQUAL_PTR(play_phase, final_phase);
}
