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
