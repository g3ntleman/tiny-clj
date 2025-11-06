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
        EvalState *st = evalstate_new();
        
        // Test: (defn add [a b] (+ a b)) should define a function
        const char *code = "(defn add [a b] (+ a b))";
        CljValue result = eval_string(code, st);
        
        // defn should return the function name (symbol)
        TEST_ASSERT_NOT_NULL(result);
        
        // Test that the function can be called
        const char *call_code = "(add 3 4)";
        CljValue call_result = eval_string(call_code, st);
        
        TEST_ASSERT_NOT_NULL(call_result);
        TEST_ASSERT_TRUE(is_fixnum(call_result));
        TEST_ASSERT_EQUAL_INT(7, as_fixnum(call_result));
        
        evalstate_free(st);
}

// ============================================================================
// TEST: defn with single parameter
// ============================================================================
TEST(test_defn_single_parameter) {
        EvalState *st = evalstate_new();
        
        // Test: (defn square [x] (* x x))
        eval_string("(defn square [x] (* x x))", st);
        
        // Test function call
        const char *code = "(square 5)";
        CljValue result = eval_string(code, st);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(25, as_fixnum(result));
        
        evalstate_free(st);
}

// ============================================================================
// TEST: defn with no parameters
// ============================================================================
TEST(test_defn_no_parameters) {
        EvalState *st = evalstate_new();
        
        // Test: (defn answer [] 42)
        eval_string("(defn answer [] 42)", st);
        
        // Test function call
        const char *code = "(answer)";
        CljValue result = eval_string(code, st);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(42, as_fixnum(result));
        
        evalstate_free(st);
}

// ============================================================================
// TEST: defn with multiple body expressions
// ============================================================================
TEST(test_defn_multiple_body_expressions) {
        EvalState *st = evalstate_new();
        
        // Test: (defn test-fn [x] (+ x 1) (+ x 2))
        eval_string("(defn test-fn [x] (+ x 1) (+ x 2))", st);
        
        // Test function call - should return last expression
        const char *code = "(test-fn 5)";
        CljValue result = eval_string(code, st);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(7, as_fixnum(result));
        
        evalstate_free(st);
}

// ============================================================================
// TEST: defn with recursive function
// ============================================================================
TEST(test_defn_recursive_function) {
        EvalState *st = evalstate_new();
        
        // Test: (defn factorial [n] (if (= n 0) 1 (* n (factorial (- n 1)))))
        eval_string("(defn factorial [n] (if (= n 0) 1 (* n (factorial (- n 1)))))", st);
        
        // Test function call
        const char *code = "(factorial 5)";
        CljValue result = eval_string(code, st);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(120, as_fixnum(result));
        
        evalstate_free(st);
}

// ============================================================================
// TEST: defn symbol resolution in REPL context (reproduces current bug)
// ============================================================================
TEST(test_defn_symbol_resolution_in_repl_context) {
    EvalState *st = evalstate_new();
    
    // Simuliere REPL-Kontext: evaluiere defn wie im REPL
    // Das sollte aktuell fehlschlagen mit "Unable to resolve symbol: defn"
    const char *code = "(defn fib [n] (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))";
    CljValue result = eval_string(code, st);
    
    // Test sollte zeigen, dass defn funktioniert
    TEST_ASSERT_NOT_NULL(result);
    
    // Funktion sollte aufrufbar sein
    CljValue call = eval_string("(fib 5)", st);
    TEST_ASSERT_NOT_NULL(call);
    TEST_ASSERT_TRUE(is_fixnum(call));
    TEST_ASSERT_EQUAL_INT(5, as_fixnum(call));
    
    evalstate_free(st);
}

// ============================================================================
// TEST: Parameter lookup optimization
// ============================================================================
TEST(test_parameter_lookup_optimization) {
    EvalState *st = evalstate_new();
    
    // Define a function with 3 parameters to test lookup performance
    eval_string("(defn test-lookup [a b c] (+ a (+ b c)))", st);
    
    // Measure time for 1000 parameter lookups
    // This test establishes baseline for parameter lookup performance
    struct timeval start, end;
    gettimeofday(&start, NULL);
    
    // Call function 1000 times - each call does parameter lookups
    for (int i = 0; i < 1000; i++) {
        char code[64];
        snprintf(code, sizeof(code), "(test-lookup %d %d %d)", i, i+1, i+2);
        CljValue result = eval_string(code, st);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(i + (i+1) + (i+2), as_fixnum(result));
    }
    
    gettimeofday(&end, NULL);
    double elapsed_ms = (end.tv_sec - start.tv_sec) * 1000.0 + 
                       (end.tv_usec - start.tv_usec) / 1000.0;
    
    printf("Baseline: 1000 function calls with parameter lookups took %.2f ms\n", elapsed_ms);
    
    evalstate_free(st);
}

// ============================================================================
// TEST: defn Special Form Recognition and Parsing Tests
// ============================================================================

// Test: Verify that defn is recognized as special form
TEST(test_defn_symbol_recognized) {
    WITH_AUTORELEASE_POOL({
        EvalState *st = evalstate_new();
        TEST_ASSERT_NOT_NULL(st);
        
        // Get 'defn' symbol from parser
        Reader reader;
        reader_init(&reader, "(defn test-fn [x] (+ x 1))");
        ID form = value_by_parsing_expr(&reader, st);
        TEST_ASSERT_NOT_NULL(form);
        
        // Extract the 'defn' symbol from the list
        CljList *list = as_list(form);
        TEST_ASSERT_NOT_NULL(list);
        CljObject *defn_sym = LIST_FIRST(list);
        TEST_ASSERT_NOT_NULL(defn_sym);
        TEST_ASSERT_TRUE_MESSAGE(is_type(defn_sym, CLJ_SYMBOL), 
                                 "first element should be a symbol");
        
        // Check if defn_sym matches SYM_DEFN
        extern CljObject *SYM_DEFN;
        TEST_ASSERT_NOT_NULL(SYM_DEFN);
        
        // Check pointer equality
        bool pointer_match = (defn_sym == SYM_DEFN);
        
        // If pointer doesn't match, check if they're the same symbol via symbol table
        if (!pointer_match) {
            CljSymbol *parsed_sym = as_symbol(defn_sym);
            CljSymbol *special_sym = as_symbol(SYM_DEFN);
            if (parsed_sym && special_sym && parsed_sym->name && special_sym->name) {
                bool name_match = (strcmp(parsed_sym->name, special_sym->name) == 0);
                TEST_FAIL_MESSAGE("defn symbol pointer mismatch - parsed symbol has different pointer than SYM_DEFN (symbol interning issue)");
            } else {
                TEST_FAIL_MESSAGE("defn symbol pointer mismatch and cannot compare names");
            }
        }
        
        RELEASE((CljObject*)form);
        evalstate_free(st);
    });
}

// Test: Verify that (defn test-fn ...) is parsed correctly
TEST(test_defn_test_fn_parsed) {
    WITH_AUTORELEASE_POOL({
        EvalState *st = evalstate_new();
        TEST_ASSERT_NOT_NULL(st);
        
        // Parse (defn test-fn [x] (+ x 1))
        Reader reader;
        reader_init(&reader, "(defn test-fn [x] (+ x 1))");
        ID form = value_by_parsing_expr(&reader, st);
        TEST_ASSERT_NOT_NULL_MESSAGE(form, "should parse (defn test-fn ...)");
        
        // Verify it's a list
        CljList *list = as_list(form);
        TEST_ASSERT_NOT_NULL_MESSAGE(list, "parsed form should be a list");
        
        // Verify first element is 'defn'
        CljObject *defn_sym = LIST_FIRST(list);
        TEST_ASSERT_NOT_NULL_MESSAGE(defn_sym, "first element should be 'defn' symbol");
        TEST_ASSERT_TRUE_MESSAGE(is_type(defn_sym, CLJ_SYMBOL), 
                                "first element should be a symbol");
        
        // Verify second element is 'test-fn'
        CljList *rest = as_list((ID)list->rest);
        CljObject *test_fn_sym = rest ? LIST_FIRST(rest) : NULL;
        TEST_ASSERT_NOT_NULL_MESSAGE(test_fn_sym, "second element should be 'test-fn' symbol");
        TEST_ASSERT_TRUE_MESSAGE(is_type(test_fn_sym, CLJ_SYMBOL), 
                                "second element should be a symbol");
        
        CljSymbol *test_fn = as_symbol(test_fn_sym);
        TEST_ASSERT_NOT_NULL(test_fn);
        TEST_ASSERT_NOT_NULL(test_fn->name);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("test-fn", test_fn->name, 
                                        "second element should be 'test-fn' symbol");
        
        RELEASE((CljObject*)form);
        evalstate_free(st);
    });
}

// Test: Verify that eval_defn is called when (defn test-fn ...) is evaluated
TEST(test_defn_test_fn_evaluated) {
    WITH_AUTORELEASE_POOL({
        EvalState *st = evalstate_new();
        TEST_ASSERT_NOT_NULL(st);
        
        // Set current namespace to clojure.core
        evalstate_set_ns(st, "clojure.core");
        
        // Ensure clojure.core cache is set
        if (st->current_ns && !g_runtime.clojure_core_cache) {
            g_runtime.clojure_core_cache = (void*)st->current_ns;
        }
        
        // Parse and evaluate (defn test-fn [x] (+ x 1))
        Reader reader;
        reader_init(&reader, "(defn test-fn [x] (+ x 1))");
        ID form = value_by_parsing_expr(&reader, st);
        TEST_ASSERT_NOT_NULL(form);
        
        // Evaluate the form
        CljMap *env = st->current_ns ? (CljMap*)st->current_ns->mappings : NULL;
        ID result = eval_list(as_list(form), env, st);
        
        // Should return the symbol 'test-fn'
        TEST_ASSERT_NOT_NULL_MESSAGE(result, "eval_defn should return the symbol");
        TEST_ASSERT_TRUE_MESSAGE(is_type(result, CLJ_SYMBOL), 
                                "eval_defn should return a symbol");
        
        // Verify 'test-fn' is now in the namespace mappings
        CljNamespace *ns = st->current_ns;
        TEST_ASSERT_NOT_NULL(ns);
        TEST_ASSERT_NOT_NULL_MESSAGE(ns->mappings, "namespace should have mappings");
        
        CljObject *test_fn_sym = intern_symbol_global("test-fn");
        ID test_fn_value = map_get((CljValue)ns->mappings, (CljValue)test_fn_sym);
        TEST_ASSERT_NOT_NULL_MESSAGE(test_fn_value, 
                                     "'test-fn' should be in namespace mappings after eval_defn");
        
        // Verify that test-fn_value is a function (CLJ_CLOSURE)
        TEST_ASSERT_TRUE_MESSAGE(is_type(test_fn_value, CLJ_CLOSURE) || is_type(test_fn_value, CLJ_FUNC),
                                 "test-fn should be a function");
        
        RELEASE((CljObject*)form);
        evalstate_free(st);
    });
}

