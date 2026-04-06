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

void macos_fx_install_menu(void) {}
void macos_fx_set_window_title(const char *title) { (void)title; }
void macos_fx_register_window_callbacks(void) {}
void macos_fx_restore_window_position(void) {}
void macos_fx_save_window_position(void) {}
void macos_fx_activate_app_window(void) {}
bool macos_fx_get_content_size(unsigned *out_w, unsigned *out_h) {
    (void)out_w;
    (void)out_h;
    return false;
}
void macos_fx_begin_performance_activity(void) {}
void macos_fx_end_performance_activity(void) {}
void macos_fx_start_runloop_watchdog(void) { g_macos_runloop_watchdog_start_calls++; }
void macos_fx_stop_runloop_watchdog(void) { g_macos_runloop_watchdog_stop_calls++; }
#endif
#include "../fx_spatial_bridge.h"
#define main tinyclj_fx_host_app_test_main
#include "../fx_host_app.c"
#undef main
#include "../builtins.h"
#include "../meta.h"
#include "../strings.h"
#include "../to_string.h"
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

static void breakout_init_single_rule_set(ViewerSpatialRuleSet *rule_set,
                                          ViewerCollisionPolicy *policy_storage,
                                          VgCollisionState *state_storage,
                                          const ViewerCollisionPolicy *policy) {
    TEST_ASSERT_NOT_NULL(rule_set);
    TEST_ASSERT_NOT_NULL(policy_storage);
    TEST_ASSERT_NOT_NULL(state_storage);
    TEST_ASSERT_NOT_NULL(policy);

    memset(policy_storage, 0, sizeof(*policy_storage));
    *policy_storage = *policy;
    memset(state_storage, 0, sizeof(*state_storage));
    memset(rule_set, 0, sizeof(*rule_set));
    rule_set->items = policy_storage;
    rule_set->states = state_storage;
    rule_set->count = 1u;
    rule_set->capacity = 1u;
}

typedef bool (*BreakoutStdoutCaptureAction)(void *ctx);

static char *breakout_capture_stdout(BreakoutStdoutCaptureAction action, void *ctx) {
    if (!action) {
        return NULL;
    }
    FILE *tmp = tmpfile();
    if (!tmp) {
        return NULL;
    }
    int stdout_fd = fileno(stdout);
    int saved_stdout = dup(stdout_fd);
    if (saved_stdout < 0) {
        fclose(tmp);
        return NULL;
    }
    fflush(stdout);
    if (dup2(fileno(tmp), stdout_fd) < 0) {
        close(saved_stdout);
        fclose(tmp);
        return NULL;
    }

    (void)action(ctx);

    fflush(stdout);
    (void)dup2(saved_stdout, stdout_fd);
    close(saved_stdout);

    if (fseek(tmp, 0, SEEK_END) != 0) {
        fclose(tmp);
        return NULL;
    }
    long size = ftell(tmp);
    if (size < 0) {
        fclose(tmp);
        return NULL;
    }
    if (fseek(tmp, 0, SEEK_SET) != 0) {
        fclose(tmp);
        return NULL;
    }
    char *buf = (char *)CLJ_MALLOC((size_t)size + 1u);
    if (!buf) {
        fclose(tmp);
        return NULL;
    }
    size_t read_n = fread(buf, 1u, (size_t)size, tmp);
    buf[read_n] = '\0';
    fclose(tmp);
    return buf;
}

typedef struct {
    EvalState *st;
} BreakoutRunloopDrainCaptureCtx;

static bool breakout_capture_drain_one_runloop_task(void *ctx) {
    BreakoutRunloopDrainCaptureCtx *capture_ctx = (BreakoutRunloopDrainCaptureCtx *)ctx;
    if (!capture_ctx) {
        return false;
    }
    return fx_drain_one_runloop_task(capture_ctx->st);
}

static inline void breakout_apply_incremental_heap_budget(size_t budget_bytes) {
    memory_set_heap_limit_bytes(memory_current_usage_bytes() + budget_bytes);
}

static bool breakout_fx_test_context_init_with_heap_budget(BreakoutViewerTestContext *ctx,
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
    fx_seed_gpio_key_levels();
    if (apply_host_heap_budget) {
        breakout_apply_incremental_heap_budget(tiny_fx_host_heap_limit_bytes());
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
    if (!fx_load_deployment_config(ctx->st, config_source, &ctx->bundle, &ctx->spatial_rules)) {
        return false;
    }
    return true;
}

static bool breakout_fx_test_context_init_with_heap_limit(BreakoutViewerTestContext *ctx,
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
    fx_seed_gpio_key_levels();
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
    if (!fx_load_deployment_config(ctx->st, config_source, &ctx->bundle, &ctx->spatial_rules)) {
        return false;
    }
    return true;
}

static bool breakout_fx_test_context_init(BreakoutViewerTestContext *ctx) {
    return breakout_fx_test_context_init_with_heap_budget(ctx, false);
}

static void breakout_fx_test_context_destroy(BreakoutViewerTestContext *ctx) {
    if (!ctx) {
        return;
    }
    stop_runloop_thread();
    event_loop_clear();
    destroy_scene_bundle(&ctx->bundle);
    destroy_spatial_rule_set(&ctx->spatial_rules);
    evalstate_free(ctx->st);
    memset(ctx, 0, sizeof(*ctx));
}

TEST(test_breakout_runtime_startup_host_app_fits_debug_heap_limit) {
    BreakoutViewerTestContext ctx = {0};
    size_t previous_limit = memory_get_heap_limit_bytes();
    size_t host_limit = tiny_fx_host_heap_limit_bytes();
    size_t baseline_usage = memory_current_usage_bytes();
    bool init_ok = false;
    bool caught = false;
    ID caught_ex = NULL;

    TEST_ASSERT_EQUAL_UINT64(614400u, host_limit);
    TRY {
        init_ok = breakout_fx_test_context_init_with_heap_limit(&ctx, baseline_usage + host_limit);
    } CATCH(ex) {
        caught = true;
        caught_ex = ex;
    } END_TRY
    memory_set_heap_limit_bytes(previous_limit);

    if (init_ok) {
        breakout_fx_test_context_destroy(&ctx);
    }
    TEST_ASSERT_FALSE_MESSAGE(caught, caught_ex ? "breakout host startup should not OOM within 640KB incremental heap headroom" : "");
    TEST_ASSERT_TRUE_MESSAGE(init_ok, "breakout host startup should fit inside the tiny-fx debug incremental heap limit");
}

TEST(test_breakout_runtime_startup_host_app_fits_400k_startup_budget) {
    BreakoutViewerTestContext ctx = {0};
    size_t previous_limit = memory_get_heap_limit_bytes();
    size_t baseline_usage = memory_current_usage_bytes();
    const size_t startup_budget = 400u * 1024u;
    const size_t startup_required_headroom = 2u * 1024u;
    const size_t startup_limit = startup_budget + (128u * 1024u);
    bool init_ok = false;
    bool caught = false;
    ID caught_ex = NULL;
    MemoryStats stats = {0};

    TRY {
        init_ok = breakout_fx_test_context_init_with_heap_limit(&ctx, baseline_usage + startup_limit);
    } CATCH(ex) {
        caught = true;
        caught_ex = ex;
    } END_TRY
    stats = memory_profiler_get_stats();
    memory_set_heap_limit_bytes(previous_limit);

    if (init_ok) {
        breakout_fx_test_context_destroy(&ctx);
    }
    TEST_ASSERT_FALSE_MESSAGE(caught, caught_ex ? "breakout host startup should not OOM under a 400KB incremental startup budget with 2KB reserved headroom" : "");
    TEST_ASSERT_TRUE_MESSAGE(init_ok, "breakout host startup should fit inside a 400KB incremental startup budget while preserving 2KB free");
    TEST_ASSERT_TRUE_MESSAGE(stats.current_memory_usage >= baseline_usage,
                             "current heap usage should not drop below startup baseline");
    TEST_ASSERT_TRUE_MESSAGE((stats.current_memory_usage - baseline_usage) + startup_required_headroom <= startup_budget,
                             "startup should retain at least 2KB free incremental heap headroom inside the 400KB budget");
}

TEST(test_breakout_runtime_startup_first_launch_fits_debug_heap_limit) {
    BreakoutViewerTestContext ctx = {0};
    size_t previous_limit = memory_get_heap_limit_bytes();
    bool init_ok = false;
    bool caught = false;
    ID caught_ex = NULL;
    ID launched = NULL;

    TRY {
        init_ok = breakout_fx_test_context_init_with_heap_budget(&ctx, true);
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
        breakout_fx_test_context_destroy(&ctx);
    }

    TEST_ASSERT_FALSE_MESSAGE(caught, caught_ex ? "first breakout launch should not OOM under 640KB incremental heap headroom" : "");
    TEST_ASSERT_TRUE_MESSAGE(init_ok, "breakout host startup should initialize before first-launch heap assertion");
    TEST_ASSERT_EQUAL_PTR(clj_true, launched);
}

TEST(test_breakout_runtime_startup_runtime_init_reuses_existing_builtins_under_tight_headroom) {
    size_t previous_limit = memory_get_heap_limit_bytes();
    bool caught = false;
    ID caught_ex = NULL;

    runtime_reset(&g_runtime);
    WITH_AUTORELEASE_POOL({
        runtime_init(&g_runtime);
    });
    event_loop_init();
    meta_registry_init();
    register_builtins();
    g_runtime.builtins_registered = true;

    WITH_AUTORELEASE_POOL({
        runtime_init(&g_runtime);
    });

    size_t baseline_usage = memory_current_usage_bytes();

    TRY {
        memory_set_heap_limit_bytes(baseline_usage + 4096u);
        evalstate_ensure_builtins_ready();
    } CATCH(ex) {
        caught = true;
        caught_ex = ex;
    } END_TRY

    memory_set_heap_limit_bytes(previous_limit);

    TEST_ASSERT_FALSE_MESSAGE(caught,
                              caught_ex ? "runtime_init should not force builtin re-registration when bootstrap mappings already exist" : "");
    TEST_ASSERT_TRUE_MESSAGE(g_runtime.builtins_registered,
                             "runtime_init should preserve builtin-ready state when bootstrap mappings are already present");
}

TEST(test_breakout_runtime_startup_first_launch_heap_profile_stays_bounded) {
    BreakoutViewerTestContext ctx = {0};
    size_t previous_limit = memory_get_heap_limit_bytes();
    ID stats = NULL;
    ID k_total = NULL;
    ID k_peak = NULL;
    ID total = NULL;
    ID peak = NULL;

    TEST_ASSERT_TRUE_MESSAGE(breakout_fx_test_context_init_with_heap_budget(&ctx, true),
                             "breakout first-launch heap profile should initialize inside the host incremental heap budget");

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

    TEST_ASSERT_TRUE_MESSAGE(as_fixnum(peak) < 128 * 1024,
                             "direct first-launch apply-input path should stay below 128KB local peak");

    memory_set_heap_limit_bytes(previous_limit);
    breakout_fx_test_context_destroy(&ctx);
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
        init_ok = breakout_fx_test_context_init(&ctx);
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
        breakout_fx_test_context_destroy(&ctx);
    }

    TEST_ASSERT_FALSE_MESSAGE(caught, caught_ex ? "breakout host startup should tolerate host audio init failures" : "");
    TEST_ASSERT_TRUE_MESSAGE(init_ok, "breakout host startup should still load when host audio init fails");
}

TEST(test_breakout_runtime_startup_does_not_autoload_sound_demos_namespace) {
    BreakoutViewerTestContext ctx = {0};

    TEST_ASSERT_TRUE(breakout_fx_test_context_init(&ctx));
    TEST_ASSERT_NULL_MESSAGE(ns_find("tiny-fx.sound-demos"),
                             "loading breakout runtime config must not autoload tiny-fx.sound-demos");

    ID ok = eval_string(
        "(do "
        "  (tiny-breakout.runtime/start-runtime! nil) "
        "  true)",
        ctx.st);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
    TEST_ASSERT_NULL_MESSAGE(ns_find("tiny-fx.sound-demos"),
                             "start-runtime! must use tiny-breakout.audio directly, not tiny-fx.sound-demos");

    breakout_fx_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_defaults_host_demo_selection_to_breakout) {
    const char *env_name = "TINYCLJ_HOST_DEMO";
    const char *saved_env = getenv(env_name);
    char *saved_copy = saved_env ? strdup(saved_env) : NULL;

    unsetenv(env_name);
    ViewerConfigSource config_source = fx_default_config_source();

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
    SubjectiveCThread *first_thread = NULL;

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
    TEST_ASSERT_EQUAL_PTR(first_thread, g_runloop_thread.thread);
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
    fx_runloop_liveness_reset();

    ViewerRunloopLivenessSnapshot snapshot = fx_runloop_liveness_snapshot(9ull * 1000ull * 1000ull * 1000ull);

    TEST_ASSERT_EQUAL_UINT64(0u, snapshot.last_tick_ns);
    TEST_ASSERT_EQUAL_UINT64(0u, snapshot.iteration_count);
    TEST_ASSERT_EQUAL_UINT64(0u, snapshot.age_ns);
    TEST_ASSERT_EQUAL_INT(FX_RUNLOOP_LIVENESS_HEALTHY, snapshot.state);
}

TEST(test_breakout_runtime_startup_runloop_liveness_snapshot_tracks_progress) {
    fx_runloop_liveness_reset();
    fx_runloop_liveness_note_progress_for_tests(2ull * 1000ull * 1000ull * 1000ull);
    fx_runloop_liveness_note_progress_for_tests(3ull * 1000ull * 1000ull * 1000ull);

    ViewerRunloopLivenessSnapshot snapshot = fx_runloop_liveness_snapshot(4ull * 1000ull * 1000ull * 1000ull);

    TEST_ASSERT_EQUAL_UINT64(3ull * 1000ull * 1000ull * 1000ull, snapshot.last_tick_ns);
    TEST_ASSERT_EQUAL_UINT64(2u, snapshot.iteration_count);
    TEST_ASSERT_EQUAL_UINT64(1ull * 1000ull * 1000ull * 1000ull, snapshot.age_ns);
    TEST_ASSERT_EQUAL_INT(FX_RUNLOOP_LIVENESS_HEALTHY, snapshot.state);
}

TEST(test_breakout_runtime_startup_runloop_liveness_snapshot_flags_stalls_after_threshold) {
    fx_runloop_liveness_reset();
    fx_runloop_liveness_note_progress_for_tests(1ull * 1000ull * 1000ull * 1000ull);

    ViewerRunloopLivenessSnapshot snapshot = fx_runloop_liveness_snapshot(7ull * 1000ull * 1000ull * 1000ull);

    TEST_ASSERT_EQUAL_UINT64(6ull * 1000ull * 1000ull * 1000ull, snapshot.age_ns);
    TEST_ASSERT_EQUAL_INT(FX_RUNLOOP_LIVENESS_STALLED, snapshot.state);
}

TEST(test_breakout_runtime_startup_runloop_warns_to_stdout_when_event_exceeds_20ms) {
    EvalState *st = NULL;

    runtime_init(&g_runtime);
    event_loop_init();
    event_loop_clear();

    st = evalstate_new(true);
    TEST_ASSERT_NOT_NULL(st);
    evalstate_set_ns(st, "user");

    ID fn = eval_string("(fn [event] (sleep 25) nil)", st);
    ID payload = eval_string("{:id :slow-runloop-event}", st);
    TEST_ASSERT_NOT_NULL(fn);
    TEST_ASSERT_NOT_NULL(payload);
    TEST_ASSERT_TRUE(event_loop_enqueue_ingress_call((CljObject *)fn, payload));

    BreakoutRunloopDrainCaptureCtx ctx = {.st = st};
    char *stdout_output = breakout_capture_stdout(breakout_capture_drain_one_runloop_task, &ctx);
    TEST_ASSERT_NOT_NULL_MESSAGE(stdout_output, "failed to capture stdout for slow runloop warning");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(stdout_output, "[runloop] warning: clojure runloop event took"),
                                 "expected slow runloop warning on stdout");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(stdout_output, "threshold: 20ms"),
                                 "expected warning to mention 20ms threshold");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(stdout_output, "payload="),
                                 "expected warning to include payload context");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(stdout_output, ":slow-runloop-event"),
                                 "expected warning to include event payload details");
    CLJ_FREE(stdout_output);
    evalstate_free(st);
}

TEST(test_breakout_runtime_startup_host_keys_drive_gpio_levels_for_fire_and_y) {
    ViewerRuntimeFlags flags = {0};
    uint8_t keys[KB_KEY_LAST + 1] = {0};

    gpio_runtime_reset_state();
    fx_seed_gpio_key_levels();

    ID fire_idle = gpio_runtime_read_digital_level(13);
    ID y_idle = gpio_runtime_read_digital_level(15);
    TEST_ASSERT_TRUE(is_fixnum(fire_idle));
    TEST_ASSERT_TRUE(is_fixnum(y_idle));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(fire_idle));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(y_idle));

    keys[KB_KEY_SPACE] = 1u;
    fx_simulate_gpio_keys(keys, &flags);
    ID fire_down = gpio_runtime_read_digital_level(13);
    TEST_ASSERT_TRUE(is_fixnum(fire_down));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(fire_down));

    keys[KB_KEY_SPACE] = 0u;
    keys[KB_KEY_Y] = 1u;
    fx_simulate_gpio_keys(keys, &flags);
    ID fire_up = gpio_runtime_read_digital_level(13);
    ID y_down = gpio_runtime_read_digital_level(15);
    TEST_ASSERT_TRUE(is_fixnum(fire_up));
    TEST_ASSERT_TRUE(is_fixnum(y_down));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(fire_up));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(y_down));

    keys[KB_KEY_Y] = 0u;
    fx_simulate_gpio_keys(keys, &flags);
    ID y_up = gpio_runtime_read_digital_level(15);
    TEST_ASSERT_TRUE(is_fixnum(y_up));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(y_up));
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
        fx_host_window_open_with_backend(breakout_test_open_host_window, "tiny-fx", 640u, 480u);

    TEST_ASSERT_NOT_NULL(window);
    TEST_ASSERT_EQUAL_INT(1, g_macos_runloop_watchdog_start_calls);
    TEST_ASSERT_EQUAL_INT(0, g_macos_runloop_watchdog_stop_calls);
}

TEST(test_breakout_runtime_startup_host_window_close_stops_macos_runloop_watchdog) {
    breakout_reset_macos_watchdog_counters();

    fx_host_window_close_with_backend((ViewerHostWindow *)(uintptr_t)0x1, breakout_test_close_host_window);

    TEST_ASSERT_EQUAL_INT(0, g_macos_runloop_watchdog_start_calls);
    TEST_ASSERT_EQUAL_INT(1, g_macos_runloop_watchdog_stop_calls);
}
#endif

TEST(test_breakout_runtime_startup_loads_breakout_host_config_into_generic_fx_bundle) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_fx_test_context_init(&ctx));

    TEST_ASSERT_TRUE(ctx.bundle.has_primary_slot);
    TEST_ASSERT_NOT_NULL(ctx.bundle.startup_callback);
    TEST_ASSERT_NOT_NULL(ctx.bundle.primary_scene_atom);
    TEST_ASSERT_NOT_NULL(ctx.bundle.primary_scene);
    TEST_ASSERT_NOT_NULL(ctx.bundle.spatial_callback);
    TEST_ASSERT_TRUE(ctx.bundle.primary_slot_index < ctx.bundle.slot_count);
    TEST_ASSERT_EQUAL_PTR(ctx.bundle.primary_scene, ctx.bundle.slots[ctx.bundle.primary_slot_index].scene);
    TEST_ASSERT_TRUE(ctx.spatial_rules.count >= 1u);

    breakout_fx_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_retain_slot_scenes_independently_of_scene_atoms) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_fx_test_context_init(&ctx));

    TEST_ASSERT_TRUE(ctx.bundle.has_primary_slot);
    TEST_ASSERT_NOT_NULL(ctx.bundle.primary_scene);
    TEST_ASSERT_TRUE(((CljObject *)ctx.bundle.primary_scene)->rc > 1);

    breakout_fx_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_spatial_events_include_entity_snapshots) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_fx_test_context_init(&ctx));
    TEST_ASSERT_TRUE(ctx.spatial_rules.count > 0u);

    FrameScene *scene = fx_frame_scene_from_atom(ctx.bundle.primary_scene_atom);
    TEST_ASSERT_NOT_NULL(scene);

    ViewerCollisionPolicy *policy = &ctx.spatial_rules.items[0];
    ID expected_self = map_get_sentinel(fx_collision_scene_entity_map(scene), policy->self_entity_id, NULL);
    ID expected_other = map_get_sentinel(fx_collision_scene_entity_map(scene), policy->other_entity_id, NULL);
    TEST_ASSERT_NOT_NULL(expected_self);
    TEST_ASSERT_NOT_NULL(expected_other);

    VgAabb self_box = {.min_x = 1, .min_y = 2, .max_x = 3, .max_y = 4};
    VgAabb other_box = {.min_x = 5, .min_y = 6, .max_x = 7, .max_y = 8};
    ID event = fx_collision_make_spatial_event(&ctx.bundle,
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
    breakout_fx_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_fire_button_seeded_inactive_before_breakout_watchers) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_fx_test_context_init(&ctx));

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

    breakout_fx_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_restarts_title_overlay_fade_on_runtime_start) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_fx_test_context_init(&ctx));

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

    breakout_fx_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_bootstrap_title_overlay_starts_fade_immediately) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_fx_test_context_init(&ctx));

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

    breakout_fx_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_level_clear_fire_press_advances_once_to_serve) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_fx_test_context_init(&ctx));

    ID state = eval_string(
        "(do "
        "  (require 'tiny-clj.gpio) "
        "  (require 'tiny-breakout.runtime) "
        "  (tiny-breakout.runtime/start-runtime! nil) "
        "  (tiny-breakout.runtime/publish-state! "
        "    (assoc @tiny-breakout.runtime/state* "
        "      :phase :level-clear "
        "      :level-no 0 "
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
    ID k_bricks = intern_symbol_global(":bricks");
    TEST_ASSERT_NOT_NULL(k_phase);
    TEST_ASSERT_NOT_NULL(k_bricks);

    ID phase = map_get_sentinel(state, k_phase, NULL);
    ID bricks = map_get_sentinel(state, k_bricks, NULL);

    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":serve"), phase);
    TEST_ASSERT_TRUE(is_map(bricks));
    TEST_ASSERT_TRUE(map_count(bricks) > 0);

    breakout_fx_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_level_clear_direct_launch_ingress_advances_to_serve) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_fx_test_context_init(&ctx));

    ID resolved = eval_string(
        "(do "
        "  (require 'tiny-breakout.runtime) "
        "  [tiny-breakout.runtime/apply-input! {:launch true}])",
        ctx.st);
    TEST_ASSERT_NOT_NULL(resolved);
    TEST_ASSERT_TRUE(TAG(resolved) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *resolved_vec = as_vector(resolved);
    TEST_ASSERT_NOT_NULL(resolved_vec);
    TEST_ASSERT_EQUAL_UINT(2u, vector_count(resolved_vec));

    ID input_fn = vector_nth(resolved_vec, 0u);
    ID launch_arg = vector_nth(resolved_vec, 1u);
    TEST_ASSERT_NOT_NULL(input_fn);
    TEST_ASSERT_NOT_NULL(launch_arg);

    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.runtime) "
        "  (tiny-breakout.runtime/start-runtime! nil) "
        "  (tiny-breakout.runtime/publish-state! "
        "    (assoc @tiny-breakout.runtime/state* "
        "      :phase :level-clear "
        "      :level-no 0 "
        "      :ball-segment nil)) "
        "  true)",
        ctx.st);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);

    TEST_ASSERT_TRUE(start_runloop_thread(ctx.st));
    TEST_ASSERT_TRUE(event_loop_enqueue_ingress_call((CljObject *)input_fn, launch_arg));

    bool advanced = false;
    for (int i = 0; i < 120; i++) {
        usleep(10000);
        ID state = eval_string("@tiny-breakout.runtime/state*", ctx.st);
        if (state && is_map(state)) {
            ID phase = map_get_sentinel(state, intern_symbol_global(":phase"), NULL);
            ID level_index = map_get_sentinel(state, intern_symbol_global(":level-no"), NULL);
            if (phase == intern_symbol_global(":serve") &&
                level_index && is_fixnum(level_index) && as_fixnum(level_index) == 1) {
                advanced = true;
                break;
            }
        }
    }

    stop_runloop_thread();
    breakout_fx_test_context_destroy(&ctx);

    TEST_ASSERT_TRUE_MESSAGE(advanced,
                             "direct host launch ingress should advance level-clear to the next serve state");
}

TEST(test_breakout_runtime_startup_advancing_to_third_level_reloads_spatial_rules) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_fx_test_context_init(&ctx));

    ID counts = eval_string(
        "(do "
        "  (require 'tiny-breakout.runtime) "
        "  (tiny-breakout.runtime/start-runtime! nil) "
        "  (tiny-breakout.runtime/publish-state! "
        "    (assoc @tiny-breakout.runtime/state* "
        "      :phase :level-clear "
        "      :level-no 0 "
        "      :ball-segment nil)) "
        "  (tiny-breakout.runtime/apply-input! {:launch true}) "
        "  (tiny-breakout.runtime/publish-state! "
        "    (assoc @tiny-breakout.runtime/state* "
        "      :phase :level-clear "
        "      :ball-segment nil)) "
        "  (tiny-breakout.runtime/apply-input! {:launch true}) "
        "  [(count (:collision-rules @tiny-breakout.runtime/state*)) "
        "   (:level-no @tiny-breakout.runtime/state*)])",
        ctx.st);
    TEST_ASSERT_NOT_NULL(counts);
    TEST_ASSERT_TRUE(TAG(counts) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *counts_vec = as_vector(counts);
    TEST_ASSERT_NOT_NULL(counts_vec);
    TEST_ASSERT_EQUAL_UINT(2u, vector_count(counts_vec));
    ID rule_count = vector_nth(counts_vec, 0u);
    ID level_index = vector_nth(counts_vec, 1u);
    TEST_ASSERT_TRUE(is_fixnum(rule_count));
    TEST_ASSERT_TRUE(is_fixnum(level_index));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(level_index));

    fx_sync_configured_slots(&ctx.bundle, &ctx.spatial_rules, NULL, false);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)AS_FIXNUM(rule_count), ctx.spatial_rules.count);

    breakout_fx_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_last_brick_level_clear_then_direct_launch_advances) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_fx_test_context_init(&ctx));
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.runtime) "
        "  (require 'tiny-clj.event) "
        "  (tiny-breakout.runtime/bootstrap-runtime!) "
        "  (tiny-breakout.runtime/start-runtime! nil) "
        "  (tiny-breakout.runtime/apply-input! {:launch true}) "
        "  (let [bricks (:bricks @tiny-breakout.runtime/state*) "
        "        b (first (if (map? bricks) (vals bricks) bricks)) "
        "        bx (:x b) "
        "        by (:y b) "
        "        now-ms (current-time-ms) "
        "        seeded (-> @tiny-breakout.runtime/state* "
        "                   (assoc :phase :play) "
        "                   (assoc :bricks {(:id b) b}) "
        "                   (assoc :ball-x bx) "
        "                   (assoc :ball-y by) "
        "                   (assoc :ball-vx 2) "
        "                   (assoc :ball-vy 2) "
        "                   (assoc :ball-segment "
        "                          {:id 901 "
        "                           :start-ms (- now-ms 16) "
        "                           :end-ms now-ms "
        "                           :from-x bx "
        "                           :from-y by "
        "                           :to-x bx "
        "                           :to-y by "
        "                           :collision {:hit-id (:id b) :normal :top}})) "
        "        _ (tiny-breakout.runtime/publish-state! seeded) "
        "        _ (tiny-clj.event/dispatch-timeline-progress! "
        "            {:event-id :tiny-breakout/segment-end "
        "             :end-event true "
        "             :at-end true "
        "             :phase-ms 1 "
        "             :period-ms 1}) "
        "        _ (dotimes [_ 8] (run-next-task)) "
        "        s1 @tiny-breakout.runtime/state* "
        "        _ (tiny-breakout.runtime/apply-input! {:launch true}) "
        "        s2 @tiny-breakout.runtime/state*] "
        "    (and (= :level-clear (:phase s1)) "
        "         (= :serve (:phase s2))))))",
        ctx.st);
    breakout_fx_test_context_destroy(&ctx);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_runtime_startup_fire_button_heap_profile_stays_bounded) {
    BreakoutViewerTestContext ctx = {0};
    size_t previous_limit = memory_get_heap_limit_bytes();
    TEST_ASSERT_TRUE(breakout_fx_test_context_init_with_heap_budget(&ctx, true));

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
    breakout_fx_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_spatial_callback_scene_replacement_reloads_rules_safely) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_fx_test_context_init(&ctx));
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

    TEST_ASSERT_TRUE(fx_collision_detect_step(&ctx.bundle, &ctx.spatial_rules, 0u, NULL, 0u));
    TEST_ASSERT_EQUAL_PTR(replacement_scene, atom_reset(ctx.bundle.primary_scene_atom, replacement_scene));

    fx_sync_configured_slots(&ctx.bundle, &ctx.spatial_rules, NULL, false);
    TEST_ASSERT_EQUAL_UINT32(0u, ctx.spatial_rules.count);

    breakout_fx_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_collision_latch_recovers_after_missing_snapshot_entities) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_fx_test_context_init(&ctx));
    TEST_ASSERT_TRUE(ctx.bundle.has_primary_slot);

    ID paddle_rule_id = intern_symbol_global(":ball-vs-paddle");
    ViewerCollisionPolicy *policy = breakout_find_policy_by_id(&ctx.spatial_rules, paddle_rule_id);
    TEST_ASSERT_NOT_NULL(policy);
    ViewerSpatialRuleSet single_rule_set = {0};
    ViewerCollisionPolicy single_policy = {0};
    VgCollisionState single_state = {0};
    breakout_init_single_rule_set(&single_rule_set, &single_policy, &single_state, policy);

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
    TEST_ASSERT_TRUE(fx_collision_detect_step(&ctx.bundle, &ctx.spatial_rules, 0u, NULL, 0u));

    vg_rendered_state_capture_begin(slot, 2u, 0u);
    vg_rendered_state_capture_commit();
    TEST_ASSERT_FALSE(fx_collision_detect_step(&ctx.bundle, &ctx.spatial_rules, 0u, NULL, 0u));

    vg_rendered_state_capture_begin(slot, 3u, 0u);
    vg_rendered_state_capture_record_entity((uintptr_t)policy->self_entity_id, world_t);
    vg_rendered_state_capture_record_entity_aabb((uintptr_t)policy->self_entity_id, ball_box);
    vg_rendered_state_capture_record_entity((uintptr_t)policy->other_entity_id, world_t);
    vg_rendered_state_capture_record_entity_aabb((uintptr_t)policy->other_entity_id, paddle_box);
    vg_rendered_state_capture_commit();
    TEST_ASSERT_TRUE(fx_collision_detect_step(&ctx.bundle, &ctx.spatial_rules, 0u, NULL, 0u));

    breakout_fx_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_collision_step_filters_candidates_by_dirty_rects) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_fx_test_context_init(&ctx));
    TEST_ASSERT_TRUE(ctx.bundle.has_primary_slot);

    ID paddle_rule_id = intern_symbol_global(":ball-vs-paddle");
    ViewerCollisionPolicy *policy = breakout_find_policy_by_id(&ctx.spatial_rules, paddle_rule_id);
    TEST_ASSERT_NOT_NULL(policy);
    ViewerSpatialRuleSet single_rule_set = {0};
    ViewerCollisionPolicy single_policy = {0};
    VgCollisionState single_state = {0};
    breakout_init_single_rule_set(&single_rule_set, &single_policy, &single_state, policy);

    uint8_t slot = ctx.bundle.primary_slot_index;
    VgTransformFixed world_t = {
        .m00 = VG_SCALE_ONE,
        .m01 = 0,
        .m02 = 0,
        .m10 = 0,
        .m11 = VG_SCALE_ONE,
        .m12 = 0,
    };
    VgAabb ball_box = {.min_x = 156, .max_x = 159, .min_y = 223, .max_y = 226};
    VgAabb paddle_box = {.min_x = 140, .max_x = 179, .min_y = 224, .max_y = 227};

    vg_rendered_state_reset_all();
    vg_rendered_state_capture_begin(slot, 1u, 0u);
    vg_rendered_state_capture_record_entity((uintptr_t)policy->self_entity_id, world_t);
    vg_rendered_state_capture_record_entity_aabb((uintptr_t)policy->self_entity_id, ball_box);
    vg_rendered_state_capture_record_entity((uintptr_t)policy->other_entity_id, world_t);
    vg_rendered_state_capture_record_entity_aabb((uintptr_t)policy->other_entity_id, paddle_box);
    vg_rendered_state_capture_commit();

    VgClipRect far_dirty = {.x = 0, .y = 0, .w = 16, .h = 16};
    TEST_ASSERT_FALSE(fx_collision_detect_step(&ctx.bundle,
                                                   &single_rule_set,
                                                   0u,
                                                   &far_dirty,
                                                   1u));

    VgClipRect hit_dirty = {.x = 150, .y = 216, .w = 40, .h = 20};
    TEST_ASSERT_TRUE(fx_collision_detect_step(&ctx.bundle,
                                                  &single_rule_set,
                                                  0u,
                                                  &hit_dirty,
                                                  1u));

    breakout_fx_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_collision_step_pushes_dispatch_into_ingress_immediately) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_fx_test_context_init(&ctx));
    TEST_ASSERT_TRUE(ctx.bundle.has_primary_slot);

    ID callback = eval_string(
        "(do "
        "  (def breakout-collision-drain-events (atom [])) "
        "  (fn [event] "
        "    (swap! breakout-collision-drain-events conj [(:phase event) (:id event)]) "
        "    nil))",
        ctx.st);
    TEST_ASSERT_NOT_NULL(callback);
    RELEASE(ctx.bundle.spatial_callback);
    ctx.bundle.spatial_callback = RETAIN(callback);
    event_loop_clear();

    ID paddle_rule_id = intern_symbol_global(":ball-vs-paddle");
    ViewerCollisionPolicy *policy = breakout_find_policy_by_id(&ctx.spatial_rules, paddle_rule_id);
    TEST_ASSERT_NOT_NULL(policy);
    ViewerSpatialRuleSet single_rule_set = {0};
    ViewerCollisionPolicy single_policy = {0};
    VgCollisionState single_state = {0};
    breakout_init_single_rule_set(&single_rule_set, &single_policy, &single_state, policy);

    uint8_t slot = ctx.bundle.primary_slot_index;
    VgTransformFixed world_t = {
        .m00 = VG_SCALE_ONE,
        .m01 = 0,
        .m02 = 0,
        .m10 = 0,
        .m11 = VG_SCALE_ONE,
        .m12 = 0,
    };
    VgAabb ball_box = {.min_x = 156, .max_x = 159, .min_y = 223, .max_y = 226};
    VgAabb paddle_box = {.min_x = 140, .max_x = 179, .min_y = 224, .max_y = 227};

    vg_rendered_state_reset_all();
    vg_rendered_state_capture_begin(slot, 1u, 0u);
    vg_rendered_state_capture_record_entity((uintptr_t)policy->self_entity_id, world_t);
    vg_rendered_state_capture_record_entity_aabb((uintptr_t)policy->self_entity_id, ball_box);
    vg_rendered_state_capture_record_entity((uintptr_t)policy->other_entity_id, world_t);
    vg_rendered_state_capture_record_entity_aabb((uintptr_t)policy->other_entity_id, paddle_box);
    vg_rendered_state_capture_commit();

    TEST_ASSERT_TRUE(fx_collision_detect_step(&ctx.bundle, &single_rule_set, 0u, NULL, 0u));
    TEST_ASSERT_TRUE(event_loop_has_pending_tasks());
    TEST_ASSERT_TRUE(event_loop_ingress_has_pending());
    ID drained_before = eval_string("(count @breakout-collision-drain-events)", ctx.st);
    TEST_ASSERT_TRUE(is_fixnum(drained_before));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(drained_before));

    ID marker_ok = NULL;
    for (int i = 0; i < 64 && event_loop_has_pending_tasks(); i++) {
        TEST_ASSERT_TRUE(event_loop_run_next(NULL, ctx.st));
        marker_ok = eval_string("(some #(= % [:enter :ball-vs-paddle]) @breakout-collision-drain-events)", ctx.st);
        if (marker_ok == clj_true) {
            break;
        }
    }
    ID drained_events = eval_string("@breakout-collision-drain-events", ctx.st);
    TEST_ASSERT_NOT_NULL(drained_events);
    TEST_ASSERT_TRUE(TAG(drained_events) == CLJ_VECTOR_PERSISTENT);
    if (marker_ok != clj_true) {
        const char *events_text = "<render-failed>";
        CljString *rendered_events = to_string(drained_events);
        if (rendered_events) {
            events_text = clj_string_data(rendered_events);
        }
        EventLoopIngressStats ingress_stats = {0};
        (void)event_loop_ingress_stats(&ingress_stats);
        char fail_msg[256];
        mini_snprintf(fail_msg,
                      sizeof(fail_msg),
                      "expected [:enter :ball-vs-paddle] in drained events %s (pending ingress=%u)",
                      events_text,
                      ingress_stats.pending_count);
        fprintf(stderr, "[collision-drain-debug] %s\n", fail_msg);
        TEST_FAIL_MESSAGE(fail_msg);
    }
    breakout_fx_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_collision_step_drops_callback_under_tight_heap_limit_without_crashing) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_fx_test_context_init(&ctx));
    TEST_ASSERT_TRUE(ctx.bundle.has_primary_slot);

    ID paddle_rule_id = intern_symbol_global(":ball-vs-paddle");
    ViewerCollisionPolicy *policy = breakout_find_policy_by_id(&ctx.spatial_rules, paddle_rule_id);
    TEST_ASSERT_NOT_NULL(policy);
    ViewerSpatialRuleSet single_rule_set = {0};
    ViewerCollisionPolicy single_policy = {0};
    VgCollisionState single_state = {0};
    breakout_init_single_rule_set(&single_rule_set, &single_policy, &single_state, policy);

    uint8_t slot = ctx.bundle.primary_slot_index;
    VgTransformFixed world_t = {
        .m00 = VG_SCALE_ONE,
        .m01 = 0,
        .m02 = 0,
        .m10 = 0,
        .m11 = VG_SCALE_ONE,
        .m12 = 0,
    };
    VgAabb ball_box = {.min_x = 156, .max_x = 159, .min_y = 223, .max_y = 226};
    VgAabb paddle_box = {.min_x = 140, .max_x = 179, .min_y = 224, .max_y = 227};

    vg_rendered_state_reset_all();
    vg_rendered_state_capture_begin(slot, 1u, 0u);
    vg_rendered_state_capture_record_entity((uintptr_t)policy->self_entity_id, world_t);
    vg_rendered_state_capture_record_entity_aabb((uintptr_t)policy->self_entity_id, ball_box);
    vg_rendered_state_capture_record_entity((uintptr_t)policy->other_entity_id, world_t);
    vg_rendered_state_capture_record_entity_aabb((uintptr_t)policy->other_entity_id, paddle_box);
    vg_rendered_state_capture_commit();

    /* Warm up cached keywords before forcing zero heap headroom. */
    VgClipRect far_dirty = {.x = 0, .y = 0, .w = 8, .h = 8};
    TEST_ASSERT_FALSE(fx_collision_detect_step(&ctx.bundle,
                                                   &single_rule_set,
                                                   0u,
                                                   &far_dirty,
                                                   1u));

    size_t prev_limit = memory_get_heap_limit_bytes();
    bool caught = false;
    bool triggered = false;
    TRY {
        memory_set_heap_limit_bytes(0u);
        triggered = fx_collision_detect_step(&ctx.bundle, &single_rule_set, 0u, NULL, 0u);
    } CATCH(ex) {
        (void)ex;
        caught = true;
    } END_TRY
    memory_set_heap_limit_bytes(prev_limit);

    TEST_ASSERT_FALSE(caught);
    TEST_ASSERT_TRUE(triggered);
    TEST_ASSERT_FALSE(event_loop_ingress_has_pending());

    breakout_fx_test_context_destroy(&ctx);
}

#if defined(__APPLE__)
TEST(test_breakout_runtime_startup_maps_macos_virtual_keys_to_runtime_keys) {
    TEST_ASSERT_EQUAL_INT(MFB_KB_KEY_SPACE, tinyfx_macos_key_from_virtual_key(0x31));
    TEST_ASSERT_EQUAL_INT(MFB_KB_KEY_ENTER, tinyfx_macos_key_from_virtual_key(0x24));
    TEST_ASSERT_EQUAL_INT(MFB_KB_KEY_Q, tinyfx_macos_key_from_virtual_key(0x0C));
    TEST_ASSERT_EQUAL_INT(MFB_KB_KEY_LEFT, tinyfx_macos_key_from_virtual_key(0x7B));
    TEST_ASSERT_EQUAL_INT(MFB_KB_KEY_RIGHT, tinyfx_macos_key_from_virtual_key(0x7C));
    TEST_ASSERT_EQUAL_INT(MFB_KB_KEY_LEFT_SUPER, tinyfx_macos_key_from_virtual_key(0x37));
    TEST_ASSERT_EQUAL_INT(MFB_KB_KEY_UNKNOWN, tinyfx_macos_key_from_virtual_key(0xFFFFu));
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

    TEST_ASSERT_FALSE(fx_should_run_collision_step(0u, &last_collision_frame_serial));
    TEST_ASSERT_EQUAL_UINT64(0u, (uint64_t)last_collision_frame_serial);

    TEST_ASSERT_TRUE(fx_should_run_collision_step(1u, &last_collision_frame_serial));
    TEST_ASSERT_EQUAL_UINT64(1u, (uint64_t)last_collision_frame_serial);
    TEST_ASSERT_FALSE(fx_should_run_collision_step(1u, &last_collision_frame_serial));

    TEST_ASSERT_TRUE(fx_should_run_collision_step(2u, &last_collision_frame_serial));
    TEST_ASSERT_EQUAL_UINT64(2u, (uint64_t)last_collision_frame_serial);
}

TEST(test_breakout_runtime_startup_redraw_overlay_keeps_last_non_empty_transfer_until_presented) {
    memset(&g_render_thread, 0, sizeof(g_render_thread));
    g_render_thread.transfer_rects_mutex = subjective_c_mutex_create();
    TEST_ASSERT_NOT_NULL(g_render_thread.transfer_rects_mutex);

    VgClipRect transfer_rect = {.x = 10, .y = 20, .w = 30, .h = 40};
    fx_store_last_transfer_result(5u, &transfer_rect, 1u, 123u);
    fx_store_last_transfer_result(0u, NULL, 0u, 0u);

    VgClipRect overlay_rects[4] = {0};
    uint_fast32_t last_presented_overlay_frame_serial = 0u;
    uint_fast32_t overlay_frame_serial = 0u;
    size_t overlay_count = fx_take_pending_overlay_rects(&last_presented_overlay_frame_serial,
                                                             overlay_rects,
                                                             4u,
                                                             &overlay_frame_serial);

    TEST_ASSERT_EQUAL_UINT(1u, overlay_count);
    TEST_ASSERT_EQUAL_UINT64(5u, (uint64_t)overlay_frame_serial);
    TEST_ASSERT_EQUAL_UINT64(5u, (uint64_t)last_presented_overlay_frame_serial);
    TEST_ASSERT_TRUE(vg_clip_rect_equal(transfer_rect, overlay_rects[0]));
    TEST_ASSERT_EQUAL_UINT(0u, fx_copy_last_transfer_rects(overlay_rects, 4u));
    TEST_ASSERT_EQUAL_UINT(0u,
                           fx_take_pending_overlay_rects(&last_presented_overlay_frame_serial,
                                                             overlay_rects,
                                                             4u,
                                                             &overlay_frame_serial));

    subjective_c_mutex_destroy(g_render_thread.transfer_rects_mutex);
    memset(&g_render_thread, 0, sizeof(g_render_thread));
}

TEST(test_breakout_runtime_startup_runloop_play_loop_survives_timeline_watch_driven_scene_updates) {
    BreakoutViewerTestContext ctx = {0};
    uint16_t pixels[320u * 240u] = {0};
    VgFrameBuffer fb = {0};
    VgRenderSlotState render_state = {0};

    TEST_ASSERT_TRUE(breakout_fx_test_context_init(&ctx));
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
        "  (let [bricks (:bricks @tiny-breakout.runtime/state*) "
        "        b (first (if (map? bricks) (vals bricks) bricks)) "
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
        fx_sync_configured_slots(&ctx.bundle, &ctx.spatial_rules, NULL, false);
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

    breakout_fx_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_post_launch_runloop_frames_fit_debug_heap_limit) {
    BreakoutViewerTestContext ctx = {0};
    VgSlotChangeTracker slot_change_tracker = {0};
    size_t previous_limit = memory_get_heap_limit_bytes();
    const size_t stricter_limit = 592u * 1024u;
    size_t mem_before_frames = 0u;
    size_t mem_after_frames = 0u;
    size_t mem_growth = 0u;
    bool caught = false;
    ID caught_ex = NULL;

    TRY {
        TEST_ASSERT_TRUE(breakout_fx_test_context_init_with_heap_budget(&ctx, true));
        breakout_apply_incremental_heap_budget(stricter_limit);
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

        mem_before_frames = memory_current_usage_bytes();

        TEST_ASSERT_TRUE(start_runloop_thread(ctx.st));

        for (int i = 0; i < 90; i++) {
            usleep(16000);
            fx_sync_configured_slots(&ctx.bundle, &ctx.spatial_rules, &slot_change_tracker, true);
            (void)fx_collision_detect_step(&ctx.bundle,
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
    mem_after_frames = memory_current_usage_bytes();
    mem_growth = (mem_after_frames > mem_before_frames) ? (mem_after_frames - mem_before_frames) : 0u;
    vg_slot_change_tracker_destroy(&slot_change_tracker);
    breakout_fx_test_context_destroy(&ctx);

    if (caught_ex) {
        print_exception(caught_ex);
    }

    char growth_msg[128];
    mini_snprintf(growth_msg, sizeof(growth_msg),
                  "post-launch runloop leaked %zu bytes (limit 8192)", mem_growth);

    TEST_ASSERT_FALSE_MESSAGE(caught,
                              caught_ex ? "post-launch runloop frames should fit inside the stricter 592KB simulated device heap budget"
                                        : "");
    TEST_ASSERT_TRUE_MESSAGE(mem_growth <= 8192u, growth_msg);
}

TEST(test_breakout_runtime_startup_brick_collision_runloop_path_survives_and_scores_once) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_fx_test_context_init(&ctx));
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
        "  (let [bricks (:bricks @tiny-breakout.runtime/state*) "
        "        b (first (if (map? bricks) (vals bricks) bricks)) "
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
    breakout_fx_test_context_destroy(&ctx);

    TEST_ASSERT_TRUE_MESSAGE(saw_brick_score,
                             "expected at least one brick-hit score update while runloop + collision dispatch are active");
}

TEST(test_breakout_runtime_startup_loads_collision_rules_for_all_launched_bricks) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_fx_test_context_init(&ctx));

    ID counts = eval_string(
        "(do "
        "  (require 'tiny-breakout.runtime) "
        "  (tiny-breakout.runtime/start-runtime! nil) "
        "  (tiny-breakout.runtime/apply-input! {:launch true}) "
        "  (let [bricks (:bricks @tiny-breakout.runtime/state*) "
        "        bricks (if (map? bricks) (vals bricks) bricks)] "
        "    [(count (:collision-rules @tiny-breakout.runtime/scene*)) "
        "     (count bricks)]))",
        ctx.st);
    TEST_ASSERT_NOT_NULL(counts);
    TEST_ASSERT_TRUE(TAG(counts) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *counts_vec = as_vector(counts);
    TEST_ASSERT_NOT_NULL(counts_vec);
    TEST_ASSERT_EQUAL_UINT(2u, vector_count(counts_vec));
    ID expected_rule_count = vector_nth(counts_vec, 0u);
    ID launched_brick_count = vector_nth(counts_vec, 1u);
    TEST_ASSERT_TRUE(is_fixnum(expected_rule_count));
    TEST_ASSERT_TRUE(is_fixnum(launched_brick_count));
    TEST_ASSERT_TRUE(as_fixnum(launched_brick_count) > 0);

    fx_sync_configured_slots(&ctx.bundle, &ctx.spatial_rules, NULL, false);

    TEST_ASSERT_EQUAL_UINT32((uint32_t)AS_FIXNUM(expected_rule_count), ctx.spatial_rules.count);
    bool saw_paddle_rule = false;
    ID paddle_rule_id = intern_symbol_global(":ball-vs-paddle");
    TEST_ASSERT_NOT_NULL(paddle_rule_id);
    for (uint32_t i = 0; i < ctx.spatial_rules.count; i++) {
        ViewerCollisionPolicy *policy = &ctx.spatial_rules.items[i];
        if (policy->rule_id == paddle_rule_id) {
            saw_paddle_rule = true;
            break;
        }
    }

    breakout_fx_test_context_destroy(&ctx);
    TEST_ASSERT_TRUE_MESSAGE(saw_paddle_rule,
                             "expected collision policy set to include the canonical paddle collision rule");
}

TEST(test_breakout_runtime_startup_loads_spatial_rules_beyond_legacy_fixed_cap) {
    BreakoutViewerTestContext ctx = {0};
    bool init_ok = breakout_fx_test_context_init(&ctx);
    TEST_ASSERT_TRUE_MESSAGE(init_ok, "breakout test context init failed");

    ID kw_collision_rules = intern_symbol_global(":collision-rules");
    TEST_ASSERT_NOT_NULL_MESSAGE(kw_collision_rules, "missing :collision-rules keyword");
    FrameScene *scene = fx_frame_scene_from_atom(ctx.bundle.primary_scene_atom);
    TEST_ASSERT_NOT_NULL_MESSAGE(scene, "primary scene atom did not deref to a frame scene");
    ID rules = tiny_fx_gfx_get_field((ID)scene, kw_collision_rules, NULL);
    TEST_ASSERT_NOT_NULL_MESSAGE(rules, "scene did not expose :collision-rules");
    TEST_ASSERT_TRUE_MESSAGE(is_vector(rules), "scene :collision-rules must stay a vector");
    CljPersistentVector *rules_vec = as_vector(rules);
    TEST_ASSERT_NOT_NULL_MESSAGE(rules_vec, "scene :collision-rules vector could not be accessed");
    ID base_rule = vector_nth(rules_vec, 0u);
    TEST_ASSERT_NOT_NULL_MESSAGE(base_rule, "scene must contain at least one base collision rule");

    CljPersistentVector *expanded = make_vector(80, STRONG);
    TEST_ASSERT_NOT_NULL_MESSAGE(expanded, "failed to allocate expanded collision rule vector");
    for (uint32_t i = 0; i < 80u; i++) {
        vector_conj_inplace(&expanded, base_rule);
    }
    AUTORELEASE((ID)expanded);

    ID updated_scene = record_assoc((ID)scene, kw_collision_rules, (ID)expanded);
    TEST_ASSERT_NOT_NULL_MESSAGE(updated_scene, "record_assoc failed to build updated scene");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(updated_scene,
                                  atom_reset(ctx.bundle.primary_scene_atom, updated_scene),
                                  "atom_reset should publish the updated primary scene");

    fx_sync_configured_slots(&ctx.bundle, &ctx.spatial_rules, NULL, false);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(80u,
                                     ctx.spatial_rules.count,
                                     "dynamic spatial loader should keep all 80 concrete rules");

    breakout_fx_test_context_destroy(&ctx);
}

TEST(test_breakout_runtime_startup_segment_rearm_ignores_stale_at_end_snapshot_until_new_frame) {
    BreakoutViewerTestContext ctx = {0};
    uint16_t pixels[320u * 240u] = {0};
    VgFrameBuffer fb = {0};
    VgRenderSlotState render_state = {0};
    TEST_ASSERT_TRUE(breakout_fx_test_context_init(&ctx));
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
    breakout_fx_test_context_destroy(&ctx);

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
    TEST_ASSERT_TRUE(breakout_fx_test_context_init(&ctx));
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
        "  (let [bricks (:bricks @tiny-breakout.runtime/state*) "
        "        b (first (if (map? bricks) (vals bricks) bricks)) "
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
    breakout_fx_test_context_destroy(&ctx);

    TEST_ASSERT_TRUE_MESSAGE(saw_brick_score,
                             "expected to observe at least one scored brick hit");
    TEST_ASSERT_TRUE_MESSAGE(saw_segment_advance_after_brick || reached_serve_or_game_over_after_brick,
                             "after first brick hit, expected either further segment-id-seq advance or a clean serve/game-over transition");
}

TEST(test_breakout_runtime_startup_many_brick_hit_events_do_not_hang_runloop) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_fx_test_context_init(&ctx));
    TEST_ASSERT_NOT_NULL(ctx.bundle.spatial_callback);

    ID payload = eval_string(
        "(do "
        "  (require 'tiny-breakout.runtime) "
        "  (tiny-breakout.runtime/start-runtime! nil) "
        "  (tiny-breakout.runtime/apply-input! {:launch true}) "
        "  (let [all-bricks (if (map? (:bricks @tiny-breakout.runtime/state*)) "
        "                     (vals (:bricks @tiny-breakout.runtime/state*)) "
        "                     (:bricks @tiny-breakout.runtime/state*)) "
        "        bricks (vec (take 12 all-bricks)) "
        "        events (mapv (fn [b] "
        "                       (let [bx (:x b) by (:y b) bw (:w b) bh (:h b)] "
        "                         {:source :spatial "
        "                          :id :ball-vs-brick "
        "                          :rule {:id :ball-vs-brick} "
        "                          :phase :enter "
        "                          :other (:id b) "
        "                          :self-aabb {:min-x bx :min-y (+ by bh) :max-x (+ bx 4) :max-y (+ by bh 4)} "
        "                          :other-aabb {:min-x bx :min-y by :max-x (+ bx bw) :max-y (+ by bh)}})) "
        "                     bricks) "
        "        expected-score (loop [remaining bricks total 0] "
        "                         (if (empty? remaining) "
        "                           total "
        "                           (recur (rest remaining) (+ total (:points (first remaining))))))] "
        "    {:events events :expected-score expected-score}))",
        ctx.st);
    TEST_ASSERT_NOT_NULL(payload);
    TEST_ASSERT_TRUE(is_map(payload));

    ID events_kw = intern_symbol_global(":events");
    ID expected_score_kw = intern_symbol_global(":expected-score");
    ID score_kw = intern_symbol_global(":score");
    ID phase_kw = intern_symbol_global(":phase");
    ID level_clear_kw = intern_symbol_global(":level-clear");
    ID serve_kw = intern_symbol_global(":serve");
    TEST_ASSERT_NOT_NULL(events_kw);
    TEST_ASSERT_NOT_NULL(expected_score_kw);
    TEST_ASSERT_NOT_NULL(score_kw);
    TEST_ASSERT_NOT_NULL(phase_kw);
    TEST_ASSERT_NOT_NULL(level_clear_kw);
    TEST_ASSERT_NOT_NULL(serve_kw);

    ID events = map_get_sentinel(payload, events_kw, NULL);
    ID expected_score = map_get_sentinel(payload, expected_score_kw, NULL);
    TEST_ASSERT_NOT_NULL(events);
    TEST_ASSERT_TRUE(TAG(events) == CLJ_VECTOR_PERSISTENT);
    TEST_ASSERT_TRUE(is_fixnum(expected_score));
    CljPersistentVector *events_vec = as_vector(events);
    TEST_ASSERT_NOT_NULL(events_vec);
    TEST_ASSERT_TRUE(vector_count(events_vec) >= 8u);

    ID state_atom_id = eval_string("tiny-breakout.runtime/state*", ctx.st);
    TEST_ASSERT_NOT_NULL(state_atom_id);
    TEST_ASSERT_EQUAL_UINT8(CLJ_ATOM, TAG(state_atom_id));
    CljAtom *state_atom = as_atom(state_atom_id);
    TEST_ASSERT_NOT_NULL(state_atom);

    TEST_ASSERT_TRUE(start_runloop_thread(ctx.st));
    for (uint32_t i = 0; i < vector_count(events_vec); i++) {
        ID event = vector_nth(events_vec, i);
        TEST_ASSERT_NOT_NULL(event);
        ID retained_event = RETAIN(event);
        TEST_ASSERT_TRUE(event_loop_enqueue_ingress_call((CljObject *)ctx.bundle.spatial_callback, retained_event));
        RELEASE(retained_event);
    }

    bool reached_expected_score = false;
    bool reached_transition = false;
    for (int i = 0; i < 240; i++) {
        usleep(10000);
        ID state = atom_deref_owned(state_atom);
        if (state && is_map(state)) {
            ID score = map_get_sentinel(state, score_kw, NULL);
            ID phase = map_get_sentinel(state, phase_kw, NULL);
            if (score && is_fixnum(score) && as_fixnum(score) == as_fixnum(expected_score)) {
                reached_expected_score = true;
            }
            if (phase == level_clear_kw || phase == serve_kw) {
                reached_transition = true;
            }
        }
        RELEASE(state);
        if (reached_expected_score || reached_transition) {
            break;
        }
    }

    stop_runloop_thread();
    breakout_fx_test_context_destroy(&ctx);

    TEST_ASSERT_TRUE_MESSAGE(reached_expected_score || reached_transition,
                             "many queued brick-hit events should keep progressing instead of hanging the runloop");
}

TEST(test_breakout_runtime_startup_many_brick_hits_then_launch_advances_after_level_clear) {
    BreakoutViewerTestContext ctx = {0};
    TEST_ASSERT_TRUE(breakout_fx_test_context_init(&ctx));
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.runtime) "
        "  (require 'tiny-clj.event) "
        "  (tiny-breakout.runtime/bootstrap-runtime!) "
        "  (tiny-breakout.runtime/start-runtime! nil) "
        "  (tiny-breakout.runtime/apply-input! {:launch true}) "
        "  (let [bricks (:bricks @tiny-breakout.runtime/state*) "
        "        b (first (if (map? bricks) (vals bricks) bricks)) "
        "        bx (:x b) "
        "        by (:y b) "
        "        now-ms (current-time-ms) "
        "        seeded (-> @tiny-breakout.runtime/state* "
        "                   (assoc :phase :play) "
        "                   (assoc :bricks {(:id b) b}) "
        "                   (assoc :ball-x bx) "
        "                   (assoc :ball-y by) "
        "                   (assoc :ball-vx 2) "
        "                   (assoc :ball-vy 2) "
        "                   (assoc :ball-segment "
        "                          {:id 912 "
        "                           :start-ms (- now-ms 16) "
        "                           :end-ms now-ms "
        "                           :from-x bx "
        "                           :from-y by "
        "                           :to-x bx "
        "                           :to-y by "
        "                           :collision {:hit-id (:id b) :normal :top}})) "
        "        _ (tiny-breakout.runtime/publish-state! seeded) "
        "        _ (dotimes [_ 24] "
        "            (tiny-clj.event/dispatch-timeline-progress! "
        "              {:event-id :tiny-breakout/segment-end "
        "               :end-event true "
        "               :at-end true "
        "               :phase-ms 1 "
        "               :period-ms 1})) "
        "        _ (dotimes [_ 24] (run-next-task)) "
        "        s1 @tiny-breakout.runtime/state* "
        "        _ (tiny-breakout.runtime/apply-input! {:launch true}) "
        "        s2 @tiny-breakout.runtime/state*] "
        "    (and (= :level-clear (:phase s1)) "
        "         (= :serve (:phase s2)))))",
        ctx.st);
    breakout_fx_test_context_destroy(&ctx);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_runtime_startup_render_thread_collision_bounces_without_main_thread_poll) {
    BreakoutViewerTestContext ctx = {0};
    VgSlotChangeTracker slot_change_tracker = {0};
    VgFrameBuffer fb = {0};
    ID seeded = NULL;
    ID state_atom_id = NULL;
    ID final_state = NULL;
    ID k_ball_vy = intern_symbol_global(":ball-vy");
    ID final_ball_vy = NULL;

    TEST_ASSERT_TRUE(breakout_fx_test_context_init(&ctx));
    TEST_ASSERT_TRUE(fx_init_slot_runtime_buffers(&ctx.bundle));
    TEST_ASSERT_TRUE(vg_slot_change_tracker_init(&slot_change_tracker, ctx.bundle.slot_count));
    memset(g_render_buffer, 0, sizeof(g_render_buffer));
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, VIEW_W, VIEW_H, g_render_buffer, VIEW_W * VIEW_H));

    seeded = eval_string(
        "(do "
        "  (require 'tiny-breakout.runtime) "
        "  (tiny-breakout.runtime/start-runtime! nil) "
        "  (let [now-ms (current-time-ms) "
        "        s (-> @tiny-breakout.runtime/state* "
        "              (assoc :phase :play) "
        "              (assoc :paddle-x 140) "
        "              (assoc :ball-x 158 :ball-y 210) "
        "              (assoc :ball-vx 0 :ball-vy 2) "
        "              (assoc :segment-id-seq 1) "
        "              (assoc :ball-segment {:id 1 :start-ms now-ms :end-ms (+ now-ms 256) :to-x 158 :to-y 242 :wall :bottom}))] "
        "    (tiny-breakout.runtime/publish-state! s) "
        "    true))",
        ctx.st);
    TEST_ASSERT_EQUAL_PTR(clj_true, seeded);
    state_atom_id = eval_string("tiny-breakout.runtime/state*", ctx.st);
    TEST_ASSERT_NOT_NULL(state_atom_id);
    TEST_ASSERT_EQUAL_UINT8(CLJ_ATOM, TAG(state_atom_id));

    fx_sync_and_publish_configured_slots(&ctx.bundle, &ctx.spatial_rules, &slot_change_tracker, true);
    fx_configure_render_thread_collision(&ctx.bundle, &ctx.spatial_rules, true);
    tiny_renderer_lifecycle_set_callbacks(fx_renderer_start_callback,
                                          fx_renderer_stop_callback,
                                          &fb);
    TEST_ASSERT_TRUE(tiny_renderer_lifecycle_start(NULL));
    TEST_ASSERT_TRUE(start_runloop_thread(ctx.st));

    for (int i = 0; i < 40; i++) {
        usleep(16000);
        fx_sync_and_publish_configured_slots(&ctx.bundle, &ctx.spatial_rules, &slot_change_tracker, true);
    }

    final_state = atom_deref((CljAtom *)state_atom_id);
    TEST_ASSERT_NOT_NULL(final_state);
    TEST_ASSERT_TRUE(is_map(final_state));
    final_ball_vy = map_get_sentinel((CljPersistentMap *)final_state, k_ball_vy, NULL);

    stop_runloop_thread();
    (void)tiny_renderer_lifecycle_stop();
    tiny_renderer_lifecycle_set_callbacks(NULL, NULL, NULL);
    fx_configure_render_thread_collision(NULL, NULL, false);
    vg_slot_change_tracker_destroy(&slot_change_tracker);
    fx_destroy_slot_runtime_buffers();
    breakout_fx_test_context_destroy(&ctx);

    TEST_ASSERT_TRUE(is_fixnum(final_ball_vy) && as_fixnum(final_ball_vy) < 0);
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
        TEST_ASSERT_TRUE(breakout_fx_test_context_init_with_heap_budget(&ctx, true));
        TEST_ASSERT_TRUE(fx_init_slot_runtime_buffers(&ctx.bundle));
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
        tiny_renderer_lifecycle_set_callbacks(fx_renderer_start_callback,
                                              fx_renderer_stop_callback,
                                              &fb);
        TEST_ASSERT_TRUE(tiny_renderer_lifecycle_start(NULL));
        TEST_ASSERT_TRUE(start_runloop_thread(ctx.st));
        TEST_ASSERT_TRUE(event_loop_enqueue_ingress_call((CljObject *)startup_fn, NULL));
        TEST_ASSERT_TRUE(event_loop_enqueue_ingress_call((CljObject *)launch_fn, launch_arg));

        for (int i = 0; i < 90; i++) {
            usleep(16000);
            fx_sync_configured_slots(&ctx.bundle, &ctx.spatial_rules, &slot_change_tracker, true);
            ViewerFrameRenderResult render_result = fx_poll_render_frame();
            if (fx_should_run_collision_step(render_result.frame_serial,
                                                 &last_collision_frame_serial)) {
                (void)fx_collision_detect_step(&ctx.bundle,
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
    fx_destroy_slot_runtime_buffers();
    RELEASE(startup_fn);
    RELEASE(launch_fn);
    RELEASE(launch_arg);
    RELEASE(state_atom_id);
    breakout_fx_test_context_destroy(&ctx);

    if (caught_ex) {
        print_exception(caught_ex);
    }

    TEST_ASSERT_FALSE_MESSAGE(caught,
                              caught_ex ? "first launch with render thread should fit inside the 640KB debug heap budget"
                                        : "");
    TEST_ASSERT_EQUAL_PTR(play_phase, final_phase);
}
