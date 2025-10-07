/*
 * Clojure Core Functions Tests (MinUnit)
 * 
 * Tests for clojure.core functions implemented in clojure.core.clj
 */

#include "minunit.h"
#include "../CljObject.h"
#include "../clj_symbols.h"
#include "../namespace.h"
#include "../function_call.h"
#include "../clj_parser.h"
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
    load_clojure_core();
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
    return obj && obj->type == CLJ_NUMBER && 
           as_number(obj)->value == expected;
}

static bool is_type(CljObject *obj, CljType expected_type) {
    return obj && obj->type == expected_type;
}

// Boolean singleton getters
extern CljObject* clj_true(void);
extern CljObject* clj_false(void);

// ============================================================================
// NUMERIC PREDICATES
// ============================================================================

static char* test_zero_predicate(void) {
    printf("\n=== Testing zero? ===\n");
    test_setup();
    
    CljObject *result1 = eval_code("(zero? 0)");
    mu_assert("(zero? 0) should return true", 
        is_type(result1, CLJ_BOOL) && result1 == clj_true());
    
    CljObject *result2 = eval_code("(zero? 1)");
    mu_assert("(zero? 1) should return false", 
        is_type(result2, CLJ_BOOL) && result2 == clj_false());
    
    test_teardown();
    return NULL;
}

static char* test_pos_predicate(void) {
    printf("\n=== Testing pos? ===\n");
    test_setup();
    
    CljObject *result1 = eval_code("(pos? 5)");
    mu_assert("(pos? 5) should return true", 
        is_type(result1, CLJ_BOOL) && result1 == clj_true());
    
    CljObject *result2 = eval_code("(pos? 0)");
    mu_assert("(pos? 0) should return false", 
        is_type(result2, CLJ_BOOL) && result2 == clj_false());
    
    CljObject *result3 = eval_code("(pos? -5)");
    mu_assert("(pos? -5) should return false", 
        is_type(result3, CLJ_BOOL) && result3 == clj_false());
    
    test_teardown();
    return NULL;
}

static char* test_neg_predicate(void) {
    printf("\n=== Testing neg? ===\n");
    test_setup();
    
    CljObject *result1 = eval_code("(neg? -5)");
    mu_assert("(neg? -5) should return true", is_type(result, CLJ_BOOL) && result == clj_true()1));
    
    CljObject *result2 = eval_code("(neg? 0)");
    mu_assert("(neg? 0) should return false", is_type(result, CLJ_BOOL) && result == clj_false()2));
    
    CljObject *result3 = eval_code("(neg? 5)");
    mu_assert("(neg? 5) should return false", is_type(result, CLJ_BOOL) && result == clj_false()3));
    
    test_teardown();
    return NULL;
}

// ============================================================================
// LOGIC FUNCTIONS
// ============================================================================

static char* test_not_function(void) {
    printf("\n=== Testing not ===\n");
    test_setup();
    
    CljObject *result1 = eval_code("(not true)");
    mu_assert("(not true) should return false", 
        is_type(result1, CLJ_BOOL) && result1 == clj_false());
    
    CljObject *result2 = eval_code("(not false)");
    mu_assert("(not false) should return true", 
        is_type(result2, CLJ_BOOL) && result2 == clj_true());
    
    CljObject *result3 = eval_code("(not 0)");
    mu_assert("(not 0) should return false (0 is truthy)", 
        is_type(result3, CLJ_BOOL) && result3 == clj_false());
    
    test_teardown();
    return NULL;
}

// ============================================================================
// COMPARISON FUNCTIONS
// ============================================================================

static char* test_max_function(void) {
    printf("\n=== Testing max ===\n");
    test_setup();
    
    CljObject *result1 = eval_code("(max 10 20)");
    mu_assert("(max 10 20) should return 20", is_number(result1, 20));
    
    CljObject *result2 = eval_code("(max 100 50)");
    mu_assert("(max 100 50) should return 100", is_number(result2, 100));
    
    CljObject *result3 = eval_code("(max -5 -10)");
    mu_assert("(max -5 -10) should return -5", is_number(result3, -5));
    
    test_teardown();
    return NULL;
}

static char* test_min_function(void) {
    printf("\n=== Testing min ===\n");
    test_setup();
    
    CljObject *result1 = eval_code("(min 10 20)");
    mu_assert("(min 10 20) should return 10", is_number(result1, 10));
    
    CljObject *result2 = eval_code("(min 100 50)");
    mu_assert("(min 100 50) should return 50", is_number(result2, 50));
    
    CljObject *result3 = eval_code("(min -5 -10)");
    mu_assert("(min -5 -10) should return -10", is_number(result3, -10));
    
    test_teardown();
    return NULL;
}

// ============================================================================
// COLLECTION FUNCTIONS
// ============================================================================

static char* test_second_function(void) {
    printf("\n=== Testing second ===\n");
    test_setup();
    
    CljObject *result1 = eval_code("(second [1 2 3])");
    mu_assert("(second [1 2 3]) should return 2", is_number(result1, 2));
    
    CljObject *result2 = eval_code("(second [42])");
    mu_assert("(second [42]) should return nil", is_type(result2, CLJ_NIL));
    
    test_teardown();
    return NULL;
}

static char* test_empty_predicate(void) {
    printf("\n=== Testing empty? ===\n");
    test_setup();
    
    CljObject *result1 = eval_code("(empty? [])");
    mu_assert("(empty? []) should return true", 
        is_type(result1, CLJ_BOOL) && result1 == clj_true());
    
    CljObject *result2 = eval_code("(empty? [1 2 3])");
    mu_assert("(empty? [1 2 3]) should return false", 
        is_type(result2, CLJ_BOOL) && result2 == clj_false());
    
    test_teardown();
    return NULL;
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

static char* test_identity_function(void) {
    printf("\n=== Testing identity ===\n");
    test_setup();
    
    CljObject *result1 = eval_code("(identity 42)");
    mu_assert("(identity 42) should return 42", is_number(result1, 42));
    
    CljObject *result2 = eval_code("(identity [1 2 3])");
    mu_assert("(identity [1 2 3]) should return vector", is_type(result2, CLJ_VECTOR));
    
    test_teardown();
    return NULL;
}

static char* test_constantly_function(void) {
    printf("\n=== Testing constantly ===\n");
    test_setup();
    
    // constantly returns a function that always returns the same value
    CljObject *result1 = eval_code("((constantly 42) 99)");
    mu_assert("((constantly 42) 99) should return 42", is_number(result1, 42));
    
    CljObject *result2 = eval_code("((constantly 10) 20)");
    mu_assert("((constantly 10) 20) should return 10", is_number(result2, 10));
    
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

