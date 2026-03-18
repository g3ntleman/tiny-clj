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
static void breakout_runtime_test_enter_play(BreakoutRuntimeTestContext *ctx,
                                             ViewerRuntimeFlags *runtime_flags,
                                             uint8_t *keys);
static size_t breakout_runtime_test_measure_play_loop_heap_growth(BreakoutRuntimeTestContext *ctx,
                                                                  ViewerRuntimeFlags *runtime_flags,
                                                                  uint8_t *keys,
                                                                  int frame_count,
                                                                  bool sync_slots);
static size_t breakout_runtime_test_run_play_loop(BreakoutRuntimeTestContext *ctx,
                                                  ViewerRuntimeFlags *runtime_flags,
                                                  uint8_t *keys,
                                                  int frame_count,
                                                  bool sync_slots,
                                                  bool render_scene,
                                                  bool log_each_second);
static double breakout_runtime_test_bytes_to_mib(size_t bytes);

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
    stop_runloop_thread();
    viewer_breakout_runtime_clear(&g_breakout_runtime);
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

static void breakout_runtime_test_enter_play(BreakoutRuntimeTestContext *ctx,
                                             ViewerRuntimeFlags *runtime_flags,
                                             uint8_t *keys) {
    TEST_ASSERT_NOT_NULL(ctx);
    TEST_ASSERT_NOT_NULL(runtime_flags);
    TEST_ASSERT_NOT_NULL(keys);

    memset(keys, 0, (size_t)(KB_KEY_LAST + 1u));
    keys[KB_KEY_ENTER] = 1;
    viewer_breakout_runtime_step(&g_breakout_runtime, &ctx->bundle, runtime_flags, keys);
    viewer_sync_configured_slots(&ctx->bundle, &ctx->spatial_rules, false);
    runtime_flags->fire_key_was_down = true;

    memset(keys, 0, (size_t)(KB_KEY_LAST + 1u));
    viewer_breakout_runtime_step(&g_breakout_runtime, &ctx->bundle, runtime_flags, keys);
    viewer_sync_configured_slots(&ctx->bundle, &ctx->spatial_rules, false);
    runtime_flags->fire_key_was_down = false;

    keys[KB_KEY_ENTER] = 1;
    viewer_breakout_runtime_step(&g_breakout_runtime, &ctx->bundle, runtime_flags, keys);
    viewer_sync_configured_slots(&ctx->bundle, &ctx->spatial_rules, false);
    runtime_flags->fire_key_was_down = true;

    memset(keys, 0, (size_t)(KB_KEY_LAST + 1u));
    viewer_breakout_runtime_step(&g_breakout_runtime, &ctx->bundle, runtime_flags, keys);
    viewer_sync_configured_slots(&ctx->bundle, &ctx->spatial_rules, false);
    runtime_flags->fire_key_was_down = false;

    TEST_ASSERT_EQUAL_INT(BREAKOUT_PHASE_PLAY, g_breakout_runtime.phase);
}

static size_t breakout_runtime_test_measure_play_loop_heap_growth(BreakoutRuntimeTestContext *ctx,
                                                                  ViewerRuntimeFlags *runtime_flags,
                                                                  uint8_t *keys,
                                                                  int frame_count,
                                                                  bool sync_slots) {
    return breakout_runtime_test_run_play_loop(ctx,
                                               runtime_flags,
                                               keys,
                                               frame_count,
                                               sync_slots,
                                               false,
                                               false);
}

static double breakout_runtime_test_bytes_to_mib(size_t bytes) {
    return (double)bytes / (1024.0 * 1024.0);
}

static size_t breakout_runtime_test_run_play_loop(BreakoutRuntimeTestContext *ctx,
                                                  ViewerRuntimeFlags *runtime_flags,
                                                  uint8_t *keys,
                                                  int frame_count,
                                                  bool sync_slots,
                                                  bool render_scene,
                                                  bool log_each_second) {
    TEST_ASSERT_NOT_NULL(ctx);
    TEST_ASSERT_NOT_NULL(runtime_flags);
    TEST_ASSERT_NOT_NULL(keys);
    TEST_ASSERT_TRUE(frame_count >= 0);

    size_t baseline = memory_current_usage_bytes();
    uint16_t *pixels = NULL;
    VgFrameBuffer fb = {0};
    VgRenderSlotState render_state = {0};

    if (render_scene) {
        pixels = (uint16_t *)CLJ_HOST_CALLOC((size_t)VIEW_W * (size_t)VIEW_H, sizeof(uint16_t));
        TEST_ASSERT_NOT_NULL(pixels);
        TEST_ASSERT_TRUE(vg_framebuffer_init(&fb,
                                             VIEW_W,
                                             VIEW_H,
                                             pixels,
                                             (size_t)VIEW_W * (size_t)VIEW_H));
    }

    for (int frame = 0; frame < frame_count; frame++) {
        WITH_AUTORELEASE_POOL({
            memset(keys, 0, (size_t)(KB_KEY_LAST + 1u));
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

            viewer_breakout_runtime_step(&g_breakout_runtime, &ctx->bundle, runtime_flags, keys);
            if (sync_slots) {
                viewer_sync_configured_slots(&ctx->bundle, &ctx->spatial_rules, false);
            }
            if (render_scene) {
                FrameScene *scene = viewer_frame_scene_from_atom(ctx->bundle.game_scene_atom);
                TEST_ASSERT_NOT_NULL(scene);
                TEST_ASSERT_TRUE(vg_render_frame_slot_record_result_at_ms((ID)scene,
                                                                          &render_state,
                                                                          &fb,
                                                                          (uint32_t)frame + 1u,
                                                                          (uint32_t)frame * 16u,
                                                                          true,
                                                                          NULL));
            }
        });
        runtime_flags->fire_key_was_down = keys[KB_KEY_ENTER] != 0;
        runtime_flags->pause_key_was_down = keys[KB_KEY_Y] != 0;

        if (log_each_second && (((frame + 1) % 60) == 0 || frame == frame_count - 1)) {
            size_t current = memory_current_usage_bytes();
            size_t growth = (current >= baseline) ? (current - baseline) : 0u;
            size_t heap_limit = memory_get_heap_limit_bytes();
            bool heap_limit_active = heap_limit > 0u;
            size_t headroom = 0u;
            char headroom_buf[96] = {0};
            if (heap_limit_active && heap_limit > current) {
                headroom = heap_limit - current;
            }
            if (heap_limit_active) {
                test_snprintf(headroom_buf,
                              sizeof(headroom_buf),
                              " limit=%.2f MiB headroom=%lu B",
                              breakout_runtime_test_bytes_to_mib(heap_limit),
                              (unsigned long)headroom);
            }
            test_fprintf(stderr,
                         "[breakout-heap] t=%2ds frame=%4d baseline=%.2f MiB current=%.2f MiB delta=%lu B%s phase=%d level=%d bricks=%d\n",
                         (frame + 1) / 60,
                         frame + 1,
                         breakout_runtime_test_bytes_to_mib(baseline),
                         breakout_runtime_test_bytes_to_mib(current),
                         (unsigned long)growth,
                         headroom_buf,
                         (int)g_breakout_runtime.phase,
                         g_breakout_runtime.level_index + 1,
                         (int)g_breakout_runtime.remaining_brick_count);
        }
    }

    size_t growth = memory_current_usage_bytes() - baseline;
    if (pixels) {
        CLJ_HOST_FREE(pixels);
    }
    return growth;
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

TEST(test_breakout_runtime_startup_preserves_loaded_spatial_rules_across_native_scene_publish) {
    BreakoutRuntimeTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_runtime_test_context_init(&ctx));
    TEST_ASSERT_TRUE(ctx.spatial_rules.count > 1u);

    uint8_t keys[KB_KEY_LAST + 1] = {0};
    ViewerRuntimeFlags runtime_flags = {0};
    keys[KB_KEY_ENTER] = 1;
    viewer_breakout_runtime_step(&g_breakout_runtime, &ctx.bundle, &runtime_flags, keys);
    viewer_sync_configured_slots(&ctx.bundle, &ctx.spatial_rules, false);

    TEST_ASSERT_TRUE(ctx.spatial_rules.count > 1u);
    breakout_runtime_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_loads_concrete_spatial_rules_for_active_bricks) {
    BreakoutRuntimeTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_runtime_test_context_init(&ctx));

    uint8_t keys[KB_KEY_LAST + 1] = {0};
    ViewerRuntimeFlags runtime_flags = {0};
    keys[KB_KEY_ENTER] = 1;
    viewer_breakout_runtime_step(&g_breakout_runtime, &ctx.bundle, &runtime_flags, keys);
    viewer_sync_configured_slots(&ctx.bundle, &ctx.spatial_rules, false);
    memset(keys, 0, sizeof(keys));
    viewer_breakout_runtime_step(&g_breakout_runtime, &ctx.bundle, &runtime_flags, keys);
    viewer_sync_configured_slots(&ctx.bundle, &ctx.spatial_rules, false);

    TEST_ASSERT_TRUE(g_breakout_runtime.active_brick_count > 0u);
    TEST_ASSERT_EQUAL_UINT32(1u + g_breakout_runtime.active_brick_count, ctx.spatial_rules.count);
    TEST_ASSERT_EQUAL_INT(1003, AS_FIXNUM(ctx.spatial_rules.items[0].self_entity_id));
    TEST_ASSERT_EQUAL_INT(1002, AS_FIXNUM(ctx.spatial_rules.items[0].other_entity_id));

    bool found_brick_rule = false;
    for (uint32_t i = 1u; i < ctx.spatial_rules.count; i++) {
        ViewerCollisionPolicy *policy = &ctx.spatial_rules.items[i];
        if (AS_FIXNUM(policy->self_entity_id) != 1003) {
            continue;
        }
        for (uint32_t brick_i = 0u; brick_i < g_breakout_runtime.active_brick_count; brick_i++) {
            if (policy->other_entity_id == fixnum(g_breakout_runtime.active_bricks[brick_i].id)) {
                found_brick_rule = true;
                break;
            }
        }
        if (found_brick_rule) {
            break;
        }
    }
    TEST_ASSERT_TRUE(found_brick_rule);

    breakout_runtime_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_spatial_events_include_entity_snapshots) {
    BreakoutRuntimeTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_runtime_test_context_init(&ctx));
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

TEST(test_breakout_runtime_startup_level_clear_fire_publishes_next_level_immediately) {
    BreakoutRuntimeTestContext ctx = {0};
    ViewerRuntimeFlags runtime_flags = {0};
    uint8_t keys[KB_KEY_LAST + 1] = {0};

    TEST_ASSERT_TRUE(breakout_runtime_test_context_init(&ctx));
    TEST_ASSERT_TRUE(g_breakout_runtime.level_count > 1u);

    breakout_runtime_test_enter_play(&ctx, &runtime_flags, keys);
    g_breakout_runtime.phase = BREAKOUT_PHASE_LEVEL_CLEAR;
    g_breakout_runtime.level_index = 0;
    viewer_breakout_commit_scene(&ctx.bundle, &g_breakout_runtime);

    ID level_clear_scene = RETAIN(atom_peek(ctx.bundle.game_scene_atom));
    TEST_ASSERT_NOT_NULL(level_clear_scene);

    memset(keys, 0, sizeof(keys));
    keys[KB_KEY_SPACE] = 1;
    viewer_breakout_runtime_step(&g_breakout_runtime, &ctx.bundle, &runtime_flags, keys);
    viewer_sync_configured_slots(&ctx.bundle, &ctx.spatial_rules, false);

    ID next_scene = atom_peek(ctx.bundle.game_scene_atom);
    TEST_ASSERT_NOT_NULL(next_scene);
    TEST_ASSERT_NOT_EQUAL(level_clear_scene, next_scene);
    TEST_ASSERT_EQUAL_INT(BREAKOUT_PHASE_SERVE, g_breakout_runtime.phase);
    TEST_ASSERT_EQUAL_INT(1, g_breakout_runtime.level_index);

    FrameScene *serve_scene = (FrameScene *)next_scene;
    TEST_ASSERT_NOT_NULL(serve_scene->root);
    Group *root_group = (Group *)serve_scene->root;
    TEST_ASSERT_NOT_NULL(root_group->children);
    TEST_ASSERT_EQUAL_UINT32(5u + g_breakout_runtime.active_brick_count,
                             vector_count((CljPersistentVector *)root_group->children));

    RELEASE(level_clear_scene);
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

TEST(test_breakout_runtime_play_step_updates_scene_in_place) {
    BreakoutRuntimeTestContext ctx = {0};
    ViewerRuntimeFlags runtime_flags = {0};
    uint8_t keys[KB_KEY_LAST + 1] = {0};

    TEST_ASSERT_TRUE(breakout_runtime_test_context_init(&ctx));
    breakout_runtime_test_enter_play(&ctx, &runtime_flags, keys);

    ID scene_obj = RETAIN(atom_peek(ctx.bundle.game_scene_atom));
    TEST_ASSERT_NOT_NULL(scene_obj);
    FrameScene *scene = (FrameScene *)scene_obj;
    TEST_ASSERT_NOT_NULL(scene->root);
    TEST_ASSERT_TRUE(is_map(scene->index));
    Group *root_group = (Group *)scene->root;
    TEST_ASSERT_NOT_NULL(root_group->children);

    ID root_before = scene->root;
    ID children_before = root_group->children;
    Rect *paddle_before = (Rect *)map_get_sentinel(scene->index, fixnum(1002), NULL);
    Rect *ball_before = (Rect *)map_get_sentinel(scene->index, fixnum(1003), NULL);
    int32_t previous_ball_x = AS_FIXNUM(ball_before->x);
    int32_t previous_ball_y = AS_FIXNUM(ball_before->y);

    memset(keys, 0, sizeof(keys));
    viewer_breakout_runtime_step(&g_breakout_runtime, &ctx.bundle, &runtime_flags, keys);

    ID updated_scene_obj = atom_peek(ctx.bundle.game_scene_atom);
    TEST_ASSERT_EQUAL_PTR(scene_obj, updated_scene_obj);

    FrameScene *updated_scene = (FrameScene *)updated_scene_obj;
    TEST_ASSERT_EQUAL_PTR(root_before, updated_scene->root);
    TEST_ASSERT_EQUAL_PTR(children_before, ((Group *)updated_scene->root)->children);
    TEST_ASSERT_EQUAL_PTR(scene->index, updated_scene->index);
    TEST_ASSERT_EQUAL_PTR(updated_scene, ctx.bundle.game_scene);
    TEST_ASSERT_EQUAL_PTR(updated_scene, ctx.bundle.slots[ctx.bundle.game_slot_index].scene);

    Rect *updated_paddle = (Rect *)map_get_sentinel(updated_scene->index, fixnum(1002), NULL);
    Rect *updated_ball = (Rect *)map_get_sentinel(updated_scene->index, fixnum(1003), NULL);
    TEST_ASSERT_EQUAL_PTR(paddle_before, updated_paddle);
    TEST_ASSERT_EQUAL_PTR(ball_before, updated_ball);
    TEST_ASSERT_EQUAL_INT(g_breakout_runtime.paddle_x, AS_FIXNUM(updated_paddle->x));
    TEST_ASSERT_EQUAL_INT(g_breakout_runtime.ball_x, AS_FIXNUM(updated_ball->x));
    TEST_ASSERT_EQUAL_INT(g_breakout_runtime.ball_y, AS_FIXNUM(updated_ball->y));
    TEST_ASSERT_TRUE(previous_ball_x != AS_FIXNUM(updated_ball->x) ||
                     previous_ball_y != AS_FIXNUM(updated_ball->y));

    RELEASE(scene_obj);

    breakout_runtime_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_play_step_in_place_update_publishes_slot_change) {
    BreakoutRuntimeTestContext ctx = {0};
    ViewerRuntimeFlags runtime_flags = {0};
    uint8_t keys[KB_KEY_LAST + 1] = {0};
    uint32_t seen[VIEWER_MAX_SLOTS] = {0};
    uint32_t current[VIEWER_MAX_SLOTS] = {0};
    uint32_t changed_mask = 0u;

    TEST_ASSERT_TRUE(breakout_runtime_test_context_init(&ctx));
    TEST_ASSERT_TRUE(viewer_init_slot_runtime_buffers(&ctx.bundle));
    TEST_ASSERT_TRUE(vg_slot_change_tracker_init(&g_slot_change_tracker, ctx.bundle.slot_count));

    for (uint8_t i = 0; i < ctx.bundle.slot_count; i++) {
        publish_frame_scene_slot_record(i, (ID)ctx.bundle.slots[i].scene, NULL);
    }
    (void)vg_slot_change_tracker_wait_for_changes(&g_slot_change_tracker, seen, current, 0u);

    breakout_runtime_test_enter_play(&ctx, &runtime_flags, keys);
    memset(keys, 0, sizeof(keys));
    viewer_breakout_runtime_step(&g_breakout_runtime, &ctx.bundle, &runtime_flags, keys);

    changed_mask = vg_slot_change_tracker_wait_for_changes(&g_slot_change_tracker, seen, current, 0u);
    TEST_ASSERT_NOT_EQUAL_UINT32(0u, changed_mask & (1u << ctx.bundle.game_slot_index));

    vg_slot_change_tracker_destroy(&g_slot_change_tracker);
    viewer_destroy_slot_runtime_buffers();
    breakout_runtime_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_play_loop_does_not_accumulate_unbounded_heap_growth) {
    BreakoutRuntimeTestContext ctx = {0};
    ViewerRuntimeFlags runtime_flags = {0};
    uint8_t keys[KB_KEY_LAST + 1] = {0};
    size_t growth = 0;
    const size_t allowed_growth = 16384u;
    char msg[160];

    TEST_ASSERT_TRUE(breakout_runtime_test_context_init(&ctx));
    breakout_runtime_test_enter_play(&ctx, &runtime_flags, keys);
    growth = breakout_runtime_test_measure_play_loop_heap_growth(&ctx, &runtime_flags, keys, 600, true);

    test_snprintf(msg, sizeof(msg),
                  "native breakout play loop growth=%lu limit=%lu",
                  (unsigned long)growth,
                  (unsigned long)allowed_growth);
    TEST_ASSERT_TRUE_MESSAGE(growth <= allowed_growth, msg);

    breakout_runtime_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_play_loop_publish_only_does_not_accumulate_unbounded_heap_growth) {
    BreakoutRuntimeTestContext ctx = {0};
    ViewerRuntimeFlags runtime_flags = {0};
    uint8_t keys[KB_KEY_LAST + 1] = {0};
    size_t growth = 0;
    const size_t allowed_growth = 16384u;
    char msg[160];

    TEST_ASSERT_TRUE(breakout_runtime_test_context_init(&ctx));
    breakout_runtime_test_enter_play(&ctx, &runtime_flags, keys);
    growth = breakout_runtime_test_measure_play_loop_heap_growth(&ctx, &runtime_flags, keys, 600, false);

    test_snprintf(msg, sizeof(msg),
                  "native breakout publish-only growth=%lu limit=%lu",
                  (unsigned long)growth,
                  (unsigned long)allowed_growth);
    TEST_ASSERT_TRUE_MESSAGE(growth <= allowed_growth, msg);

    breakout_runtime_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_play_loop_with_slot_render_does_not_crash) {
    BreakoutRuntimeTestContext ctx = {0};
    ViewerRuntimeFlags runtime_flags = {0};
    uint8_t keys[KB_KEY_LAST + 1] = {0};
    size_t growth = 0;
    const size_t allowed_growth = 16384u;
    char msg[160];

    TEST_ASSERT_TRUE(breakout_runtime_test_context_init(&ctx));
    breakout_runtime_test_enter_play(&ctx, &runtime_flags, keys);

    growth = breakout_runtime_test_run_play_loop(&ctx,
                                                 &runtime_flags,
                                                 keys,
                                                 2400,
                                                 true,
                                                 true,
                                                 true);
    test_snprintf(msg, sizeof(msg),
                  "native breakout render loop growth=%lu limit=%lu",
                  (unsigned long)growth,
                  (unsigned long)allowed_growth);
    TEST_ASSERT_TRUE_MESSAGE(growth <= allowed_growth, msg);

    breakout_runtime_test_context_destroy(&ctx);
}
