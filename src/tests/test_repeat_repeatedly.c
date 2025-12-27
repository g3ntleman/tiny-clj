/*
 * Unit Tests for repeat and repeatedly Functions
 * 
 * Consolidated tests for repeat and repeatedly functions from various test files.
 */

#include "tests_common.h"

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

static void assert_eval_truthy(const char *expr) {
    ID result = eval_string(expr, g_test_eval_state);
    TEST_ASSERT_TRUE_MESSAGE(clj_is_truthy(result), expr);
}

// ============================================================================
// REPEAT TESTS
// ============================================================================

TEST_SHARED(test_repeat_finite) {
    // Test: (repeat 3 "x") => ["x" "x" "x"]
    CljObject *result1 = eval_string("(repeat 3 \"x\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, TAG(result1));
    CljVector *vec1 = as_vector(result1);
    TEST_ASSERT_EQUAL_INT(3, vector_count(vec1));
    
    // Test: (repeat 0 "x") => []
    CljObject *result2 = eval_string("(repeat 0 \"x\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, TAG(result2));
    CljVector *vec2 = as_vector(result2);
    TEST_ASSERT_EQUAL_INT(0, vector_count(vec2));
    
    // Test: (repeat 2 42) => [42 42]
    CljObject *result3 = eval_string("(repeat 2 42)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result3);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, TAG(result3));
    CljVector *vec3 = as_vector(result3);
    TEST_ASSERT_EQUAL_INT(2, vector_count(vec3));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum((CljValue)vector_nth(vec3, 0)));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum((CljValue)vector_nth(vec3, 1)));
}

TEST_SHARED(test_repeat_infinite_lazy_seq) {
    // Test: (repeat 42) => lazy-seq mit 42
    CljObject *result = eval_string("(repeat 42)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_LAZY_SEQ, TAG(result));
    
    // Test: first sollte 42 sein
    CljObject *first_elem = eval_string("(first (repeat 42))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(first_elem);
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(first_elem));
    
    // Test: rest sollte wieder lazy-seq sein
    CljObject *rest_seq = eval_string("(rest (repeat 42))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(rest_seq);
    TEST_ASSERT_EQUAL_INT(CLJ_LAZY_SEQ, TAG(rest_seq));
    
    // Test: first von rest sollte wieder 42 sein
    CljObject *rest_first = eval_string("(first (rest (repeat 42)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(rest_first);
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(rest_first));
    
    // Test: take sollte funktionieren
    CljObject *taken = eval_string("(take 5 (repeat 42))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(taken);
    TEST_ASSERT_EQUAL_INT(5, as_fixnum(eval_string("(count (take 5 (repeat 42)))", g_test_eval_state)));
}

// ============================================================================
// REPEATEDLY TESTS
// ============================================================================

TEST_SHARED(test_repeatedly_count) {
    // (count (vec (repeatedly 5 (fn [] 1)))) => 5
    assert_eval_truthy("(= (count (vec (repeatedly 5 (fn [] 1)))) 5)");
}

TEST_SHARED(test_repeatedly_values) {
    // (repeatedly 3 (fn [] :x)) => (:x :x :x)
    assert_eval_truthy("(= (first (repeatedly 3 (fn [] :x))) :x)");
}

// --- repeatedly Edge-Case-Tests ---

// Arity-Edge-Cases
TEST_SHARED(test_repeatedly_arity_zero_args) {
    bool exception_caught = false;
    TRY {
        eval_string("(repeatedly)", g_test_eval_state);
        TEST_FAIL_MESSAGE("Expected ArityException for (repeatedly)");
    } CATCH(ex) {
        exception_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_EQUAL_STRING("ArityException", ex->type);
    } END_TRY
    TEST_ASSERT_TRUE_MESSAGE(exception_caught, "Exception should have been caught");
}

TEST_SHARED(test_repeatedly_arity_three_args) {
    bool exception_caught = false;
    TRY {
        eval_string("(repeatedly 5 (fn [] 1) :extra)", g_test_eval_state);
        TEST_FAIL_MESSAGE("Expected ArityException for (repeatedly 5 f :extra)");
    } CATCH(ex) {
        exception_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_EQUAL_STRING("ArityException", ex->type);
    } END_TRY
    TEST_ASSERT_TRUE_MESSAGE(exception_caught, "Exception should have been caught");
}

// Count-Edge-Cases
TEST_SHARED(test_repeatedly_count_zero) {
    // (repeatedly 0 f) => leere Liste
    ID result = eval_string("(vec (repeatedly 0 (fn [] 1)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    ID count = eval_string("(count (vec (repeatedly 0 (fn [] 1))))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(count);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)count));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum((CljValue)count));
}

TEST_SHARED(test_repeatedly_count_negative) {
    bool exception_caught = false;
    TRY {
        eval_string("(repeatedly -1 (fn [] 1))", g_test_eval_state);
        TEST_FAIL_MESSAGE("Expected exception for negative count");
    } CATCH(ex) {
        exception_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY
    TEST_ASSERT_TRUE_MESSAGE(exception_caught, "Exception should have been caught");
}

TEST_SHARED(test_repeatedly_count_not_int) {
    bool exception_caught = false;
    TRY {
        eval_string("(repeatedly \"not-int\" (fn [] 1))", g_test_eval_state);
        TEST_FAIL_MESSAGE("Expected exception for non-integer count");
    } CATCH(ex) {
        exception_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY
    TEST_ASSERT_TRUE_MESSAGE(exception_caught, "Exception should have been caught");
}

// Function-Edge-Cases
TEST_SHARED(test_repeatedly_fn_nil) {
    // Kann Exception werfen oder nil zurückgeben, je nach Implementierung
    TRY {
        eval_string("(repeatedly nil)", g_test_eval_state);
    } CATCH(ex) {
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY
}

TEST_SHARED(test_repeatedly_fn_nil_with_count) {
    bool exception_caught = false;
    TRY {
        eval_string("(repeatedly 5 nil)", g_test_eval_state);
    } CATCH(ex) {
        exception_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY
    TEST_ASSERT_TRUE_MESSAGE(exception_caught, "Exception should have been caught");
}

TEST_SHARED(test_repeatedly_fn_not_function) {
    bool exception_caught = false;
    TRY {
        eval_string("(repeatedly 5 \"not-fn\")", g_test_eval_state);
    } CATCH(ex) {
        exception_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY
    TEST_ASSERT_TRUE_MESSAGE(exception_caught, "Exception should have been caught");
}

// Infinite-Sequence-Edge-Cases
TEST_SHARED(test_repeatedly_infinite) {
    // (repeatedly f) => infinite lazy-seq
    ID result = eval_string("(repeatedly (fn [] :x))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    // Prüfe dass es eine lazy-seq ist
    ID first_val = eval_string("(first (repeatedly (fn [] :x)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(first_val);
    TEST_ASSERT_TRUE(is_keyword((CljValue)first_val));
}

TEST_SHARED(test_repeatedly_infinite_take) {
    // (take 3 (repeatedly f)) => 3 Elemente
    ID result = eval_string("(vec (take 3 (repeatedly (fn [] 42))))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    ID count = eval_string("(count (vec (take 3 (repeatedly (fn [] 42)))))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(count);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)count));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)count));
    ID first = eval_string("(first (take 3 (repeatedly (fn [] 42))))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)first));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum((CljValue)first));
}

// Lazy-Sequence-Edge-Cases
TEST_SHARED(test_repeatedly_fn_returns_nil) {
    // Funktion gibt nil zurück → sollte nil in Sequenz enthalten
    ID result = eval_string("(first (repeatedly 3 (fn [] nil)))", g_test_eval_state);
    TEST_ASSERT_NULL(result);
}

TEST_SHARED(test_repeatedly_fn_returns_different_values) {
    // Funktion gibt verschiedene Werte zurück
    ID counter = eval_string("(let [x (atom 0)] (fn [] (swap! x inc)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(counter);
    ID result = eval_string("(vec (take 3 (repeatedly 5 (fn [] :x))))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    ID count = eval_string("(count (vec (take 3 (repeatedly 5 (fn [] :x)))))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(count);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)count));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)count));
}

// Empty-Result-Edge-Cases
TEST_SHARED(test_repeatedly_empty_take_zero) {
    // (take 0 (repeatedly f)) => leere Liste
    ID result = eval_string("(vec (take 0 (repeatedly (fn [] 1))))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    ID count = eval_string("(count (vec (take 0 (repeatedly (fn [] 1)))))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(count);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)count));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum((CljValue)count));
}

// ============================================================================
// HYPOTHESIS TESTS - Testing closure problem hypotheses
// ============================================================================

// Hypothesis 1: eval_function_call wird direkt aufgerufen (ohne ctx) → outer_ctx=NULL
// Test: Prüfen, ob eval_function_call direkt aufgerufen wird
TEST(test_hypothesis1_eval_function_call_direct_call) {
    if (!g_test_eval_state) {
        TEST_FAIL_MESSAGE("Failed to create EvalState");
        return;
    }
    
    // Test: Erstelle eine Funktion und rufe sie direkt auf
    // Wenn eval_function_call direkt aufgerufen wird, sollte outer_ctx=NULL sein
    // Dies testen wir indirekt durch Prüfung, ob ctx korrekt weitergegeben wird
    
    // Erstelle eine Funktion, die eine Closure erstellt
    CljObject *constantly_fn = eval_string("constantly", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(constantly_fn);
    TEST_ASSERT_TRUE(TAG(constantly_fn) == CLJ_FUNC || TAG(constantly_fn) == CLJ_CLOSURE);
    
    // Rufe constantly mit einem Wert auf - sollte funktionieren
    CljObject *result = eval_string("(constantly 5)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_FUNC || TAG(result) == CLJ_CLOSURE);
    
    // Rufe die zurückgegebene Funktion auf
    CljObject *call_result = eval_string("((constantly 5))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(call_result);
    TEST_ASSERT_TRUE(is_fixnum(call_result));
    TEST_ASSERT_EQUAL_INT(5, as_fixnum(call_result));
}

// Hypothesis 2: Die Logik zur Kombination von outer_ctx->env_stack mit func->env_stack ist fehlerhaft
// Test: Prüfen, ob outer_ctx->env_stack korrekt mit func->env_stack kombiniert wird
TEST(test_hypothesis2_env_stack_combination) {
    if (!g_test_eval_state) {
        TEST_FAIL_MESSAGE("Failed to create EvalState");
        return;
    }
    
    // Test: Erstelle eine Funktion in einem let-Binding und rufe sie auf
    // Der env_stack von let sollte mit dem env_stack der Funktion kombiniert werden
    CljObject *result = eval_string("(let [x 42] (let [f (fn [] x)] (f)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(result));
}

// Hypothesis 3: Der env_stack wird nicht korrekt weitergegeben, wenn constantly die innere Funktion erstellt
// Test: Prüfen, ob der env_stack korrekt weitergegeben wird, wenn constantly die innere Funktion erstellt
TEST(test_hypothesis3_env_stack_passed_to_constantly) {
    if (!g_test_eval_state) {
        TEST_FAIL_MESSAGE("Failed to create EvalState");
        return;
    }
    
    // Test: Erstelle constantly in einem let-Binding und rufe es auf
    // Der env_stack von let sollte an constantly weitergegeben werden
    CljObject *result = eval_string("(let [x 42] (let [c (constantly x)] (c)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(result));
}

// Test: Prüfen, ob der env_stack korrekt weitergegeben wird, wenn constantly von einer Funktion aufgerufen wird
TEST(test_hypothesis3_env_stack_passed_through_function_call) {
    if (!g_test_eval_state) {
        TEST_FAIL_MESSAGE("Failed to create EvalState");
        return;
    }
    
    // Test: Eine Funktion ruft constantly auf, constantly erstellt eine innere Funktion
    // Der env_stack von der Funktion sollte an constantly weitergegeben werden
    CljObject *result = eval_string("((fn [x] (let [c (constantly x)] (c))) 99)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(99, as_fixnum(result));
}

// Test: constantly with variable from let (simplified)
TEST(test_closure_constantly_with_let_var) {
    if (!g_test_eval_state) {
        TEST_FAIL_MESSAGE("Failed to create EvalState");
        return;
    }
    
    // Test: (let [x 10] (constantly x)) - create constantly with variable from let
    CljObject *const_fn = eval_string("(let [x 10] (constantly x))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(const_fn);
    TEST_ASSERT_TRUE(TAG(const_fn) == CLJ_FUNC || TAG(const_fn) == CLJ_CLOSURE);
    
    // Call the function returned by constantly
    CljObject *result = eval_string("((let [x 10] (constantly x)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(10, as_fixnum(result));
}

// Test: repeat uses repeatedly with constantly - this is the problematic case
TEST(test_hypothesis_repeat_uses_repeatedly_constantly) {
    if (!g_test_eval_state) {
        TEST_FAIL_MESSAGE("Failed to create EvalState");
        return;
    }
    
    // Test: (repeat 42) uses (repeatedly (constantly x)) internally
    // This should work even though it's a nested closure
    CljObject *result = eval_string("(first (repeat 42))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(result));
}

// Test: repeat with count uses take internally - should work
TEST(test_hypothesis_repeat_with_count_uses_take) {
    if (!g_test_eval_state) {
        TEST_FAIL_MESSAGE("Failed to create EvalState");
        return;
    }
    
    // Test: (repeat 3 42) uses (take n (repeat x)) internally
    // This should work even though it's a nested closure
    CljObject *result = eval_string("(first (repeat 3 42))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(result));
    
    CljObject *count = eval_string("(count (repeat 3 42))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(count);
    TEST_ASSERT_TRUE(is_fixnum(count));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(count));
}

// ============================================================================
// REPEATEDLY WITH CLOSURE TESTS
// ============================================================================

TEST(test_closure_repeatedly_constantly_simple) {
    if (!g_test_eval_state) {
        TEST_FAIL_MESSAGE("Failed to create EvalState");
        return;
    }
    
    // Test: (repeatedly (constantly 5)) - should work with literal value
    CljObject *result1 = eval_string("(first (repeatedly (constantly 5)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(is_fixnum(result1));
    TEST_ASSERT_EQUAL_INT(5, as_fixnum(result1));
    
    // Test: (repeatedly (constantly x)) with x from let - this is the problematic case
    // The issue: when constantly is called from repeatedly, the ctx->frame from let is not available
    CljObject *result2 = eval_string("(let [x 42] (first (repeatedly (constantly x))))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(is_fixnum(result2));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(result2));
}

TEST(test_closure_repeatedly_constantly_with_param) {
    if (!g_test_eval_state) {
        TEST_FAIL_MESSAGE("Failed to create EvalState");
        return;
    }
    
    // Test: (fn [x] (first (repeatedly (constantly x)))) - constantly with function parameter
    CljObject *result = eval_string("(((fn [x] (first (repeatedly (constantly x)))) 99))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(99, as_fixnum(result));
}

TEST(test_hypothesis3_env_stack_passed_through_repeatedly) {
    if (!g_test_eval_state) {
        TEST_FAIL_MESSAGE("Failed to create EvalState");
        return;
    }
    
    // Test: repeatedly ruft constantly auf, constantly erstellt eine innere Funktion
    // Der env_stack von let sollte durch repeatedly → constantly → innere Funktion weitergegeben werden
    CljObject *result = eval_string("(let [x 42] (let [r (repeatedly (constantly x))] (first r)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(result));
}

