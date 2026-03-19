#include <stdbool.h>

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
#include "unity/src/unity.h"
#include "test_registry.h"

typedef struct {
    ViewerSceneBundle bundle;
    ViewerSpatialRuleSet spatial_rules;
    EvalState *st;
} BreakoutViewerTestContext;

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
    return viewer_load_game_demo_config(ctx->st, config_source, &ctx->bundle, &ctx->spatial_rules);
}

static bool breakout_viewer_test_context_init(BreakoutViewerTestContext *ctx) {
    return breakout_viewer_test_context_init_with_heap_budget(ctx, false);
}

static void breakout_viewer_test_context_destroy(BreakoutViewerTestContext *ctx) {
    if (!ctx) {
        return;
    }
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

TEST(test_breakout_runtime_startup_applies_absolute_host_heap_limit_before_clojure_bootstrap) {
    size_t previous_limit = memory_get_heap_limit_bytes();
    size_t host_limit = viewer_tiny_fx_host_heap_limit_bytes();

    TEST_ASSERT_TRUE(memory_current_usage_bytes() > 0u);
    viewer_tiny_fx_host_apply_heap_limit();
    TEST_ASSERT_EQUAL_UINT64(host_limit, memory_get_heap_limit_bytes());

    memory_set_heap_limit_bytes(previous_limit);
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

TEST(test_breakout_runtime_startup_loads_breakout_host_config_into_generic_viewer_bundle) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_viewer_test_context_init(&ctx));

    TEST_ASSERT_TRUE(ctx.bundle.has_game_slot);
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
    ID expected_self = map_get_sentinel(viewer_scene_entity_map(scene), policy->self_entity_id, NULL);
    ID expected_other = map_get_sentinel(viewer_scene_entity_map(scene), policy->other_entity_id, NULL);
    TEST_ASSERT_NOT_NULL(expected_self);
    TEST_ASSERT_NOT_NULL(expected_other);

    VgAabb self_box = {.min_x = 1, .min_y = 2, .max_x = 3, .max_y = 4};
    VgAabb other_box = {.min_x = 5, .min_y = 6, .max_x = 7, .max_y = 8};
    ID event = viewer_make_spatial_event(&ctx.bundle,
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
