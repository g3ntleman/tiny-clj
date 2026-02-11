/*
 * Unity Test Runner for Tiny-CLJ
 * 
 * Central test runner that includes all test suites with command-line parameter support.
 */

#include "tests_common.h"
// test_registry.h is included via tests_common.h (uses subjective-c test infrastructure)
#include "memory_profiler.h"
#include "../tiny_clj.h"
#include "../event_loop.h"
#include "../fs_layer.h"
#include "unity/src/unity_internals.h"  // For Unity.TestFile and Unity.CurrentTestLineNumber
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <malloc/malloc.h>
#endif

// Forward declaration for clojure_core_set_quiet
extern void clojure_core_set_quiet(bool quiet);

// Access to global memory stats for leak checking
extern MemoryStats g_memory_stats;
extern bool g_memory_verbose_mode;

// Forward declaration for load_clojure_core
int load_clojure_core(EvalState *st);

// Static flags to ensure initialization happens only once
static bool g_special_symbols_initialized = false;

// Batch mode: skip heavy setUp between tests in same batch (for TEST_SHARED)
static bool g_batch_mode = false;

// Quiet output mode: suppress PASS lines and stdout from passing tests
static bool g_quiet_output = false;

// Single test mode: enable verbose memory stats when only one test runs
static bool g_single_test_mode = false;

// Global test EvalState (available in all tests via tests_common.h)
EvalState *g_test_eval_state = NULL;

// Current test entry (set by test runner)
static const SubjectiveCTestEntry *g_current_test_entry = NULL;

// Per-test heap growth baseline (profiling enabled)
#if MEMORY_PROFILING_ENABLED
static MemoryStats g_heap_baseline;
static bool g_memory_profiler_initialized = false;
#endif
static bool g_heap_check_enabled = false;
static size_t g_heap_growth_limit_bytes = 0;
static uint32_t g_heap_baseline_pool_depth = 0;

static bool is_shared_test_entry(const SubjectiveCTestEntry *entry) {
    if (!entry || !entry->group) return false;
    return strncmp(entry->group, "shared_", 7) == 0;
}

/* Groups that run without loading clojure.core. All other groups get core in setUp. */
static bool group_runs_without_core(const SubjectiveCTestEntry *entry) {
    if (!entry || !entry->group) return false;
    const char *g = entry->group;
    return strcmp(g, "test_parser") == 0
        || strcmp(g, "test_static_keywords") == 0;
}

void test_heap_growth_disable(void) { g_heap_check_enabled = false; }
void test_heap_growth_allow_all(void) {
    g_heap_check_enabled = true;
    g_heap_growth_limit_bytes = SUBJECTIVE_C_TEST_HEAP_GROWTH_UNLIMITED;
}

static void test_heap_growth_mark_baseline(void) {
#if MEMORY_PROFILING_ENABLED
    if (g_heap_check_enabled && is_memory_profiling_enabled()) {
        g_heap_baseline = memory_profiler_get_stats();
        g_heap_baseline_pool_depth = autorelease_pool_depth();
    }
#endif
}

static void test_heap_growth_check(void) {
#if MEMORY_PROFILING_ENABLED
    if (!g_heap_check_enabled || !is_memory_profiling_enabled()) {
        return;
    }
    if (g_heap_growth_limit_bytes == SUBJECTIVE_C_TEST_HEAP_GROWTH_UNLIMITED) {
        return;
    }

    MemoryStats after = memory_profiler_get_stats();
    bool failed = false;
    char msg[1024];
    size_t off = 0;
    size_t total_delta = 0;
    msg[0] = '\0';

    for (int i = 0; i < CLJ_TYPE_COUNT; i++) {
        size_t before_bytes = g_heap_baseline.bytes_current_by_type[i];
        size_t after_bytes = after.bytes_current_by_type[i];
        if (after_bytes > before_bytes && i != CLJ_CALLSITE_CACHE && i != CLJ_SYMBOL) {
            size_t delta = after_bytes - before_bytes;
            const char *name = clj_type_name((CljType)i);
            off = format_append(msg, off, sizeof(msg), " ");
            off = format_append(msg, off, sizeof(msg), name ? name : "unknown");
            off = format_append(msg, off, sizeof(msg), "+");
            off = format_append_ulong(msg, off, sizeof(msg), (unsigned long)delta);
            total_delta += delta;
            failed = true;
        }
    }

    if (after.raw_bytes_current > g_heap_baseline.raw_bytes_current) {
        size_t delta = after.raw_bytes_current - g_heap_baseline.raw_bytes_current;
        off = format_append(msg, off, sizeof(msg), " raw+");
        off = format_append_ulong(msg, off, sizeof(msg), (unsigned long)delta);
        total_delta += delta;
        failed = true;
    }

    if (failed && total_delta > g_heap_growth_limit_bytes) {
        char full[1200];
        const char *tname = g_current_test_entry && g_current_test_entry->qualified_name
            ? g_current_test_entry->qualified_name
            : (g_current_test_entry ? g_current_test_entry->name : "unknown");
        test_snprintf(full, sizeof(full),
                      "Heap grew by %lu bytes (limit %lu) in %s:%s",
                      (unsigned long)total_delta,
                      (unsigned long)g_heap_growth_limit_bytes,
                      tname ? tname : "unknown",
                      msg[0] ? msg : " (no details)");
        TEST_FAIL_MESSAGE(full);
    }
#endif
}

// ============================================================================
// GLOBAL SETUP/TEARDOWN
// ============================================================================

static void warm_shared_group_for_heap_baseline(void) {
    if (!g_current_test_entry || !g_current_test_entry->group || !g_test_eval_state) {
        return;
    }

    const char *group = g_current_test_entry->group;

    // Keep per-test heap checks focused on the test expression, not one-time
    // namespace/bootstrap/callsite costs that are identical across tests.
    if (strcmp(group, "shared_test_string") == 0) {
        WITH_AUTORELEASE_POOL({
            (void)eval_string("(require 'clojure.string)", g_test_eval_state);
            (void)eval_string("(clojure.string/escape \"abc\" {})", g_test_eval_state);
            (void)eval_string("(clojure.string/includes? \"hello\" \"ell\")", g_test_eval_state);
            (void)eval_string("(clojure.string/includes? \"hello\" \"xyz\")", g_test_eval_state);
            (void)eval_string("(clojure.string/index-of \"hello\" \"l\" 0)", g_test_eval_state);
        });
        return;
    }

    if (strcmp(group, "shared_test_loops") == 0) {
        WITH_AUTORELEASE_POOL({
            (void)eval_string("(for [x [1 2 3]] (* x x))", g_test_eval_state);
            (void)eval_string("(vec (for [x [1 2] y [3 4]] [x y]))", g_test_eval_state);
            (void)eval_string("(vec (for [x (range 6) :when (even? x)] x))", g_test_eval_state);
            (void)eval_string("(vec (for [x [1 2 3] :let [y (* x 2)]] y))", g_test_eval_state);
            (void)eval_string("(vec (for [x (range) :while (< x 3)] x))", g_test_eval_state);
            (void)eval_string("(vec (take 3 (for [x (range)] x)))", g_test_eval_state);
            (void)eval_string("(let [s (for [x (range 5)] x)] (= (vec s) (vec s)))", g_test_eval_state);
        });
        return;
    }

    if (strcmp(group, "shared_test_core_functions") == 0) {
        WITH_AUTORELEASE_POOL({
            (void)eval_string("(first (keep (fn [x] (if (even? x) x nil)) '(1 2 3 4 5 6)))", g_test_eval_state);
            (void)eval_string("(last (keep (fn [x] (if (even? x) x nil)) '(1 2 3 4 5 6)))", g_test_eval_state);
        });
    }
}


void setUp(void) {
    // Default: enforce heap growth checks for shared (read-only) tests only
    g_heap_check_enabled = is_shared_test_entry(g_current_test_entry);
    g_heap_growth_limit_bytes = 0;
    if (g_current_test_entry) {
        size_t limit = g_current_test_entry->heap_growth_limit_bytes;
        if (limit == SUBJECTIVE_C_TEST_HEAP_GROWTH_UNLIMITED) {
            g_heap_check_enabled = true;
            g_heap_growth_limit_bytes = SUBJECTIVE_C_TEST_HEAP_GROWTH_UNLIMITED;
        } else if (limit != SUBJECTIVE_C_TEST_HEAP_GROWTH_UNSPECIFIED) {
            g_heap_check_enabled = true;
            g_heap_growth_limit_bytes = limit;
        } else if (g_heap_check_enabled) {
            // Shared tests with UNSPECIFIED: allow small growth (lazy/concat etc.)
            g_heap_growth_limit_bytes = 2048;
        }
    }
    
    // In batch mode, skip heavy initialization (clojure.core already loaded)
    if (g_batch_mode) {
        warm_shared_group_for_heap_baseline();
        test_heap_growth_mark_baseline();
        return;
    }
    
    // Suppress time output in tests
    set_suppress_time_output(true);
    
    // CRITICAL: Reset all runtime state for test isolation
    // This consolidates all reset operations for CljRuntime
    runtime_reset(&g_runtime);
    
    // runtime_init may call functions that use AUTORELEASE (e.g., make_map, vector_transient)
    WITH_AUTORELEASE_POOL({
        runtime_init(&g_runtime);
    });
    event_loop_init();  // Initialize event loop keywords
    
    // Set clojure.core to quiet mode BEFORE any eval state is created
    // This suppresses "=== Loading Clojure Core Functions ===" output
    clojure_core_set_quiet(true);
    
    // IMPORTANT:
    // runtime_reset() clears the symbol table. Even though the global SYM_* pointers
    // persist, they must be re-registered in the symbol table for intern_symbol_global()
    // to return the singleton pointers (pointer identity tests rely on this).
    init_special_symbols();
    g_special_symbols_initialized = true;
    
    
    if (!g_runtime.builtins_registered) {
        meta_registry_init();
        register_builtins();
        g_runtime.builtins_registered = true;
    }
        
#if MEMORY_PROFILING_ENABLED
        if (!g_memory_profiler_initialized) {
            MEMORY_PROFILER_INIT();
            g_memory_profiler_initialized = true;
        }
        enable_memory_profiling(true);
        set_memory_verbose_mode(g_single_test_mode);
        memory_set_debug_output_enabled(memory_get_debug_output_enabled());
#endif

// Zombie mode is automatically enabled via __attribute__((constructor)) if ZOMBIE_ENABLED is defined
    
    // Load clojure.core only for groups that need it; others run with builtins/special forms only.
    bool load_core = (g_current_test_entry == NULL) || !group_runs_without_core(g_current_test_entry);
    TRY {
        WITH_AUTORELEASE_POOL({
            evalstate_reset(&g_test_eval_state, load_core);
        });
    } CATCH(ex) {
        if (ex) {
            test_fprintf(stderr, "Warning: Exception during clojure.core loading: %s - %s\n",
                         ex->type, ex->message);
        }
    } END_TRY

    warm_shared_group_for_heap_baseline();

    test_heap_growth_mark_baseline();
}

EvalState* test_get_eval_state(void) {
    return g_test_eval_state;
}

void test_ensure_clojure_core(void) {
    if (g_test_eval_state)
        load_clojure_core(g_test_eval_state);
}

void tearDown(void) {
    // In batch mode, skip heavy teardown
    if (g_batch_mode) {
        // Same semantics as non-batch: exclude test-local autorelease residues
        // from heap growth checks.
        autorelease_pool_drain_to_depth(g_heap_baseline_pool_depth);
        test_heap_growth_check();
        return;
    }
    
    set_suppress_time_output(false);
    
    if (g_test_eval_state) {
        g_test_eval_state->current_ns = NULL;
    }
    
    if (g_memory_verbose_mode) {
        memory_profiler_print_stats("Test Complete");
    }
    // Exclude test-local autoreleased temporaries (including eval results) from
    // heap growth checks by restoring pool depth to the setup baseline first.
    autorelease_pool_drain_to_depth(g_heap_baseline_pool_depth);
    test_heap_growth_check();
    memory_profiler_check_leaks("Test Complete");
    // Drain and free autorelease pool so it does not grow across tests (e.g. when
    // tests throw and WITH_AUTORELEASE_POOL drain is skipped), avoiding Vector+3.5MB
    // growth in integer_overflow_detection and similar.
    autorelease_pool_free();
    fs_global_store_reset();
    runtime_reset(&g_runtime);
    // Reset symbol table between tests to avoid cross-test contamination.
    symbol_table_cleanup();
#ifdef __APPLE__
    int heap_ok = malloc_zone_check(malloc_default_zone());
    if (heap_ok != 1) {
        test_fprintf(stderr, "Heap check failed after test (malloc_zone_check=%d)\n", heap_ok);
        fflush(stderr);
        abort();
    }
#endif
}

// ============================================================================
// EMBEDDED ARRAY TESTS (defined in this file)
// ============================================================================

// Forward declarations for embedded array tests (now using TEST macro)
// No forward declarations needed - TEST macro handles registration automatically

// Note: All other tests are automatically registered via TEST() macro
// No extern declarations needed for tests using TEST() macro

// ============================================================================
// COMMAND LINE INTERFACE
// ============================================================================


// Legacy test group functions removed - all tests use TEST() macro for automatic registration
// Legacy command-line options now use run_tests_by_registry() which runs all registered tests

// ============================================================================
// COMMAND LINE INTERFACE
// ============================================================================
// Note: Command-line interface and main function are now in subjective-c/tests/test_runner.c
// This file only contains tiny-clj specific setUp/tearDown and helper functions

// Helper function to set Unity's TestFile and CurrentTestLineNumber for correct error reporting
static void set_unity_test_file_info(const SubjectiveCTestEntry *entry) {
    if (entry->file) {
        Unity.TestFile = entry->file;
    }
    if (entry->line > 0) {
        Unity.CurrentTestLineNumber = (UNITY_LINE_TYPE)entry->line;
    }
}

// Helper function to run a single test with exception handling
static void run_test_with_exception_handling(const SubjectiveCTestEntry *entry) {
    FILE *captured_stdout = NULL;
    int saved_stdout = -1;
    bool capturing_stdout = false;
    bool test_failed = false;
    bool quiet_capture_failed = false;

    // In quiet mode, capture stdout to a temporary file
    if (g_quiet_output) {
        captured_stdout = tmpfile();
        if (!captured_stdout) {
            test_fprintf(stderr, "Warning: Could not create temporary file for stdout capture, running test normally\n");
            quiet_capture_failed = true;
        } else {
            saved_stdout = dup(STDOUT_FILENO);
            if (saved_stdout < 0) {
                fclose(captured_stdout);
                captured_stdout = NULL;
                test_fprintf(stderr, "Warning: Could not save stdout, running test normally\n");
                quiet_capture_failed = true;
            } else {
                if (dup2(fileno(captured_stdout), STDOUT_FILENO) < 0) {
                    close(saved_stdout);
                    saved_stdout = -1;
                    fclose(captured_stdout);
                    captured_stdout = NULL;
                    test_fprintf(stderr, "Warning: Could not redirect stdout, running test normally\n");
                    quiet_capture_failed = true;
                } else {
                    capturing_stdout = true;
                }
            }
        }
    }

    // Save initial failure count to detect if this test failed
    UNITY_COUNTER_TYPE initial_failures = Unity.TestFailures;
    
    g_current_test_entry = entry;
    TRY {
        // Call Unity directly with the line number from the test registry.
        // This avoids using RUN_TEST(__LINE__) from this file, so that the
        // reported line matches the TEST() macro in the test source file.
        const char *cname = entry->qualified_name ? entry->qualified_name : entry->name;
        WITH_AUTORELEASE_POOL({
            UnityDefaultTestRun(entry->fn, cname, (UNITY_LINE_TYPE)entry->line);
        });
        
        // Check if test failed by comparing failure count
        // UnityConcludeTest() is called inside UnityDefaultTestRun() and increments
        // Unity.TestFailures if the test failed
        test_failed = (Unity.TestFailures > initial_failures);
    } CATCH(ex) {
        // Unhandled exception caught - mark test as failed
        test_failed = true;
        if (ex) {
            test_fprintf(stderr, "Unhandled exception in %s: %s - %s\n",
                         entry->qualified_name, ex->type, ex->message);
            if (ex->stacktrace) {
                print_exception(ex);
            }
        }
        // IMPORTANT: Do not increment Unity.TestFailures directly here.
        // Let UnityConcludeTest() print the FAIL line and increment failures exactly once.
        Unity.CurrentTestFailed = 1;

        // Guard against exceptions thrown before UnityDefaultTestRun() had a chance
        // to initialize Unity.CurrentTestName.
        if (Unity.CurrentTestName == NULL) {
            Unity.CurrentTestName = entry->qualified_name ? entry->qualified_name : entry->name;
        }

        UnityConcludeTest();
    } END_TRY
    g_current_test_entry = NULL;

    // Restore stdout if we captured it
    if (capturing_stdout && saved_stdout >= 0) {
        fflush(stdout);  // Flush any remaining output to temp file
        dup2(saved_stdout, STDOUT_FILENO);
        close(saved_stdout);
        saved_stdout = -1;
    }

    // Quiet mode contract: print ONLY FAIL lines (no PASS lines).
    // We intentionally do NOT replay captured stdout, because it can contain arbitrary
    // output (including Unity PASS lines when capture fails). Instead, emit a single
    // normalized FAIL line when the test fails.
    if (g_quiet_output) {
        if (test_failed) {
            const char *file = (entry && entry->file) ? entry->file : "unknown";
            int line = (entry && entry->line > 0) ? entry->line : 0;
            const char *name = (entry && entry->qualified_name) ? entry->qualified_name : (entry ? entry->name : "unknown");
            if (line > 0) {
                test_fprintf(stderr, "%s:%d:%s:FAIL\n", file, line, name);
            } else {
                test_fprintf(stderr, "%s:%s:FAIL\n", file, name);
            }
        }
        if (test_failed) {
            const char *file = (entry && entry->file) ? entry->file : "unknown";
            int line = (entry && entry->line > 0) ? entry->line : 0;
            const char *name = (entry && entry->qualified_name) ? entry->qualified_name : (entry ? entry->name : "unknown");
            if (line > 0) {
                test_fprintf(stdout, "%s:%d:%s:FAIL\n", file, line, name);
            } else {
                test_fprintf(stdout, "%s:%s:FAIL\n", file, name);
            }
        }
        if (quiet_capture_failed) {
            test_fprintf(stderr, "Note: Quiet output capture failed; PASS lines may appear.\n");
        }
        if (captured_stdout) {
            fclose(captured_stdout);
            captured_stdout = NULL;
        }
    } else if (captured_stdout) {
        // Not quiet: close any tmpfile we opened.
        fclose(captured_stdout);
        captured_stdout = NULL;
    }

#if MEMORY_PROFILING_ENABLED
    // In quiet mode, stdout was captured during the test. Emit per-type stats
    // after restoring stdout so they are visible for single-test runs.
    if (g_single_test_mode && g_quiet_output && g_memory_profiling_enabled) {
        bool old_verbose = g_memory_verbose_mode;
        g_memory_verbose_mode = true;
        memory_profiler_print_stats("Single Test");
        g_memory_verbose_mode = old_verbose;
    }
#endif
}

// Run shared tests in batches (one setUp/tearDown per batch)
void run_shared_tests_batched(void) {
    size_t test_count;
    const SubjectiveCTestEntry *all_tests = subjective_c_test_registry_entries(&test_count);
    
    // Collect unique shared groups
    const char *shared_groups[64];
    size_t group_count = 0;
    
    for (size_t i = 0; i < test_count; i++) {
        if (strncmp(all_tests[i].group, "shared_", 7) == 0) {
            // Check if group already collected
            bool found = false;
            for (size_t j = 0; j < group_count; j++) {
                if (strcmp(shared_groups[j], all_tests[i].group) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found && group_count < 64) {
                shared_groups[group_count++] = all_tests[i].group;
            }
        }
    }
    
    // Run each shared group as a batch
    for (size_t g = 0; g < group_count; g++) {
        // One setUp for the batch
        g_batch_mode = false;
        TRY {
            setUp();
            g_batch_mode = true;
            
            // Run all tests in this group
            for (size_t i = 0; i < test_count; i++) {
                if (strcmp(all_tests[i].group, shared_groups[g]) == 0) {
                    set_unity_test_file_info(&all_tests[i]);
                    run_test_with_exception_handling(&all_tests[i]);
                }
            }
            
            // One tearDown for the batch
            g_batch_mode = false;
            tearDown();
        } CATCH(ex) {
            // Exception in setUp/tearDown - mark batch as failed
            if (ex) {
                test_fprintf(stderr, "Exception in setUp/tearDown for shared batch %s: %s - %s\n",
                             shared_groups[g], ex->type, ex->message);
            }
            Unity.TestFailures++;
            g_batch_mode = false;
        } END_TRY
    }
}

// One-line test runner: Unity already prints one line per test.
// Each test gets setUp/tearDown so clojure.core is loaded (unless group_runs_without_core).
void run_tests_by_registry_impl(void) {
    size_t test_count;
    const SubjectiveCTestEntry *all_tests = subjective_c_test_registry_entries(&test_count);
    g_single_test_mode = false;

    for (size_t i = 0; i < test_count; i++) {
        const SubjectiveCTestEntry *e = &all_tests[i];
        set_unity_test_file_info(e);
        g_current_test_entry = e;
        TRY {
            setUp();
            run_test_with_exception_handling(e);
        } CATCH(ex) {
            if (ex)
                test_fprintf(stderr, "Exception in setUp/tearDown for %s: %s - %s\n",
                            e->qualified_name ? e->qualified_name : e->name, ex->type, ex->message);
        } END_TRY
        tearDown();
    }
}

static bool contains_wildcard(const char *pattern) {
    return strchr(pattern, '*') != NULL;
}

void run_specific_test_impl(const char *test_name_or_pattern) {
    g_single_test_mode = false;
    // Check if it's a wildcard pattern
    if (contains_wildcard(test_name_or_pattern)) {
        // Use pattern matching logic
        size_t test_count;
        const SubjectiveCTestEntry *all_tests = subjective_c_test_registry_entries(&test_count);
        int found = 0;
        
        // First pass: count matching tests
        for (size_t i = 0; i < test_count; i++) {
            // Try matching against qualified name first
            if (subjective_c_test_name_matches_pattern(all_tests[i].qualified_name, test_name_or_pattern)) {
                found++;
            }
            // Fallback to simple name matching for backward compatibility
            else if (subjective_c_test_name_matches_pattern(all_tests[i].name, test_name_or_pattern)) {
                found++;
            }
        }
        
        if (found == 0) {
            // No tests found - treat as a test selection error.
            // In quiet mode, emit a single FAIL line so CI surfaces the reason.
            if (g_quiet_output) {
                test_fprintf(stdout, "unknown:0:%s:FAIL: No tests found matching pattern\n", test_name_or_pattern);
            } else {
                test_fprintf(stderr, "ERROR: No tests found matching pattern: %s\n", test_name_or_pattern);
                test_fprintf(stdout, "unknown:0:%s:FAIL: No tests found matching pattern\n", test_name_or_pattern);
            }
            Unity.NumberOfTests++;
            Unity.TestFailures++;
            return;
        }

        if (found == 1) {
            g_single_test_mode = true;
        }

        // Set g_current_test_entry to first matching test so setUp() gets correct heap limit and load_core
        for (size_t i = 0; i < test_count; i++) {
            if (subjective_c_test_name_matches_pattern(all_tests[i].qualified_name, test_name_or_pattern) ||
                subjective_c_test_name_matches_pattern(all_tests[i].name, test_name_or_pattern)) {
                g_current_test_entry = &all_tests[i];
                break;
            }
        }
        // One-line output for pattern matching (setUp/tearDown so clojure.core is loaded)
        TRY {
            setUp();
            for (size_t i = 0; i < test_count; i++) {
                if (subjective_c_test_name_matches_pattern(all_tests[i].qualified_name, test_name_or_pattern) ||
                    subjective_c_test_name_matches_pattern(all_tests[i].name, test_name_or_pattern)) {
                    set_unity_test_file_info(&all_tests[i]);
                    run_test_with_exception_handling(&all_tests[i]);
                }
            }
            tearDown();
        } CATCH(ex) {
            if (ex) test_fprintf(stderr, "Exception in setUp/tearDown: %s - %s\n", ex->type, ex->message);
            tearDown();
        } END_TRY
    } else {
        // Exact name match (existing logic)
        SubjectiveCTestEntry *test = NULL;
        
        // First try to find by qualified name
        test = subjective_c_test_registry_find_by_qualified_name(test_name_or_pattern);
        
        // If not found, try by simple name (backward compatibility)
        if (!test) {
            test = subjective_c_test_registry_find(test_name_or_pattern);
        }
        
        if (test) {
            g_single_test_mode = true;
            TRY {
                setUp();
                set_unity_test_file_info(test);
                run_test_with_exception_handling(test);
                tearDown();
            } CATCH(ex) {
                if (ex) test_fprintf(stderr, "Exception in setUp/tearDown: %s - %s\n", ex->type, ex->message);
                tearDown();
            } END_TRY
            // Summary will be printed at end of main()
        } else {
            // Test not found - fail without noisy output.
            // Emit a single FAIL line so CI/user sees the reason (quiet or verbose).
            if (!g_quiet_output) {
                test_fprintf(stderr, "ERROR: Test '%s' not found.\n", test_name_or_pattern);
            }
            test_fprintf(stdout, "unknown:0:%s:FAIL: Test not found\n", test_name_or_pattern);
            Unity.NumberOfTests++;
            Unity.TestFailures++;
        }
    }
}

// Set quiet output mode (suppress PASS lines and stdout from passing tests)
void tiny_clj_tests_set_quiet_output(bool quiet) {
    g_quiet_output = quiet;
}

// Tiny-CLJ specific cleanup function (called from test_runner.c)
void tiny_clj_test_cleanup(bool show_memory_summary) {
    (void)show_memory_summary;
#if MEMORY_PROFILING_ENABLED
    if (show_memory_summary) {
        // Memory profiling without printf output (silent mode for tests)
        memory_profiler_print_stats("All Tests Complete");
        memory_profiler_check_leaks("All Tests Complete");
    } else {
        // Memory leak summary only if there are leaks (JUnit-style: minimal output)
        if (g_memory_stats.memory_leaks > 0) {
            memory_profiler_check_leaks("All Tests Complete");
        }
    }
#endif
    
    if (g_test_eval_state) {
        g_test_eval_state->current_ns = NULL;
    }
    
    reset_eval_state_current_ns();
    
    runtime_reset(&g_runtime);
    
    evalstate_free(g_test_eval_state);
    g_test_eval_state = NULL;
}

// Note: main() function is now in subjective-c/tests/test_runner.c

// ============================================================================
// EMBEDDED ARRAY TESTS
// ============================================================================
// Note: Embedded array tests moved to test_map.c
