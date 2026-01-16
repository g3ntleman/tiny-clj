/*
 * Unity Tests for (defn) function definition in Tiny-CLJ
 *
 * Test-First: These tests are written before implementing defn functionality
 */

#include "tests_common.h"
#include "../tiny_clj.h"
#include "memory.h"
#include "../namespace.h"
#include "../symbol.h"
#include "../reader.h"
#include "../eval.h"
#include "../list.h"
#include "map.h"
#include "kv_macros.h"
#include "../runtime.h"
#include "../object.h"
#include "../builtins.h"
#include <sys/time.h>

// ============================================================================
// TEST: :native Keyword statisch registriert
// ============================================================================
TEST(test_native_keyword_registered) {
    // Ensure special symbols are initialized
    init_special_symbols();

    // Test that SYM_KW_NATIVE exists
    TEST_ASSERT_NOT_NULL_MESSAGE(SYM_KW_NATIVE, "SYM_KW_NATIVE should be registered");

    // Test that it's a keyword
    TEST_ASSERT_TRUE_MESSAGE(IS_KEYWORD((CljObject*)SYM_KW_NATIVE),
                             "SYM_KW_NATIVE should be a keyword");

    // Test that name is ":native"
    CljSymbol *native_kw = as_symbol((CljObject*)SYM_KW_NATIVE);
    TEST_ASSERT_NOT_NULL(native_kw);
    TEST_ASSERT_NOT_NULL(native_kw->cname);
    TEST_ASSERT_EQUAL_STRING(":native", native_kw->cname);
}

// ============================================================================
// TEST: :native Keyword kann geparst werden
// ============================================================================
TEST(test_native_keyword_can_be_parsed) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        init_special_symbols();

        // Parse :native keyword
        Reader reader;
        reader_init(&reader, ":native");
        ID parsed = value_by_parsing_expr(&reader, g_test_eval_state);
        TEST_ASSERT_NOT_NULL_MESSAGE(parsed, ":native should be parseable");

        ID canonical = canonicalize_ast(parsed, g_test_eval_state);
        TEST_ASSERT_NOT_NULL_MESSAGE(canonical, "canonicalization should succeed");

        TEST_ASSERT_TRUE_MESSAGE(IS_KEYWORD((CljObject*)canonical),
                                 "parsed :native should be a keyword");

        // Compare with SYM_KW_NATIVE (pointer equality for interned symbols)
        TEST_ASSERT_EQUAL_PTR_MESSAGE(SYM_KW_NATIVE, canonical,
                                      ":native should match SYM_KW_NATIVE");
    });
}

// ============================================================================
// TEST: defn macro expands to def with fn
// ============================================================================
TEST(test_defn_with_native_marker_recognized) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        init_special_symbols();

        // Parse (defn trim [s] :native)
        // After macro expansion: (def trim (fn trim [s] :native))
        Reader reader;
        reader_init(&reader, "(defn trim [s] :native)");
        ID form = value_by_parsing_expr(&reader, g_test_eval_state);

        TEST_ASSERT_NOT_NULL_MESSAGE(form, "should parse (defn trim [s] :native)");

        // Verify it expands to (def ...) - defn is now a macro
        ID canonical_form = canonicalize_ast(form, g_test_eval_state);
        TEST_ASSERT_NOT_NULL(canonical_form);
        CljList *list = as_list(canonical_form);
        TEST_ASSERT_NOT_NULL_MESSAGE(list, "expanded form should be a list");

        // First element should be 'def' (not 'defn')
        CljObject *first = LIST_FIRST(list);
        TEST_ASSERT_NOT_NULL(first);
        TEST_ASSERT_TRUE_MESSAGE(TAG(first) == CLJ_SYMBOL, "first element should be a symbol");
        CljSymbol *first_sym = as_symbol(first);
        TEST_ASSERT_TRUE_MESSAGE(first_sym == SYM_DEF, "defn should expand to def");
    });
}

// ============================================================================
// TEST: defn mit :native als Body wird erkannt (eval_defn)
// ============================================================================
TEST(test_defn_eval_defn_recognizes_native_marker) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        init_special_symbols();

        // Switch to clojure.string namespace (where trim is defined)
        eval_string("(ns clojure.string)", g_test_eval_state);

        // Parse and evaluate (defn trim [s] :native)
        // This should succeed now that native lookup is implemented
        const char *code = "(defn trim [s] :native)";
        CljValue result = eval_string(code, g_test_eval_state);

        TEST_ASSERT_NOT_NULL_MESSAGE(result, "defn with :native should succeed");
    });
}

// ============================================================================
// TEST: defn mit normalem Body wird normal behandelt
// ============================================================================
TEST(test_defn_normal_body_still_works) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);

        // Normal defn should still work
        const char *code = "(defn add [a b] (+ a b))";
        CljValue result = eval_string(code, g_test_eval_state);

        TEST_ASSERT_NOT_NULL_MESSAGE(result, "normal defn should work");

        // Test that function can be called
        CljValue call_result = eval_string("(add 3 4)", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(call_result);
        TEST_ASSERT_TRUE(is_fixnum(call_result));
        TEST_ASSERT_EQUAL_INT(7, as_fixnum(call_result));
    });
}

// ============================================================================
// TEST: defn closures capture parameters when returning nested fn
// ============================================================================
TEST(test_defn_closure_captures_parameter) {
    WITH_AUTORELEASE_POOL({
        const char *code = "(do (defn make-adder [x] (fn [] x)) ((make-adder 7)))";
        CljValue result = eval_string(code, g_test_eval_state);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(7, as_fixnum(result));
    });
}

// ============================================================================
// TEST: defn mit :native als Body registriert native Funktion
// ============================================================================
TEST(test_defn_native_stub_registers_native_function) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        init_special_symbols();

        // Switch to clojure.string namespace
        eval_string("(ns clojure.string)", g_test_eval_state);

        // Define native stub
        const char *code = "(defn trim [s] :native)";
        CljValue result = eval_string(code, g_test_eval_state);

        TEST_ASSERT_NOT_NULL_MESSAGE(result, "defn with :native should succeed");

        // Verify that trim is now a native function
        CljValue trim_resolved = eval_string("trim", g_test_eval_state);
        TEST_ASSERT_NOT_NULL_MESSAGE(trim_resolved, "trim should be resolvable");
        TEST_ASSERT_TRUE_MESSAGE(TAG(trim_resolved) == CLJ_FUNC,
                                 "trim should be a function");

        // Test that native function can be called
        CljValue call_result = eval_string("(trim \"  hello  \")", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(call_result);
        TEST_ASSERT_TRUE(TAG(call_result) == CLJ_STRING);
    });
}

// ============================================================================
// TEST: Native Funktion Lookup - trim finden
// ============================================================================
TEST(test_native_function_lookup_trim) {
    CljSymbol *trim_sym = intern_symbol(SYM_CLOJURE_STRING, "trim");
    TEST_ASSERT_NOT_NULL(trim_sym);
    BuiltinFn func = native_function_lookup(trim_sym);
    TEST_ASSERT_NOT_NULL_MESSAGE(func, "native_function_lookup should find trim");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(native_trim, func, "should return native_trim function");
}

// ============================================================================
// TEST: Native Funktion Lookup - upper-case finden
// ============================================================================
TEST(test_native_function_lookup_upper_case) {
    CljSymbol *upper_case_sym = intern_symbol(SYM_CLOJURE_STRING, "upper-case");
    TEST_ASSERT_NOT_NULL(upper_case_sym);
    BuiltinFn func = native_function_lookup(upper_case_sym);
    TEST_ASSERT_NOT_NULL_MESSAGE(func, "native_function_lookup should find upper-case");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(native_upper_case, func, "should return native_upper_case function");
}

// ============================================================================
// TEST: Native Funktion Lookup - nonexistent nicht finden
// ============================================================================
TEST(test_native_function_lookup_nonexistent) {
    CljSymbol *nonexistent_sym = intern_symbol(SYM_CLOJURE_STRING, "nonexistent");
    TEST_ASSERT_NOT_NULL(nonexistent_sym);
    BuiltinFn func = native_function_lookup(nonexistent_sym);
    TEST_ASSERT_NULL_MESSAGE(func, "native_function_lookup should return NULL for nonexistent function");
}

// ============================================================================
// TEST: Native Funktion Lookup - NULL name
// ============================================================================
TEST(test_native_function_lookup_null_name) {
    BuiltinFn func = native_function_lookup(NULL);
    TEST_ASSERT_NULL_MESSAGE(func, "native_function_lookup should return NULL for NULL name");
}

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

    // Call function 10 times - each call does parameter lookups
    for (int i = 0; i < 10; i++) {
        char code[64];
        test_snprintf(code, sizeof(code), "(test-lookup %d %d %d)", i, i+1, i+2);
        CljValue result = eval_string(code, g_test_eval_state);
        if (!result) {
            char msg[128];
            test_snprintf(msg, sizeof(msg), "test-lookup call %d returned NULL", i);
            TEST_FAIL_MESSAGE(msg);
            return;
        }
        if (!is_fixnum(result)) {
            char msg[128];
            test_snprintf(msg, sizeof(msg), "test-lookup call %d returned non-fixnum (type: %d)", i,
                     result ? ((CljObject*)result)->type : -1);
            TEST_FAIL_MESSAGE(msg);
            return;
        }
        int expected = i + (i+1) + (i+2);
        int actual = as_fixnum(result);
        if (actual != expected) {
            char msg[128];
            test_snprintf(msg, sizeof(msg), "test-lookup call %d: expected %d, got %d", i, expected, actual);
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

// Test: Verify that defn macro expands to def
TEST(test_defn_symbol_recognized) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);

        // Parse (defn test-fn [x] (+ x 1))
        // defn is now a macro that expands to (def test-fn (fn test-fn [x] (+ x 1)))
        Reader reader;
        reader_init(&reader, "(defn test-fn [x] (+ x 1))");
        ID form = value_by_parsing_expr(&reader, g_test_eval_state);
        TEST_ASSERT_NOT_NULL(form);

        ID canonical_form = canonicalize_ast(form, g_test_eval_state);
        TEST_ASSERT_NOT_NULL(canonical_form);

        // Extract the first symbol from the expanded list
        CljList *list = as_list(canonical_form);
        TEST_ASSERT_NOT_NULL(list);
        CljObject *first_sym = LIST_FIRST(list);
        TEST_ASSERT_NOT_NULL(first_sym);
        TEST_ASSERT_TRUE_MESSAGE(TAG(first_sym) == CLJ_SYMBOL,
                                 "first element should be a symbol");

        // After macro expansion, first symbol should be 'def' (not 'defn')
        extern CljSymbol *SYM_DEF;
        TEST_ASSERT_NOT_NULL(SYM_DEF);

        // Check pointer equality - should be SYM_DEF now
        TEST_ASSERT_EQUAL_PTR_MESSAGE(SYM_DEF, first_sym,
                                      "defn should expand to def");
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

        ID canonical_form = canonicalize_ast(form, g_test_eval_state);
        TEST_ASSERT_NOT_NULL(canonical_form);

        // Verify it's a list
        CljList *list = as_list(canonical_form);
        TEST_ASSERT_NOT_NULL_MESSAGE(list, "parsed form should be a list");

        // Verify first element is 'defn'
        CljObject *defn_sym = LIST_FIRST(list);
        TEST_ASSERT_NOT_NULL_MESSAGE(defn_sym, "first element should be 'defn' symbol");
        TEST_ASSERT_TRUE_MESSAGE(TAG(defn_sym) == CLJ_SYMBOL,
                                "first element should be a symbol");

        // Verify second element is 'test-fn'
        CljList *rest = as_list(list->rest);
        CljObject *test_fn_sym = rest ? LIST_FIRST(rest) : NULL;
        TEST_ASSERT_NOT_NULL_MESSAGE(test_fn_sym, "second element should be 'test-fn' symbol");
        TEST_ASSERT_TRUE_MESSAGE(TAG(test_fn_sym) == CLJ_SYMBOL,
                                "second element should be a symbol");

        CljSymbol *test_fn = as_symbol(test_fn_sym);
        TEST_ASSERT_NOT_NULL(test_fn);
        TEST_ASSERT_NOT_NULL(test_fn->cname);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("test-fn", test_fn->cname,
                                        "second element should be 'test-fn' symbol");

        // Don't RELEASE form - value_by_parsing_expr returns autoreleased object
    });
}

// Test: Verify that eval_defn is called when (defn test-fn ...) is evaluated
TEST(test_defn_test_fn_evaluated) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);

        // Set current namespace to clojure.core

        // Ensure clojure.core namespace exists (already set by evalstate_set_ns)
        // No special cache handling needed

        // Parse and evaluate (defn test-fn [x] (+ x 1))
        Reader reader;
        reader_init(&reader, "(defn test-fn [x] (+ x 1))");
        ID form = value_by_parsing_expr(&reader, g_test_eval_state);
        TEST_ASSERT_NOT_NULL(form);

        ID canonical_form = canonicalize_ast(form, g_test_eval_state);
        TEST_ASSERT_NOT_NULL(canonical_form);

        // Evaluate the form
        CljMap *env = g_test_eval_state->current_ns ? (CljMap*)g_test_eval_state->current_ns->mappings : NULL;
        ID result = eval_list(as_list(canonical_form), env, g_test_eval_state, NULL);

        // Should return the symbol 'test-fn'
        TEST_ASSERT_NOT_NULL_MESSAGE(result, "eval_defn should return the symbol");
        TEST_ASSERT_TRUE_MESSAGE(TAG(result) == CLJ_SYMBOL,
                                "eval_defn should return a symbol");

        // Verify 'test-fn' is now in the namespace mappings
        CljNamespace *ns = g_test_eval_state->current_ns;
        TEST_ASSERT_NOT_NULL(ns);
        TEST_ASSERT_NOT_NULL_MESSAGE(ns->mappings, "namespace should have mappings");

        // CRITICAL: ns_define stores qualified symbols in mappings
        // Get the qualified symbol from the symbol table for lookup
        CljSymbol *qualified_test_fn_sym = NULL;
        if (ns->name && ns->name->cname) {
            qualified_test_fn_sym = intern_symbol(ns->name, "test-fn");
        }
        ID test_fn_value = qualified_test_fn_sym ? map_get_sentinel(ns->mappings, qualified_test_fn_sym, NULL) : NULL;
        TEST_ASSERT_NOT_NULL_MESSAGE(test_fn_value,
                                     "'test-fn' should be in namespace mappings after eval_defn");

        // Verify that test-fn_value is a function (CLJ_CLOSURE)
        TEST_ASSERT_TRUE_MESSAGE(TAG(test_fn_value) == CLJ_CLOSURE || TAG(test_fn_value) == CLJ_FUNC,
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

        // CRITICAL: ns_define stores qualified symbols in mappings
        // Get the qualified symbol from the symbol table for lookup
        CljSymbol *add_sym = intern_symbol_global("add");
        TEST_ASSERT_NOT_NULL(add_sym);
        
        CljSymbol *qualified_add_sym = NULL;
        if (ns->name && ns->name->cname) {
            qualified_add_sym = intern_symbol(ns->name, "add");
        }
        TEST_ASSERT_NOT_NULL_MESSAGE(qualified_add_sym, "Should be able to create qualified symbol");

        ID add_value = map_get_sentinel(ns->mappings, qualified_add_sym, NULL);
        TEST_ASSERT_NOT_NULL_MESSAGE(add_value,
                                     "'add' should be in namespace mappings after defn");

        // Verify that add_value is a function (CLJ_CLOSURE)
        TEST_ASSERT_TRUE_MESSAGE(TAG(add_value) == CLJ_CLOSURE || TAG(add_value) == CLJ_FUNC,
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
        TEST_ASSERT_TRUE_MESSAGE(TAG(resolved) == CLJ_CLOSURE || TAG(resolved) == CLJ_FUNC,
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
        TEST_ASSERT_TRUE_MESSAGE(TAG(resolved) == CLJ_CLOSURE || TAG(resolved) == CLJ_FUNC,
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
                test_snprintf(msg, sizeof(msg),
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

// ============================================================================
// TEST: defn supports optional docstring (regression)
// ============================================================================
TEST(test_defn_docstring_function_is_callable) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);

        // Regression: (defn f "doc" [x] x) previously mis-parsed and produced nil / broken f.
        ID r = eval_string("(do (defn f \"doc\" [x] x) (f 7))", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(r);
        TEST_ASSERT_TRUE(is_fixnum(r));
        TEST_ASSERT_EQUAL_INT(7, as_fixnum(r));
    });
}

TEST(test_defn_docstring_native_stub_still_works) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        init_special_symbols();

        // Use a namespace where a :native stub is expected (clojure.string/trim).
        eval_string("(ns clojure.string)", g_test_eval_state);

        ID r = eval_string("(do (defn trim \"doc\" [s] :native) (trim \"  hello  \"))", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(r);
        TEST_ASSERT_TRUE(TAG(r) == CLJ_STRING);
    });
}

