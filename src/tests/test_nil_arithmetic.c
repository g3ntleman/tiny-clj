#include "minunit.h"
#include "object.h"
#include "function_call.h"
#include "parser.h"
#include "namespace.h"
#include "memory.h"
#include "memory_profiler.h"
#include <stdio.h>
#include <string.h>

// ============================================================================
// NIL ARITHMETIC TESTS
// ============================================================================

static char *test_nil_plus_number() {
    // Test: (+ nil 1) should throw NumberFormatException
    WITH_AUTORELEASE_POOL_EVAL({
        TRY {
            CljObject *result = eval_string("(+ nil 1)", eval_state);
            (void)result; // Suppress unused variable warning
            return "(+ nil 1) should have thrown an exception";
        } CATCH(ex) {
            // Expected exception - test passes
            return NULL;
        } END_TRY;
    });
    return NULL;
}

static char *test_number_plus_nil() {
    // Test: (+ 1 nil) should throw NumberFormatException
    WITH_AUTORELEASE_POOL_EVAL({
        TRY {
            CljObject *result = eval_string("(+ 1 nil)", eval_state);
            (void)result; // Suppress unused variable warning
            return "(+ 1 nil) should have thrown an exception";
        } CATCH(ex) {
            // Expected exception - test passes
            return NULL;
        } END_TRY;
    });
    return NULL;
}

static char *test_nil_plus_nil() {
    // Test: (+ nil nil) should throw NumberFormatException
    WITH_AUTORELEASE_POOL_EVAL({
        TRY {
            CljObject *result = eval_string("(+ nil nil)", eval_state);
            (void)result; // Suppress unused variable warning
            return "(+ nil nil) should have thrown an exception";
        } CATCH(ex) {
            // Expected exception - test passes
            return NULL;
        } END_TRY;
    });
    return NULL;
}

static char *test_nil_multiplication() {
    // Test: (* nil 5) should throw NumberFormatException
    WITH_AUTORELEASE_POOL_EVAL({
        TRY {
            CljObject *result = eval_string("(* nil 5)", eval_state);
            (void)result; // Suppress unused variable warning
            return "(* nil 5) should have thrown an exception";
        } CATCH(ex) {
            // Expected exception - test passes
            return NULL;
        } END_TRY;
    });
    return NULL;
}

static char *test_nil_subtraction() {
    // Test: (- nil 2) should throw NumberFormatException
    WITH_AUTORELEASE_POOL_EVAL({
        TRY {
            CljObject *result = eval_string("(- nil 2)", eval_state);
            (void)result; // Suppress unused variable warning
            return "(- nil 2) should have thrown an exception";
        } CATCH(ex) {
            // Expected exception - test passes
            return NULL;
        } END_TRY;
    });
    return NULL;
}

static char *test_nil_division() {
    // Test: (/ nil 3) should throw NumberFormatException
    WITH_AUTORELEASE_POOL_EVAL({
        TRY {
            CljObject *result = eval_string("(/ nil 3)", eval_state);
            (void)result; // Suppress unused variable warning
            return "(/ nil 3) should have thrown an exception";
        } CATCH(ex) {
            // Expected exception - test passes
            return NULL;
        } END_TRY;
    });
    return NULL;
}

static char *test_normal_arithmetic_still_works() {
    // Test: Normal arithmetic should still work
    WITH_AUTORELEASE_POOL_EVAL({
        CljObject *result = eval_string("(+ 1 2)", eval_state);
        mu_assert("(+ 1 2) should work", result != NULL);
        mu_assert("(+ 1 2) should be 3", is_type(result, CLJ_INT) && result->as.i == 3);
    });
    return NULL;
}

static char *test_nil_literals_parse_correctly() {
    // Test: nil, true, false should parse as literals
    WITH_AUTORELEASE_POOL_EVAL({
        // Test nil literal
        CljObject *nil_obj = parse("nil", eval_state);
        mu_assert("nil should parse as CLJ_NIL", nil_obj && is_type(nil_obj, CLJ_NIL));
        
        // Test true literal
        CljObject *true_obj = parse("true", eval_state);
        mu_assert("true should parse as CLJ_BOOL true", true_obj && is_type(true_obj, CLJ_BOOL) && true_obj->as.b);
        
        // Test false literal
        CljObject *false_obj = parse("false", eval_state);
        mu_assert("false should parse as CLJ_BOOL false", false_obj && is_type(false_obj, CLJ_BOOL) && !false_obj->as.b);
    });
    return NULL;
}

static char *all_nil_arithmetic_tests(void) {
    mu_run_test(test_nil_plus_number);
    mu_run_test(test_number_plus_nil);
    mu_run_test(test_nil_plus_nil);
    mu_run_test(test_nil_multiplication);
    mu_run_test(test_nil_subtraction);
    mu_run_test(test_nil_division);
    mu_run_test(test_normal_arithmetic_still_works);
    mu_run_test(test_nil_literals_parse_correctly);
    
    return 0;
}

// Export the test function
char *test_nil_arithmetic_suite(void) {
    return all_nil_arithmetic_tests();
}