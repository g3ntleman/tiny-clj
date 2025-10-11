/*
 * Clojure Core Functions Tests (MinUnit)
 * 
 * Tests for clojure.core functions implemented in clojure.core.clj
 */

#include "minunit.h"
#include "../object.h"
#include "../clj_symbols.h"
#include "../namespace.h"
#include "../function_call.h"
#include "../parser.h"
#include "../tiny_clj.h"
#include <stdio.h>
#include <string.h>

// Forward declaration
int load_clojure_core(void);

// ============================================================================
// TEST HELPERS
// ============================================================================

static EvalState *global_eval_state = NULL;

static void test_setup(void) {
    init_special_symbols();
    meta_registry_init();
    global_eval_state = evalstate_new();
    load_clojure_core(global_eval_state);
}

static void test_teardown(void) {
    if (global_eval_state) {
        evalstate_free(global_eval_state);
        global_eval_state = NULL;
    }
    symbol_table_cleanup();
    meta_registry_cleanup();
}

static CljObject* eval_code(const char *code) {
    return eval_string(code, global_eval_state);
}

// ============================================================================
// TYPE CHECKING HELPERS
// ============================================================================

static bool is_number(CljObject *obj, int expected) {
    return obj && is_type(obj, CLJ_NUMBER) && 
           as_number(obj)->value == expected;
}

static bool is_type(CljObject *obj, CljType expected_type) {
    return obj && obj->type == expected_type;
}

// Boolean singleton getters
extern CljObject* clj_true(void);
extern CljObject* clj_false(void);

// Boolean assertion macros
#define assert_clj_true(msg, obj) mu_assert(msg, (obj) == clj_true())
#define assert_clj_false(msg, obj) mu_assert(msg, (obj) == clj_false())
#define assert_number(msg, obj, val) mu_assert(msg, is_number(obj, val))
#define assert_type(msg, obj, type) mu_assert(msg, is_type(obj, type))

// ============================================================================
// NUMERIC PREDICATES
// ============================================================================

static char* test_zero_predicate(void) {
    test_setup();
    
    assert_clj_true("(zero? 0) should return true", eval_code("(zero? 0)"));
    assert_clj_false("(zero? 1) should return false", eval_code("(zero? 1)"));
    
    test_teardown();
    return NULL;
}

static char* test_pos_predicate(void) {
    test_setup();
    
    assert_clj_true("(pos? 5) should return true", eval_code("(pos? 5)"));
    assert_clj_false("(pos? 0) should return false", eval_code("(pos? 0)"));
    assert_clj_false("(pos? -5) should return false", eval_code("(pos? -5)"));
    
    test_teardown();
    return NULL;
}

static char* test_neg_predicate(void) {
    test_setup();
    
    assert_clj_true("(neg? -5) should return true", eval_code("(neg? -5)"));
    assert_clj_false("(neg? 0) should return false", eval_code("(neg? 0)"));
    assert_clj_false("(neg? 5) should return false", eval_code("(neg? 5)"));
    
    test_teardown();
    return NULL;
}

// ============================================================================
// LOGIC FUNCTIONS
// ============================================================================

static char* test_not_function(void) {
    test_setup();
    
    assert_clj_false("(not true) should return false", eval_code("(not true)"));
    assert_clj_true("(not false) should return true", eval_code("(not false)"));
    assert_clj_false("(not 0) should return false (0 is truthy)", eval_code("(not 0)"));
    
    test_teardown();
    return NULL;
}

// ============================================================================
// COMPARISON FUNCTIONS
// ============================================================================

static char* test_max_function(void) {
    test_setup();
    
    assert_number("(max 10 20) should return 20", eval_code("(max 10 20)"), 20);
    assert_number("(max 100 50) should return 100", eval_code("(max 100 50)"), 100);
    assert_number("(max -5 -10) should return -5", eval_code("(max -5 -10)"), -5);
    
    test_teardown();
    return NULL;
}

static char* test_min_function(void) {
    test_setup();
    
    assert_number("(min 10 20) should return 10", eval_code("(min 10 20)"), 10);
    assert_number("(min 100 50) should return 50", eval_code("(min 100 50)"), 50);
    assert_number("(min -5 -10) should return -10", eval_code("(min -5 -10)"), -10);
    
    test_teardown();
    return NULL;
}

// ============================================================================
// COLLECTION FUNCTIONS
// ============================================================================

static char* test_second_function(void) {
    test_setup();
    
    assert_number("(second [1 2 3]) should return 2", eval_code("(second [1 2 3])"), 2);
    assert_type("(second [42]) should return nil", eval_code("(second [42])"), CLJ_NIL);
    
    test_teardown();
    return NULL;
}

static char* test_empty_predicate(void) {
    test_setup();
    
    assert_clj_true("(empty? []) should return true", eval_code("(empty? [])"));
    assert_clj_false("(empty? [1 2 3]) should return false", eval_code("(empty? [1 2 3])"));
    
    test_teardown();
    return NULL;
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

static char* test_identity_function(void) {
    test_setup();
    
    assert_number("(identity 42) should return 42", eval_code("(identity 42)"), 42);
    assert_type("(identity [1 2 3]) should return vector", eval_code("(identity [1 2 3])"), CLJ_VECTOR);
    
    test_teardown();
    return NULL;
}

static char* test_constantly_function(void) {
    test_setup();
    
    // constantly returns a function that always returns the same value
    assert_number("((constantly 42) 99) should return 42", eval_code("((constantly 42) 99)"), 42);
    assert_number("((constantly 10) 20) should return 10", eval_code("((constantly 10) 20)"), 10);
    
    test_teardown();
    return NULL;
}

// ============================================================================
// TEST RUNNER
// ============================================================================

#ifndef UNIFIED_TEST_RUNNER

static char* all_tests(void) {
    printf("\n🧪 === Clojure Core Functions Tests ===\n");
    
    // Numeric Predicates
    mu_run_test(test_zero_predicate);
    mu_run_test(test_pos_predicate);
    mu_run_test(test_neg_predicate);
    
    // Logic
    mu_run_test(test_not_function);
    
    // Comparison
    mu_run_test(test_max_function);
    mu_run_test(test_min_function);
    
    // Collections
    mu_run_test(test_second_function);
    mu_run_test(test_empty_predicate);
    
    // Utilities
    mu_run_test(test_identity_function);
    mu_run_test(test_constantly_function);
    
    return NULL;
}

int main(void) {
    char *result = all_tests();
    
    if (result != NULL) {
        printf("❌ %s\n", result);
        return 1;
    }
    
    printf("✅ ALL TESTS PASSED\n");
    printf("Tests run: %d\n", tests_run);
    
    return 0;
}

#else

// Export for unified test runner
char *run_clojure_core_tests(void) {
    // Numeric Predicates
    mu_run_test_verbose("zero?", test_zero_predicate);
    mu_run_test_verbose("pos?", test_pos_predicate);
    mu_run_test_verbose("neg?", test_neg_predicate);
    
    // Logic
    mu_run_test_verbose("not", test_not_function);
    
    // Comparison
    mu_run_test_verbose("max", test_max_function);
    mu_run_test_verbose("min", test_min_function);
    
    // Collections
    mu_run_test_verbose("second", test_second_function);
    mu_run_test_verbose("empty?", test_empty_predicate);
    
    // Utilities
    mu_run_test_verbose("identity", test_identity_function);
    mu_run_test_verbose("constantly", test_constantly_function);
    
    return NULL;
}

#endif

