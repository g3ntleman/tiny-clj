/*
 * Test Registry for MinUnit Tests
 * 
 * Central registry for all MinUnit tests to enable single executable runner.
 * Pure C - no POSIX dependencies for STM32 compatibility.
 */

#ifndef TEST_REGISTRY_H
#define TEST_REGISTRY_H

typedef struct {
    const char *name;         // Test suite name
    const char *suite;        // Suite category
    char *(*test_func)(void); // Test runner function
} TestEntry;

// Forward declarations for remaining MinUnit test suite runners
extern char *run_namespace_tests(void);
extern char *run_seq_tests(void);
extern char *run_for_loop_tests(void);
extern char *run_eval_string_api_tests(void);
extern char *run_function_types_tests(void);
extern char *test_nil_arithmetic_suite(void);

// Line editor test declarations
extern char *run_line_editor_tests(void);
extern char *run_platform_mock_tests(void);
extern char *run_platform_abstraction_tests(void);
extern char *run_repl_line_editing_tests(void);

// Global test registry (compile-time, no dlsym needed)
static TestEntry all_minunit_tests[] = {
    {"namespace",       "core",    run_namespace_tests},
    {"seq",             "data",    run_seq_tests},
    {"for_loops",       "control", run_for_loop_tests},
    {"eval_string_api", "api",     run_eval_string_api_tests},
    {"function_types",  "core",    run_function_types_tests},
    {"nil_arithmetic",  "core",    test_nil_arithmetic_suite},
    
    // Line editor tests
    {"line_editor",     "ui",      run_line_editor_tests},
    {"platform_mock",   "ui",      run_platform_mock_tests},
    {"platform_abstraction", "ui", run_platform_abstraction_tests},
    {"repl_line_editing", "ui",    run_repl_line_editing_tests},
};

static const int minunit_test_count = sizeof(all_minunit_tests) / sizeof(all_minunit_tests[0]);

#endif // TEST_REGISTRY_H
