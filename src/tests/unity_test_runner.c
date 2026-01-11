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
#include "unity/src/unity_internals.h"  // For Unity.TestFile and Unity.CurrentTestLineNumber
#include "build_info.h"
#include <time.h>
#include <unistd.h>

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

// Global test EvalState (available in all tests via tests_common.h)
EvalState *g_test_eval_state = NULL;

// ============================================================================
// GLOBAL SETUP/TEARDOWN
// ============================================================================


void setUp(void) {
    // Always reset memory profiler statistics BEFORE each test
    memory_profiler_reset();
    
    // In batch mode, skip heavy initialization (clojure.core already loaded)
    if (g_batch_mode) {
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
    
    // Initialize special symbols only once (they should persist across tests)
    if (!g_special_symbols_initialized) {
        init_special_symbols();
        g_special_symbols_initialized = true;
    }
    
    
    if (!g_runtime.builtins_registered) {
        meta_registry_init();
        WITH_AUTORELEASE_POOL({
            register_builtins();
        });
        g_runtime.builtins_registered = true;
    }
        
#if MEMORY_PROFILING_ENABLED
        MEMORY_PROFILER_INIT();
        enable_memory_profiling(true);
        set_memory_verbose_mode(false);
        // Update cached debug output flag after initialization
        extern void memory_update_debug_output_active(void);
        memory_update_debug_output_active();
#endif

// Zombie mode is automatically enabled via __attribute__((constructor)) if ZOMBIE_ENABLED is defined
    
    // Load clojure.core for each test (refresh state between tests)
    // Use autorelease pool for load_clojure_core to handle AUTORELEASE calls
    // Wrap in TRY/CATCH to handle ParseErrors during clojure.core loading
    TRY {
        WITH_AUTORELEASE_POOL({
            evalstate_reset(&g_test_eval_state, true);
        });
    } CATCH(ex) {
        // ParseError during clojure.core loading - log but continue
        if (ex) {
            fprintf(stderr, "Warning: Exception during clojure.core loading: %s - %s\n", 
                    ex->type, ex->message);
        }
        // Continue anyway - some tests may not need clojure.core
    } END_TRY
}

// Get the global test evalState (with inc available)
EvalState* test_get_eval_state(void) {
    return g_test_eval_state;
}

void tearDown(void) {
    // In batch mode, skip heavy teardown
    if (g_batch_mode) {
        return;
    }
    
    set_suppress_time_output(false);
    
    if (g_test_eval_state) {
        g_test_eval_state->current_ns = NULL;
    }
    
    if (g_memory_verbose_mode) {
        memory_profiler_print_stats("Test Complete");
    }
    memory_profiler_check_leaks("Test Complete");
    
    runtime_reset(&g_runtime);
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

    // In quiet mode, capture stdout to a temporary file
    if (g_quiet_output) {
        captured_stdout = tmpfile();
        if (!captured_stdout) {
            fprintf(stderr, "Warning: Could not create temporary file for stdout capture, running test normally\n");
        } else {
            saved_stdout = dup(STDOUT_FILENO);
            if (saved_stdout < 0) {
                fclose(captured_stdout);
                captured_stdout = NULL;
                fprintf(stderr, "Warning: Could not save stdout, running test normally\n");
            } else {
                if (dup2(fileno(captured_stdout), STDOUT_FILENO) < 0) {
                    close(saved_stdout);
                    saved_stdout = -1;
                    fclose(captured_stdout);
                    captured_stdout = NULL;
                    fprintf(stderr, "Warning: Could not redirect stdout, running test normally\n");
                } else {
                    capturing_stdout = true;
                }
            }
        }
    }

    // Save initial failure count to detect if this test failed
    UNITY_COUNTER_TYPE initial_failures = Unity.TestFailures;
    
    TRY {
        // Call Unity directly with the line number from the test registry.
        // This avoids using RUN_TEST(__LINE__) from this file, so that the
        // reported line matches the TEST() macro in the test source file.
        const char *cname = entry->qualified_name ? entry->qualified_name : entry->name;
        UnityDefaultTestRun(entry->fn, cname, (UNITY_LINE_TYPE)entry->line);
        
        // Check if test failed by comparing failure count
        // UnityConcludeTest() is called inside UnityDefaultTestRun() and increments
        // Unity.TestFailures if the test failed
        test_failed = (Unity.TestFailures > initial_failures);
    } CATCH(ex) {
        // Unhandled exception caught - mark test as failed
        test_failed = true;
        if (ex) {
            fprintf(stderr, "Unhandled exception in %s: %s - %s\n", 
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

    // Restore stdout if we captured it
    if (capturing_stdout && saved_stdout >= 0) {
        fflush(stdout);  // Flush any remaining output to temp file
        dup2(saved_stdout, STDOUT_FILENO);
        close(saved_stdout);
        saved_stdout = -1;
    }

    // In quiet mode, replay captured output only if test failed
    if (g_quiet_output && capturing_stdout && captured_stdout != NULL) {
        if (test_failed) {
            // Test failed - replay captured stdout
            fflush(captured_stdout);
            fseek(captured_stdout, 0, SEEK_SET);

            char buffer[4096];
            size_t bytes_read;
            size_t bytes_total = 0;

            // Detect if we saw a FAIL token in the replayed output.
            // This matters for failures that don't trigger a Unity assertion line
            // (e.g. exceptions). In that case we emit a minimal FAIL line.
            int fail_match = 0; // matches "FAIL"
            while ((bytes_read = fread(buffer, 1, sizeof(buffer), captured_stdout)) > 0) {
                bytes_total += bytes_read;
                fwrite(buffer, 1, bytes_read, stdout);

                for (size_t i = 0; i < bytes_read; i++) {
                    char c = buffer[i];
                    if ((fail_match == 0 && c == 'F') ||
                        (fail_match == 1 && c == 'A') ||
                        (fail_match == 2 && c == 'I') ||
                        (fail_match == 3 && c == 'L')) {
                        fail_match++;
                        if (fail_match == 4) {
                            break;
                        }
                    } else {
                        fail_match = (c == 'F') ? 1 : 0;
                    }
                }
                if (fail_match == 4) {
                    // keep draining, but we already know we saw FAIL
                }
            }

            if (fail_match != 4) {
                const char *file = (entry && entry->file) ? entry->file : "unknown";
                int line = (entry && entry->line > 0) ? entry->line : 0;
                const char *name = (entry && entry->qualified_name) ? entry->qualified_name : (entry ? entry->name : "unknown");

                if (line > 0) {
                    fprintf(stdout, "%s:%d:%s:FAIL\n", file, line, name);
                } else {
                    fprintf(stdout, "%s:%s:FAIL\n", file, name);
                }
            }

            // If absolutely nothing was captured, still ensure a FAIL line exists.
            // (The FAIL token detection above covers most cases.)
            if (bytes_total == 0) {
                const char *file = (entry && entry->file) ? entry->file : "unknown";
                int line = (entry && entry->line > 0) ? entry->line : 0;
                const char *name = (entry && entry->qualified_name) ? entry->qualified_name : (entry ? entry->name : "unknown");
                if (line > 0) {
                    fprintf(stdout, "%s:%d:%s:FAIL\n", file, line, name);
                } else {
                    fprintf(stdout, "%s:%s:FAIL\n", file, name);
                }
            }
        }

        fclose(captured_stdout);
        captured_stdout = NULL;
    } else if (captured_stdout) {
        // Not capturing_stdout (or not quiet): close any tmpfile we opened.
        fclose(captured_stdout);
        captured_stdout = NULL;
    }
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
                fprintf(stderr, "Exception in setUp/tearDown for shared batch %s: %s - %s\n", 
                        shared_groups[g], ex->type, ex->message);
            }
            Unity.TestFailures++;
            g_batch_mode = false;
        } END_TRY
    }
}

// One-line test runner: Unity already prints one line per test
void run_tests_by_registry_impl(void) {
    size_t test_count;
    const SubjectiveCTestEntry *all_tests = subjective_c_test_registry_entries(&test_count);
    
    // First: Run shared tests batched (one setUp/tearDown per group)
    run_shared_tests_batched();
    
    // Then: Run non-shared tests normally (one setUp/tearDown per test)
    for (size_t i = 0; i < test_count; i++) {
        if (strncmp(all_tests[i].group, "shared_", 7) != 0) {
            // UnityDefaultTestRun() already calls setUp()/tearDown().
            // run_test_with_exception_handling() wraps UnityDefaultTestRun() in TRY/CATCH.
            set_unity_test_file_info(&all_tests[i]);
            run_test_with_exception_handling(&all_tests[i]);
        }
    }
}

static bool contains_wildcard(const char *pattern) {
    return strchr(pattern, '*') != NULL;
}

void run_specific_test_impl(const char *test_name_or_pattern) {
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
            // No tests found - silently return (no printf output in tests)
            return;
        }
        
        // One-line output for pattern matching
        for (size_t i = 0; i < test_count; i++) {
            if (subjective_c_test_name_matches_pattern(all_tests[i].qualified_name, test_name_or_pattern) ||
                subjective_c_test_name_matches_pattern(all_tests[i].name, test_name_or_pattern)) {
                set_unity_test_file_info(&all_tests[i]);
                run_test_with_exception_handling(&all_tests[i]);
            }
        }
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
            set_unity_test_file_info(test);
            run_test_with_exception_handling(test);
            // Summary will be printed at end of main()
        } else {
            // Test not found - fail without noisy output.
            // In quiet mode, still emit a single FAIL line so CI can surface the reason.
            if (g_quiet_output) {
                fprintf(stdout, "unknown:0:%s:FAIL: Test not found\n", test_name_or_pattern);
            }
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
