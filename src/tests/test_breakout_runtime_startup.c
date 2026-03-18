#include "tests_common.h"

#ifndef TINYCLJ_WITH_MINIFB
#define TINYCLJ_WITH_MINIFB 1
#endif
#if defined(__APPLE__)
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
#endif
#define main tinyclj_game_demo_minifb_test_main
#include "../game_demo_minifb.c"
#undef main

typedef struct {
    ViewerSceneBundle bundle;
    ViewerSpatialRuleSet spatial_rules;
    EvalState *st;
} BreakoutRuntimeTestContext;

static bool breakout_runtime_test_context_init_with_heap_budget(BreakoutRuntimeTestContext *ctx,
                                                                bool apply_host_heap_budget);

static void breakout_runtime_test_context_destroy(BreakoutRuntimeTestContext *ctx) {
    if (!ctx) {
        return;
    }
    destroy_scene_bundle(&ctx->bundle);
    destroy_spatial_rule_set(&ctx->spatial_rules);
    viewer_breakout_runtime_clear(&g_breakout_runtime);
    evalstate_free(ctx->st);
    memset(ctx, 0, sizeof(*ctx));
}

static bool breakout_runtime_test_context_init(BreakoutRuntimeTestContext *ctx) {
    return breakout_runtime_test_context_init_with_heap_budget(ctx, false);
}

static bool breakout_runtime_test_context_init_with_heap_budget(BreakoutRuntimeTestContext *ctx,
                                                                bool apply_host_heap_budget) {
    if (!ctx) {
        return false;
    }
    memset(ctx, 0, sizeof(*ctx));
    ViewerConfigSource config_source = {
        .namespace_name = "tiny-clj.deployment",
        .config_expr = "(tiny-clj.deployment/breakout-host-config)",
        .display_name = "tiny-clj.deployment/breakout-host-config",
    };

    runtime_init(&g_runtime);
    event_loop_init();
    vg_rendered_state_reset_all();

    ctx->st = evalstate_new(true);
    if (!ctx->st) {
        return false;
    }
    evalstate_set_ns(ctx->st, "user");
    if (!tiny_fx_gfx_ensure_schema(ctx->st)) {
        return false;
    }
    if (apply_host_heap_budget) {
        viewer_tiny_fx_host_apply_heap_limit_after_bootstrap();
    }
    if (!viewer_load_game_demo_config(ctx->st, config_source, &ctx->bundle, &ctx->spatial_rules)) {
        return false;
    }
    if (!viewer_breakout_runtime_enabled(&ctx->bundle)) {
        return false;
    }
    if (!viewer_breakout_runtime_init_from_state(&g_breakout_runtime, &ctx->bundle)) {
        return false;
    }
    return viewer_breakout_runtime_activate(&g_breakout_runtime,
                                            &ctx->bundle,
                                            &ctx->spatial_rules,
                                            false);
}

TEST(test_breakout_runtime_startup_host_app_fits_debug_heap_limit) {
    BreakoutRuntimeTestContext ctx = {0};
    size_t previous_limit = memory_get_heap_limit_bytes();
    size_t host_limit = viewer_tiny_fx_host_heap_limit_bytes();
    bool init_ok = false;

    TEST_ASSERT_EQUAL_UINT64(614400u, host_limit);
    init_ok = breakout_runtime_test_context_init_with_heap_budget(&ctx, true);
    memory_set_heap_limit_bytes(previous_limit);

    if (init_ok) {
        breakout_runtime_test_context_destroy(&ctx);
    }
    TEST_ASSERT_TRUE_MESSAGE(init_ok, "breakout host startup should fit inside the tiny-fx debug heap limit");
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

TEST(test_breakout_runtime_startup_activates_native_title_scene_before_first_input) {
    BreakoutRuntimeTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_runtime_test_context_init(&ctx));

    FrameScene *initial_scene = viewer_frame_scene_from_atom(ctx.bundle.game_scene_atom);
    TEST_ASSERT_NOT_NULL(initial_scene);

    FrameScene *native_scene = viewer_frame_scene_from_atom(ctx.bundle.game_scene_atom);
    TEST_ASSERT_NOT_NULL(native_scene);
    TEST_ASSERT_TRUE(ctx.bundle.has_game_slot);
    TEST_ASSERT_EQUAL_PTR(native_scene, ctx.bundle.game_scene);
    TEST_ASSERT_EQUAL_PTR(native_scene, ctx.bundle.slots[ctx.bundle.game_slot_index].scene);

    uint8_t keys[KB_KEY_LAST + 1] = {0};
    ViewerRuntimeFlags runtime_flags = {0};
    keys[KB_KEY_ENTER] = 1;
    viewer_breakout_runtime_step(&g_breakout_runtime, &ctx.bundle, &runtime_flags, keys);
    viewer_sync_configured_slots(&ctx.bundle, &ctx.spatial_rules, false);

    FrameScene *staged_scene = viewer_frame_scene_from_atom(ctx.bundle.game_scene_atom);
    TEST_ASSERT_NOT_NULL(staged_scene);
    TEST_ASSERT_NOT_EQUAL(native_scene, staged_scene);
    TEST_ASSERT_EQUAL_INT(BREAKOUT_PHASE_SERVE, g_breakout_runtime.phase);
    TEST_ASSERT_EQUAL_PTR(staged_scene, ctx.bundle.game_scene);
    TEST_ASSERT_EQUAL_PTR(staged_scene, ctx.bundle.slots[ctx.bundle.game_slot_index].scene);

    memset(keys, 0, sizeof(keys));
    viewer_breakout_runtime_step(&g_breakout_runtime, &ctx.bundle, &runtime_flags, keys);
    viewer_sync_configured_slots(&ctx.bundle, &ctx.spatial_rules, false);

    FrameScene *started_scene = viewer_frame_scene_from_atom(ctx.bundle.game_scene_atom);
    TEST_ASSERT_NOT_NULL(started_scene);
    TEST_ASSERT_NOT_EQUAL(staged_scene, started_scene);
    TEST_ASSERT_EQUAL_PTR(started_scene, ctx.bundle.game_scene);
    TEST_ASSERT_EQUAL_PTR(started_scene, ctx.bundle.slots[ctx.bundle.game_slot_index].scene);

    breakout_runtime_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_only_materializes_active_level_brick_records) {
    BreakoutRuntimeTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_runtime_test_context_init(&ctx));

    TEST_ASSERT_TRUE(g_breakout_runtime.level_count > 0u);
    TEST_ASSERT_TRUE(g_breakout_runtime.levels[0].brick_count > 0u);
    TEST_ASSERT_NULL(g_breakout_runtime.levels[0].bricks[0].record);

    uint8_t keys[KB_KEY_LAST + 1] = {0};
    ViewerRuntimeFlags runtime_flags = {0};
    keys[KB_KEY_ENTER] = 1;
    viewer_breakout_runtime_step(&g_breakout_runtime, &ctx.bundle, &runtime_flags, keys);
    viewer_sync_configured_slots(&ctx.bundle, &ctx.spatial_rules, false);
    memset(keys, 0, sizeof(keys));
    viewer_breakout_runtime_step(&g_breakout_runtime, &ctx.bundle, &runtime_flags, keys);
    viewer_sync_configured_slots(&ctx.bundle, &ctx.spatial_rules, false);

    TEST_ASSERT_TRUE(g_breakout_runtime.active_brick_count > 0u);
    TEST_ASSERT_NOT_NULL(g_breakout_runtime.active_bricks[0].record);
    TEST_ASSERT_NULL(g_breakout_runtime.levels[0].bricks[0].record);

    breakout_runtime_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_first_fire_releases_title_scene_before_serve_scene_build) {
    BreakoutRuntimeTestContext probe_ctx = {0};
    BreakoutRuntimeTestContext run_ctx = {0};
    size_t previous_limit = memory_get_heap_limit_bytes();
    size_t serve_growth = 0;
    bool caught = false;
    ID caught_ex = NULL;

    TEST_ASSERT_TRUE(breakout_runtime_test_context_init(&probe_ctx));
    TEST_ASSERT_NOT_NULL(viewer_frame_scene_from_atom(probe_ctx.bundle.game_scene_atom));
    TEST_ASSERT_NULL(atom_peek(probe_ctx.bundle.game_state_atom));

    viewer_breakout_runtime_fresh_game(&g_breakout_runtime);
    ID probe_scene = viewer_breakout_make_scene_record(&g_breakout_runtime);
    TEST_ASSERT_NOT_NULL(probe_scene);
    serve_growth = memory_current_usage_bytes();
    RELEASE(probe_scene);
    TEST_ASSERT_TRUE(serve_growth > 0u);

    breakout_runtime_test_context_destroy(&probe_ctx);

    TEST_ASSERT_TRUE(breakout_runtime_test_context_init(&run_ctx));
    TEST_ASSERT_NULL(atom_peek(run_ctx.bundle.game_state_atom));
    size_t constrained_limit = memory_current_usage_bytes() + serve_growth + 64u;
    memory_set_heap_limit_bytes(constrained_limit);

    uint8_t keys[KB_KEY_LAST + 1] = {0};
    ViewerRuntimeFlags runtime_flags = {0};
    keys[KB_KEY_ENTER] = 1;
    TRY {
        viewer_breakout_runtime_step(&g_breakout_runtime, &run_ctx.bundle, &runtime_flags, keys);
        viewer_sync_configured_slots(&run_ctx.bundle, &run_ctx.spatial_rules, false);
        memset(keys, 0, sizeof(keys));
        viewer_breakout_runtime_step(&g_breakout_runtime, &run_ctx.bundle, &runtime_flags, keys);
        viewer_sync_configured_slots(&run_ctx.bundle, &run_ctx.spatial_rules, false);
    } CATCH(ex) {
        caught = true;
        caught_ex = ex;
    } END_TRY

    memory_set_heap_limit_bytes(previous_limit);
    TEST_ASSERT_FALSE_MESSAGE(caught, caught_ex ? "first fire should not OOM under constrained heap" : "");
    TEST_ASSERT_EQUAL_INT(BREAKOUT_PHASE_SERVE, g_breakout_runtime.phase);
    TEST_ASSERT_NOT_NULL(viewer_frame_scene_from_atom(run_ctx.bundle.game_scene_atom));

    breakout_runtime_test_context_destroy(&run_ctx);
}

TEST(test_breakout_runtime_idle_serve_does_not_rebuild_identical_scene_under_tight_heap_limit) {
    BreakoutRuntimeTestContext ctx = {0};
    size_t before_idle = 0;
    size_t after_idle = 0;

    TEST_ASSERT_TRUE(breakout_runtime_test_context_init(&ctx));

    uint8_t keys[KB_KEY_LAST + 1] = {0};
    ViewerRuntimeFlags runtime_flags = {0};
    keys[KB_KEY_ENTER] = 1;
    viewer_breakout_runtime_step(&g_breakout_runtime, &ctx.bundle, &runtime_flags, keys);
    viewer_sync_configured_slots(&ctx.bundle, &ctx.spatial_rules, false);
    memset(keys, 0, sizeof(keys));
    viewer_breakout_runtime_step(&g_breakout_runtime, &ctx.bundle, &runtime_flags, keys);
    viewer_sync_configured_slots(&ctx.bundle, &ctx.spatial_rules, false);

    TEST_ASSERT_EQUAL_INT(BREAKOUT_PHASE_SERVE, g_breakout_runtime.phase);
    before_idle = memory_current_usage_bytes();
    viewer_breakout_runtime_step(&g_breakout_runtime, &ctx.bundle, &runtime_flags, keys);
    viewer_sync_configured_slots(&ctx.bundle, &ctx.spatial_rules, false);
    after_idle = memory_current_usage_bytes();

    TEST_ASSERT_EQUAL_UINT64(before_idle, after_idle);
    TEST_ASSERT_EQUAL_INT(BREAKOUT_PHASE_SERVE, g_breakout_runtime.phase);

    breakout_runtime_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_play_loop_does_not_accumulate_unbounded_heap_growth) {
    BreakoutRuntimeTestContext ctx = {0};
    ViewerRuntimeFlags runtime_flags = {0};
    uint8_t keys[KB_KEY_LAST + 1] = {0};
    size_t baseline = 0;
    size_t after = 0;
    const size_t allowed_growth = 16384u;

    TEST_ASSERT_TRUE(breakout_runtime_test_context_init(&ctx));

    keys[KB_KEY_ENTER] = 1;
    viewer_breakout_runtime_step(&g_breakout_runtime, &ctx.bundle, &runtime_flags, keys);
    viewer_sync_configured_slots(&ctx.bundle, &ctx.spatial_rules, false);
    runtime_flags.fire_key_was_down = true;

    memset(keys, 0, sizeof(keys));
    viewer_breakout_runtime_step(&g_breakout_runtime, &ctx.bundle, &runtime_flags, keys);
    viewer_sync_configured_slots(&ctx.bundle, &ctx.spatial_rules, false);
    runtime_flags.fire_key_was_down = false;

    keys[KB_KEY_ENTER] = 1;
    viewer_breakout_runtime_step(&g_breakout_runtime, &ctx.bundle, &runtime_flags, keys);
    viewer_sync_configured_slots(&ctx.bundle, &ctx.spatial_rules, false);
    runtime_flags.fire_key_was_down = true;

    memset(keys, 0, sizeof(keys));
    viewer_breakout_runtime_step(&g_breakout_runtime, &ctx.bundle, &runtime_flags, keys);
    viewer_sync_configured_slots(&ctx.bundle, &ctx.spatial_rules, false);
    runtime_flags.fire_key_was_down = false;

    baseline = memory_current_usage_bytes();

    for (int frame = 0; frame < 600; frame++) {
        memset(keys, 0, sizeof(keys));
        if (g_breakout_runtime.phase == BREAKOUT_PHASE_TITLE ||
            g_breakout_runtime.phase == BREAKOUT_PHASE_SERVE ||
            g_breakout_runtime.phase == BREAKOUT_PHASE_LEVEL_CLEAR ||
            g_breakout_runtime.phase == BREAKOUT_PHASE_GAME_OVER ||
            g_breakout_runtime.phase == BREAKOUT_PHASE_VICTORY) {
            keys[KB_KEY_ENTER] = 1;
        } else if (g_breakout_runtime.ball_x + 2 < g_breakout_runtime.paddle_x + 20) {
            keys[KB_KEY_LEFT] = 1;
        } else if (g_breakout_runtime.ball_x + 2 > g_breakout_runtime.paddle_x + 20) {
            keys[KB_KEY_RIGHT] = 1;
        }

        viewer_breakout_runtime_step(&g_breakout_runtime, &ctx.bundle, &runtime_flags, keys);
        viewer_sync_configured_slots(&ctx.bundle, &ctx.spatial_rules, false);

        runtime_flags.fire_key_was_down = keys[KB_KEY_ENTER] != 0;
        runtime_flags.pause_key_was_down = keys[KB_KEY_Y] != 0;
    }

    after = memory_current_usage_bytes();
    TEST_ASSERT_TRUE_MESSAGE(after <= baseline + allowed_growth,
                             "native breakout play loop should not show unbounded heap growth");

    breakout_runtime_test_context_destroy(&ctx);
}
