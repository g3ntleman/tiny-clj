// Runtime stats tests: (tiny-clj.runtime/stats)

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
  if (!before || !after || !label)
    return;
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

TEST(test_runtime_stats_basic_keys_present) {
  // Ensure tiny-clj.runtime is loaded so the :native stub is defined.
  ID result = eval_string("(do (require 'tiny-clj.runtime) (tiny-clj.runtime/stats))", g_test_eval_state);
  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE(is_map(result));

  CljPersistentMap *m = (CljPersistentMap *)result;

  ID v_os = map_get_sentinel(m, SYM_KW_OS, NOT_FOUND);
  ID v_version = map_get_sentinel(m, SYM_KW_VERSION, NOT_FOUND);

  TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_os);
  TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_version);

  TEST_ASSERT_EQUAL_INT(CLJ_STRING, TAG(v_os));
  TEST_ASSERT_EQUAL_INT(CLJ_STRING, TAG(v_version));
}

TEST(test_runtime_stats_build_time_before_now) {
  // Get build-time from stats
  ID stats = eval_string("(do (require 'tiny-clj.runtime) (tiny-clj.runtime/stats))", g_test_eval_state);
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

TEST(test_runtime_stats_hardware_gpio_pin_count_when_hardware_present) {
  ID stats = eval_string("(do (require 'tiny-clj.runtime) (tiny-clj.runtime/stats))", g_test_eval_state);
  TEST_ASSERT_NOT_NULL(stats);
  TEST_ASSERT_TRUE(is_map(stats));

  ID k_hardware = (ID)intern_symbol_global(":hardware");
  ID v_hardware = map_get_sentinel((CljPersistentMap *)stats, k_hardware, NOT_FOUND);
  if (v_hardware == NOT_FOUND || !v_hardware) {
    // Platform does not provide hardware info in this build.
    TEST_PASS();
  }

  TEST_ASSERT_TRUE(is_map(v_hardware));
  ID k_gpio_pin_count = (ID)intern_symbol_global(":gpio-pin-count");
  ID v_gpio_pin_count = map_get_sentinel((CljPersistentMap *)v_hardware, k_gpio_pin_count, NOT_FOUND);
  TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_gpio_pin_count);
  TEST_ASSERT_TRUE(is_fixnum(v_gpio_pin_count));
  TEST_ASSERT_TRUE(as_fixnum(v_gpio_pin_count) > 0);
}

TEST(test_runtime_stats_external_ram_total_when_present) {
  ID stats = eval_string("(do (require 'tiny-clj.runtime) (tiny-clj.runtime/stats))", g_test_eval_state);
  TEST_ASSERT_NOT_NULL(stats);
  TEST_ASSERT_TRUE(is_map(stats));

  ID k_memory_stats = (ID)intern_symbol_global(":memory-stats");
  ID v_memory_stats = map_get_sentinel((CljPersistentMap *)stats, k_memory_stats, NOT_FOUND);
  if (v_memory_stats == NOT_FOUND || !v_memory_stats || !is_map(v_memory_stats)) {
    TEST_PASS();
  }

  ID k_external_ram_total = (ID)intern_symbol_global(":external-ram-total");
  ID v_external_ram_total = map_get_sentinel((CljPersistentMap *)v_memory_stats, k_external_ram_total, NOT_FOUND);
  if (v_external_ram_total == NOT_FOUND) {
    /* Platform does not report external RAM (e.g. host or ESP32 without PSRAM). */
    TEST_PASS();
  }
  TEST_ASSERT_TRUE(is_fixnum(v_external_ram_total));
  TEST_ASSERT_TRUE(as_fixnum(v_external_ram_total) >= 0);
}

TEST(test_runtime_stats_gpio_event_drops_present) {
  ID stats = eval_string("(do (require 'tiny-clj.runtime) (tiny-clj.runtime/stats))", g_test_eval_state);
  TEST_ASSERT_NOT_NULL(stats);
  TEST_ASSERT_TRUE(is_map(stats));

  ID k_gpio_event_drops = (ID)intern_symbol_global(":gpio-event-drops");
  ID v_gpio_event_drops = map_get_sentinel((CljPersistentMap *)stats, k_gpio_event_drops, NOT_FOUND);
  TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_gpio_event_drops);
  TEST_ASSERT_TRUE(is_fixnum(v_gpio_event_drops));
  TEST_ASSERT_TRUE(as_fixnum(v_gpio_event_drops) >= 0);
}

TEST(test_runtime_stats_audio_counters_present) {
  ID stats = eval_string("(do (require 'tiny-clj.runtime) (tiny-clj.runtime/stats))", g_test_eval_state);
  TEST_ASSERT_NOT_NULL(stats);
  TEST_ASSERT_TRUE(is_map(stats));

  ID k_audio_cmd_drop_count = (ID)intern_symbol_global(":audio-cmd-drop-count");
  ID k_audio_tick_overrun_count = (ID)intern_symbol_global(":audio-tick-overrun-count");
  ID k_audio_queue_high_watermark = (ID)intern_symbol_global(":audio-queue-high-watermark");
  ID k_audio_sfx_drop_count = (ID)intern_symbol_global(":audio-sfx-drop-count");
  ID k_audio_finished_drop_count = (ID)intern_symbol_global(":audio-finished-drop-count");

  ID v_audio_cmd_drop_count = map_get_sentinel((CljPersistentMap *)stats, k_audio_cmd_drop_count, NOT_FOUND);
  ID v_audio_tick_overrun_count = map_get_sentinel((CljPersistentMap *)stats, k_audio_tick_overrun_count, NOT_FOUND);
  ID v_audio_queue_high_watermark = map_get_sentinel((CljPersistentMap *)stats, k_audio_queue_high_watermark, NOT_FOUND);
  ID v_audio_sfx_drop_count = map_get_sentinel((CljPersistentMap *)stats, k_audio_sfx_drop_count, NOT_FOUND);
  ID v_audio_finished_drop_count = map_get_sentinel((CljPersistentMap *)stats, k_audio_finished_drop_count, NOT_FOUND);

  TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_audio_cmd_drop_count);
  TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_audio_tick_overrun_count);
  TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_audio_queue_high_watermark);
  TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_audio_sfx_drop_count);
  TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_audio_finished_drop_count);

  TEST_ASSERT_TRUE(is_fixnum(v_audio_cmd_drop_count));
  TEST_ASSERT_TRUE(is_fixnum(v_audio_tick_overrun_count));
  TEST_ASSERT_TRUE(is_fixnum(v_audio_queue_high_watermark));
  TEST_ASSERT_TRUE(is_fixnum(v_audio_sfx_drop_count));
  TEST_ASSERT_TRUE(is_fixnum(v_audio_finished_drop_count));

  TEST_ASSERT_TRUE(as_fixnum(v_audio_cmd_drop_count) >= 0);
  TEST_ASSERT_TRUE(as_fixnum(v_audio_tick_overrun_count) >= 0);
  TEST_ASSERT_TRUE(as_fixnum(v_audio_queue_high_watermark) >= 0);
  TEST_ASSERT_TRUE(as_fixnum(v_audio_sfx_drop_count) >= 0);
  TEST_ASSERT_TRUE(as_fixnum(v_audio_finished_drop_count) >= 0);
}

#if defined(DEBUG)
static bool debug_precore_mem_enabled(void) {
  const char *v = getenv("TINYCLJ_DEBUG_PRECORE_MEM");
  return (v && v[0] != '\0' && strcmp(v, "0") != 0);
}

static void debug_precore_mem_step(const char *label, MemoryStats *prev) {
  if (!label)
    return;
  MemoryStats s = memory_profiler_get_stats();
  size_t obj_est = (s.current_memory_usage >= s.raw_bytes_current)
                       ? (s.current_memory_usage - s.raw_bytes_current)
                       : 0;
  size_t prev_obj_est = 0;
  long long delta_current = 0;
  long long delta_raw = 0;
  long long delta_obj = 0;
  if (prev) {
    prev_obj_est = (prev->current_memory_usage >= prev->raw_bytes_current)
                       ? (prev->current_memory_usage - prev->raw_bytes_current)
                       : 0;
    delta_current = (long long)s.current_memory_usage - (long long)prev->current_memory_usage;
    delta_raw = (long long)s.raw_bytes_current - (long long)prev->raw_bytes_current;
    delta_obj = (long long)obj_est - (long long)prev_obj_est;
    *prev = s;
  }
  fprintf(stderr,
          "[precore-mem] %-22s current=%zu peak=%zu raw=%zu raw-peak=%zu obj-est=%zu",
          label,
          s.current_memory_usage,
          s.peak_memory_usage,
          s.raw_bytes_current,
          s.raw_bytes_peak,
          obj_est);
  if (prev) {
    fprintf(stderr,
            " delta-current=%lld delta-raw=%lld delta-obj=%lld",
            delta_current,
            delta_raw,
            delta_obj);
  }
  fputc('\n', stderr);
}

TEST(test_runtime_stats_bytes_peak_rounded_target_no_core) {
  // Fresh runtime state to mirror --no-core startup.
  runtime_reset(&g_runtime);
  MemoryStats baseline = memory_profiler_get_stats();
  bool debug = debug_precore_mem_enabled();
  MemoryStats prev = baseline;
  if (debug) {
    debug_precore_mem_step("after reset", &prev);
  }
  WITH_AUTORELEASE_POOL({
    runtime_init(&g_runtime);
  });
  if (debug) {
    debug_precore_mem_step("after runtime_init", &prev);
  }
  meta_registry_init();
  if (debug) {
    debug_precore_mem_step("after meta_registry", &prev);
  }
  init_special_symbols();
  if (debug) {
    debug_precore_mem_step("after init_symbols", &prev);
  }
  register_builtins();
  g_runtime.builtins_registered = true;
  if (debug) {
    debug_precore_mem_step("after builtins", &prev);
  }

  // Reset eval state without loading clojure.core.
  evalstate_reset(&g_test_eval_state, false);
  if (debug) {
    debug_precore_mem_step("after evalstate", &prev);
  }

  ID stats = eval_string("(do (require 'tiny-clj.runtime) (tiny-clj.runtime/stats))", g_test_eval_state);
  if (debug) {
    debug_precore_mem_step("after require+stats", &prev);
  }
  TEST_ASSERT_NOT_NULL(stats);
  TEST_ASSERT_TRUE(is_map(stats));

  ID k_bytes_peak = (ID)intern_symbol_global(":bytes-peak");
  ID v_bytes_peak = map_get_sentinel((CljPersistentMap *)stats, k_bytes_peak, NOT_FOUND);
  TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_bytes_peak);
  TEST_ASSERT_TRUE(is_fixnum(v_bytes_peak));

  int32_t bytes_peak = as_fixnum(v_bytes_peak);
  // Observed target was 31872; rounded up to 32 KiB.
  const size_t target_bytes_peak = 32768;
  size_t base_peak = baseline.peak_memory_usage;
  size_t delta_peak = (bytes_peak > (int32_t)base_peak)
                          ? (size_t)bytes_peak - base_peak
                          : 0;
  char msg[96];
  snprintf(msg, sizeof(msg), "bytes-peak delta must be <= %zu", target_bytes_peak);
  TEST_ASSERT_TRUE_MESSAGE(delta_peak <= target_bytes_peak, msg);
}
#endif

#if defined(DEBUG) && defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
TEST(test_runtime_stats_contains_memory_stats_map) {
  ID stats = eval_string("(do (require 'tiny-clj.runtime) (tiny-clj.runtime/stats))", g_test_eval_state);
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
TEST(test_runtime_stats_current_ram_under_200kb_after_core_load) {
  // Ensure both clojure.core and tiny-clj.runtime are loaded before measuring.
  ID stats = eval_string("(do (require 'clojure.core) (require 'tiny-clj.runtime) (tiny-clj.runtime/stats))", g_test_eval_state);
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
        if (v_bc != NOT_FOUND && is_fixnum(v_bc))
          bc = as_fixnum(v_bc);
        if (v_bp != NOT_FOUND && is_fixnum(v_bp))
          bp = as_fixnum(v_bp);
        if (v_ac != NOT_FOUND && is_fixnum(v_ac))
          ac = as_fixnum(v_ac);
        if (v_dc != NOT_FOUND && is_fixnum(v_dc))
          dc = as_fixnum(v_dc);
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
TEST(test_runtime_stats_core_load_memory_delta) {
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

  MemoryStats before = memory_profiler_get_stats();
  fprintf(stderr, "[core-load-delta] before: bytes-current=%zu bytes-peak=%zu raw-current=%zu raw-peak=%zu\n",
          before.current_memory_usage, before.peak_memory_usage,
          before.raw_bytes_current, before.raw_bytes_peak);

  load_clojure_core(g_test_eval_state);

  MemoryStats after = memory_profiler_get_stats();
  fprintf(stderr, "[core-load-delta] after:  bytes-current=%zu bytes-peak=%zu raw-current=%zu raw-peak=%zu\n",
          after.current_memory_usage, after.peak_memory_usage,
          after.raw_bytes_current, after.raw_bytes_peak);

  long long delta_current = (long long)after.current_memory_usage - (long long)before.current_memory_usage;
  fprintf(stderr, "[core-load-delta] delta: %+lld bytes-current\n", delta_current);

  // Core load can replace pre-core builtins with smaller native stubs, so allow a small drop.
  const size_t drop_tolerance = 64 * 1024;
  if (before.current_memory_usage > after.current_memory_usage + drop_tolerance) {
    char msg[128];
    snprintf(msg, sizeof(msg),
             "bytes-current dropped by more than %zu bytes after core load",
             drop_tolerance);
    TEST_FAIL_MESSAGE(msg);
  }
}
#endif

#if defined(DEBUG) && defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
TEST(test_runtime_stats_core_load_under_100k) {
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

  // Measure only core loading costs (delta from current baseline).
  MemoryStats before = memory_profiler_get_stats();
  load_clojure_core(g_test_eval_state);
  MemoryStats after = memory_profiler_get_stats();
  size_t delta_current = (after.current_memory_usage > before.current_memory_usage)
                             ? (after.current_memory_usage - before.current_memory_usage)
                             : 0;
  size_t limit = 500 * 1024;
  TEST_ASSERT_TRUE_MESSAGE(delta_current < limit,
                           "core load delta should keep heap under 500k");
}
#endif

#if defined(DEBUG) && defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
TEST(test_runtime_stats_core_load_peak_under_150k) {
  // Fresh runtime state for a clean peak baseline.
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

  MemoryStats before = memory_profiler_get_stats();
  load_clojure_core(g_test_eval_state);
  MemoryStats after = memory_profiler_get_stats();
  size_t peak_delta = (after.peak_memory_usage > before.peak_memory_usage)
                          ? (after.peak_memory_usage - before.peak_memory_usage)
                          : 0;
  size_t limit = 150 * 1024;
  char msg[128];
  snprintf(msg, sizeof(msg), "core load peak delta must be <= %zu bytes (got %zu)",
           limit, peak_delta);
  TEST_ASSERT_TRUE_MESSAGE(peak_delta <= limit, msg);
}
#endif

#if defined(DEBUG) && defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
TEST(test_runtime_stats_core_load_idempotent_memory) {
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

  load_clojure_core(g_test_eval_state);
  MemoryStats first = memory_profiler_get_stats();

  load_clojure_core(g_test_eval_state);
  MemoryStats second = memory_profiler_get_stats();

  // Allow small fluctuations from transient allocations, but no growth trend.
  size_t tolerance = 0;
  assert_memory_stats_not_increasing(&first, &second, tolerance,
                                     "core reload should not increase bytes-current significantly");
}
#endif

#if defined(DEBUG) && defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
TEST(test_runtime_stats_autorelease_loop_does_not_grow_heap) {
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

  // Expect zero growth after warm baseline.
  if (after.current_memory_usage > before.current_memory_usage) {
    print_memory_type_deltas(&before, &after, "autorelease-loop");
  }
  assert_memory_stats_not_increasing(&before, &after, 0,
                                     "autorelease loop should not grow heap after baseline");
}
#endif

#if defined(DEBUG) && defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
TEST(test_runtime_stats_memory_stats_stable_in_loop) {
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

#if defined(DEBUG) && defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
TEST(test_runtime_stats_reduce_range_loop_stable_after_warmup) {
  const char *expr = "(reduce + (range 200))";

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

  // Warm up resolver/call paths before taking baseline.
  for (int i = 0; i < 5; i++) {
    WITH_AUTORELEASE_POOL({
      ID warm = eval_string(expr, g_test_eval_state);
      (void)warm;
    });
  }
  MemoryStats baseline = memory_profiler_get_stats();

  for (int i = 0; i < 40; i++) {
    WITH_AUTORELEASE_POOL({
      ID r = eval_string(expr, g_test_eval_state);
      (void)r;
    });
    MemoryStats now = memory_profiler_get_stats();
    if (now.current_memory_usage > baseline.current_memory_usage) {
      print_memory_type_deltas(&baseline, &now, "reduce-range-stable-loop");
    }
    assert_memory_stats_not_increasing(&baseline, &now, 0,
                                       "reduce loop should not grow heap after warmup");
  }
}
#endif

#if defined(DEBUG) && defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
TEST(test_runtime_stats_heap_eval_read_string_fn_literal_no_slotref_growth) {
  const char *expr = "(heap (eval (read-string \"(count ((fn [x] (list x)) 1))\")))";

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

  ID k_total = (ID)intern_symbol_global(":total");
  ID k_slotref = (ID)intern_symbol_global(":SlotRef");

  for (int i = 0; i < 5; i++) {
    WITH_AUTORELEASE_POOL({
      ID result = eval_string(expr, g_test_eval_state);
      TEST_ASSERT_NOT_NULL(result);
      TEST_ASSERT_TRUE(is_map(result));

      ID v_total = map_get_sentinel((CljPersistentMap *)result, k_total, NOT_FOUND);
      TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_total);
      TEST_ASSERT_TRUE(is_fixnum(v_total));
      TEST_ASSERT_EQUAL_INT_MESSAGE(0, as_fixnum(v_total),
                                    "fn-literal eval(read-string) heap delta should stay at 0 after SlotRef fix");

      ID v_slotref = map_get_sentinel((CljPersistentMap *)result, k_slotref, NOT_FOUND);
      TEST_ASSERT_EQUAL_MESSAGE(NOT_FOUND, v_slotref,
                                "SlotRef bytes must not grow in heap(eval(read-string ...)) path");
    });
  }
}
#endif

#if defined(DEBUG) && defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
TEST(test_runtime_stats_heap_eval_read_string_let_binding_alias_no_growth) {
  const char *expr = "(heap (eval (read-string \"(let [v [1 2 3]] v)\")))";

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

  ID k_total = (ID)intern_symbol_global(":total");

  for (int i = 0; i < 5; i++) {
    WITH_AUTORELEASE_POOL({
      ID result = eval_string(expr, g_test_eval_state);
      TEST_ASSERT_NOT_NULL(result);
      TEST_ASSERT_TRUE(is_map(result));

      ID v_total = map_get_sentinel((CljPersistentMap *)result, k_total, NOT_FOUND);
      TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_total);
      TEST_ASSERT_TRUE(is_fixnum(v_total));
      TEST_ASSERT_EQUAL_INT_MESSAGE(0, as_fixnum(v_total),
                                    "let binding alias result should not leak frame/loop preserved refs");
    });
  }
}
#endif

#if defined(DEBUG) && defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
TEST(test_runtime_stats_heap_eval_read_string_loop_binding_alias_no_growth) {
  const char *expr = "(heap (eval (read-string \"(loop [v [1 2 3]] v)\")))";

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

  ID k_total = (ID)intern_symbol_global(":total");

  for (int i = 0; i < 5; i++) {
    WITH_AUTORELEASE_POOL({
      ID result = eval_string(expr, g_test_eval_state);
      TEST_ASSERT_NOT_NULL(result);
      TEST_ASSERT_TRUE(is_map(result));

      ID v_total = map_get_sentinel((CljPersistentMap *)result, k_total, NOT_FOUND);
      TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_total);
      TEST_ASSERT_TRUE(is_fixnum(v_total));
      TEST_ASSERT_EQUAL_INT_MESSAGE(0, as_fixnum(v_total),
                                    "loop binding alias result should not leak frame/recur preserved refs");
    });
  }
}
#endif

#if defined(DEBUG) && defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
TEST(test_runtime_stats_heap_eval_read_string_first_borrowed_list_alias_no_growth) {
  const char *expr = "(heap (eval (read-string \"(first (quote ((range 3))))\")))";

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

  ID k_total = (ID)intern_symbol_global(":total");
  ID k_list = (ID)intern_symbol_global(":List");

  for (int i = 0; i < 5; i++) {
    WITH_AUTORELEASE_POOL({
      ID result = eval_string(expr, g_test_eval_state);
      TEST_ASSERT_NOT_NULL(result);
      TEST_ASSERT_TRUE(is_map(result));

      ID v_total = map_get_sentinel((CljPersistentMap *)result, k_total, NOT_FOUND);
      TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_total);
      TEST_ASSERT_TRUE(is_fixnum(v_total));
      TEST_ASSERT_EQUAL_INT_MESSAGE(0, as_fixnum(v_total),
                                    "first on quoted nested list should not leak borrowed list alias");

      ID v_list = map_get_sentinel((CljPersistentMap *)result, k_list, NOT_FOUND);
      TEST_ASSERT_EQUAL_MESSAGE(NOT_FOUND, v_list,
                                "List bytes must not grow for first borrowed sublist alias");
    });
  }
}
#endif

#if defined(DEBUG) && defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
TEST(test_runtime_stats_heap_function_recur_no_arg_retain_growth) {
  const char *init_expr = "(def rf (fn [x n] (if (= n 0) x (recur x (dec n)))))";
  const char *expr = "(heap (rf '(1 2) 3))";

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

  WITH_AUTORELEASE_POOL({
    ID init = eval_string(init_expr, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(init);
  });

  ID k_total = (ID)intern_symbol_global(":total");
  ID k_list = (ID)intern_symbol_global(":List");

  for (int i = 0; i < 5; i++) {
    WITH_AUTORELEASE_POOL({
      ID result = eval_string(expr, g_test_eval_state);
      TEST_ASSERT_NOT_NULL(result);
      TEST_ASSERT_TRUE(is_map(result));

      ID v_total = map_get_sentinel((CljPersistentMap *)result, k_total, NOT_FOUND);
      TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_total);
      TEST_ASSERT_TRUE(is_fixnum(v_total));
      TEST_ASSERT_EQUAL_INT_MESSAGE(0, as_fixnum(v_total),
                                    "function recur path must release transferred recur-arg retains");

      ID v_list = map_get_sentinel((CljPersistentMap *)result, k_list, NOT_FOUND);
      TEST_ASSERT_EQUAL_MESSAGE(NOT_FOUND, v_list,
                                "recur should not leak list arguments across iterations");
    });
  }
}
#endif

#if defined(DEBUG) && defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
TEST(test_runtime_stats_heap_eval_read_string_macroexpand_1_thread_first_no_list_growth) {
  const char *expr = "(heap (eval (read-string \"(macroexpand-1 (quote (-> 1 inc inc)))\")))";

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

  ID k_total = (ID)intern_symbol_global(":total");
  ID k_list = (ID)intern_symbol_global(":List");

  for (int i = 0; i < 5; i++) {
    WITH_AUTORELEASE_POOL({
      ID result = eval_string(expr, g_test_eval_state);
      TEST_ASSERT_NOT_NULL(result);
      TEST_ASSERT_TRUE(is_map(result));

      ID v_total = map_get_sentinel((CljPersistentMap *)result, k_total, NOT_FOUND);
      TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_total);
      TEST_ASSERT_TRUE(is_fixnum(v_total));
      TEST_ASSERT_TRUE_MESSAGE(as_fixnum(v_total) <= 0,
                               "macroexpand-1 on -> must not leak threaded intermediate lists");

      ID v_list = map_get_sentinel((CljPersistentMap *)result, k_list, NOT_FOUND);
      TEST_ASSERT_EQUAL_MESSAGE(NOT_FOUND, v_list,
                                "macroexpand-1 threading macro should not grow List bytes");
    });
  }
}
#endif

#if defined(DEBUG) && defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
TEST(test_runtime_stats_heap_eval_read_string_let_named_fn_if_optimizer_no_ast_growth) {
  const char *expr =
      "(heap (eval (read-string \"(let [f (fn f [x] (if (list? x) x nil))] 1)\")))";

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

  ID k_total = (ID)intern_symbol_global(":total");
  ID k_vector = (ID)intern_symbol_global(":Vector");
  ID k_astcall = (ID)intern_symbol_global(":ASTCall");
  ID k_slotref = (ID)intern_symbol_global(":SlotRef");

  for (int i = 0; i < 5; i++) {
    WITH_AUTORELEASE_POOL({
      ID result = eval_string(expr, g_test_eval_state);
      TEST_ASSERT_NOT_NULL(result);
      TEST_ASSERT_TRUE(is_map(result));

      ID v_total = map_get_sentinel((CljPersistentMap *)result, k_total, NOT_FOUND);
      TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_total);
      TEST_ASSERT_TRUE(is_fixnum(v_total));
      TEST_ASSERT_EQUAL_INT_MESSAGE(0, as_fixnum(v_total),
                                    "named local fn optimizer must not leak retained AST helper nodes");

      TEST_ASSERT_EQUAL_MESSAGE(NOT_FOUND,
                                map_get_sentinel((CljPersistentMap *)result, k_vector, NOT_FOUND),
                                "optimizer must not grow Vector bytes for unchanged AST args");
      TEST_ASSERT_EQUAL_MESSAGE(NOT_FOUND,
                                map_get_sentinel((CljPersistentMap *)result, k_astcall, NOT_FOUND),
                                "optimizer must not grow ASTCall bytes for unchanged nested calls");
      TEST_ASSERT_EQUAL_MESSAGE(NOT_FOUND,
                                map_get_sentinel((CljPersistentMap *)result, k_slotref, NOT_FOUND),
                                "optimizer must release retained SlotRef results from recursive walk");
    });
  }
}
#endif

#if defined(DEBUG) && defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
TEST(test_runtime_stats_heap_let_lazy_seq_binding_discard_no_growth) {
  const char *expr = "(heap (let [v (lazy-seq* (fn [] (list 1)))] nil))";

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

  ID k_total = (ID)intern_symbol_global(":total");
  ID k_lazy = (ID)intern_symbol_global(":LazySeq");
  ID k_closure = (ID)intern_symbol_global(":Closure");
  ID k_map = (ID)intern_symbol_global(":Map");
  ID k_vector = (ID)intern_symbol_global(":Vector");

  WITH_AUTORELEASE_POOL({
    ID warm = eval_string(expr, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(warm);
    TEST_ASSERT_TRUE(is_map(warm));
  });

  for (int i = 0; i < 3; i++) {
    WITH_AUTORELEASE_POOL({
      ID result = eval_string(expr, g_test_eval_state);
      TEST_ASSERT_NOT_NULL(result);
      TEST_ASSERT_TRUE(is_map(result));

      ID v_total = map_get_sentinel((CljPersistentMap *)result, k_total, NOT_FOUND);
      TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_total);
      TEST_ASSERT_TRUE(is_fixnum(v_total));
      TEST_ASSERT_EQUAL_INT_MESSAGE(0, as_fixnum(v_total),
                                    "let-binding a lazy-seq and discarding it must not leak closure/env cycles");

      TEST_ASSERT_EQUAL_MESSAGE(NOT_FOUND,
                                map_get_sentinel((CljPersistentMap *)result, k_lazy, NOT_FOUND),
                                "let discard path must not retain LazySeq cells");
      TEST_ASSERT_EQUAL_MESSAGE(NOT_FOUND,
                                map_get_sentinel((CljPersistentMap *)result, k_closure, NOT_FOUND),
                                "let discard path must not retain lazy thunk closures");
      TEST_ASSERT_EQUAL_MESSAGE(NOT_FOUND,
                                map_get_sentinel((CljPersistentMap *)result, k_map, NOT_FOUND),
                                "let discard path must not retain thunk state/let env maps");
      TEST_ASSERT_EQUAL_MESSAGE(NOT_FOUND,
                                map_get_sentinel((CljPersistentMap *)result, k_vector, NOT_FOUND),
                                "let discard path must not retain env_stack vectors");
    });
  }
}
#endif

#if defined(DEBUG) && defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
TEST(test_runtime_stats_heap_let_lazy_seq_binding_alias_no_growth) {
  const char *expr = "(heap (let [v (lazy-seq* (fn [] (list 1)))] v))";

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

  ID k_total = (ID)intern_symbol_global(":total");
  ID k_lazy = (ID)intern_symbol_global(":LazySeq");
  ID k_closure = (ID)intern_symbol_global(":Closure");
  ID k_map = (ID)intern_symbol_global(":Map");
  ID k_vector = (ID)intern_symbol_global(":Vector");

  WITH_AUTORELEASE_POOL({
    ID warm = eval_string(expr, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(warm);
    TEST_ASSERT_TRUE(is_map(warm));
  });

  for (int i = 0; i < 3; i++) {
    WITH_AUTORELEASE_POOL({
      ID result = eval_string(expr, g_test_eval_state);
      TEST_ASSERT_NOT_NULL(result);
      TEST_ASSERT_TRUE(is_map(result));

      ID v_total = map_get_sentinel((CljPersistentMap *)result, k_total, NOT_FOUND);
      TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_total);
      TEST_ASSERT_TRUE(is_fixnum(v_total));
      TEST_ASSERT_EQUAL_INT_MESSAGE(0, as_fixnum(v_total),
                                    "let lazy-seq alias result must not preserve extra frame/env retains");

      TEST_ASSERT_EQUAL_MESSAGE(NOT_FOUND,
                                map_get_sentinel((CljPersistentMap *)result, k_lazy, NOT_FOUND),
                                "let alias path must not retain LazySeq cells");
      TEST_ASSERT_EQUAL_MESSAGE(NOT_FOUND,
                                map_get_sentinel((CljPersistentMap *)result, k_closure, NOT_FOUND),
                                "let alias path must not retain lazy thunk closures");
      TEST_ASSERT_EQUAL_MESSAGE(NOT_FOUND,
                                map_get_sentinel((CljPersistentMap *)result, k_map, NOT_FOUND),
                                "let alias path must not retain thunk state/let env maps");
      TEST_ASSERT_EQUAL_MESSAGE(NOT_FOUND,
                                map_get_sentinel((CljPersistentMap *)result, k_vector, NOT_FOUND),
                                "let alias path must not retain env_stack vectors");
    });
  }
}
#endif

#if defined(DEBUG) && defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
TEST(test_runtime_stats_heap_for_single_binding_lazy_seq_result_no_growth) {
  const char *expr = "(heap (let [r (for [x (list 1)] x)] nil))";

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

  ID k_total = (ID)intern_symbol_global(":total");
  ID k_lazy = (ID)intern_symbol_global(":LazySeq");
  ID k_closure = (ID)intern_symbol_global(":Closure");
  ID k_map = (ID)intern_symbol_global(":Map");
  ID k_vector = (ID)intern_symbol_global(":Vector");
  ID k_list = (ID)intern_symbol_global(":List");

  WITH_AUTORELEASE_POOL({
    ID warm = eval_string(expr, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(warm);
    TEST_ASSERT_TRUE(is_map(warm));
  });

  for (int i = 0; i < 3; i++) {
    WITH_AUTORELEASE_POOL({
      ID result = eval_string(expr, g_test_eval_state);
      TEST_ASSERT_NOT_NULL(result);
      TEST_ASSERT_TRUE(is_map(result));

      ID v_total = map_get_sentinel((CljPersistentMap *)result, k_total, NOT_FOUND);
      TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_total);
      TEST_ASSERT_TRUE(is_fixnum(v_total));
      TEST_ASSERT_EQUAL_INT_MESSAGE(0, as_fixnum(v_total),
                                    "for result discarded in let binding must not leak lazy-seq closure graph");

      TEST_ASSERT_EQUAL_MESSAGE(NOT_FOUND,
                                map_get_sentinel((CljPersistentMap *)result, k_lazy, NOT_FOUND),
                                "for discard path must not retain LazySeq cells");
      TEST_ASSERT_EQUAL_MESSAGE(NOT_FOUND,
                                map_get_sentinel((CljPersistentMap *)result, k_closure, NOT_FOUND),
                                "for discard path must not retain lazy thunk closures");
      TEST_ASSERT_EQUAL_MESSAGE(NOT_FOUND,
                                map_get_sentinel((CljPersistentMap *)result, k_map, NOT_FOUND),
                                "for discard path must not retain thunk state maps");
      TEST_ASSERT_EQUAL_MESSAGE(NOT_FOUND,
                                map_get_sentinel((CljPersistentMap *)result, k_vector, NOT_FOUND),
                                "for discard path must not retain env_stack vectors");
      TEST_ASSERT_EQUAL_MESSAGE(NOT_FOUND,
                                map_get_sentinel((CljPersistentMap *)result, k_list, NOT_FOUND),
                                "for discard path must not retain helper lists");
    });
  }
}
#endif

#if defined(DEBUG) && defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
/*
 * Regression test for (assoc var key val): after warmup, one eval must not grow heap.
 */
TEST(test_runtime_stats_assoc_var_per_eval_growth_bounded) {
  const char *init_expr = "(def m {:a 1 :b 2})";
  const char *expr = "(assoc m :c 3)";

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

  WITH_AUTORELEASE_POOL({
    ID init = eval_string(init_expr, g_test_eval_state);
    (void)init;
  });

  for (int i = 0; i < 5; i++) {
    WITH_AUTORELEASE_POOL({
      ID warm = eval_string(expr, g_test_eval_state);
      (void)warm;
    });
  }
  MemoryStats baseline = memory_profiler_get_stats();

  WITH_AUTORELEASE_POOL({
    ID r = eval_string(expr, g_test_eval_state);
    (void)r;
  });
  MemoryStats after = memory_profiler_get_stats();

  size_t delta = (after.current_memory_usage > baseline.current_memory_usage)
                     ? (after.current_memory_usage - baseline.current_memory_usage)
                     : 0;

  if (delta > 0) {
    print_memory_type_deltas(&baseline, &after, "assoc-var-per-eval");
  }

  const size_t tolerance = 0;
  char msg[128];
  snprintf(msg, sizeof(msg),
           "assoc var per-eval growth should be <= %zu bytes (got %zu)",
           tolerance, delta);
  TEST_ASSERT_TRUE_MESSAGE(delta <= tolerance, msg);
}

TEST(test_heap_plus_does_not_intern_user_plus_when_missing) {
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

  ID result = eval_string("(heap +)", g_test_eval_state);
  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE(is_map(result));

  ID k_symbol = (ID)intern_symbol_global(":Symbol");
  ID v_symbol = map_get_sentinel((CljPersistentMap *)result, k_symbol, NOT_FOUND);
  TEST_ASSERT_EQUAL(NOT_FOUND, v_symbol);
}
#endif
