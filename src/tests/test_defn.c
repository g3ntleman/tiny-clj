/*
 * Unity Tests for (defn) function definition in Tiny-CLJ
 * 
 * Test-First: These tests are written before implementing defn functionality
 */

#include "tests_common.h"
#include "../tiny_clj.h"
#include "../memory.h"
#include "../namespace.h"
#include "../symbol.h"
#include "../reader.h"
#include "../function_call.h"
#include "../list.h"
#include "../map.h"
#include "../kv_macros.h"
#include "../runtime.h"
#include "../object.h"
#include <sys/time.h>

// ============================================================================
// TEST: Basic defn function definition
// ============================================================================
TEST(test_defn_basic_function) {
        
        // Test: (defn add [a b] (+ a b)) should define a function
        const char *code = "(defn add [a b] (+ a b))";
        CljValue result = eval_string(code, g_test_eval_state);
        
        // defn should return the function name (symbol)
        TEST_ASSERT_NOT_NULL(result);
        
        // Test that the function can be called
        const char *call_code = "(add 3 4)";
        CljValue call_result = eval_string(call_code, g_test_eval_state);
        
        TEST_ASSERT_NOT_NULL(call_result);
        TEST_ASSERT_TRUE(is_fixnum(call_result));
        TEST_ASSERT_EQUAL_INT(7, as_fixnum(call_result));
        
}

// ============================================================================
// TEST: defn with single parameter
// ============================================================================
TEST(test_defn_single_parameter) {
        
        // Test: (defn square [x] (* x x))
        eval_string("(defn square [x] (* x x))", g_test_eval_state);
        
        // Test function call
        const char *code = "(square 5)";
        CljValue result = eval_string(code, g_test_eval_state);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(25, as_fixnum(result));
        
}

// ============================================================================
// TEST: defn with no parameters
// ============================================================================
TEST(test_defn_no_parameters) {
        
        // Test: (defn answer [] 42)
        eval_string("(defn answer [] 42)", g_test_eval_state);
        
        // Test function call
        const char *code = "(answer)";
        CljValue result = eval_string(code, g_test_eval_state);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(42, as_fixnum(result));
        
}

// ============================================================================
// TEST: defn with multiple body expressions
// ============================================================================
TEST(test_defn_multiple_body_expressions) {
        
        // Test: (defn test-fn [x] (+ x 1) (+ x 2))
        eval_string("(defn test-fn [x] (+ x 1) (+ x 2))", g_test_eval_state);
        
        // Test function call - should return last expression
        const char *code = "(test-fn 5)";
        CljValue result = eval_string(code, g_test_eval_state);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(7, as_fixnum(result));
        
}

// ============================================================================
// TEST: defn with recursive function
// ============================================================================
TEST(test_defn_recursive_function) {
        
        // Test: (defn factorial [n] (if (= n 0) 1 (* n (factorial (- n 1)))))
        eval_string("(defn factorial [n] (if (= n 0) 1 (* n (factorial (- n 1)))))", g_test_eval_state);
        
        // Test function call
        const char *code = "(factorial 5)";
        CljValue result = eval_string(code, g_test_eval_state);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(120, as_fixnum(result));
        
}

// ============================================================================
// TEST: defn symbol resolution in REPL context (reproduces current bug)
// ============================================================================
TEST(test_defn_symbol_resolution_in_repl_context) {
    
    // Simuliere REPL-Kontext: evaluiere defn wie im REPL
    // Das sollte aktuell fehlschlagen mit "Unable to resolve symbol: defn"
    const char *code = "(defn fib [n] (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))";
    CljValue result = eval_string(code, g_test_eval_state);
    
    // Test sollte zeigen, dass defn funktioniert
    TEST_ASSERT_NOT_NULL(result);
    
    // Funktion sollte aufrufbar sein
    CljValue call = eval_string("(fib 5)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(call);
    TEST_ASSERT_TRUE(is_fixnum(call));
    TEST_ASSERT_EQUAL_INT(5, as_fixnum(call));
    
}

// ============================================================================
// TEST: Parameter lookup optimization
// ============================================================================
TEST(test_parameter_lookup_optimization) {
    
    // Define a function with 3 parameters to test lookup performance
    CljValue defn_result = eval_string("(defn test-lookup [a b c] (+ a (+ b c)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL_MESSAGE(defn_result, "defn should return the function symbol");
    
    // Verify that the function is actually defined and callable
    CljValue test_call = eval_string("(test-lookup 1 2 3)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL_MESSAGE(test_call, "test-lookup function should be callable");
    TEST_ASSERT_TRUE_MESSAGE(is_fixnum(test_call), "test-lookup should return a fixnum");
    TEST_ASSERT_EQUAL_INT_MESSAGE(6, as_fixnum(test_call), "test-lookup(1,2,3) should return 6");
    
    // Measure time for 100 parameter lookups
    // This test establishes baseline for parameter lookup performance
    struct timeval start, end;
    gettimeofday(&start, NULL);
    
    // Call function 100 times - each call does parameter lookups
    for (int i = 0; i < 100; i++) {
        char code[64];
        snprintf(code, sizeof(code), "(test-lookup %d %d %d)", i, i+1, i+2);
        CljValue result = eval_string(code, g_test_eval_state);
        if (!result) {
            char msg[128];
            snprintf(msg, sizeof(msg), "test-lookup call %d returned NULL", i);
            TEST_FAIL_MESSAGE(msg);
            return;
        }
        if (!is_fixnum(result)) {
            char msg[128];
            snprintf(msg, sizeof(msg), "test-lookup call %d returned non-fixnum (type: %d)", i, 
                     result ? ((CljObject*)result)->type : -1);
            TEST_FAIL_MESSAGE(msg);
            return;
        }
        int expected = i + (i+1) + (i+2);
        int actual = as_fixnum(result);
        if (actual != expected) {
            char msg[128];
            snprintf(msg, sizeof(msg), "test-lookup call %d: expected %d, got %d", i, expected, actual);
            TEST_FAIL_MESSAGE(msg);
            return;
        }
    }
    
    gettimeofday(&end, NULL);
    (void)((end.tv_sec - start.tv_sec) * 1000.0 + 
           (end.tv_usec - start.tv_usec) / 1000.0);  // Suppress unused variable warning
}

// ============================================================================
// TEST: defn Special Form Recognition and Parsing Tests
// ============================================================================

// Test: Verify that defn is recognized as special form
TEST(test_defn_symbol_recognized) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        
        // Get 'defn' symbol from parser
        Reader reader;
        reader_init(&reader, "(defn test-fn [x] (+ x 1))");
        ID form = value_by_parsing_expr(&reader, g_test_eval_state);
        TEST_ASSERT_NOT_NULL(form);
        
        // Extract the 'defn' symbol from the list
        CljList *list = as_list(form);
        TEST_ASSERT_NOT_NULL(list);
        CljObject *defn_sym = LIST_FIRST(list);
        TEST_ASSERT_NOT_NULL(defn_sym);
        TEST_ASSERT_TRUE_MESSAGE(defn_sym && TAG(defn_sym) == CLJ_SYMBOL, 
                                 "first element should be a symbol");
        
        // Check if defn_sym matches SYM_DEFN
        extern CljSymbol *SYM_DEFN;
        TEST_ASSERT_NOT_NULL(SYM_DEFN);
        
        // Check pointer equality
        bool pointer_match = (defn_sym == (CljObject *)SYM_DEFN);
        
        // If pointer doesn't match, check if they're the same symbol via symbol table
        if (!pointer_match) {
            CljSymbol *parsed_sym = as_symbol(defn_sym);
            CljSymbol *special_sym = as_symbol(SYM_DEFN);
            if (parsed_sym && special_sym && parsed_sym->name && special_sym->name) {
                TEST_FAIL_MESSAGE("defn symbol pointer mismatch - parsed symbol has different pointer than SYM_DEFN (symbol interning issue)");
            } else {
                TEST_FAIL_MESSAGE("defn symbol pointer mismatch and cannot compare names");
            }
        }
        
        // Don't RELEASE form - value_by_parsing_expr returns autoreleased object
    });
}

// Test: Verify that (defn test-fn ...) is parsed correctly
TEST(test_defn_test_fn_parsed) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        
        // Parse (defn test-fn [x] (+ x 1))
        Reader reader;
        reader_init(&reader, "(defn test-fn [x] (+ x 1))");
        ID form = value_by_parsing_expr(&reader, g_test_eval_state);
        TEST_ASSERT_NOT_NULL_MESSAGE(form, "should parse (defn test-fn ...)");
        
        // Verify it's a list
        CljList *list = as_list(form);
        TEST_ASSERT_NOT_NULL_MESSAGE(list, "parsed form should be a list");
        
        // Verify first element is 'defn'
        CljObject *defn_sym = LIST_FIRST(list);
        TEST_ASSERT_NOT_NULL_MESSAGE(defn_sym, "first element should be 'defn' symbol");
        TEST_ASSERT_TRUE_MESSAGE(defn_sym && TAG(defn_sym) == CLJ_SYMBOL, 
                                "first element should be a symbol");
        
        // Verify second element is 'test-fn'
        CljList *rest = as_list((ID)list->rest);
        CljObject *test_fn_sym = rest ? LIST_FIRST(rest) : NULL;
        TEST_ASSERT_NOT_NULL_MESSAGE(test_fn_sym, "second element should be 'test-fn' symbol");
        TEST_ASSERT_TRUE_MESSAGE(test_fn_sym && TAG(test_fn_sym) == CLJ_SYMBOL, 
                                "second element should be a symbol");
        
        CljSymbol *test_fn = as_symbol(test_fn_sym);
        TEST_ASSERT_NOT_NULL(test_fn);
        TEST_ASSERT_NOT_NULL(test_fn->name);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("test-fn", test_fn->name, 
                                        "second element should be 'test-fn' symbol");
        
        // Don't RELEASE form - value_by_parsing_expr returns autoreleased object
    });
}

// Test: Verify that eval_defn is called when (defn test-fn ...) is evaluated
TEST(test_defn_test_fn_evaluated) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        
        // Set current namespace to clojure.core
        
        // Ensure clojure.core cache is set
        if (g_test_eval_state->current_ns && !g_runtime.clojure_core_cache) {
            g_runtime.clojure_core_cache = (void*)g_test_eval_state->current_ns;
        }
        
        // Parse and evaluate (defn test-fn [x] (+ x 1))
        Reader reader;
        reader_init(&reader, "(defn test-fn [x] (+ x 1))");
        ID form = value_by_parsing_expr(&reader, g_test_eval_state);
        TEST_ASSERT_NOT_NULL(form);
        
        // Evaluate the form
        CljMap *env = g_test_eval_state->current_ns ? (CljMap*)g_test_eval_state->current_ns->mappings : NULL;
        ID result = eval_list(as_list(form), env, g_test_eval_state, NULL);
        
        // Should return the symbol 'test-fn'
        TEST_ASSERT_NOT_NULL_MESSAGE(result, "eval_defn should return the symbol");
        TEST_ASSERT_TRUE_MESSAGE(result && TAG(result) == CLJ_SYMBOL, 
                                "eval_defn should return a symbol");
        
        // Verify 'test-fn' is now in the namespace mappings
        CljNamespace *ns = g_test_eval_state->current_ns;
        TEST_ASSERT_NOT_NULL(ns);
        TEST_ASSERT_NOT_NULL_MESSAGE(ns->mappings, "namespace should have mappings");
        
        CljSymbol *test_fn_sym = intern_symbol_global("test-fn");
        ID test_fn_value = map_get(ns->mappings, test_fn_sym, NULL);
        TEST_ASSERT_NOT_NULL_MESSAGE(test_fn_value, 
                                     "'test-fn' should be in namespace mappings after eval_defn");
        
        // Verify that test-fn_value is a function (CLJ_CLOSURE)
        TEST_ASSERT_TRUE_MESSAGE(test_fn_value && TAG(test_fn_value) == CLJ_CLOSURE || test_fn_value && TAG(test_fn_value) == CLJ_FUNC,
                                 "test-fn should be a function");
        
        // Don't RELEASE form - value_by_parsing_expr returns autoreleased object
    });
}

// ============================================================================
// TEST: Verify that add is stored in namespace after defn
// ============================================================================
TEST(test_defn_add_stored_in_namespace) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        
        // Define add function
        const char *code = "(defn add [a b] (+ a b))";
        CljValue result = eval_string(code, g_test_eval_state);
        TEST_ASSERT_NOT_NULL(result);
        
        // Verify that 'add' is now in the namespace mappings
        CljNamespace *ns = g_test_eval_state->current_ns;
        TEST_ASSERT_NOT_NULL(ns);
        TEST_ASSERT_NOT_NULL_MESSAGE(ns->mappings, "namespace should have mappings");
        
        CljSymbol *add_sym = intern_symbol_global("add");
        TEST_ASSERT_NOT_NULL(add_sym);
        
        ID add_value = map_get(ns->mappings, add_sym, NULL);
        TEST_ASSERT_NOT_NULL_MESSAGE(add_value, 
                                     "'add' should be in namespace mappings after defn");
        
        // Verify that add_value is a function (CLJ_CLOSURE)
        TEST_ASSERT_TRUE_MESSAGE(add_value && TAG(add_value) == CLJ_CLOSURE || add_value && TAG(add_value) == CLJ_FUNC,
                                 "add should be a function");
        
    });
}

// ============================================================================
// TEST: Verify that ns_resolve finds add after defn
// ============================================================================
TEST(test_defn_ns_resolve_finds_add) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        
        // Define add function
        const char *code = "(defn add [a b] (+ a b))";
        CljValue result = eval_string(code, g_test_eval_state);
        TEST_ASSERT_NOT_NULL(result);
        
        // Try to resolve 'add' using ns_resolve
        CljSymbol *add_sym = intern_symbol_global("add");
        TEST_ASSERT_NOT_NULL(add_sym);
        
        ID resolved = ns_resolve(g_test_eval_state, add_sym);
        TEST_ASSERT_NOT_NULL_MESSAGE(resolved, 
                                     "ns_resolve should find 'add' after defn");
        
        // Verify that resolved is a function
        TEST_ASSERT_TRUE_MESSAGE(resolved && TAG(resolved) == CLJ_CLOSURE || resolved && TAG(resolved) == CLJ_FUNC,
                                 "resolved 'add' should be a function");
        
    });
}

// ============================================================================
// TEST: Verify that eval_symbol resolves add after defn
// ============================================================================
TEST(test_defn_eval_symbol_resolves_add) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        
        // Define add function
        const char *code = "(defn add [a b] (+ a b))";
        CljValue result = eval_string(code, g_test_eval_state);
        TEST_ASSERT_NOT_NULL(result);
        
        // Try to resolve 'add' using eval_symbol
        CljSymbol *add_sym = intern_symbol_global("add");
        TEST_ASSERT_NOT_NULL(add_sym);
        
        ID resolved = eval_symbol(add_sym, g_test_eval_state);
        TEST_ASSERT_NOT_NULL_MESSAGE(resolved, 
                                     "eval_symbol should resolve 'add' after defn");
        
        // Verify that resolved is a function
        TEST_ASSERT_TRUE_MESSAGE(resolved && TAG(resolved) == CLJ_CLOSURE || resolved && TAG(resolved) == CLJ_FUNC,
                                 "resolved 'add' should be a function");
        
    });
}

// ============================================================================
// TEST: Verify that a function can be called after defn
// ============================================================================
TEST(test_defn_add_can_be_called) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        
        // Define a function (not 'add' since it already exists in clojure.core)
        const char *defn_code = "(defn my-sum [a b] (+ a b))";
        CljValue defn_result = eval_string(defn_code, g_test_eval_state);
        TEST_ASSERT_NOT_NULL(defn_result);
        
        // Verify that 'my-sum' is in namespace
        CljSymbol *my_sum_sym = intern_symbol_global("my-sum");
        ID my_sum_value = ns_resolve(g_test_eval_state, my_sum_sym);
        TEST_ASSERT_NOT_NULL_MESSAGE(my_sum_value, 
                                     "'my-sum' should be resolvable after defn");
        
        // Try to call my-sum
        const char *call_code = "(my-sum 3 4)";
        CljValue call_result = eval_string(call_code, g_test_eval_state);
        
        TEST_ASSERT_NOT_NULL_MESSAGE(call_result, 
                                     "Calling (my-sum 3 4) should return a result");
        
        if (call_result) {
            CljObject *obj = (CljObject*)call_result;
            if (!is_fixnum(call_result)) {
                char msg[256];
                snprintf(msg, sizeof(msg), 
                        "Calling (my-sum 3 4) returned type %d (%s), expected fixnum", 
                        obj->type, clj_type_name(obj->type));
                TEST_FAIL_MESSAGE(msg);
            }
        }
        
        TEST_ASSERT_TRUE_MESSAGE(is_fixnum(call_result), 
                                 "Calling (my-sum 3 4) should return a fixnum");
        TEST_ASSERT_EQUAL_INT_MESSAGE(7, as_fixnum(call_result), 
                                      "Calling (my-sum 3 4) should return 7");
        
    });
}

