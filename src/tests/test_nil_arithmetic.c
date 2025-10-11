#include "minunit.h"
#include "../object.h"
#include "../function_call.h"
#include "../parser.h"
#include "../namespace.h"
#include <stdio.h>
#include <string.h>

// ============================================================================
// NIL ARITHMETIC TESTS
// ============================================================================

static char *test_nil_plus_number() {
    // Test: (+ nil 1) should throw NumberFormatException
    EvalState *st = evalstate_new();
    
    TRY {
        CljObject *result = eval_string("(+ nil 1)", st);
        if (result) {
            evalstate_free(st);
            return "(+ nil 1) should have thrown an exception";
        }
    } CATCH(ex) {
        // Expected exception - test passes
        evalstate_free(st);
        return NULL;
    } END_TRY;
    
    evalstate_free(st);
    return NULL;
}

static char *test_number_plus_nil() {
    // Test: (+ 1 nil) should throw NumberFormatException
    EvalState *st = evalstate_new();
    
    TRY {
        CljObject *result = eval_string("(+ 1 nil)", st);
        if (result) {
            evalstate_free(st);
            return "(+ 1 nil) should have thrown an exception";
        }
    } CATCH(ex) {
        // Expected exception - test passes
        evalstate_free(st);
        return NULL;
    } END_TRY;
    
    evalstate_free(st);
    return NULL;
}

static char *test_nil_plus_nil() {
    // Test: (+ nil nil) should throw NumberFormatException
    EvalState *st = evalstate_new();
    
    TRY {
        CljObject *result = eval_string("(+ nil nil)", st);
        if (result) {
            evalstate_free(st);
            return "(+ nil nil) should have thrown an exception";
        }
    } CATCH(ex) {
        // Expected exception - test passes
        evalstate_free(st);
        return NULL;
    } END_TRY;
    
    evalstate_free(st);
    return NULL;
}

static char *test_nil_multiplication() {
    // Test: (* nil 5) should throw NumberFormatException
    EvalState *st = evalstate_new();
    
    TRY {
        CljObject *result = eval_string("(* nil 5)", st);
        if (result) {
            evalstate_free(st);
            return "(* nil 5) should have thrown an exception";
        }
    } CATCH(ex) {
        // Expected exception - test passes
        evalstate_free(st);
        return NULL;
    } END_TRY;
    
    evalstate_free(st);
    return NULL;
}

static char *test_nil_subtraction() {
    // Test: (- nil 2) should throw NumberFormatException
    EvalState *st = evalstate_new();
    
    TRY {
        CljObject *result = eval_string("(- nil 2)", st);
        if (result) {
            evalstate_free(st);
            return "(- nil 2) should have thrown an exception";
        }
    } CATCH(ex) {
        // Expected exception - test passes
        evalstate_free(st);
        return NULL;
    } END_TRY;
    
    evalstate_free(st);
    return NULL;
}

static char *test_nil_division() {
    // Test: (/ nil 3) should throw NumberFormatException
    EvalState *st = evalstate_new();
    
    TRY {
        CljObject *result = eval_string("(/ nil 3)", st);
        if (result) {
            evalstate_free(st);
            return "(/ nil 3) should have thrown an exception";
        }
    } CATCH(ex) {
        // Expected exception - test passes
        evalstate_free(st);
        return NULL;
    } END_TRY;
    
    evalstate_free(st);
    return NULL;
}

static char *test_normal_arithmetic_still_works() {
    // Test: Normal arithmetic should still work
    EvalState *st = evalstate_new();
    
    CljObject *result = eval_string("(+ 1 2)", st);
    mu_assert("(+ 1 2) should work", result != NULL);
    mu_assert("(+ 1 2) should be 3", is_type(result, CLJ_INT) && result->as.i == 3);
    
    evalstate_free(st);
    return NULL;
}

static char *test_nil_literals_parse_correctly() {
    // Test: nil, true, false should parse as literals
    EvalState *st = evalstate_new();
    
    // Test nil literal
    CljObject *nil_obj = parse("nil", st);
    mu_assert("nil should parse as CLJ_NIL", nil_obj && is_type(nil_obj, CLJ_NIL));
    
    // Test true literal
    CljObject *true_obj = parse("true", st);
    mu_assert("true should parse as CLJ_BOOL true", true_obj && is_type(true_obj, CLJ_BOOL) && true_obj->as.b);
    
    // Test false literal
    CljObject *false_obj = parse("false", st);
    mu_assert("false should parse as CLJ_BOOL false", false_obj && is_type(false_obj, CLJ_BOOL) && !false_obj->as.b);
    
    evalstate_free(st);
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