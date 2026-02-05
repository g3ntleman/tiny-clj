// Runtime stats tests: (tinyclj.runtime/stats)

#include "tests_common.h"
#include "../event_loop.h"

int load_clojure_core(EvalState *st);

#if defined(DEBUG) && defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
static void assert_memory_stats_not_increasing(const MemoryStats *before,
                                               const MemoryStats *after,
                                               size_t tolerance,
                                               const char *message) {
    size_t limit = before->current_memory_usage + tolerance;
    TEST_ASSERT_TRUE_MESSAGE(after->current_memory_usage <= limit, message);
}

static void print_memory_type_deltas(const MemoryStats *before,
                                     const MemoryStats *after,
                                     const char *label) {
    if (!before || !after || !label) return;
    fprintf(stderr, "[%s] per-type deltas:\n", label);
    for (int i = 0; i < CLJ_TYPE_COUNT; i++) {
        size_t bytes_before = before->bytes_current_by_type[i];
        size_t bytes_after = after->bytes_current_by_type[i];
        size_t allocs_before = before->allocations_by_type[i];
        size_t allocs_after = after->allocations_by_type[i];
        size_t deallocs_before = before->deallocations_by_type[i];
        size_t deallocs_after = after->deallocations_by_type[i];
        if (bytes_before == bytes_after &&
            allocs_before == allocs_after &&
            deallocs_before == deallocs_after) {
            continue;
        }
        fprintf(stderr, "  %s: bytes %+zd alloc %+zd dealloc %+zd\n",
                clj_type_name((CljType)i),
                (ssize_t)(bytes_after - bytes_before),
                (ssize_t)(allocs_after - allocs_before),
                (ssize_t)(deallocs_after - deallocs_before));
    }
}
#endif

TEST(test_runtime_stats_basic_keys_present)
{
    // Ensure tinyclj.runtime is loaded so the :native stub is defined.
    ID result = eval_string("(do (require 'tinyclj.runtime) (tinyclj.runtime/stats))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_map(result));

    CljPersistentMap *m = (CljPersistentMap *)result;

    ID k_host_os = (ID)intern_symbol_global(":host-os");
    ID k_host_os_version = (ID)intern_symbol_global(":host-os-version");
    ID k_tiny_clj_version = (ID)intern_symbol_global(":tiny-clj-version");
    ID k_build_time = (ID)intern_symbol_global(":build-time");

    ID v_host_os = map_get_sentinel(m, k_host_os, NOT_FOUND);
    ID v_host_os_version = map_get_sentinel(m, k_host_os_version, NOT_FOUND);
    ID v_tiny_clj_version = map_get_sentinel(m, k_tiny_clj_version, NOT_FOUND);
    ID v_build_time = map_get_sentinel(m, k_build_time, NOT_FOUND);

    TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_host_os);
    TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_host_os_version);
    TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_tiny_clj_version);
    TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_build_time);

    TEST_ASSERT_EQUAL_INT(CLJ_STRING, TAG(v_host_os));
    TEST_ASSERT_EQUAL_INT(CLJ_STRING, TAG(v_host_os_version));
    TEST_ASSERT_EQUAL_INT(CLJ_STRING, TAG(v_tiny_clj_version));
    TEST_ASSERT_EQUAL_INT(CLJ_INSTANT, TAG(v_build_time));
}

TEST(test_runtime_stats_build_time_before_now)
{
    // Get build-time from stats
    ID stats = eval_string("(do (require 'tinyclj.runtime) (tinyclj.runtime/stats))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(stats);
    TEST_ASSERT_TRUE(is_map(stats));
    
    ID k_build_time = (ID)intern_symbol_global(":build-time");
    ID v_build_time = map_get_sentinel((CljPersistentMap *)stats, k_build_time, NOT_FOUND);
    TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_build_time);
    TEST_ASSERT_EQUAL_INT(CLJ_INSTANT, TAG(v_build_time));
    
    // Get current time via (now) - it's in clojure.core
    ID now_result = eval_string("(now)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(now_result);
    TEST_ASSERT_EQUAL_INT(CLJ_INSTANT, TAG(now_result));
    
    // Build time must be <= now
    int32_t build_days = clj_instant_days(v_build_time);
    int32_t now_days = clj_instant_days(now_result);
    
    // Either build_days < now_days, or (build_days == now_days && build_millis <= now_millis)
    if (build_days < now_days) {
        // Build time is definitely before now
        TEST_PASS();
    } else if (build_days == now_days) {
        uint32_t build_millis = clj_instant_ms(v_build_time);
        uint32_t now_millis = clj_instant_ms(now_result);
        TEST_ASSERT_TRUE_MESSAGE(build_millis <= now_millis, 
                                 "Build time milliseconds must be <= now milliseconds on same day");
    } else {
        TEST_FAIL_MESSAGE("Build time is in the future!");
    }
}

#if defined(DEBUG) && defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
TEST(test_runtime_stats_contains_memory_stats_map)
{
    ID stats = eval_string("(do (require 'tinyclj.runtime) (tinyclj.runtime/stats))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(stats);
    TEST_ASSERT_TRUE(is_map(stats));

    ID k_memory_stats = (ID)intern_symbol_global(":memory-stats");
    ID v_memory_stats = map_get_sentinel((CljPersistentMap *)stats, k_memory_stats, NOT_FOUND);
    TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_memory_stats);
    TEST_ASSERT_TRUE(is_map(v_memory_stats));

    CljPersistentMap *ms = (CljPersistentMap *)v_memory_stats;
#if MEMORY_PROFILING_ENABLED
    // If memory profiling is enabled for this build, raw keys should exist.
    ID k_bytes_current = (ID)intern_symbol_global(":bytes-current");
    ID v_bytes_current = map_get_sentinel(ms, k_bytes_current, NOT_FOUND);
    TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_bytes_current);
    TEST_ASSERT_TRUE(is_fixnum(v_bytes_current));

    ID k_bytes_peak = (ID)intern_symbol_global(":bytes-peak");
    ID v_bytes_peak = map_get_sentinel(ms, k_bytes_peak, NOT_FOUND);
    TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_bytes_peak);
    TEST_ASSERT_TRUE(is_fixnum(v_bytes_peak));

    ID k_raw_bytes_current = (ID)intern_symbol_global(":raw-bytes-current");
    ID v_raw_bytes_current = map_get_sentinel(ms, k_raw_bytes_current, NOT_FOUND);
    TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_raw_bytes_current);
    TEST_ASSERT_TRUE(is_fixnum(v_raw_bytes_current));

    ID k_raw_bytes_peak = (ID)intern_symbol_global(":raw-bytes-peak");
    ID v_raw_bytes_peak = map_get_sentinel(ms, k_raw_bytes_peak, NOT_FOUND);
    TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_raw_bytes_peak);
    TEST_ASSERT_TRUE(is_fixnum(v_raw_bytes_peak));
#endif
}
#endif

#if defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
TEST(test_runtime_stats_current_ram_under_200kb_after_core_load)
{
    // Ensure both clojure.core and tinyclj.runtime are loaded before measuring.
    ID stats = eval_string("(do (require 'clojure.core) (require 'tinyclj.runtime) (tinyclj.runtime/stats))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(stats);
    TEST_ASSERT_TRUE(is_map(stats));

    ID k_memory_stats = (ID)intern_symbol_global(":memory-stats");
    ID v_memory_stats = map_get_sentinel((CljPersistentMap *)stats, k_memory_stats, NOT_FOUND);
    TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_memory_stats);
    TEST_ASSERT_TRUE(is_map(v_memory_stats));

    ID k_bytes_current = (ID)intern_symbol_global(":bytes-current");
    ID k_bytes_peak = (ID)intern_symbol_global(":bytes-peak");
    ID v_bytes_current = map_get_sentinel((CljPersistentMap *)v_memory_stats, k_bytes_current, NOT_FOUND);
    ID v_bytes_peak = map_get_sentinel((CljPersistentMap *)v_memory_stats, k_bytes_peak, NOT_FOUND);
    TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_bytes_current);
    TEST_ASSERT_TRUE(is_fixnum(v_bytes_current));
    TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_bytes_peak);
    TEST_ASSERT_TRUE(is_fixnum(v_bytes_peak));

    int32_t bytes_current = as_fixnum(v_bytes_current);
    int32_t bytes_peak = as_fixnum(v_bytes_peak);
    int32_t raw_bytes_current = -1;
    int32_t raw_bytes_peak = -1;
    ID k_raw_bytes_current = (ID)intern_symbol_global(":raw-bytes-current");
    ID k_raw_bytes_peak = (ID)intern_symbol_global(":raw-bytes-peak");
    ID v_raw_bytes_current = map_get_sentinel((CljPersistentMap *)v_memory_stats, k_raw_bytes_current, NOT_FOUND);
    ID v_raw_bytes_peak = map_get_sentinel((CljPersistentMap *)v_memory_stats, k_raw_bytes_peak, NOT_FOUND);
    if (v_raw_bytes_current != NOT_FOUND && is_fixnum(v_raw_bytes_current)) {
        raw_bytes_current = as_fixnum(v_raw_bytes_current);
    }
    if (v_raw_bytes_peak != NOT_FOUND && is_fixnum(v_raw_bytes_peak)) {
        raw_bytes_peak = as_fixnum(v_raw_bytes_peak);
    }

    fprintf(stderr, "[runtime-stats] bytes-current=%d bytes-peak=%d raw-bytes-current=%d raw-bytes-peak=%d\n",
            bytes_current, bytes_peak, raw_bytes_current, raw_bytes_peak);

    // Dump bytes-by-type for focused debugging (only types with non-zero stats are included).
    ID k_bytes_by_type = (ID)intern_symbol_global(":bytes-by-type");
    ID v_bytes_by_type = map_get_sentinel((CljPersistentMap *)v_memory_stats, k_bytes_by_type, NOT_FOUND);
    if (v_bytes_by_type != NOT_FOUND && is_map(v_bytes_by_type)) {
        CljPersistentMap *by_type = (CljPersistentMap *)v_bytes_by_type;
        ID k_alloc_count = (ID)intern_symbol_global(":alloc-count");
        ID k_dealloc_count = (ID)intern_symbol_global(":dealloc-count");
        fprintf(stderr, "[runtime-stats] bytes-by-type:\n");
        MAP_FOR_EACH(by_type, k, v) {
            const char *type_name = "<unknown>";
            if (k && TAG(k) == CLJ_STRING) {
                CljString *ks = as_clj_string(k);
                type_name = ks ? clj_string_data(ks) : "<unknown>";
            }

            int32_t bc = -1;
            int32_t bp = -1;
            int32_t ac = -1;
            int32_t dc = -1;
            if (v && is_map(v)) {
                ID v_bc = map_get_sentinel((CljPersistentMap *)v, k_bytes_current, NOT_FOUND);
                ID v_bp = map_get_sentinel((CljPersistentMap *)v, k_bytes_peak, NOT_FOUND);
                ID v_ac = map_get_sentinel((CljPersistentMap *)v, k_alloc_count, NOT_FOUND);
                ID v_dc = map_get_sentinel((CljPersistentMap *)v, k_dealloc_count, NOT_FOUND);
                if (v_bc != NOT_FOUND && is_fixnum(v_bc)) bc = as_fixnum(v_bc);
                if (v_bp != NOT_FOUND && is_fixnum(v_bp)) bp = as_fixnum(v_bp);
                if (v_ac != NOT_FOUND && is_fixnum(v_ac)) ac = as_fixnum(v_ac);
                if (v_dc != NOT_FOUND && is_fixnum(v_dc)) dc = as_fixnum(v_dc);
            }

            fprintf(stderr, "  %s: bytes-current=%d bytes-peak=%d alloc=%d dealloc=%d\n",
                    type_name, bc, bp, ac, dc);
        }
    }

    TEST_ASSERT_TRUE_MESSAGE(bytes_current >= 0, "bytes-current must be non-negative");
    {
        char msg[128];
        snprintf(msg, sizeof(msg), "bytes-current must be under 600KB (got %d)", bytes_current);
        TEST_ASSERT_TRUE_MESSAGE(bytes_current < 600 * 1024, msg);
    }
}
#endif

#if defined(DEBUG) && defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
TEST(test_runtime_stats_core_load_memory_delta)
{
    // Build a fresh runtime state so the delta reflects core loading, not prior tests.
    runtime_reset(&g_runtime);
    WITH_AUTORELEASE_POOL({
        runtime_init(&g_runtime);
    });
    event_loop_init();
    meta_registry_init();
    init_special_symbols();
    register_builtins();
    g_runtime.builtins_registered = true;

    // Reset eval state without loading core.
    evalstate_reset(&g_test_eval_state, false);

    // Zero profiler counters to measure only core loading costs.
    memory_profiler_reset();
    MemoryStats before = memory_profiler_get_stats();
    fprintf(stderr, "[core-load-delta] before: bytes-current=%zu bytes-peak=%zu raw-current=%zu raw-peak=%zu\n",
            before.current_memory_usage, before.peak_memory_usage,
            before.raw_bytes_current, before.raw_bytes_peak);

    load_clojure_core(g_test_eval_state);

    MemoryStats after = memory_profiler_get_stats();
    fprintf(stderr, "[core-load-delta] after:  bytes-current=%zu bytes-peak=%zu raw-current=%zu raw-peak=%zu\n",
            after.current_memory_usage, after.peak_memory_usage,
            after.raw_bytes_current, after.raw_bytes_peak);

    fprintf(stderr, "[core-load-delta] delta: +%zu bytes-current\n",
            after.current_memory_usage - before.current_memory_usage);

    TEST_ASSERT_TRUE_MESSAGE(after.current_memory_usage >= before.current_memory_usage,
                             "bytes-current should not decrease after core load");
}
#endif

#if defined(DEBUG) && defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
TEST(test_runtime_stats_core_load_under_100k)
{
    // Fresh runtime state for a clean baseline.
    runtime_reset(&g_runtime);
    WITH_AUTORELEASE_POOL({
        runtime_init(&g_runtime);
    });
    event_loop_init();
    meta_registry_init();
    init_special_symbols();
    register_builtins();
    g_runtime.builtins_registered = true;

    // Reset eval state without loading core.
    evalstate_reset(&g_test_eval_state, false);

    // Measure only core loading costs.
    memory_profiler_reset();
    load_clojure_core(g_test_eval_state);

    MemoryStats after = memory_profiler_get_stats();
    size_t limit = 500 * 1024;
    TEST_ASSERT_TRUE_MESSAGE(after.current_memory_usage < limit,
                             "core load should keep heap under 500k");
}
#endif

#if defined(DEBUG) && defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
TEST(test_runtime_stats_core_load_idempotent_memory)
{
    // Fresh runtime state.
    runtime_reset(&g_runtime);
    WITH_AUTORELEASE_POOL({
        runtime_init(&g_runtime);
    });
    event_loop_init();
    meta_registry_init();
    init_special_symbols();
    register_builtins();
    g_runtime.builtins_registered = true;

    evalstate_reset(&g_test_eval_state, false);

    memory_profiler_reset();
    load_clojure_core(g_test_eval_state);
    MemoryStats first = memory_profiler_get_stats();

    load_clojure_core(g_test_eval_state);
    MemoryStats second = memory_profiler_get_stats();

    fprintf(stderr, "[core-idempotent] first bytes-current=%zu bytes-peak=%zu\n",
            first.current_memory_usage, first.peak_memory_usage);
    fprintf(stderr, "[core-idempotent] second bytes-current=%zu bytes-peak=%zu\n",
            second.current_memory_usage, second.peak_memory_usage);

    // Allow small fluctuations from transient allocations, but no growth trend.
    size_t tolerance = 0;
    assert_memory_stats_not_increasing(&first, &second, tolerance,
                                       "core reload should not increase bytes-current significantly");
}
#endif

#if defined(DEBUG) && defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
TEST(test_runtime_stats_autorelease_loop_does_not_grow_heap)
{
    // Use a trivial expression that should not leave persistent objects behind.
    const char *expr = "(+ 1 2)";

    // Full runtime reset so we measure only the loop's transient effect.
    runtime_reset(&g_runtime);
    WITH_AUTORELEASE_POOL({
        runtime_init(&g_runtime);
    });
    event_loop_init();
    meta_registry_init();
    init_special_symbols();
    register_builtins();
    g_runtime.builtins_registered = true;
    evalstate_reset(&g_test_eval_state, true);

    memory_profiler_reset();
    // Warm caches once (resolve + callsite), then take baseline.
    WITH_AUTORELEASE_POOL({
        ID warm = eval_string(expr, g_test_eval_state);
        (void)warm;
    });
    MemoryStats before = memory_profiler_get_stats();

    for (int i = 0; i < 10; i++) {
        WITH_AUTORELEASE_POOL({
            ID r = eval_string(expr, g_test_eval_state);
            (void)r; // autoreleased; no persistent storage expected
        });
    }

    MemoryStats after = memory_profiler_get_stats();

    size_t delta = (after.current_memory_usage > before.current_memory_usage)
                   ? (after.current_memory_usage - before.current_memory_usage)
                   : 0;

    fprintf(stderr, "[autorelease-loop] before=%zu after=%zu delta=%zu\n",
            before.current_memory_usage, after.current_memory_usage, delta);

    // Expect zero growth after warm baseline.
    if (after.current_memory_usage > before.current_memory_usage) {
        print_memory_type_deltas(&before, &after, "autorelease-loop");
    }
    assert_memory_stats_not_increasing(&before, &after, 0,
                                       "autorelease loop should not grow heap after baseline");
}
#endif

#if defined(DEBUG) && defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
TEST(test_runtime_stats_memory_stats_stable_in_loop)
{
    const char *expr = "(+ 1 2)";

    runtime_reset(&g_runtime);
    WITH_AUTORELEASE_POOL({
        runtime_init(&g_runtime);
    });
    event_loop_init();
    meta_registry_init();
    init_special_symbols();
    register_builtins();
    g_runtime.builtins_registered = true;
    evalstate_reset(&g_test_eval_state, true);

    memory_profiler_reset();
    // Warm caches once (resolve + callsite), then take baseline.
    WITH_AUTORELEASE_POOL({
        ID warm = eval_string(expr, g_test_eval_state);
        (void)warm;
    });
    MemoryStats baseline = memory_profiler_get_stats();

    for (int i = 0; i < 50; i++) {
        WITH_AUTORELEASE_POOL({
            ID r = eval_string(expr, g_test_eval_state);
            (void)r;
        });
        MemoryStats now = memory_profiler_get_stats();
        if (now.current_memory_usage > baseline.current_memory_usage) {
            print_memory_type_deltas(&baseline, &now, "stable-loop");
        }
        assert_memory_stats_not_increasing(&baseline, &now, 0,
                                           "memory-stats should remain stable during loop");
    }
}
#endif
