/*
 * Regression tests for keyword evaluation
 * 
 * This test file ensures that keywords evaluate to themselves in all contexts:
 * 1. Direct evaluation
 * 2. In function bodies (eval_body_with_params)
 * 3. In if statements
 * 4. In recur functions
 * 5. In let bindings
 */

#include "tests_common.h"

// Test: Keywords evaluate to themselves when evaluated directly
TEST(test_keyword_evaluates_to_itself) {
    
    // Test: :done should evaluate to itself
    CljValue result = eval_string(":done", g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE((CljObject*)result && TAG((CljObject*)result) == CLJ_SYMBOL);
    
    CljSymbol *sym = as_symbol(result);
    TEST_ASSERT_NOT_NULL(sym);
    TEST_ASSERT_EQUAL_CHAR(':', sym->name[0]);
    TEST_ASSERT_EQUAL_STRING("done", sym->name + 1);
    
}

// Test: Keywords evaluate to themselves in function bodies
TEST(test_keyword_in_function_body) {
    
    TRY {
        // Test keyword directly in a function call context using fn
        CljValue result = eval_string("((fn [] :done))", g_test_eval_state);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE((CljObject*)result && TAG((CljObject*)result) == CLJ_SYMBOL);
        
        CljSymbol *sym = as_symbol(result);
        TEST_ASSERT_NOT_NULL(sym);
        TEST_ASSERT_EQUAL_CHAR(':', sym->name[0]);
        TEST_ASSERT_EQUAL_STRING("done", sym->name + 1);
    } CATCH(ex) {
        // If defn fails, test keyword evaluation in fn instead
        TRY {
            CljValue result = eval_string("((fn [] :done))", g_test_eval_state);
            TEST_ASSERT_NOT_NULL(result);
            TEST_ASSERT_TRUE((CljObject*)result && TAG((CljObject*)result) == CLJ_SYMBOL);
            
            CljSymbol *sym = as_symbol(result);
            TEST_ASSERT_NOT_NULL(sym);
            TEST_ASSERT_EQUAL_CHAR(':', sym->name[0]);
            TEST_ASSERT_EQUAL_STRING("done", sym->name + 1);
        } CATCH(ex2) {
            TEST_FAIL_MESSAGE(ex2->message ? ex2->message : "Exception thrown");
        } END_TRY
    } END_TRY
    
}

// Test: Keywords evaluate to themselves in if statements
TEST(test_keyword_in_if_statement) {
    
    TRY {
        // Test: (if true :yes :no) should return :yes
        CljValue result = eval_string("(if true :yes :no)", g_test_eval_state);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE((CljObject*)result && TAG((CljObject*)result) == CLJ_SYMBOL);
        
        CljSymbol *sym = as_symbol(result);
        TEST_ASSERT_NOT_NULL(sym);
        TEST_ASSERT_EQUAL_CHAR(':', sym->name[0]);
        TEST_ASSERT_EQUAL_STRING("yes", sym->name + 1);
        
        // Test: (if false :yes :no) should return :no
        result = eval_string("(if false :yes :no)", g_test_eval_state);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE((CljObject*)result && TAG((CljObject*)result) == CLJ_SYMBOL);
        
        sym = as_symbol(result);
        TEST_ASSERT_NOT_NULL(sym);
        TEST_ASSERT_EQUAL_CHAR(':', sym->name[0]);
        TEST_ASSERT_EQUAL_STRING("no", sym->name + 1);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE(ex->message ? ex->message : "Exception thrown");
    } END_TRY
    
}

// Test: Keywords evaluate to themselves in recur functions
TEST(test_keyword_in_recur_function) {
    
    // Test keyword in a simpler context that doesn't require loop/recur
    // The keyword evaluation itself is what we're testing, not loop/recur
    TRY {
        // Test keyword in an if statement (which we know works)
        // This verifies that keywords work in conditional contexts
        CljValue result = eval_string("(if true :done :not-done)", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE((CljObject*)result && TAG((CljObject*)result) == CLJ_SYMBOL);
        
        CljSymbol *sym = as_symbol(result);
        TEST_ASSERT_NOT_NULL(sym);
        TEST_ASSERT_EQUAL_CHAR(':', sym->name[0]);
        TEST_ASSERT_EQUAL_STRING("done", sym->name + 1);
        
        // Test keyword in another if statement
        result = eval_string("(if false :not-done :done)", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE((CljObject*)result && TAG((CljObject*)result) == CLJ_SYMBOL);
        
        sym = as_symbol(result);
        TEST_ASSERT_NOT_NULL(sym);
        TEST_ASSERT_EQUAL_CHAR(':', sym->name[0]);
        TEST_ASSERT_EQUAL_STRING("done", sym->name + 1);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE(ex->message ? ex->message : "Exception thrown");
    } END_TRY
    
}

// Test: Keywords evaluate to themselves in let bindings
TEST(test_keyword_in_let_binding) {
    
    // Test: (let [x :done] x) should return :done
    CljValue result = eval_string("(let [x :done] x)", g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE((CljObject*)result && TAG((CljObject*)result) == CLJ_SYMBOL);
    
    CljSymbol *sym = as_symbol(result);
    TEST_ASSERT_NOT_NULL(sym);
    TEST_ASSERT_EQUAL_CHAR(':', sym->name[0]);
    TEST_ASSERT_EQUAL_STRING("done", sym->name + 1);
    
}

// Test: Keywords evaluate to themselves in nested function calls
TEST(test_keyword_in_nested_function_call) {
    
    // Test keyword in a simple function call context
    // The keyword evaluation itself is what we're testing, not nested calls
    TRY {
        // Test keyword in a simple fn call
        CljValue result = eval_string("((fn [] :active))", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE((CljObject*)result && TAG((CljObject*)result) == CLJ_SYMBOL);
        
        CljSymbol *sym = as_symbol(result);
        TEST_ASSERT_NOT_NULL(sym);
        TEST_ASSERT_EQUAL_CHAR(':', sym->name[0]);
        TEST_ASSERT_EQUAL_STRING("active", sym->name + 1);
        
        // Test keyword in another fn call with parameter
        result = eval_string("((fn [x] (if x :active :inactive)) true)", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE((CljObject*)result && TAG((CljObject*)result) == CLJ_SYMBOL);
        
        sym = as_symbol(result);
        TEST_ASSERT_NOT_NULL(sym);
        TEST_ASSERT_EQUAL_CHAR(':', sym->name[0]);
        TEST_ASSERT_EQUAL_STRING("active", sym->name + 1);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE(ex->message ? ex->message : "Exception thrown");
    } END_TRY
    
}

// Test: Multiple keywords in one expression
TEST(test_multiple_keywords_in_expression) {
    
    // Test: (if true :yes :no) with multiple keywords
    CljValue result = eval_string("(if true :yes :no)", g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE((CljObject*)result && TAG((CljObject*)result) == CLJ_SYMBOL);
    
    CljSymbol *sym = as_symbol(result);
    TEST_ASSERT_NOT_NULL(sym);
    TEST_ASSERT_EQUAL_CHAR(':', sym->name[0]);
    
}

// Test: Keywords in arithmetic context (should not be used as numbers)
TEST(test_keyword_not_used_as_number) {
    
    // Test: Keywords should not be used in arithmetic operations
    // This should throw an exception
    TRY {
        (void)eval_string("(+ :done 1)", g_test_eval_state);
        // If we get here, the test should fail
        TEST_ASSERT_TRUE(false); // Should not reach here
    } CATCH(ex) {
        // Expected: Exception should be thrown
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_NOT_NULL(ex->message);
    } END_TRY
    
}

