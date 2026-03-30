#include "tests_common.h"
#include "../builtins.h"
#include "../event_loop.h"
#include "../fx_host_runloop.h"
#include "../rendered_state_snapshot.h"
#include "../source_resolver.h"
#include "../vector_scene_graph.h"

TEST(test_breakout_contract_runloop_liveness_treats_blocking_wait_as_healthy) {
    fx_runloop_liveness_reset();
    fx_runloop_liveness_note_progress_for_tests(1ull * 1000ull * 1000ull * 1000ull);
    atomic_store_explicit(&g_runloop_thread.blocked_in_event_loop_wait, true, memory_order_relaxed);

    ViewerRunloopLivenessSnapshot waiting_snapshot =
        fx_runloop_liveness_snapshot(7ull * 1000ull * 1000ull * 1000ull);
    TEST_ASSERT_EQUAL_UINT64(6ull * 1000ull * 1000ull * 1000ull, waiting_snapshot.age_ns);
    TEST_ASSERT_EQUAL_INT(FX_RUNLOOP_LIVENESS_HEALTHY, waiting_snapshot.state);

    atomic_store_explicit(&g_runloop_thread.blocked_in_event_loop_wait, false, memory_order_relaxed);
    ViewerRunloopLivenessSnapshot stalled_snapshot =
        fx_runloop_liveness_snapshot(7ull * 1000ull * 1000ull * 1000ull);
    TEST_ASSERT_EQUAL_INT(FX_RUNLOOP_LIVENESS_STALLED, stalled_snapshot.state);
}

TEST(test_breakout_contract_audio_namespace_loads_tiny_fx_sound_runtime) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    (void)eval_string("(ns-unload 'tiny-breakout.audio)", g_test_eval_state);
    (void)eval_string("(ns-unload 'tiny-fx.sound)", g_test_eval_state);
    CljNamespace *sound_ns_before = ns_find("tiny-fx.sound");
    TEST_ASSERT_NULL(sound_ns_before);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.audio :reload) "
        "  (tiny-breakout.audio/play-events! [:brick-hit :victory]) "
        "  true)",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);

    CljNamespace *sound_ns_after = ns_find("tiny-fx.sound");
    TEST_ASSERT_NOT_NULL_MESSAGE(sound_ns_after,
                                 "tiny-breakout.audio should load tiny-fx.sound for runtime SFX playback");
}

TEST(test_breakout_contract_runtime_namespace_reload_is_reclaimable) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    WITH_AUTORELEASE_POOL({
        ID bytes = resolve_path_to_bytes("/libs/tiny-breakout/runtime.clj");
        TEST_ASSERT_NOT_NULL(bytes);
        TEST_ASSERT_TRUE(load_namespace_from_bytes(g_test_eval_state,
                                                   "tiny-breakout.runtime",
                                                   bytes,
                                                   "/libs/tiny-breakout/runtime.clj"));
    });

    MemoryStats before = memory_profiler_get_stats();
    for (int i = 0; i < 5; i++) {
        WITH_AUTORELEASE_POOL({
            ID bytes = resolve_path_to_bytes("/libs/tiny-breakout/runtime.clj");
            TEST_ASSERT_NOT_NULL(bytes);
            CljNamespace *ns = ns_find("tiny-breakout.runtime");
            TEST_ASSERT_NOT_NULL(ns);
            ns->loaded = false;
            ns->loading = false;
            TEST_ASSERT_TRUE(load_namespace_from_bytes(g_test_eval_state,
                                                       "tiny-breakout.runtime",
                                                       bytes,
                                                       "/libs/tiny-breakout/runtime.clj"));
        });
    }
    MemoryStats after = memory_profiler_get_stats();

    long long diff = (long long)after.current_memory_usage - (long long)before.current_memory_usage;
    long long tracked_before = 0;
    long long tracked_after = 0;
    for (int i = 0; i < CLJ_TYPE_COUNT; i++) {
        tracked_before += (long long)before.bytes_current_by_type[i];
        tracked_after += (long long)after.bytes_current_by_type[i];
    }
    long long tracked_diff = tracked_after - tracked_before;
    long long raw_diff = (long long)after.raw_bytes_current - (long long)before.raw_bytes_current;
    long long raw_blocks_diff = (long long)after.raw_blocks_current - (long long)before.raw_blocks_current;
    fprintf(stderr, "\nBreakout runtime leak after 5 reloads: %lld bytes\n", diff);
    fprintf(stderr, "  tracked object diff: %lld bytes\n", tracked_diff);
    fprintf(stderr, "  raw/native diff (profiler): %lld bytes\n", raw_diff);
    fprintf(stderr, "  raw blocks diff: %lld\n", raw_blocks_diff);
    fprintf(stderr, "  untracked/native diff: %lld bytes\n", diff - tracked_diff);
    for (int i = 0; i < CLJ_TYPE_COUNT; i++) {
        long long type_diff = (long long)after.bytes_current_by_type[i] -
                              (long long)before.bytes_current_by_type[i];
        if (type_diff != 0) {
            fprintf(stderr, "  %s: %lld bytes\n", clj_type_name((CljType)i), type_diff);
        }
    }
    TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(131072, (int)diff,
                                          "tiny-breakout.runtime reload path should not leak unboundedly");
}

static long long breakout_reload_diff_for(const char *ns_name,
                                          const char *source_path,
                                          int reload_count) {
    WITH_AUTORELEASE_POOL({
        ID bytes = resolve_path_to_bytes(source_path);
        TEST_ASSERT_NOT_NULL(bytes);
        TEST_ASSERT_TRUE(load_namespace_from_bytes(g_test_eval_state, ns_name, bytes, source_path));
    });

    MemoryStats before = memory_profiler_get_stats();
    for (int i = 0; i < reload_count; i++) {
        WITH_AUTORELEASE_POOL({
            ID bytes = resolve_path_to_bytes(source_path);
            TEST_ASSERT_NOT_NULL(bytes);
            CljNamespace *ns = ns_find(ns_name);
            TEST_ASSERT_NOT_NULL(ns);
            ns->loaded = false;
            ns->loading = false;
            TEST_ASSERT_TRUE(load_namespace_from_bytes(g_test_eval_state, ns_name, bytes, source_path));
        });
    }
    MemoryStats after = memory_profiler_get_stats();
    long long diff = (long long)after.current_memory_usage - (long long)before.current_memory_usage;
    long long raw_diff = (long long)after.raw_bytes_current - (long long)before.raw_bytes_current;
    fprintf(stderr, "  %s reload x%d => total %+lld bytes, raw %+lld bytes\n",
            ns_name, reload_count, diff, raw_diff);
    return diff;
}

TEST(test_breakout_contract_namespace_reload_memory_profile_breakdown) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    fprintf(stderr, "\nBreakout namespace reload memory profile:\n");
    long long core_diff = breakout_reload_diff_for("tiny-breakout.core",
                                                   "/libs/tiny-breakout/core.clj",
                                                   5);
    long long scene_diff = breakout_reload_diff_for("tiny-breakout.scene",
                                                    "/libs/tiny-breakout/scene.clj",
                                                    5);
    long long audio_diff = breakout_reload_diff_for("tiny-breakout.audio",
                                                    "/libs/tiny-breakout/audio.clj",
                                                    5);
    long long runtime_diff = breakout_reload_diff_for("tiny-breakout.runtime",
                                                      "/libs/tiny-breakout/runtime.clj",
                                                      5);
    TEST_ASSERT_TRUE_MESSAGE(core_diff <= 131072 &&
                             scene_diff <= 131072 &&
                             audio_diff <= 131072 &&
                             runtime_diff <= 131072,
                             "reload profile indicates unexpectedly large retained growth");
}

TEST(test_breakout_contract_runtime_source_bytes_are_reclaimable) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    MemoryStats before = memory_profiler_get_stats();
    for (int i = 0; i < 5; i++) {
        WITH_AUTORELEASE_POOL({
            ID bytes = resolve_path_to_bytes("/libs/tiny-breakout/runtime.clj");
            TEST_ASSERT_NOT_NULL(bytes);
            TEST_ASSERT_EQUAL_INT(CLJ_BYTE_ARRAY, TAG(bytes));
        });
    }
    MemoryStats after = memory_profiler_get_stats();
    long long total_diff = (long long)after.current_memory_usage - (long long)before.current_memory_usage;
    long long raw_diff = (long long)after.raw_bytes_current - (long long)before.raw_bytes_current;
    fprintf(stderr,
            "\nBreakout runtime source-byte reclaim check: total %+lld, raw %+lld bytes\n",
            total_diff, raw_diff);
    TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(4096, (int)total_diff,
                                          "resolve_path_to_bytes for runtime.clj must not retain growing total heap");
    TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(512, (int)raw_diff,
                                          "resolve_path_to_bytes for runtime.clj must reclaim raw/native byte buffers");
}

static void breakout_print_fixnum_vector(const char *label, ID value) {
    if (!label) {
        label = "values";
    }
    if (!value || TAG(value) != CLJ_VECTOR_PERSISTENT) {
        fprintf(stderr, "\n%s: <non-vector>\n", label);
        return;
    }
    CljPersistentVector *v = as_vector(value);
    fprintf(stderr, "\n%s:", label);
    VECTOR_FOR_EACH(v, elem) {
        if (elem && is_fixnum(elem)) {
            fprintf(stderr, " %d", as_fixnum(elem));
        } else {
            fprintf(stderr, " <non-fixnum>");
        }
    }
    fputc('\n', stderr);
}

#if defined(DEBUG) && defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
static void breakout_print_memory_type_deltas(const MemoryStats *before,
                                              const MemoryStats *after,
                                              const char *label) {
    if (!before || !after || !label) {
        return;
    }
    fprintf(stderr, "\n[%s] per-type deltas:\n", label);
    for (int i = 0; i < CLJ_TYPE_COUNT; i++) {
        long long bytes_diff = (long long)after->bytes_current_by_type[i] -
                               (long long)before->bytes_current_by_type[i];
        long long alloc_diff = (long long)after->allocations_by_type[i] -
                               (long long)before->allocations_by_type[i];
        long long dealloc_diff = (long long)after->deallocations_by_type[i] -
                                 (long long)before->deallocations_by_type[i];
        if (bytes_diff == 0 && alloc_diff == 0 && dealloc_diff == 0) {
            continue;
        }
        fprintf(stderr, "  %s: bytes %+lld alloc %+lld dealloc %+lld\n",
                clj_type_name((CljType)i),
                bytes_diff,
                alloc_diff,
                dealloc_diff);
    }
}
#endif

TEST(test_breakout_contract_heap_probe_runtime_reload_persistence) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-breakout.runtime) "
        "  (loop [i 0 out []] "
        "    (if (< i 5) "
        "      (recur (+ i 1) (conj out (:total (heap (require 'tiny-breakout.runtime :reload))))) "
        "      out)))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_EQUAL_UINT(5, vector_count(v));
    breakout_print_fixnum_vector("heap totals runtime :reload", out);
    VECTOR_FOR_EACH(v, elem) {
        TEST_ASSERT_TRUE_MESSAGE(elem && is_fixnum(elem),
                                 "heap probe must return fixnum totals");
        TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(1024, as_fixnum(elem),
                                              "runtime :reload should not accumulate persistent heap between runs");
    }
}

TEST(test_breakout_contract_heap_probe_core_reload_baseline) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (loop [i 0 out []] "
        "    (if (< i 5) "
        "      (recur (+ i 1) (conj out (:total (heap (require 'tiny-breakout.core :reload))))) "
        "      out)))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_EQUAL_UINT(5, vector_count(v));
    breakout_print_fixnum_vector("heap totals core :reload", out);
}

TEST(test_breakout_contract_heap_probe_scene_reload_baseline) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-breakout.scene) "
        "  (loop [i 0 out []] "
        "    (if (< i 5) "
        "      (recur (+ i 1) (conj out (:total (heap (require 'tiny-breakout.scene :reload))))) "
        "      out)))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_EQUAL_UINT(5, vector_count(v));
    breakout_print_fixnum_vector("heap totals scene :reload", out);
}

TEST(test_breakout_contract_heap_probe_audio_reload_baseline) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-breakout.audio) "
        "  (loop [i 0 out []] "
        "    (if (< i 5) "
        "      (recur (+ i 1) (conj out (:total (heap (require 'tiny-breakout.audio :reload))))) "
        "      out)))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_EQUAL_UINT(5, vector_count(v));
    breakout_print_fixnum_vector("heap totals audio :reload", out);
}

TEST(test_breakout_contract_heap_probe_brick_then_wall_cycle_does_not_accumulate_persistent_heap) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

#if defined(DEBUG) && defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
    MemoryStats before = memory_profiler_get_stats();
#endif

    ID out = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (require 'tiny-breakout.runtime) "
        "  (loop [i 0 out []] "
        "    (if (< i 6) "
        "      (recur (+ i 1) "
        "             (conj out "
        "               (:total "
        "                 (heap "
        "                   (do "
        "                     (tiny-breakout.runtime/reset-runtime!) "
        "                     (let [brick1 {:id 2001 :x 50 :y 50 :w 20 :h 10 :points 10} "
        "                           brick2 {:id 2002 :x 96 :y 32 :w 20 :h 10 :points 10} "
        "                           s0 (-> @tiny-breakout.runtime/state* "
        "                                  (assoc :phase :play) "
        "                                  (assoc :bricks {2001 brick1 2002 brick2}) "
        "                                  (assoc :ball-x 50) "
        "                                  (assoc :ball-y 20) "
        "                                  (assoc :ball-vx 0) "
        "                                  (assoc :ball-vy 2) "
        "                                  (assoc :ball-segment {:id 1 "
        "                                                        :start-ms 10 "
        "                                                        :end-ms 40 "
        "                                                        :from-x 50 "
        "                                                        :from-y 20 "
        "                                                        :to-x 50 "
        "                                                        :to-y 46 "
        "                                                        :collision {:hit-id 2001 :normal :top}})) "
        "                           _ (tiny-breakout.runtime/publish-state! s0) "
        "                           _ (tiny-breakout.runtime/publish-state! "
        "                               (tiny-breakout.core/apply-segment-end-at-ms "
        "                                 @tiny-breakout.runtime/state* "
        "                                 1 "
        "                                 40)) "
        "                           seg (:ball-segment @tiny-breakout.runtime/state*) "
        "                           sid (:id seg) "
        "                           end-ms (:end-ms seg) "
        "                           _ (if (number? sid) "
        "                               (tiny-breakout.runtime/publish-state! "
        "                                 (tiny-breakout.core/apply-segment-end-at-ms "
        "                                   @tiny-breakout.runtime/state* "
        "                                   sid "
        "                                   end-ms)) "
        "                               nil) "
        "                           _ (dotimes [_ 8] (run-next-task))] "
        "                       (:segment-id-seq @tiny-breakout.runtime/state*))))))) "
        "      out)))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_EQUAL_UINT(6u, vector_count(v));
    breakout_print_fixnum_vector("heap totals brick->wall cycle", out);

    int first_total = 0;
    int max_followup_total = 0;
    for (size_t i = 0; i < vector_count(v); i++) {
        ID elem = vector_nth(v, i);
        TEST_ASSERT_TRUE_MESSAGE(elem && is_fixnum(elem),
                                 "brick->wall heap probe must return fixnum totals");
        int total = as_fixnum(elem);
        if (i == 0u) {
            first_total = total;
        } else if (total > max_followup_total) {
            max_followup_total = total;
        }
    }

#if defined(DEBUG) && defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
    MemoryStats after = memory_profiler_get_stats();
    long long total_diff = (long long)after.current_memory_usage - (long long)before.current_memory_usage;
    long long raw_diff = (long long)after.raw_bytes_current - (long long)before.raw_bytes_current;
    fprintf(stderr,
            "\nbrick->wall cycle profile: total %+lld bytes, raw %+lld bytes, first heap(total) %d bytes, "
            "follow-up max heap(total) %d bytes\n",
            total_diff,
            raw_diff,
            first_total,
            max_followup_total);
    breakout_print_memory_type_deltas(&before, &after, "brick->wall cycle");
    TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(32768, first_total,
                                          "first brick->wall cycle heap(total) unexpectedly high");
    TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(1280, max_followup_total,
                                          "follow-up brick->wall cycles should stay within a small stable heap footprint");
#else
    TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(32768, first_total,
                                          "first brick->wall cycle heap(total) unexpectedly high");
    TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(1280, max_followup_total,
                                          "follow-up brick->wall cycles should stay within a small stable heap footprint");
#endif
}

TEST(test_breakout_contract_namespaces_load) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (require 'tiny-breakout.scene) "
        "  (require 'tiny-breakout.audio) "
        "  (require 'tiny-breakout.levels) "
        "  (require 'tiny-breakout.runtime) "
        "  true)",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_runtime_source_no_longer_references_runtime_play_namespace) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID bytes = resolve_path_to_bytes("/libs/tiny-breakout/runtime.clj");
    TEST_ASSERT_NOT_NULL(bytes);
    TEST_ASSERT_EQUAL_INT(CLJ_BYTE_ARRAY, TAG(bytes));

    CljByteArray *ba = as_byte_array(bytes);
    TEST_ASSERT_NOT_NULL(ba);
    TEST_ASSERT_TRUE(ba->length > 0);

    const char *marker = "runtime-play";
    size_t marker_len = strlen(marker);
    const uint8_t *p = ba->data;
    size_t rem = (size_t)ba->length;
    while (rem >= marker_len && memcmp(p, marker, marker_len) != 0) {
        p++;
        rem--;
    }
    TEST_ASSERT_TRUE_MESSAGE(rem < marker_len,
                             "tiny-breakout.runtime should not keep the old runtime-play indirection");
}

TEST(test_breakout_contract_runtime_reset_works_after_fresh_reload) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.runtime :reload) "
        "  (tiny-breakout.runtime/reset-runtime!) "
        "  true)",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_audio_events_resolve_to_playable_sfx) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.audio) "
        "  (let [cues (tiny-breakout.audio/events->cues [:brick-hit :level-clear :victory]) "
        "        played (tiny-breakout.audio/play-events! [:brick-hit :victory])] "
        "    (and (= [:sfx/brick-hit :sfx/level-clear :sfx/victory] cues) "
        "         (nil? played))))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_audio_events_play_from_deferred_event_loop_task) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    event_loop_clear();

    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.audio :reload) "
        "  (def breakout-audio-deferred-result (atom :pending)) "
        "  (schedule 0 (fn [] "
        "    (tiny-breakout.audio/play-events! [:brick-hit]) "
        "    (reset! breakout-audio-deferred-result :ok))) "
        "  true)",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);

    TEST_ASSERT_TRUE(event_loop_run_next(NULL, g_test_eval_state));

    ID result = eval_string("@breakout-audio-deferred-result", g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":ok"), result);

    event_loop_clear();
}

TEST(test_breakout_contract_core_input_flow_title_to_play_creates_segment) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (let [s0 (tiny-breakout.core/init-state) "
        "        s1 (tiny-breakout.core/apply-input s0 {:launch true} 100 nil) "
        "        seg (:ball-segment s1)] "
        "    [(:phase s0) (:phase s1) (map? seg) (:start-ms seg) (:end-ms seg)]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_EQUAL_UINT(5, vector_count(v));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":title"), vector_nth(v, 0));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":play"), vector_nth(v, 1));
    TEST_ASSERT_EQUAL_PTR(clj_true, vector_nth(v, 2));
    TEST_ASSERT_TRUE(as_fixnum(vector_nth(v, 3)) >= 100);
    TEST_ASSERT_TRUE(as_fixnum(vector_nth(v, 4)) > as_fixnum(vector_nth(v, 3)));
}

TEST(test_breakout_contract_paddle_clamps_at_bounds) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (let [s0 (assoc (tiny-breakout.core/init-state) :phase :serve :paddle-x 0) "
        "        s1 (tiny-breakout.core/apply-input s0 {:dx -1} 16 nil) "
        "        s2 (assoc s0 :paddle-x 280) "
        "        s3 (tiny-breakout.core/apply-input s2 {:dx 8} 16 nil)] "
        "    [(:paddle-x s1) (:paddle-x s3)]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(vector_nth(v, 0)));
    TEST_ASSERT_EQUAL_INT(280, as_fixnum(vector_nth(v, 1)));
}

TEST(test_breakout_contract_serve_keeps_ball_attached_to_paddle) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (let [s0 (assoc (tiny-breakout.core/init-state) :phase :serve :paddle-x 140) "
        "        s1 (tiny-breakout.core/apply-input (assoc s0 :paddle-x 120) {:dx 0} 16 nil)] "
        "    [(:phase s1) (:ball-x s1) (:ball-y s1) (:ball-segment s1)]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":serve"), vector_nth(v, 0));
    TEST_ASSERT_EQUAL_INT(140, as_fixnum(vector_nth(v, 1)));
    TEST_ASSERT_EQUAL_INT(218, as_fixnum(vector_nth(v, 2)));
    TEST_ASSERT_TRUE(vector_nth(v, 3) == NULL);
}

TEST(test_breakout_contract_launch_from_serve_starts_straight_up) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (let [s0 (assoc (tiny-breakout.core/init-state) :phase :serve :paddle-x 140) "
        "        s1 (tiny-breakout.core/apply-input s0 {:launch true} 1000 nil) "
        "        seg (:ball-segment s1)] "
        "    [(:phase s1) (:ball-x s1) (:ball-vx s1) (:ball-vy s1) (:to-x seg) (:wall seg)]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":play"), vector_nth(v, 0));
    TEST_ASSERT_EQUAL_INT(160, as_fixnum(vector_nth(v, 1)));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(vector_nth(v, 2)));
    TEST_ASSERT_EQUAL_INT(-2, as_fixnum(vector_nth(v, 3)));
    TEST_ASSERT_EQUAL_INT(160, as_fixnum(vector_nth(v, 4)));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":top"), vector_nth(v, 5));
}

TEST(test_breakout_contract_segment_end_bottom_out_decrements_life_and_enters_serve) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (let [s0 (assoc (tiny-breakout.core/init-state) "
        "                  :phase :play :lives 3 "
        "                  :levels [{:id :l1 :bricks []}] "
        "                  :bricks [] "
        "                  :ball-segment {:id 7 :end-ms 500 :to-x 100 :to-y 241 :wall :bottom}) "
        "        s1 (tiny-breakout.core/apply-segment-end s0 7)] "
        "    [(:lives s1) (:phase s1) (:ball-y s1)]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(vector_nth(v, 0)));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":serve"), vector_nth(v, 1));
    TEST_ASSERT_EQUAL_INT(218, as_fixnum(vector_nth(v, 2)));
}

TEST(test_breakout_contract_wall_only_segment_progression_never_drops_segment_while_playing) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (let [s0 (tiny-breakout.core/apply-input (tiny-breakout.core/init-state) {:launch true} 0 nil)] "
        "    (loop [s s0 i 0] "
        "      (cond "
        "        (>= i 300) true "
        "        (not= :play (:phase s)) true "
        "        (nil? (:ball-segment s)) false "
        "        :else (let [seg (:ball-segment s) "
        "                    s1 (tiny-breakout.core/apply-segment-end-at-ms s (:id seg) (:end-ms seg))] "
        "                (recur s1 (inc i)))))))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_paddle_collision_reanchors_ball_and_replans_segment) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (let [s0 (assoc (tiny-breakout.core/init-state) "
        "                  :phase :play :paddle-x 120 :ball-vx 2 :ball-vy 2 "
        "                  :ball-segment {:id 1 :start-ms 10 :end-ms 40 :to-x 150 :to-y 220 :wall :bottom}) "
        "        ev {:id :ball-vs-paddle "
        "            :phase :enter "
        "            :self-aabb {:min-x 138 :min-y 220 :max-x 142 :max-y 224} "
        "            :other-aabb {:min-x 120 :min-y 224 :max-x 160 :max-y 228}} "
        "        s1 (tiny-breakout.core/apply-spatial-event s0 ev 300)] "
        "    (and (= :play (:phase s1)) "
        "         (< (:ball-vy s1) 0) "
        "         (= 220 (:ball-y s1)) "
        "         (= :paddle-hit (first (:events s1))) "
        "         (map? (:ball-segment s1)))))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_paddle_collision_at_left_wall_still_replans_segment) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (let [s0 (assoc (tiny-breakout.core/init-state) "
        "                  :phase :play :paddle-x 0 :ball-vx -2 :ball-vy 2 "
        "                  :ball-segment {:id 1 :start-ms 10 :end-ms 40 :to-x 0 :to-y 220 :wall :bottom}) "
        "        ev {:id :ball-vs-paddle "
        "            :phase :enter "
        "            :self-aabb {:min-x 0 :min-y 220 :max-x 4 :max-y 224} "
        "            :other-aabb {:min-x 0 :min-y 224 :max-x 40 :max-y 228}} "
        "        s1 (tiny-breakout.core/apply-spatial-event s0 ev 300)] "
        "    (and (= :play (:phase s1)) "
        "         (map? (:ball-segment s1)) "
        "         (> (:end-ms (:ball-segment s1)) 300) "
        "         (< (:ball-vy s1) 0))))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_brick_collision_removes_brick_scores_and_can_win) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (let [brick {:id 2001 :x 50 :y 50 :w 20 :h 10 :points 10} "
        "        s0 (-> (tiny-breakout.core/init-state) "
        "               (assoc :phase :play) "
        "               (assoc :score 0) "
        "               (assoc :levels [{:id :only :bricks {2001 brick}}]) "
        "               (assoc :level-index 0) "
        "               (assoc :bricks {2001 brick}) "
        "               (assoc :ball-vx 0) "
        "               (assoc :ball-vy 2) "
        "               (assoc :ball-segment {:id 1 "
        "                                     :start-ms 10 "
        "                                     :end-ms 40 "
        "                                     :from-x 50 "
        "                                     :from-y 20 "
        "                                     :to-x 50 "
        "                                     :to-y 46 "
        "                                     :collision {:hit-id 2001 :normal :top}})) "
        "        s1 (tiny-breakout.core/apply-segment-end-at-ms s0 1 40)] "
        "    [(:score s1) (count (:bricks s1)) (:phase s1) (first (:events s1))]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_EQUAL_INT(10, as_fixnum(vector_nth(v, 0)));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(vector_nth(v, 1)));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":victory"), vector_nth(v, 2));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":brick-hit"), vector_nth(v, 3));
}

TEST(test_breakout_contract_same_snapshot_brick_hits_only_count_first_hit) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (let [b1 {:id 2001 :x 50 :y 50 :w 20 :h 10 :points 10} "
        "        b2 {:id 2002 :x 70 :y 50 :w 20 :h 10 :points 20} "
        "        s0 (-> (tiny-breakout.core/init-state) "
        "               (assoc :phase :play) "
        "               (assoc :score 0) "
        "               (assoc :ball-vx 0) "
        "               (assoc :ball-vy 2) "
        "               (assoc :levels [{:id :only :bricks {2001 b1 2002 b2}}]) "
        "               (assoc :level-index 0) "
        "               (assoc :bricks {2001 b1 2002 b2}) "
        "               (assoc :ball-segment {:id 1 "
        "                                     :start-ms 10 "
        "                                     :end-ms 40 "
        "                                     :from-x 60 "
        "                                     :from-y 20 "
        "                                     :to-x 60 "
        "                                     :to-y 46 "
        "                                     :collision {:hit-id 2001 :normal :top}})) "
        "        s1 (tiny-breakout.core/apply-segment-end-at-ms s0 1 40) "
        "        s2 (tiny-breakout.core/apply-segment-end-at-ms "
        "             (assoc s1 :ball-segment {:id 2 "
        "                                      :start-ms 40 "
        "                                      :end-ms 40 "
        "                                      :from-x 60 "
        "                                      :from-y 46 "
        "                                      :to-x 60 "
        "                                      :to-y 46 "
        "                                      :collision {:hit-id 2001 :normal :top}}) "
        "             2 "
        "             40)] "
        "    [(:ball-vy s2) (:score s2) (count (:bricks s2)) (:id (get (:bricks s2) 2002))]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_TRUE(as_fixnum(vector_nth(v, 0)) < 0);
    TEST_ASSERT_EQUAL_INT(10, as_fixnum(vector_nth(v, 1)));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(vector_nth(v, 2)));
    TEST_ASSERT_EQUAL_PTR(fixnum(2002), vector_nth(v, 3));
}

TEST(test_breakout_contract_brick_collision_from_below_snaps_ball_outside_brick_bounds) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (let [brick {:id 2001 :x 50 :y 0 :w 20 :h 10 :points 10} "
        "        brick2 {:id 2002 :x 80 :y 0 :w 20 :h 10 :points 10} "
        "        s0 (-> (tiny-breakout.core/init-state) "
        "               (assoc :phase :play) "
        "               (assoc :levels [{:id :only :bricks {2001 brick 2002 brick2}}]) "
        "               (assoc :level-index 0) "
        "               (assoc :bricks {2001 brick 2002 brick2}) "
        "               (assoc :ball-vx 0) "
        "               (assoc :ball-vy -2) "
        "               (assoc :ball-segment {:id 1 "
        "                                     :start-ms 10 "
        "                                     :end-ms 40 "
        "                                     :from-x 56 "
        "                                     :from-y 30 "
        "                                     :to-x 56 "
        "                                     :to-y 10 "
        "                                     :collision {:hit-id 2001 :normal :bottom}})) "
        "        s1 (tiny-breakout.core/apply-segment-end-at-ms s0 1 40)] "
        "    [(:ball-y s1) (:ball-vy s1) (map? (:ball-segment s1))]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_EQUAL_INT(10, as_fixnum(vector_nth(v, 0)));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(vector_nth(v, 1)));
    TEST_ASSERT_EQUAL_PTR(clj_true, vector_nth(v, 2));
}

TEST(test_breakout_contract_ball_sized_gap_between_bricks_does_not_pick_side_brick) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'fx) "
        "  (let [left {:id 2001 :x 40 :y 40 :w 26 :h 10 :points 10} "
        "        right {:id 2002 :x 70 :y 40 :w 26 :h 10 :points 10} "
        "        hit (fx/sweep-aabb {:x 66 :y 80 :w 4 :h 4} "
        "                           {:vx 0 :vy -2} "
        "                           [{:id -101 :x 320 :y -1 :w 1 :h 242} "
        "                            {:id -102 :x -1 :y -1 :w 1 :h 242} "
        "                            {:id -103 :x -1 :y -1 :w 322 :h 1} "
        "                            {:id -104 :x -1 :y 245 :w 322 :h 1} "
        "                            left "
        "                            right] "
        "                           5000)] "
        "    [(:hit-id hit) (:normal hit)]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_EQUAL_INT(-103, as_fixnum(vector_nth(v, 0)));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":bottom"), vector_nth(v, 1));
}

TEST(test_breakout_contract_pause_toggle_anchors_rendered_ball_and_resumes_segment) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (let [s0 (assoc (tiny-breakout.core/init-state) "
        "                  :phase :play "
        "                  :ball-segment {:id 2 :start-ms 10 :end-ms 100 :to-x 70 :to-y 80 :wall :top}) "
        "        s1 (tiny-breakout.core/apply-input s0 {:pause true} 40 {:x 33 :y 44}) "
        "        s2 (tiny-breakout.core/apply-input s1 {:pause true} 60 nil)] "
        "    [(:phase s1) (:ball-x s1) (:ball-y s1) (:ball-segment s1) (:phase s2) (map? (:ball-segment s2))]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":pause"), vector_nth(v, 0));
    TEST_ASSERT_EQUAL_INT(33, as_fixnum(vector_nth(v, 1)));
    TEST_ASSERT_EQUAL_INT(44, as_fixnum(vector_nth(v, 2)));
    TEST_ASSERT_TRUE(vector_nth(v, 3) == NULL);
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":play"), vector_nth(v, 4));
    TEST_ASSERT_EQUAL_PTR(clj_true, vector_nth(v, 5));
}

TEST(test_breakout_contract_runtime_input_supports_digital_and_rotary) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-breakout.runtime) "
        "  (tiny-breakout.runtime/reset-runtime!) "
        "  (def breakout-test-x0 (:paddle-x @tiny-breakout.runtime/state*)) "
        "  (tiny-breakout.runtime/apply-input! {:left true}) "
        "  (def breakout-test-x1 (:paddle-x @tiny-breakout.runtime/state*)) "
        "  (tiny-breakout.runtime/reset-runtime!) "
        "  (def breakout-test-y0 (:paddle-x @tiny-breakout.runtime/state*)) "
        "  (tiny-breakout.runtime/apply-input! {:rotary-delta -1}) "
        "  (def breakout-test-y1 (:paddle-x @tiny-breakout.runtime/state*)) "
        "  [(- breakout-test-x1 breakout-test-x0) "
        "   (- breakout-test-y1 breakout-test-y0)])",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_EQUAL_UINT(2, vector_count(v));
    TEST_ASSERT_EQUAL_INT(-4, as_fixnum(vector_nth(v, 0)));
    TEST_ASSERT_EQUAL_INT(-4, as_fixnum(vector_nth(v, 1)));
}

TEST(test_breakout_contract_scene_build_returns_entity_map_root_with_spatial_rules) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (require 'tiny-breakout.scene) "
        "  (let [breakout-test-brick {:id 2001 :x 40 :y 32 :w 20 :h 10 :points 10} "
        "        breakout-test-state (tiny-breakout.scene/with-expanded-collision-rules "
        "                              (-> (tiny-breakout.core/init-state) "
        "                                  (assoc :phase :play) "
        "                                  (assoc :bricks {2001 breakout-test-brick}))) "
        "        breakout-test-frame (tiny-breakout.scene/build-scene breakout-test-state) "
        "        breakout-test-root-id (:root breakout-test-frame) "
        "        breakout-test-index (:index breakout-test-frame) "
        "        breakout-test-root (get breakout-test-index breakout-test-root-id) "
        "        breakout-test-rules (:collision-rules breakout-test-frame) "
        "        breakout-test-paddle (get breakout-test-index :paddle) "
        "        breakout-test-ball (get breakout-test-index :ball) "
        "        breakout-test-lives-label (get breakout-test-index :lives-label) "
        "        breakout-test-lives-value (get breakout-test-index :lives-value) "
        "        breakout-test-brick-node (get breakout-test-index 2001) "
        "        breakout-test-paddle-rule (nth breakout-test-rules 0)] "
        "    (and (= true (:visible breakout-test-frame)) "
        "         (= :tiny-fx.scene/root breakout-test-root-id) "
        "         (= :tiny-fx.scene/root (:id breakout-test-root)) "
        "         (map? breakout-test-index) "
        "         (= [:background "
        "            :paddle "
        "            :ball "
        "            :score-text "
        "            :overlay-text "
        "            :lives-label "
        "            :lives-value "
        "            2001] "
        "            (:children breakout-test-root)) "
        "         (= 1 (count breakout-test-rules)) "
        "         (= :ball-vs-paddle (:id breakout-test-paddle-rule)) "
        "         (= :ball (:self breakout-test-paddle-rule)) "
        "         (= :paddle (:other breakout-test-paddle-rule)) "
        "         (nil? (:collision-rule-targets breakout-test-state)) "
        "         (nil? (:collision-rule-targets-for breakout-test-state)) "
        "         (= \"Lives:\" (:text breakout-test-lives-label)) "
        "         (= \"3\" (:text breakout-test-lives-value)) "
        "         (= 226 (:x breakout-test-lives-label)) "
        "         (= 286 (:x breakout-test-lives-value)) "
        "         (= :breakout/ball (:id (:prototype breakout-test-ball))) "
        "         (= :breakout/paddle (:id (:prototype breakout-test-paddle))) "
        "         (= :breakout/brick (:id (:prototype breakout-test-brick-node))))))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_collision_rules_expand_incrementally_and_keep_removed_bricks_lazy) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (require 'tiny-breakout.scene) "
        "  (let [b1 {:id 2001 :x 40 :y 32 :w 20 :h 10 :points 10} "
        "        b2 {:id 2002 :x 64 :y 32 :w 20 :h 10 :points 10} "
        "        s0 (assoc (tiny-breakout.core/init-state) :phase :play :bricks {2001 b1}) "
        "        s1 (tiny-breakout.scene/with-expanded-collision-rules s0) "
        "        r1 (:collision-rules s1) "
        "        s2 (tiny-breakout.scene/with-expanded-collision-rules "
        "             (assoc s1 :bricks {2001 b1 2002 b2})) "
        "        r2 (:collision-rules s2) "
        "        s3 (tiny-breakout.scene/with-expanded-collision-rules "
        "             (assoc s2 :bricks {2002 b2})) "
        "        r3 (:collision-rules s3)] "
        "    (and (= 1 (count r1)) "
        "         (= 1 (count r2)) "
        "         (= 1 (count r3)) "
        "         (= :ball-vs-paddle (:id (nth r1 0))) "
        "         (identical? r1 r2) "
        "         (identical? r2 r3) "
        "         (nil? (:collision-rule-targets s1)) "
        "         (nil? (:collision-rule-targets-for s1)) "
        "         (nil? (:collision-rule-targets s2)) "
        "         (nil? (:collision-rule-targets-for s2)) "
        "         (nil? (:collision-rule-targets s3)) "
        "         (nil? (:collision-rule-targets-for s3)))))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_collision_rule_expansion_persists_target_cache_for_reuse) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (require 'tiny-breakout.scene) "
        "  (let [bricks (loop [i 0 out {}] "
        "                 (if (= i 24) "
        "                   out "
        "                   (recur (+ i 1) "
        "                          (assoc out "
        "                                 (+ 3000 i) "
        "                                 {:id (+ 3000 i) "
        "                                  :x (* 20 (mod i 8)) "
        "                                  :y (+ 32 (* 10 (quot i 8))) "
        "                                  :w 20 :h 10 :points 10})))) "
        "        s0 (assoc (tiny-breakout.core/init-state) :phase :play :bricks bricks) "
        "        s1 (tiny-breakout.scene/with-expanded-collision-rules s0) "
        "        s2 (tiny-breakout.scene/with-expanded-collision-rules s1)] "
        "    (and (= 24 (count (:bricks s1))) "
        "         (identical? s1 s2) "
        "         (identical? (:collision-rules s1) (:collision-rules s2)) "
        "         (nil? (:collision-rule-targets s1)) "
        "         (nil? (:collision-rule-targets-for s1)) "
        "         (= 1 (count (:collision-rules s1))))))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_title_scene_hides_level_bricks_until_launch) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (require 'tiny-breakout.scene) "
        "  (def breakout-title-frame (tiny-breakout.scene/build-scene (tiny-breakout.core/init-state))) "
        "  (def breakout-title-index (:index breakout-title-frame)) "
        "  (def breakout-title-root (get breakout-title-index (:root breakout-title-frame))) "
        "  (def breakout-title-overlay (get breakout-title-index :overlay-text)) "
        "  (and (= [:background "
        "           :paddle "
        "           :ball "
        "           :score-text "
        "           :overlay-text "
        "           :lives-label "
        "           :lives-value] "
        "          (:children breakout-title-root)) "
        "       (= \"\" (:text breakout-title-overlay)) "
        "       (= nil (get breakout-title-index 2001))))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_scene_ball_segment_projects_absolute_timelines) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (require 'tiny-breakout.scene) "
        "  (let [s (assoc (tiny-breakout.core/init-state) "
        "                 :phase :play "
        "                 :ball-x 10 :ball-y 20 "
        "                 :ball-segment {:id 4 :start-ms 123 :end-ms 234 :to-x 44 :to-y 66 :wall :right}) "
        "        frame (tiny-breakout.scene/build-scene s) "
        "        ball (get (:index frame) :ball)] "
        "    (and (= [[123 10] [234 44]] (:keyframes (:x ball))) "
        "         (= [[123 20] [234 66]] (:keyframes (:y ball))))))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_scene_uses_exact_terminal_overlay_texts) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (require 'tiny-breakout.scene) "
        "  (let [overlay-id :overlay-text "
        "        lose (tiny-breakout.scene/build-scene (assoc (tiny-breakout.core/init-state) :phase :game-over)) "
        "        win (tiny-breakout.scene/build-scene (assoc (tiny-breakout.core/init-state) :phase :victory)) "
        "        lose-overlay (get (:index lose) overlay-id) "
        "        win-overlay (get (:index win) overlay-id)] "
        "    (and (= \"GAME OVER\" (:text lose-overlay)) "
        "         (= \"YOU WIN!\" (:text win-overlay)))))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_scene_centers_all_overlay_texts_via_shared_path) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (require 'tiny-breakout.scene) "
        "  (let [overlay-id :overlay-text "
        "        title (tiny-breakout.scene/build-scene (assoc (tiny-breakout.core/init-state) :phase :title :overlay-start-ms 123)) "
        "        clear (tiny-breakout.scene/build-scene (assoc (tiny-breakout.core/init-state) :phase :level-clear)) "
        "        lose (tiny-breakout.scene/build-scene (assoc (tiny-breakout.core/init-state) :phase :game-over)) "
        "        win (tiny-breakout.scene/build-scene (assoc (tiny-breakout.core/init-state) :phase :victory)) "
        "        pause (tiny-breakout.scene/build-scene (assoc (tiny-breakout.core/init-state) :phase :pause)) "
        "        title-overlay (get (:index title) overlay-id) "
        "        clear-overlay (get (:index clear) overlay-id) "
        "        lose-overlay (get (:index lose) overlay-id) "
        "        win-overlay (get (:index win) overlay-id) "
        "        pause-overlay (get (:index pause) overlay-id)] "
        "    (and (= 120 (:x title-overlay)) "
        "         (= 108 (:x clear-overlay)) "
        "         (= 118 (:x lose-overlay)) "
        "         (= 126 (:x win-overlay)) "
        "         (= 130 (:x pause-overlay)))))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_scene_animates_overlay_text_color_on_phase_entry) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (require 'tiny-breakout.scene) "
        "  (require 'tiny-fx.gfx) "
        "  (let [overlay-id :overlay-text "
        "        scene (tiny-breakout.scene/build-scene "
        "                (assoc (tiny-breakout.core/init-state) "
        "                       :phase :game-over "
        "                       :overlay-start-ms 123)) "
        "        overlay (get (:index scene) overlay-id) "
        "        style (:style overlay) "
        "        kf (:keyframes (:stroke-color style))] "
        "    (and (= \"GAME OVER\" (:text overlay)) "
        "         (> (count kf) 10) "
        "         (= (first (nth kf 1)) (first (nth kf 2))) "
        "         (= [123 (tiny-fx.gfx/color 24 24 24)] (first kf)) "
        "         (= [1003 (tiny-fx.gfx/color 255 255 255)] (last kf)))))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_title_overlay_text_appears_only_with_overlay_animation) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (require 'tiny-breakout.scene) "
        "  (let [overlay-id :overlay-text "
        "        static-title (tiny-breakout.scene/build-scene (assoc (tiny-breakout.core/init-state) :phase :title)) "
        "        animated-title (tiny-breakout.scene/build-scene (assoc (tiny-breakout.core/init-state) :phase :title :overlay-start-ms 123)) "
        "        static-overlay (get (:index static-title) overlay-id) "
        "        animated-overlay (get (:index animated-title) overlay-id)] "
        "    (and (= \"\" (:text static-overlay)) "
        "         (= \"BREAKOUT\" (:text animated-overlay)))))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_host_config_exposes_viewer_slots_and_atoms_without_native_runtime_flag) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-clj.deployment) "
        "  (let [cfg (tiny-clj.deployment/breakout-host-config) "
        "        _ ((:prepare-callback cfg)) "
        "        slots (:slots cfg) "
        "        game-atom (:game-scene-atom cfg)] "
        "    (and (= :tiny-breakout (:entry cfg)) "
        "         (nil? (:host-runtime cfg)) "
        "         (= 1 (count slots)) "
        "         (atom? game-atom) "
        "         (fn? (:spatial-callback cfg)) "
        "         (= game-atom (:atom (first slots))) "
        "         (= true (:visible (deref game-atom))))))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_runtime_apply_input_mutates_state_discretely) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-clj.deployment) "
        "  (require 'tiny-breakout.runtime) "
        "  (let [cfg (tiny-clj.deployment/breakout-host-config) "
        "        _ ((:prepare-callback cfg)) "
        "        s0 @tiny-breakout.runtime/state* "
        "        _ (tiny-breakout.runtime/apply-input! {:launch true}) "
        "        s1 @tiny-breakout.runtime/state* "
        "        _ (tiny-breakout.runtime/apply-input! {:right true}) "
        "        s2 @tiny-breakout.runtime/state* "
        "        scene @(:game-scene-atom cfg)] "
        "    (and (= :title (:phase s0)) "
        "         (= :play (:phase s1)) "
        "         (> (:paddle-x s2) (:paddle-x s1)) "
        "         (= true (:visible scene)))))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_host_fire_button_simulation_reaches_breakout_runtime) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-clj.deployment) "
        "  (require 'tiny-clj.gpio) "
        "  (require 'tiny-breakout.runtime) "
        "  (tiny-clj.gpio/simulate! 13 1) "
        "  (let [cfg (tiny-clj.deployment/breakout-host-config)] "
        "    ((:prepare-callback cfg)) "
        "    ((:startup-callback cfg) nil)) "
        "  (tiny-clj.gpio/simulate! 13 0) "
        "  (Thread/sleep 30) "
        "  (dotimes [_ 8] (run-next-task)) "
        "  (tiny-clj.gpio/simulate! 13 1) "
        "  (Thread/sleep 30) "
        "  (dotimes [_ 8] (run-next-task)) "
        "  (= :play (:phase @tiny-breakout.runtime/state*)))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_host_left_button_moves_until_button_up) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-clj.deployment) "
        "  (require 'tiny-clj.gpio) "
        "  (require 'tiny-breakout.runtime) "
        "  (tiny-clj.gpio/simulate! 14 1) "
        "  (let [cfg (tiny-clj.deployment/breakout-host-config)] "
        "    ((:prepare-callback cfg)) "
        "    ((:startup-callback cfg) nil)) "
        "  (tiny-breakout.runtime/apply-input! {:launch true}) "
        "  (let [x0 (:paddle-x @tiny-breakout.runtime/state*) "
        "        _ (tiny-clj.gpio/simulate! 14 0) "
        "        _ (Thread/sleep 35) "
        "        _ (dotimes [_ 8] (run-next-task)) "
        "        moving-scene @tiny-breakout.runtime/scene* "
        "        moving-paddle (get (:index moving-scene) :paddle) "
        "        moving-x (:x moving-paddle) "
        "        _ (Thread/sleep 70) "
        "        _ (tiny-clj.gpio/simulate! 14 1) "
        "        _ (Thread/sleep 35) "
        "        _ (dotimes [_ 8] (run-next-task)) "
        "        s1 @tiny-breakout.runtime/state* "
        "        stopped-scene @tiny-breakout.runtime/scene* "
        "        stopped-paddle (get (:index stopped-scene) :paddle)] "
        "    (and (map? moving-x) "
        "         (contains? moving-x :keyframes) "
        "         (< (:paddle-x s1) (- x0 4)) "
        "         (number? (:x stopped-paddle)))))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_host_right_button_release_does_not_snap_to_left_edge) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-clj.deployment) "
        "  (require 'tiny-clj.gpio) "
        "  (require 'tiny-breakout.runtime) "
        "  (tiny-clj.gpio/simulate! 12 1) "
        "  (let [cfg (tiny-clj.deployment/breakout-host-config)] "
        "    ((:prepare-callback cfg))) "
        "  (tiny-breakout.runtime/start-runtime! nil) "
        "  (tiny-breakout.runtime/apply-input! {:launch true}) "
        "  (let [x0 (:paddle-x @tiny-breakout.runtime/state*) "
        "        _ (tiny-clj.gpio/simulate! 12 0) "
        "        _ (Thread/sleep 35) "
        "        _ (dotimes [_ 8] (run-next-task)) "
        "        moving-scene @tiny-breakout.runtime/scene* "
        "        moving-paddle (get (:index moving-scene) :paddle) "
        "        moving-x (:x moving-paddle) "
        "        _ (Thread/sleep 70) "
        "        _ (tiny-clj.gpio/simulate! 12 1) "
        "        _ (Thread/sleep 35) "
        "        _ (dotimes [_ 8] (run-next-task)) "
        "        s1 @tiny-breakout.runtime/state* "
        "        stopped-scene @tiny-breakout.runtime/scene* "
        "        stopped-paddle (get (:index stopped-scene) :paddle)] "
        "    (and (map? moving-x) "
        "         (contains? moving-x :keyframes) "
        "         (> (:paddle-x s1) (+ x0 4)) "
        "         (number? (:x stopped-paddle)) "
        "         (> (:x stopped-paddle) 0))))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_host_right_button_release_ignores_stale_renderer_snapshot) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    vg_rendered_state_reset_all();

    ID setup_ok = eval_string(
        "(do "
        "  (require 'tiny-clj.deployment) "
        "  (require 'tiny-clj.gpio) "
        "  (require 'tiny-clj.runtime) "
        "  (require 'tiny-breakout.runtime) "
        "  (tiny-clj.gpio/simulate! 12 1) "
        "  (let [cfg (tiny-clj.deployment/breakout-host-config)] "
        "    ((:prepare-callback cfg)) "
        "    (tiny-clj.runtime/start-renderer! (:slots cfg)) "
        "    (tiny-breakout.runtime/start-runtime! nil) "
        "    (tiny-breakout.runtime/apply-input! {:launch true}) "
        "    true))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, setup_ok);

    ID moved_ok = eval_string(
        "(do "
        "  (tiny-clj.gpio/simulate! 12 0) "
        "  (Thread/sleep 35) "
        "  (dotimes [_ 8] (run-next-task)) "
        "  true)",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, moved_ok);

    VgTransformFixed stale_world_t = {
        .m00 = VG_SCALE_ONE,
        .m01 = 0,
        .m02 = 0,
        .m10 = 0,
        .m11 = VG_SCALE_ONE,
        .m12 = 0,
    };
    ID paddle_entity_id = intern_symbol_global(":paddle");
    TEST_ASSERT_NOT_NULL(paddle_entity_id);
    vg_rendered_state_capture_begin(0u, 99u, 0u);
    vg_rendered_state_capture_record_entity((uintptr_t)paddle_entity_id, stale_world_t);
    vg_rendered_state_capture_commit();

    ID ok = eval_string(
        "(do "
        "  (tiny-clj.gpio/simulate! 12 1) "
        "  (Thread/sleep 35) "
        "  (dotimes [_ 8] (run-next-task)) "
        "  (let [s @tiny-breakout.runtime/state*] "
        "    (> (:paddle-x s) 0)))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_host_right_second_press_does_not_snap_to_left_with_stale_renderer_snapshot) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    vg_rendered_state_reset_all();

    ID setup_ok = eval_string(
        "(do "
        "  (require 'tiny-clj.deployment) "
        "  (require 'tiny-clj.gpio) "
        "  (require 'tiny-clj.runtime) "
        "  (require 'tiny-breakout.runtime) "
        "  (tiny-clj.gpio/simulate! 12 1) "
        "  (let [cfg (tiny-clj.deployment/breakout-host-config)] "
        "    ((:prepare-callback cfg)) "
        "    (tiny-clj.runtime/start-renderer! (:slots cfg)) "
        "    (tiny-breakout.runtime/start-runtime! nil) "
        "    (tiny-breakout.runtime/apply-input! {:launch true}) "
        "    true))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, setup_ok);

    ID moved_ok = eval_string(
        "(do "
        "  (tiny-clj.gpio/simulate! 12 0) "
        "  (Thread/sleep 35) "
        "  (dotimes [_ 8] (run-next-task)) "
        "  true)",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, moved_ok);

    VgTransformFixed stale_world_t = {
        .m00 = VG_SCALE_ONE,
        .m01 = 0,
        .m02 = 0,
        .m10 = 0,
        .m11 = VG_SCALE_ONE,
        .m12 = 0,
    };
    ID paddle_entity_id = intern_symbol_global(":paddle");
    TEST_ASSERT_NOT_NULL(paddle_entity_id);
    vg_rendered_state_capture_begin(0u, 101u, 0u);
    vg_rendered_state_capture_record_entity((uintptr_t)paddle_entity_id, stale_world_t);
    vg_rendered_state_capture_commit();

    ID ok = eval_string(
        "(do "
        "  (tiny-clj.gpio/simulate! 12 1) "
        "  (Thread/sleep 35) "
        "  (dotimes [_ 8] (run-next-task)) "
        "  (let [x1 (:paddle-x @tiny-breakout.runtime/state*) "
        "        _ (tiny-clj.gpio/simulate! 12 0) "
        "        _ (Thread/sleep 20) "
        "        _ (dotimes [_ 6] (run-next-task)) "
        "        _ (tiny-clj.gpio/simulate! 12 1) "
        "        _ (Thread/sleep 20) "
        "        _ (dotimes [_ 6] (run-next-task)) "
        "        x2 (:paddle-x @tiny-breakout.runtime/state*)] "
        "    (and (> x1 0) "
        "         (>= x2 x1))))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_level_clear_stops_paddle_motion) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (require 'tiny-breakout.scene) "
        "  (let [brick {:id 2001 :x 10 :y 10 :w 32 :h 12 :points 7} "
        "        s0 (-> (tiny-breakout.core/init-state) "
        "               (assoc :phase :play) "
        "               (assoc :level-index 0) "
        "               (assoc :bricks {2001 brick}) "
        "               (assoc :paddle-x 120) "
        "               (assoc :paddle-motion {:dir 1 :start-ms 100 :end-ms 300 :to-x 280}) "
        "               (assoc :ball-x 10) "
        "               (assoc :ball-y 6) "
        "               (assoc :ball-vx 2) "
        "               (assoc :ball-vy 2) "
        "               (assoc :events []) "
        "               (assoc :ball-segment {:id 1 "
        "                                     :start-ms 100 "
        "                                     :end-ms 200 "
        "                                     :from-x 10 "
        "                                     :from-y 6 "
        "                                     :to-x 10 "
        "                                     :to-y 6 "
        "                                     :collision {:hit-id 2001 :normal :top}})) "
        "        s1 (tiny-breakout.core/apply-segment-end-at-ms s0 1 200) "
        "        frame (tiny-breakout.scene/build-scene s1) "
        "        paddle (get (:index frame) :paddle)] "
        "    (and (= :level-clear (:phase s1)) "
        "         (nil? (:paddle-motion s1)) "
        "         (number? (:x paddle)))))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_segment_progression_no_longer_uses_named_segment_timer) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-clj.deployment) "
        "  (require 'tiny-breakout.runtime) "
        "  (let [cfg (tiny-clj.deployment/breakout-host-config)] "
        "    ((:prepare-callback cfg))) "
        "  (tiny-breakout.runtime/apply-input! {:launch true}) "
        "  (= false (cancel-timer :tiny-breakout/segment-end)))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_segment_timeline_event_replans_even_if_event_arrives_before_wall_clock_end) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    event_loop_clear();
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.runtime) "
        "  (require 'tiny-fx.gfx-timeline) "
        "  (tiny-breakout.runtime/reset-runtime!) "
        "  (let [future-end (+ (current-time-ms) 60000) "
        "        seeded (assoc @tiny-breakout.runtime/state* "
        "                 :phase :play "
        "                 :ball-x 160 :ball-y 120 "
        "                 :ball-vx 2 :ball-vy -2 "
        "                 :events [] "
        "                 :ball-segment {:id 42 :start-ms 1000 :end-ms future-end :to-x 316 :to-y 60 :wall :right}) "
        "        _ (tiny-breakout.runtime/publish-state! seeded) "
        "        watcher (get @tiny-fx.gfx-timeline/timeline-watchers* :tiny-breakout/segment-end) "
        "        callback (:callback watcher) "
        "        before @tiny-breakout.runtime/state* "
        "        _ (callback "
        "           {:source :timeline "
        "            :id :tiny-breakout/segment-end "
        "            :progress {:end-event true :at-end true :phase-ms 1 :period-ms 1}}) "
        "        _ (dotimes [_ 8] (run-next-task)) "
        "        after @tiny-breakout.runtime/state* "
        "        before-seg (:ball-segment before) "
        "        after-seg (:ball-segment after)] "
        "    (and (fn? callback) "
        "         (= 42 (:id before-seg)) "
        "         (map? after-seg) "
        "         (= -2 (:ball-vx after)) "
        "         (not= (:id after-seg) (:id before-seg)))))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_segment_timeline_deferred_callback_does_not_overwrite_newer_paddle_state) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.runtime) "
        "  (require 'tiny-fx.gfx-timeline) "
        "  (tiny-breakout.runtime/reset-runtime!) "
        "  (let [now-ms (current-time-ms) "
        "        seeded (-> @tiny-breakout.runtime/state* "
        "                   (assoc :phase :play) "
        "                   (assoc :paddle-x 120) "
        "                   (assoc :ball-x 160) "
        "                   (assoc :ball-y 120) "
        "                   (assoc :ball-vx 2) "
        "                   (assoc :ball-vy -2) "
        "                   (assoc :events []) "
        "                   (assoc :ball-segment {:id 7 :start-ms (- now-ms 1000) :end-ms now-ms :to-x 316 :to-y 60 :wall :right})) "
        "        _ (tiny-breakout.runtime/publish-state! seeded) "
        "        watcher (get @tiny-fx.gfx-timeline/timeline-watchers* :tiny-breakout/segment-end) "
        "        callback (:callback watcher) "
        "        _ (callback {:source :timeline "
        "                     :id :tiny-breakout/segment-end "
        "                     :progress {:end-event true :at-end true :phase-ms 1 :period-ms 1}}) "
        "        _ (tiny-breakout.runtime/publish-state! (assoc @tiny-breakout.runtime/state* :paddle-x 250 :paddle-motion nil)) "
        "        _ (dotimes [_ 16] (run-next-task)) "
        "        final @tiny-breakout.runtime/state*] "
        "    (and (fn? callback) "
        "         (= 250 (:paddle-x final)))))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_segment_timeline_new_segment_rearms_watcher_edge_state) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.runtime) "
        "  (require 'tiny-fx.gfx-timeline) "
        "  (tiny-breakout.runtime/reset-runtime!) "
        "  (let [now-ms (current-time-ms) "
        "        watch-id :tiny-breakout/segment-end "
        "        seeded (-> @tiny-breakout.runtime/state* "
        "                   (assoc :phase :play) "
        "                   (assoc :ball-x 160) "
        "                   (assoc :ball-y 120) "
        "                   (assoc :ball-vx 2) "
        "                   (assoc :ball-vy -2) "
        "                   (assoc :events []) "
        "                   (assoc :ball-segment {:id 7 :start-ms (- now-ms 1000) :end-ms now-ms :to-x 316 :to-y 60 :wall :right})) "
        "        _ (tiny-breakout.runtime/publish-state! seeded) "
        "        watcher (get @tiny-fx.gfx-timeline/timeline-watchers* watch-id) "
        "        callback (:callback watcher) "
        "        _ (swap! tiny-fx.gfx-timeline/timeline-watchers* "
        "                 (fn [m] "
        "                   (assoc m watch-id "
        "                          (assoc (get m watch-id) :last-at-end true)))) "
        "        _ (callback {:source :timeline "
        "                     :id watch-id "
        "                     :progress {:end-event true :at-end true :phase-ms 1 :period-ms 1}}) "
        "        after @tiny-breakout.runtime/state* "
        "        after-seg (:ball-segment after) "
        "        watcher2 (get @tiny-fx.gfx-timeline/timeline-watchers* watch-id)] "
        "    (and (fn? callback) "
        "         (map? after-seg) "
        "         (not= 7 (:id after-seg)) "
        "         (= true (:last-at-end watcher2)))))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_timeline_ingress_replans_breakout_segment_without_polling_wake_tick) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    event_loop_clear();
    vg_rendered_state_reset_all();

    ID setup = eval_string(
        "(do "
        "  (require 'tiny-clj.deployment) "
        "  (require 'tiny-clj.runtime) "
        "  (require 'tiny-breakout.runtime) "
        "  (require 'tiny-fx.gfx-timeline) "
        "  (let [cfg (tiny-clj.deployment/breakout-host-config)] "
        "    ((:prepare-callback cfg)) "
        "    (tiny-clj.runtime/start-renderer! (:slots cfg)) "
        "    (tiny-breakout.runtime/start-runtime! nil) "
        "    (tiny-breakout.runtime/apply-input! {:launch true}) "
        "    (contains? @tiny-fx.gfx-timeline/timeline-watchers* :tiny-breakout/segment-end)))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, setup);

    ID segment_before = eval_string("(:segment-id-seq @tiny-breakout.runtime/state*)", g_test_eval_state);
    TEST_ASSERT_TRUE(is_fixnum(segment_before));
    int before_seq = as_fixnum(segment_before);
    TEST_ASSERT_TRUE_MESSAGE(before_seq >= 1, "launch should create first ball segment");

    ID dispatched = eval_string(
        "(do "
        "  (require 'tiny-clj.event) "
        "  (tiny-clj.event/dispatch-timeline-progress! "
        "    {:event-id :tiny-breakout/segment-end "
        "     :end-event true "
        "     :at-end true "
        "     :entity-id :ball "
        "     :phase-ms 100 "
        "     :period-ms 100 "
        "     :step-index 1 "
        "     :keyframe-count 2}))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, dispatched);

    int drained = 0;
    while (event_loop_has_pending_tasks() && drained < 16) {
        TEST_ASSERT_TRUE(event_loop_run_next(NULL, g_test_eval_state));
        drained++;
    }
    TEST_ASSERT_FALSE_MESSAGE(event_loop_has_pending_tasks(),
                              "timeline ingress should fully drain without polling wake tick");

    ID segment_after = eval_string("(:segment-id-seq @tiny-breakout.runtime/state*)", g_test_eval_state);
    TEST_ASSERT_TRUE(is_fixnum(segment_after));
    int after_seq = as_fixnum(segment_after);
    char seq_msg[128];
    mini_snprintf(seq_msg, sizeof(seq_msg),
                  "timeline ingress should advance segment-id-seq (before=%d after=%d)",
                  before_seq,
                  after_seq);
    TEST_ASSERT_TRUE_MESSAGE(after_seq > before_seq, seq_msg);
}

TEST(test_breakout_contract_host_spatial_callback_dispatches_generic_spatial_watchers) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-clj.deployment) "
        "  (require 'tiny-clj.event) "
        "  (let [cfg (tiny-clj.deployment/breakout-host-config) "
        "        seen (atom []) "
        "        _ (tiny-clj.event/on {:source :spatial :id :ball-vs-paddle} "
        "            (fn [event] "
        "              (swap! seen conj [(:id event) (:phase event)]) "
        "              nil)) "
        "        _ ((:spatial-callback cfg) "
        "            {:source :spatial :id :ball-vs-paddle :rule {:id :ball-vs-paddle} "
        "             :phase :enter "
        "             :self-aabb {:min-x 10 :min-y 20 :max-x 14 :max-y 24} "
        "             :other-aabb {:min-x 0 :min-y 24 :max-x 40 :max-y 28}}) "
        "        _ (tiny-clj.event/on {:source :spatial :id :ball-vs-paddle} nil)] "
        "    (= [[:ball-vs-paddle :enter]] @seen)))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_host_spatial_callback_does_not_reenter_global_collision_callback) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-clj.deployment) "
        "  (require 'tiny-clj.event) "
        "  (require 'tiny-fx.gfx-collision) "
        "  (let [cfg (tiny-clj.deployment/breakout-host-config) "
        "        callback-calls (atom 0) "
        "        watcher-calls (atom 0) "
        "        _ (tiny-fx.gfx-collision/set-collision-callback! "
        "            (fn [_event] "
        "              (swap! callback-calls inc) "
        "              nil)) "
        "        _ (tiny-clj.event/on {:source :spatial :id :ball-vs-paddle} "
        "            (fn [_event] "
        "              (swap! watcher-calls inc) "
        "              nil)) "
        "        _ ((:spatial-callback cfg) "
        "           {:source :spatial :id :ball-vs-paddle :rule {:id :ball-vs-paddle} "
        "            :phase :enter "
        "            :self-aabb {:min-x 10 :min-y 20 :max-x 14 :max-y 24} "
        "            :other-aabb {:min-x 0 :min-y 24 :max-x 40 :max-y 28}}) "
        "        _ (tiny-clj.event/on {:source :spatial :id :ball-vs-paddle} nil) "
        "        _ (tiny-fx.gfx-collision/set-collision-callback! nil)] "
        "    (and (= 0 @callback-calls) "
        "         (= 1 @watcher-calls))))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}
