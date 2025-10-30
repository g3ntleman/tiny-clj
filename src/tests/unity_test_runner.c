/*
 * Unity Test Runner for Tiny-CLJ
 * 
 * Central test runner that includes all test suites with command-line parameter support.
 */

#include "tests_common.h"
#include "test_registry.h"
#include "memory_profiler.h"

// Access to global memory stats for leak checking
extern MemoryStats g_memory_stats;
extern bool g_memory_verbose_mode;


// ============================================================================
// GLOBAL SETUP/TEARDOWN
// ============================================================================


void setUp(void) {
    // Reset memory profiler statistics BEFORE each test
    memory_profiler_reset();
    
    runtime_init();
    
    if (!g_runtime.builtins_registered) {
        init_special_symbols();
        meta_registry_init();
            register_builtins();
            g_runtime.builtins_registered = true;
        }
        
#ifdef ENABLE_MEMORY_PROFILING
        MEMORY_PROFILER_INIT();
        enable_memory_profiling(true);
        set_memory_verbose_mode(false);
#endif
}

void tearDown(void) {
    if (g_memory_stats.memory_leaks > 0 || g_memory_verbose_mode) {
        memory_profiler_print_stats("Test Complete");
    }
    memory_profiler_check_leaks("Test Complete");
    
    runtime_free();
}

// ============================================================================
// MEMORY TESTS (from memory_tests.c)
// ============================================================================

// Forward declarations for memory tests (used in test_group_memory)
extern void test_memory_allocation(void);
extern void test_memory_deallocation(void);
extern void test_memory_leak_detection(void);
extern void test_vector_memory(void);
extern void test_autorelease_pool_basic(void);
extern void test_autorelease_pool_nested(void);
extern void test_autorelease_pool_memory_cleanup(void);
extern void test_cow_assumptions_rc_behavior(void);
extern void test_cow_actual_cow_demonstration(void);

// Forward declarations for COW functionality tests
extern void test_cow_inplace_mutation_rc_one(void);
extern void test_cow_copy_on_write_rc_greater_one(void);
extern void test_cow_original_map_unchanged(void);
extern void test_cow_with_autorelease(void);
extern void test_cow_memory_leak_detection(void);

// Forward declarations for COW eval integration tests
extern void test_cow_environment_loop_mutation(void);
extern void test_cow_closure_environment_sharing(void);
extern void test_cow_memory_efficiency_benchmark(void);
extern void test_cow_real_clojure_simulation(void);

// Forward declarations for embedded array tests
void test_embedded_array_single_malloc(void);
void test_embedded_array_memory_efficiency(void);
void test_embedded_array_cow(void);
void test_embedded_array_capacity_growth(void);
void test_embedded_array_performance(void);

// ============================================================================
// TEST GROUPS (Legacy - all tests now use TEST() macro)
// ============================================================================

// All test_group functions removed - tests now use TEST() macro for automatic registration

// ============================================================================
// MAIN TEST RUNNER FUNCTIONS
// ============================================================================

// ============================================================================
// COW EVAL INTEGRATION TESTS
// ============================================================================


// ============================================================================
// PARSER TESTS (from parser_tests.c)
// ============================================================================

// Parser tests are now registered automatically via TEST macro

// ============================================================================
// BYTE ARRAY TESTS (from byte_array_tests.c)
// ============================================================================

// Forward declaration for byte array test runner
extern void run_byte_array_tests(void);


// ============================================================================
// EXCEPTION TESTS (from exception_tests.c)
// ============================================================================

// Forward declarations for exception tests (used in test_group_exception)
extern void test_simple_try_catch_exception_caught(void);
extern void test_simple_try_catch_no_exception(void);
extern void test_nested_try_catch_inner_exception(void);
extern void test_nested_try_catch_outer_exception(void);
extern void test_exception_with_autorelease(void);


// ============================================================================
// UNIT TESTS (from unit_tests.c)
// ============================================================================

// Forward declarations for unit tests
extern void test_list_count(void);
extern void test_list_creation(void);
extern void test_symbol_creation(void);
extern void test_string_creation(void);
extern void test_vector_creation(void);
extern void test_map_creation(void);
extern void test_identical_predicate(void);
extern void test_vector_predicate(void);
// extern void test_cond_special_form(void); // TODO: Fix cond
extern void test_array_map_builtin(void);
extern void test_integer_creation(void);
extern void test_float_creation(void);
extern void test_nil_creation(void);

// Fixed-Point arithmetic tests
extern void test_fixed_creation_and_conversion(void);
extern void test_fixed_arithmetic_operations(void);
extern void test_fixed_mixed_type_operations(void);
extern void test_fixed_division_with_remainder(void);
extern void test_fixed_precision_limits(void);
extern void test_fixed_variadic_operations(void);
extern void test_fixed_error_handling(void);
extern void test_fixed_comparison_operators(void);

// Fixed-Point detailed tests
extern void test_fixed_basic_creation(void);
extern void test_fixed_negative_values(void);
extern void test_fixed_precision(void);
extern void test_fixed_multiplication_raw(void);
extern void test_fixed_mixed_type_promotion(void);
extern void test_fixed_saturation_max(void);
extern void test_fixed_saturation_min(void);
extern void test_fixed_division_raw(void);
extern void test_fixed_edge_cases(void);
extern void test_fixed_tag_consistency(void);
extern void test_fixed_addition_builtin(void);
extern void test_fixed_subtraction_builtin(void);
extern void test_fixed_mixed_addition(void);
extern void test_fixed_negative_addition(void);
extern void test_fixed_multiplication_builtin(void);
extern void test_fixed_division_builtin(void);
extern void test_fixed_mixed_multiplication(void);
extern void test_fixed_division_by_zero(void);
extern void test_fixed_complex_arithmetic(void);

// CljValue API tests
extern void test_cljvalue_immediate_helpers(void);
extern void test_cljvalue_vector_api(void);
extern void test_cljvalue_transient_vector(void);
extern void test_cljvalue_clojure_semantics(void);
extern void test_cljvalue_wrapper_functions(void);

// New immediate value tests
extern void test_cljvalue_immediates_fixnum(void);
extern void test_cljvalue_immediates_char(void);
extern void test_cljvalue_immediates_special(void);
extern void test_cljvalue_immediates_fixed(void);
extern void test_cljvalue_parser_immediates(void);
extern void test_cljvalue_memory_efficiency(void);

// Transient map tests
// extern void test_cljvalue_transient_map_clojure_semantics(void); // Moved to test_values.c

// High-level integration tests
// extern void test_cljvalue_transient_maps_high_level(void); // Moved to test_values.c
extern void test_cljvalue_vectors_high_level(void);
extern void test_cljvalue_immediates_high_level(void);
extern void test_simple_arithmetic(void);
extern void test_division_by_zero_exception(void);

// Special forms tests
extern void test_special_form_and(void);
extern void test_special_form_or(void);

// Performance tests
extern void test_seq_rest_performance(void);
// extern void test_seq_iterator_verification(void); // Disabled due to implementation issues

// Multiline file loading test
extern void test_load_multiline_file(void);

// Map function test
extern void test_map_function(void);

// Debugging tests
extern void test_as_list_valid(void);
extern void test_as_list_invalid(void);
extern void test_list_first_valid(void);
extern void test_is_type_function(void);
extern void test_eval_list_simple_arithmetic(void);
extern void test_eval_list_function_call(void);

// Recur tests
extern void test_recur_factorial(void);
extern void test_recur_deep_recursion(void);
extern void test_recur_arity_error(void);
extern void test_recur_countdown(void);
extern void test_recur_sum(void);
extern void test_recur_tail_position_error(void);
extern void test_if_bug_in_functions(void);
extern void test_integer_overflow_detection(void);

// Equal function tests
extern void test_equal_null_pointers(void);
extern void test_equal_same_objects(void);
extern void test_equal_different_strings(void);
extern void test_equal_different_types(void);
extern void test_equal_immediate_values(void);
extern void test_vector_equal_same_vectors(void);
extern void test_vector_equal_different_lengths(void);
extern void test_vector_equal_different_values(void);
extern void test_clj_equal_id_function(void);
extern void test_vector_equal_with_strings(void);
extern void test_list_equal_same_lists(void);
extern void test_list_equal_same_instance(void);
extern void test_list_equal_empty_lists(void);
extern void test_map_equal_same_maps(void);
extern void test_map_equal_different_keys(void);
extern void test_map_equal_different_values(void);
extern void test_map_equal_different_sizes(void);
extern void test_map_equal_with_nested_vectors(void);

// Forward declarations for conj and rest tests
extern void test_conj_arity_0(void);
extern void test_conj_arity_1(void);
extern void test_conj_arity_2(void);
extern void test_conj_arity_variadic(void);
extern void test_conj_nil_collection(void);
extern void test_rest_arity_0(void);
extern void test_rest_nil(void);
extern void test_rest_empty_vector(void);
extern void test_rest_single_element(void);
extern void test_group_conj_rest(void);



// ============================================================================
// NAMESPACE TESTS (from namespace_tests.c)
// ============================================================================

// Forward declarations for namespace tests
extern void test_evalstate_creation(void);
extern void test_namespace_switching(void);
extern void test_namespace_isolation(void);
extern void test_special_ns_variable(void);
extern void test_namespace_lookup(void);
extern void test_namespace_binding(void);

// Namespace tests (from test_namespace.c) - now self-registering via TEST() macro


// ============================================================================
// SEQ TESTS (from seq_tests.c)
// ============================================================================

// Forward declarations for seq tests
extern void test_seq_create_list(void);
extern void test_seq_create_vector(void);
extern void test_seq_create_string(void);
extern void test_seq_create_map(void);
extern void test_seq_first(void);
extern void test_seq_rest(void);
extern void test_seq_next(void);
extern void test_seq_equality(void);


// ============================================================================
// FOR-LOOP TESTS (from for_loop_tests.c)
// ============================================================================

// ============================================================================
// DEFN FUNCTION TESTS (from test_defn.c)
// ============================================================================

// Forward declarations for defn tests
extern void test_defn_basic_function(void);
extern void test_defn_single_parameter(void);
extern void test_defn_no_parameters(void);
extern void test_defn_multiple_body_expressions(void);
extern void test_defn_recursive_function(void);
extern void test_defn_symbol_resolution_in_repl_context(void);

extern void test_time_returns_result(void);
extern void test_time_with_let(void);
extern void test_time_with_function_call(void);
extern void test_time_measures_duration(void);

// Forward declarations for dotimes tests
extern void test_dotimes_basic_functionality(void);
extern void test_dotimes_variable_binding_problem(void);
extern void test_dotimes_with_time_measurement(void);
extern void test_dotimes_arity_validation(void);
extern void test_dotimes_invalid_binding_format_new(void);



// ============================================================================
// LET BINDING TESTS (from test_let.c)
// ============================================================================

// Forward declarations for let binding tests
extern void test_let_basic_binding(void);
extern void test_let_multiple_bindings(void);
extern void test_let_sequential_bindings(void);
extern void test_let_expression_body(void);
extern void test_let_multiple_body_expressions(void);
extern void test_let_nested(void);
extern void test_let_shadowing(void);
extern void test_let_with_function_calls(void);
extern void test_let_empty_bindings(void);

// static void test_group_let(void) {
    // RUN_TEST(test_let_basic_binding);
    // RUN_TEST(test_let_multiple_bindings);
    // RUN_TEST(test_let_sequential_bindings);
    // RUN_TEST(test_let_expression_body);
    // RUN_TEST(test_let_multiple_body_expressions);
    // RUN_TEST(test_let_nested);
    // RUN_TEST(test_let_shadowing);
    // RUN_TEST(test_let_with_function_calls);
    // RUN_TEST(test_let_empty_bindings);
// }

// ============================================================================
// DO SPECIAL FORM TESTS (from test_do.c)
// ============================================================================

// Forward declarations for do tests
extern void test_do_empty(void);
extern void test_do_single_expr(void);
extern void test_do_multiple_exprs(void);
extern void test_do_with_arithmetic(void);
extern void test_do_nested(void);
extern void test_do_in_if(void);
extern void test_do_in_if_else(void);
extern void test_do_mixed_types(void);
extern void test_do_last_nil(void);
extern void test_do_with_let(void);

// static void test_group_do(void) {
    // RUN_TEST(test_do_empty);
    // RUN_TEST(test_do_single_expr);
    // RUN_TEST(test_do_multiple_exprs);
    // RUN_TEST(test_do_with_arithmetic);
    // RUN_TEST(test_do_nested);
    // RUN_TEST(test_do_in_if);
    // RUN_TEST(test_do_in_if_else);
    // RUN_TEST(test_do_mixed_types);
    // RUN_TEST(test_do_last_nil);
    // RUN_TEST(test_do_with_let);
// }

// ============================================================================
// FOR-LOOP TESTS (from for_loop_tests.c)
// ============================================================================

// Forward declarations for for-loop tests
extern void test_dotimes_basic(void);
extern void test_doseq_basic(void);
extern void test_for_basic(void);
extern void test_dotimes_with_environment(void);
extern void test_doseq_with_environment(void);

// Symbol output tests are now in unit_tests.c


// Recur tests
extern void test_recur_factorial(void);
extern void test_recur_deep_recursion(void);
extern void test_recur_arity_error(void);

// static void test_group_for_loops(void) {
    // RUN_TEST(test_dotimes_basic);
    // RUN_TEST(test_doseq_basic);
    // RUN_TEST(test_for_basic);
    // RUN_TEST(test_dotimes_with_environment);
    // RUN_TEST(test_doseq_with_environment);
// }

// static void test_group_recur(void) {
    // RUN_TEST(test_recur_factorial);
    // RUN_TEST(test_recur_deep_recursion);
    // RUN_TEST(test_recur_arity_error);
    // RUN_TEST(test_recur_countdown);
    // RUN_TEST(test_recur_sum);
    // RUN_TEST(test_recur_tail_position_error);
    // RUN_TEST(test_if_bug_in_functions);
    // RUN_TEST(test_integer_overflow_detection);
// }




// ============================================================================
// FIXED-POINT ARITHMETIC TESTS
// ============================================================================

// Forward declarations for fixed-point tests
extern void test_fixed_creation_and_conversion(void);
extern void test_fixed_arithmetic_operations(void);
extern void test_fixed_mixed_type_operations(void);
extern void test_fixed_division_with_remainder(void);
extern void test_fixed_precision_limits(void);
extern void test_fixed_variadic_operations(void);
extern void test_fixed_error_handling(void);
extern void test_fixed_comparison_operators(void);


// ============================================================================
// SEQUENCE AND COLLECTION TESTS
// ============================================================================

// Forward declarations for sequence tests
extern void test_conj_arity_0(void);
extern void test_conj_arity_1(void);
extern void test_conj_arity_2(void);
extern void test_conj_arity_variadic(void);
extern void test_conj_nil_collection(void);
extern void test_rest_arity_0(void);
extern void test_rest_nil(void);
extern void test_rest_empty_vector(void);
extern void test_rest_single_element(void);
extern void test_seq_rest_performance(void);
extern void test_seq_iterator_verification(void);


// ============================================================================
// COW ASSUMPTIONS TESTS
// ============================================================================

// Forward declarations for COW assumptions tests (used in test_group_cow_assumptions)
extern void test_autorelease_does_not_increase_rc(void);
extern void test_retain_increases_rc(void);
extern void test_autorelease_with_retain(void);
extern void test_multiple_autorelease_same_object(void);
extern void test_autorelease_in_loop_realistic(void);


// Symbol output tests are now integrated into unit_tests.c

// ============================================================================
// COMMAND LINE INTERFACE
// ============================================================================


static void run_memory_tests(void) {
    // All tests are now automatically registered via TEST() macro
    // No need for manual test_group functions
}

static void run_parser_tests(void) {
    // Parser tests are now handled by the registry system
    printf("Parser tests are now handled by the registry system\n");
}

static void run_exception_tests(void) {
    // All tests are now automatically registered via TEST() macro
    // No need for manual test_group functions
}

static void run_unit_tests(void) {
    // All tests are now automatically registered via TEST() macro
    // No need for manual test_group functions
}

static void run_cljvalue_tests(void) {
    // All tests are now automatically registered via TEST() macro
    // No need for manual test_group functions
}

static void run_namespace_tests(void) {
    // All tests are now automatically registered via TEST() macro
    // No need for manual test_group functions
}

static void run_seq_tests(void) {
    // All tests are now automatically registered via TEST() macro
    // No need for manual test_group functions
}

static void run_for_loop_tests(void) {
    // All tests now use TEST() macro
}


static void run_equal_tests(void) {
    // All tests now use TEST() macro
}

// New logical test groups
static void run_core_tests(void) {
    // Parser tests are now handled by the registry system
    // // All tests now use TEST() macro // Temporarily disabled
    // All tests now use TEST() macro
    // All tests now use TEST() macro
}

static void run_data_tests(void) {
    // All tests now use TEST() macro
    // All tests now use TEST() macro
    // All tests now use TEST() macro
    // All tests now use TEST() macro
}

static void run_control_tests(void) {
    // All tests now use TEST() macro
    // All tests now use TEST() macro
    // All tests now use TEST() macro
}

// Symbol output tests are now integrated into unit_tests.c

static void run_all_tests(void) {
    // All tests now use TEST() macro
    // Parser tests are now handled by the registry system
    // All tests now use TEST() macro
    // All tests now use TEST() macro // Re-enabled for conj/rest tests
    // All tests now use TEST() macro
    // All tests now use TEST() macro  // Re-enabled after fixing double free
    // All tests now use TEST() macro
    // All tests now use TEST() macro  // Re-enabled after fixing type mismatch
    // All tests now use TEST() macro  // Re-enabled with minimal test
    // All tests now use TEST() macro
    // All tests now use TEST() macro
    // All tests now use TEST() macro
    // All tests now use TEST() macro // Re-enabled - recur functionality is working
    // All tests now use TEST() macro
    // All tests now use TEST() macro
}

// ============================================================================
// MAIN FUNCTION
// ============================================================================

// ============================================================================
// NEW COMMAND-LINE INTERFACE
// ============================================================================

static void print_new_usage(const char *program_name) {
    printf("Unity Test Runner for Tiny-CLJ (Dynamic Registry)\n");
    printf("Usage: %s [options]\n\n", program_name);
    printf("Options:\n");
    printf("  --test <name>        Run specific test by name (supports qualified names)\n");
    printf("  --filter <pattern>   Run tests matching pattern (supports * wildcard and exact names)\n");
    printf("  --list              List all available tests\n");
    printf("  --quiet             Run all tests with minimal memory leak output\n");
    printf("  --mem-debug         Enable verbose memory-debug logs during tests\n");
    printf("  --help, -h          Show this help\n");
    printf("  (no args)           Run all tests\n\n");
    printf("Test Names:\n");
    printf("  Tests use qualified names: <group>/<test> (without 'test_' prefix)\n");
    printf("  Examples: values/cljvalue_immediate_helpers\n");
    printf("           basics/list_count\n");
    printf("           fixed_point/fixed_creation_and_conversion\n\n");
    printf("Examples:\n");
    printf("  %s --test values/cljvalue_immediate_helpers\n", program_name);
    printf("  %s --filter \"values/*\"\n", program_name);
    printf("  %s --filter \"*/cljvalue_*\"\n", program_name);
    printf("  %s --filter \"*cow*\"\n", program_name);
    printf("  %s --quiet\n", program_name);
    printf("  %s --list\n", program_name);
    printf("  %s\n", program_name);
}

static void run_tests_by_registry(void) {
    size_t test_count;
    Test *all_tests = test_registry_get_all(&test_count);
    
    if (test_count == 0) {
        printf("No tests registered. Make sure test files include REGISTER_TEST() macros.\n");
        return;
    }
    
    printf("Running %zu registered tests...\n", test_count);
    
    for (size_t i = 0; i < test_count; i++) {
        printf("🧪 Running test: %s\n", all_tests[i].name);
        RUN_TEST(all_tests[i].func);
        printf("✅ Test completed: %s\n\n", all_tests[i].name);
    }
}

static void run_specific_test(const char *test_name) {
    Test *test = NULL;
    
    // First try to find by qualified name
    test = test_registry_find_by_qualified_name(test_name);
    
    // If not found, try by simple name (backward compatibility)
    if (!test) {
        test = test_registry_find(test_name);
    }
    
    if (test) {
        printf("🧪 Running specific test: %s\n", test->qualified_name);
        RUN_TEST(test->func);
        printf("✅ Test completed: %s\n", test->qualified_name);
    } else {
        printf("❌ Test not found: %s\n", test_name);
        printf("Use --list to see available tests\n");
    }
}

static void run_filtered_tests(const char *pattern) {
    size_t test_count;
    Test *all_tests = test_registry_get_all(&test_count);
    int found = 0;
    
    // First pass: count matching tests
    for (size_t i = 0; i < test_count; i++) {
        // Try matching against qualified name first
        if (test_name_matches_pattern(all_tests[i].qualified_name, pattern)) {
            found++;
        }
        // Fallback to simple name matching for backward compatibility
        else if (test_name_matches_pattern(all_tests[i].name, pattern)) {
            found++;
        }
    }
    
    if (found == 0) {
        printf("❌ No tests found matching pattern: %s\n", pattern);
        printf("Use --list to see available tests\n");
        return;
    }
    
    printf("🔍 Running %d test(s) matching pattern: %s\n", found, pattern);
    
    // Second pass: run matching tests
    for (size_t i = 0; i < test_count; i++) {
        // Try matching against qualified name first
        if (test_name_matches_pattern(all_tests[i].qualified_name, pattern)) {
            printf("🧪 Running: %s\n", all_tests[i].qualified_name);
            RUN_TEST(all_tests[i].func);
            printf("✅ Completed: %s\n\n", all_tests[i].qualified_name);
        }
        // Fallback to simple name matching for backward compatibility
        else if (test_name_matches_pattern(all_tests[i].name, pattern)) {
            printf("🧪 Running: %s\n", all_tests[i].qualified_name);
            RUN_TEST(all_tests[i].func);
            printf("✅ Completed: %s\n\n", all_tests[i].qualified_name);
        }
    }
    
    printf("✅ Ran %d test(s) matching pattern\n", found);
}


int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // Enable memory profiling for tests (only in DEBUG builds)
#ifdef ENABLE_MEMORY_PROFILING
    enable_memory_profiling(true);
    // Optional verbose memory logs via CLI flag --mem-debug
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--mem-debug") == 0) {
            set_memory_verbose_mode(true);
            break;
        }
    }
#endif
    
    // Parse command line arguments
    if (argc > 1) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            print_new_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[1], "--list") == 0) {
            test_registry_list_all();
            return 0;
        } else if (strcmp(argv[1], "--quiet") == 0) {
            // Reduce memory leak spam for cleaner output
            set_memory_leak_reporting_enabled(false);
            run_all_tests();
        } else if (strcmp(argv[1], "--mem-debug") == 0) {
            // Enable verbose memory debugging and run all tests
            set_memory_verbose_mode(true);
            run_all_tests();
        } else if (strcmp(argv[1], "--test") == 0) {
            if (argc < 3) {
                printf("Error: --test requires a test name\n");
                printf("Use --list to see available tests\n");
                return 1;
            }
            run_specific_test(argv[2]);
        } else if (strcmp(argv[1], "--filter") == 0) {
            if (argc < 3) {
                printf("Error: --filter requires a pattern\n");
                printf("Use --list to see available tests\n");
                return 1;
            }
            run_filtered_tests(argv[2]);
        } else {
            // Legacy suite-based interface for backward compatibility
            if (strcmp(argv[1], "memory") == 0) {
                run_memory_tests();
            } else if (strcmp(argv[1], "parser") == 0) {
                run_parser_tests();
            } else if (strcmp(argv[1], "exception") == 0) {
                run_exception_tests();
            } else if (strcmp(argv[1], "unit") == 0) {
                run_unit_tests();
            } else if (strcmp(argv[1], "fixed-point") == 0) {
                // All tests now use TEST() macro
            } else if (strcmp(argv[1], "sequences") == 0) {
                // All tests now use TEST() macro
            } else if (strcmp(argv[1], "cljvalue") == 0) {
                run_cljvalue_tests();
            } else if (strcmp(argv[1], "namespace") == 0) {
                run_namespace_tests();
            } else if (strcmp(argv[1], "seq") == 0) {
                run_seq_tests();
            } else if (strcmp(argv[1], "for-loops") == 0) {
                run_for_loop_tests();
            } else if (strcmp(argv[1], "let") == 0) {
                // All tests now use TEST() macro
            } else if (strcmp(argv[1], "do") == 0) {
                // All tests now use TEST() macro
            } else if (strcmp(argv[1], "defn") == 0) {
                // All tests now use TEST() macro
            } else if (strcmp(argv[1], "time") == 0) {
                // All tests now use TEST() macro
            } else if (strcmp(argv[1], "equal") == 0) {
                run_equal_tests();
            } else if (strcmp(argv[1], "byte-array") == 0) {
                // All tests now use TEST() macro
            } else if (strcmp(argv[1], "cow-assumptions") == 0) {
                // All tests now use TEST() macro
            } else if (strcmp(argv[1], "cow-functionality") == 0) {
                // All tests now use TEST() macro
            } else if (strcmp(argv[1], "cow-eval") == 0) {
                // All tests now use TEST() macro
            } else if (strcmp(argv[1], "core") == 0) {
                run_core_tests();
            } else if (strcmp(argv[1], "data") == 0) {
                run_data_tests();
            } else if (strcmp(argv[1], "control") == 0) {
                run_control_tests();
            } else if (strcmp(argv[1], "all") == 0) {
                run_all_tests();
            } else {
                printf("Unknown option: %s\n", argv[1]);
                printf("Use --help to see available options\n");
                return 1;
            }
        }
    } else {
        // Run all tests by default using new registry system
        run_tests_by_registry();
    }
    
    // Final memory leak summary after all tests
    printf("\n");
    printf("================================================================================\n");
    printf("🔍 FINAL MEMORY LEAK SUMMARY\n");
    printf("================================================================================\n");
    memory_profiler_check_leaks("All Tests Complete");
    printf("================================================================================\n\n");
    
    return UNITY_END();
}

// ============================================================================
// EMBEDDED ARRAY TESTS
// ============================================================================

void test_embedded_array_single_malloc(void) {
    printf("\n=== Test: Single Malloc für embedded array ===\n");
    
    WITH_AUTORELEASE_POOL({
        // Create map with embedded array
        CljMap *map = (CljMap*)make_map(4);
        printf("Map created with embedded array\n");
        
        // Verify embedded array is accessible
        TEST_ASSERT_NOT_NULL(map->data);
        TEST_ASSERT_EQUAL(4, map->capacity);
        TEST_ASSERT_EQUAL(0, map->count);
        
        // Add entries to test embedded array
        map_assoc_cow((CljValue)map, fixnum(1), fixnum(10));
        map_assoc_cow((CljValue)map, fixnum(2), fixnum(20));
        
        // Verify entries in embedded array
        CljValue val1 = map_get((CljValue)map, fixnum(1));
        CljValue val2 = map_get((CljValue)map, fixnum(2));
        TEST_ASSERT_NOT_NULL(val1);
        TEST_ASSERT_NOT_NULL(val2);
        TEST_ASSERT_EQUAL_INT(10, as_fixnum(val1));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum(val2));
        
        printf("✓ Embedded array funktioniert korrekt\n");
    });
}

void test_embedded_array_memory_efficiency(void) {
    printf("\n=== Test: Memory Efficiency ===\n");
    
    WITH_AUTORELEASE_POOL({
        // Create multiple maps to test memory efficiency
        CljMap *map1 = (CljMap*)make_map(2);
        CljMap *map2 = (CljMap*)make_map(4);
        CljMap *map3 = (CljMap*)make_map(8);
        
        // Add entries to each map
        map_assoc_cow((CljValue)map1, fixnum(1), fixnum(10));
        map_assoc_cow((CljValue)map2, fixnum(2), fixnum(20));
        map_assoc_cow((CljValue)map3, fixnum(3), fixnum(30));
        
        // Verify all maps work independently
        TEST_ASSERT_NOT_NULL(map_get((CljValue)map1, fixnum(1)));
        TEST_ASSERT_NOT_NULL(map_get((CljValue)map2, fixnum(2)));
        TEST_ASSERT_NOT_NULL(map_get((CljValue)map3, fixnum(3)));
        
        // Verify embedded arrays are separate
        TEST_ASSERT_NOT_EQUAL(map1->data, map2->data);
        TEST_ASSERT_NOT_EQUAL(map2->data, map3->data);
        TEST_ASSERT_NOT_EQUAL(map1->data, map3->data);
        
        printf("✓ Memory efficiency: Jede Map hat eigenes embedded array\n");
    });
}

void test_embedded_array_cow(void) {
    printf("\n=== Test: COW mit embedded arrays ===\n");
    
    WITH_AUTORELEASE_POOL({
        CljMap *map = (CljMap*)make_map(4);
        map_assoc_cow((CljValue)map, fixnum(1), fixnum(10));
        printf("Original map: RC=%d, count=%d\n", map->base.rc, map->count);
        
        // Simulate sharing (RC=2)
        RETAIN(map);
        TEST_ASSERT_EQUAL(2, map->base.rc);
        
        // COW operation should create new map with embedded array
        CljValue new_map = map_assoc_cow((CljValue)map, fixnum(2), fixnum(20));
        CljMap *new_map_data = as_map(new_map);
        
        // Verify new map has embedded array
        TEST_ASSERT_NOT_NULL(new_map_data->data);
        TEST_ASSERT_EQUAL(4, new_map_data->capacity);
        TEST_ASSERT_EQUAL(2, new_map_data->count);
        
        // Verify entries in new map
        CljValue val1 = map_get(new_map, fixnum(1));
        CljValue val2 = map_get(new_map, fixnum(2));
        TEST_ASSERT_NOT_NULL(val1);
        TEST_ASSERT_NOT_NULL(val2);
        TEST_ASSERT_EQUAL_INT(10, as_fixnum(val1));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum(val2));
        
        // Verify original unchanged
        TEST_ASSERT_EQUAL(1, map->count);
        TEST_ASSERT_NULL(map_get((CljValue)map, fixnum(2)));
        
        printf("✓ COW mit embedded arrays funktioniert\n");
        
        RELEASE(map);  // Cleanup
    });
}

void test_embedded_array_capacity_growth(void) {
    printf("\n=== Test: Capacity Growth mit embedded arrays ===\n");
    
    WITH_AUTORELEASE_POOL({
        CljMap *map = (CljMap*)make_map(2);  // Small capacity
        printf("Initial capacity: %d\n", map->capacity);
        
        // Fill initial capacity
        map_assoc_cow((CljValue)map, fixnum(1), fixnum(10));
        map_assoc_cow((CljValue)map, fixnum(2), fixnum(20));
        printf("After filling capacity: %d\n", map->capacity);
        
        // Simulate sharing to trigger COW with growth
        RETAIN(map);
        
        // Add more entries - should trigger COW with capacity growth
        CljValue new_map = map_assoc_cow((CljValue)map, fixnum(3), fixnum(30));
        CljMap *new_map_data = as_map(new_map);
        
        // Verify new map has larger capacity
        printf("New map capacity: %d\n", new_map_data->capacity);
        TEST_ASSERT_TRUE(new_map_data->capacity > map->capacity);
        
        // Verify all entries exist in new map
        TEST_ASSERT_NOT_NULL(map_get(new_map, fixnum(1)));
        TEST_ASSERT_NOT_NULL(map_get(new_map, fixnum(2)));
        TEST_ASSERT_NOT_NULL(map_get(new_map, fixnum(3)));
        
        printf("✓ Capacity growth mit embedded arrays funktioniert\n");
        
        RELEASE(map);  // Cleanup
    });
}

void test_embedded_array_performance(void) {
    printf("\n=== Test: Performance mit embedded arrays ===\n");
    
    WITH_AUTORELEASE_POOL({
        CljMap *env = (CljMap*)make_map(4);
        printf("Starting performance test...\n");
        
        // Simulate loop pattern with embedded arrays
        for (int i = 0; i < 50; i++) {
            env = (CljMap*)AUTORELEASE(map_assoc_cow((CljValue)env, fixnum(i), fixnum(i * 10)));
            
            // RC should stay 1 (in-place optimization)
            TEST_ASSERT_EQUAL(1, env->base.rc);
            
            if (i % 10 == 0) {
                printf("Iteration %d: RC=%d, count=%d, capacity=%d\n", 
                       i, env->base.rc, env->count, env->capacity);
            }
        }
        
        // Verify final state
        TEST_ASSERT_EQUAL(50, env->count);
        CljValue val25 = map_get((CljValue)env, fixnum(25));
        TEST_ASSERT_NOT_NULL(val25);
        TEST_ASSERT_EQUAL_INT(250, as_fixnum(val25));
        
        printf("✓ Performance test erfolgreich (50 Iterationen)\n");
    });
}
