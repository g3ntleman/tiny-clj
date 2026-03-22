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
#include "../viewer_collision_bridge.h"
#define main tinyclj_game_demo_minifb_test_main
#include "../game_demo_minifb.c"
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
        viewer_tiny_fx_host_apply_heap_limit();
    }

    ctx->st = evalstate_new(true);
    if (!ctx->st) {
        return false;
    }
    evalstate_set_ns(ctx->st, "user");
    if (!tiny_fx_gfx_ensure_schema(ctx->st)) {
        return false;
    }
    if (!viewer_load_game_demo_config(ctx->st, config_source, &ctx->bundle, &ctx->spatial_rules)) {
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
    if (!tiny_fx_gfx_ensure_schema(ctx->st)) {
        return false;
    }
    if (!viewer_load_game_demo_config(ctx->st, config_source, &ctx->bundle, &ctx->spatial_rules)) {
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
    size_t host_limit = viewer_tiny_fx_host_heap_limit_bytes();
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

TEST(test_breakout_runtime_startup_applies_absolute_host_heap_limit_before_clojure_bootstrap) {
    size_t previous_limit = memory_get_heap_limit_bytes();
    size_t host_limit = viewer_tiny_fx_host_heap_limit_bytes();

    TEST_ASSERT_TRUE(memory_current_usage_bytes() > 0u);
    viewer_tiny_fx_host_apply_heap_limit();
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
    ViewerConfigSource config_source = viewer_selected_config_source();

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

    TEST_ASSERT_TRUE(ctx.bundle.has_game_slot);
    TEST_ASSERT_NOT_NULL(ctx.bundle.startup_callback);
    TEST_ASSERT_NOT_NULL(ctx.bundle.game_scene_atom);
    TEST_ASSERT_NOT_NULL(ctx.bundle.game_scene);
    TEST_ASSERT_NOT_NULL(ctx.bundle.spatial_callback);
    TEST_ASSERT_TRUE(ctx.bundle.game_slot_index < ctx.bundle.slot_count);
    TEST_ASSERT_EQUAL_PTR(ctx.bundle.game_scene, ctx.bundle.slots[ctx.bundle.game_slot_index].scene);
    TEST_ASSERT_TRUE(ctx.spatial_rules.count >= 1u);

    breakout_viewer_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_retain_slot_scenes_independently_of_scene_atoms) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_viewer_test_context_init(&ctx));

    TEST_ASSERT_TRUE(ctx.bundle.has_game_slot);
    TEST_ASSERT_NOT_NULL(ctx.bundle.game_scene);
    TEST_ASSERT_TRUE(((CljObject *)ctx.bundle.game_scene)->rc > 1);

    breakout_viewer_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_spatial_events_include_entity_snapshots) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_viewer_test_context_init(&ctx));
    TEST_ASSERT_TRUE(ctx.spatial_rules.count > 0u);

    FrameScene *scene = viewer_frame_scene_from_atom(ctx.bundle.game_scene_atom);
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

TEST(test_breakout_runtime_startup_fire_button_heap_profile_stays_bounded) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_viewer_test_context_init_with_heap_budget(&ctx, true));

    ID stats = eval_string(
        "(do "
        "  (require 'tiny-clj.gpio) "
        "  (require 'tiny-breakout.runtime) "
        "  (require 'tiny-clj.event) "
        "  (require 'tiny-fx.gfx-timeline) "
        "  (heap "
        "    (do "
        "      (tiny-breakout.runtime/start-runtime! nil) "
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

    breakout_viewer_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_spatial_callback_scene_replacement_reloads_rules_safely) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_viewer_test_context_init(&ctx));
    TEST_ASSERT_TRUE(ctx.bundle.has_game_slot);
    TEST_ASSERT_TRUE(ctx.spatial_rules.count > 0u);

    ID replacement_scene = eval_string(
        "(do "
        "  (require 'tiny-fx.gfx) "
        "  (require '[tiny-fx.gfx-scene :refer :all]) "
        "  (record-create (quote FrameScene) ["
        "    'root "
        "    {'root (->Line 'root nil (->Style 65535 1 true false 0 false 0) true 0 0 1 1 nil)} "
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
    vg_rendered_state_capture_begin(ctx.bundle.game_slot_index, 1u, 0u);
    vg_rendered_state_capture_record_entity((uintptr_t)policy->self_entity_id, world_t);
    vg_rendered_state_capture_record_entity_aabb((uintptr_t)policy->self_entity_id, self_box);
    vg_rendered_state_capture_record_entity((uintptr_t)policy->other_entity_id, world_t);
    vg_rendered_state_capture_record_entity_aabb((uintptr_t)policy->other_entity_id, other_box);
    vg_rendered_state_capture_commit();

    TEST_ASSERT_TRUE(viewer_collision_detect_step(&ctx.bundle, &ctx.spatial_rules, 0u, NULL, 0u));
    TEST_ASSERT_EQUAL_PTR(replacement_scene, atom_reset(ctx.bundle.game_scene_atom, replacement_scene));

    viewer_sync_configured_slots(&ctx.bundle, &ctx.spatial_rules, NULL, false);
    TEST_ASSERT_EQUAL_UINT32(0u, ctx.spatial_rules.count);

    breakout_viewer_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_collision_latch_recovers_after_missing_snapshot_entities) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_viewer_test_context_init(&ctx));
    TEST_ASSERT_TRUE(ctx.bundle.has_game_slot);

    ID paddle_rule_id = intern_symbol_global(":ball-vs-paddle");
    ViewerCollisionPolicy *policy = breakout_find_policy_by_id(&ctx.spatial_rules, paddle_rule_id);
    TEST_ASSERT_NOT_NULL(policy);
    ViewerSpatialRuleSet single_rule_set = {0};
    single_rule_set.items[0] = *policy;
    single_rule_set.count = 1u;

    uint8_t slot = ctx.bundle.game_slot_index;
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
    TEST_ASSERT_TRUE(ctx.bundle.has_game_slot);

    ID paddle_rule_id = intern_symbol_global(":ball-vs-paddle");
    ViewerCollisionPolicy *policy = breakout_find_policy_by_id(&ctx.spatial_rules, paddle_rule_id);
    TEST_ASSERT_NOT_NULL(policy);
    ViewerSpatialRuleSet single_rule_set = {0};
    single_rule_set.items[0] = *policy;
    single_rule_set.count = 1u;

    uint8_t slot = ctx.bundle.game_slot_index;
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
    TEST_ASSERT_TRUE(ctx.bundle.has_game_slot);

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

    uint8_t slot = ctx.bundle.game_slot_index;
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
    TEST_ASSERT_TRUE(ctx.bundle.has_game_slot);

    ID paddle_rule_id = intern_symbol_global(":ball-vs-paddle");
    ViewerCollisionPolicy *policy = breakout_find_policy_by_id(&ctx.spatial_rules, paddle_rule_id);
    TEST_ASSERT_NOT_NULL(policy);
    ViewerSpatialRuleSet single_rule_set = {0};
    single_rule_set.items[0] = *policy;
    single_rule_set.count = 1u;

    uint8_t slot = ctx.bundle.game_slot_index;
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
    size_t baseline = memory_current_usage_bytes();
    bool caught = false;
    bool triggered = false;
    bool drained = false;
    TRY {
        memory_set_heap_limit_bytes(baseline + 16u);
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

TEST(test_breakout_runtime_startup_runloop_play_loop_survives_timeline_watch_driven_scene_updates) {
    BreakoutViewerTestContext ctx = {0};
    uint16_t pixels[320u * 240u] = {0};
    VgFrameBuffer fb = {0};
    VgRenderSlotState render_state = {0};
    size_t previous_limit = memory_get_heap_limit_bytes();
    bool caught_oom = false;

    TEST_ASSERT_TRUE(breakout_viewer_test_context_init_with_heap_budget(&ctx, true));
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

    vg_rendered_state_capture_begin(ctx.bundle.game_slot_index, 1u, (uint32_t)platform_current_time_ms());
    VgRenderFrameSlotResult initial_slot_result = {0};
    bool initial_rendered = vg_render_frame_slot_record_result_at_ms(
        atom_deref(ctx.bundle.game_scene_atom), &render_state, &fb,
        1u, (uint32_t)platform_current_time_ms(), false, &initial_slot_result);
    if (initial_rendered) {
        vg_rendered_state_capture_commit();
    } else {
        vg_rendered_state_capture_discard();
    }
    TEST_ASSERT_TRUE(initial_rendered);

    TRY {
        TEST_ASSERT_TRUE(start_runloop_thread(ctx.st));
        for (int i = 0; i < 200; i++) {
            usleep(20000);
            ID snapshot = atom_deref_owned(ctx.bundle.game_scene_atom);
            if (snapshot) {
                vg_rendered_state_capture_begin(ctx.bundle.game_slot_index, (uint32_t)(i + 2), (uint32_t)platform_current_time_ms());
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
    } CATCH(ex) {
        caught_oom = true;
        if (ex) {
            print_exception(ex);
        }
    } END_TRY

    stop_runloop_thread();
    memory_set_heap_limit_bytes(previous_limit);

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
    TEST_ASSERT_FALSE_MESSAGE(caught_oom,
                              "play loop OOM: memory leak during timeline-driven scene updates");

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
