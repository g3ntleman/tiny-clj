/*
 * Unity Tests for qualified symbol resolution in Tiny-CLJ
 *
 * Tests for resolving qualified symbols like clojure.core/reverse, clojure.string/blank?, etc.
 */

#include "tests_common.h"
#include "symbol.h"
#include "namespace.h"
#include "eval.h"
#include "ast_canon.h"

// Forward declaration
int load_clojure_core(EvalState *st);

// ============================================================================
// HELPER: Load clojure.string namespace
// ============================================================================

static void load_clojure_string_namespace(void) {
    CljObject *req_result = eval_string("(require 'clojure.string)", g_test_eval_state);
    (void)req_result; // require returns nil
}

// ============================================================================
// TESTS FOR QUALIFIED SYMBOL PARSING
// ============================================================================

// Test: Verify that qualified symbols are parsed correctly with ns field set
TEST(test_qualified_symbol_parsing) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Parse a qualified symbol
    CljObject *parsed = eval_string("'clojure.core/reverse", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(parsed);
    TEST_ASSERT_TRUE(TAG(parsed) == CLJ_SYMBOL);

    CljSymbol *sym = as_symbol(parsed);
    TEST_ASSERT_NOT_NULL(sym);
    TEST_ASSERT_NOT_NULL(sym->ns_name); // ns field should be set
    TEST_ASSERT_NOT_NULL(sym->cname); // name field should be set
    TEST_ASSERT_EQUAL_STRING("reverse", sym->cname);
    TEST_ASSERT_EQUAL_STRING("clojure.core", sym->ns_name->cname);
}

// Test: Verify that unqualified symbols have ns field NULL
TEST(test_unqualified_symbol_parsing) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Parse an unqualified symbol
    CljObject *parsed = eval_string("'reverse", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(parsed);
    TEST_ASSERT_TRUE(TAG(parsed) == CLJ_SYMBOL);

    CljSymbol *sym = as_symbol(parsed);
    TEST_ASSERT_NOT_NULL(sym);
    TEST_ASSERT_NULL(sym->ns_name); // ns field should be NULL for unqualified symbols
    TEST_ASSERT_NOT_NULL(sym->cname);
    TEST_ASSERT_EQUAL_STRING("reverse", sym->cname);
}

// ============================================================================
// TESTS FOR clojure.core QUALIFIED SYMBOLS
// ============================================================================

// Test: Resolve clojure.core/reverse
TEST(test_resolve_clojure_core_reverse) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Load clojure.core
    load_clojure_core(g_test_eval_state);

    // Test direct resolution via eval_symbol
    CljSymbol *reverse_sym = intern_symbol(SYM_CLOJURE_CORE, "reverse");
    TEST_ASSERT_NOT_NULL(reverse_sym);
    TEST_ASSERT_NOT_NULL(reverse_sym->ns_name);

    ID resolved = eval_symbol(reverse_sym, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(resolved);
    TEST_ASSERT_TRUE(TAG(resolved) == CLJ_FUNC || TAG(resolved) == CLJ_CLOSURE);
}

// Test: Evaluate clojure.core/reverse in expression
TEST(test_eval_clojure_core_reverse_expression) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Load clojure.core
    load_clojure_core(g_test_eval_state);

    // Test: (clojure.core/reverse '(1 2 3)) => '(3 2 1)
    CljObject *result = eval_string("(clojure.core/reverse '(1 2 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_list_type(TAG(result)));
}

// Test: Resolve clojure.core/reverse in let binding
TEST(test_resolve_clojure_core_reverse_in_let) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Load clojure.core
    load_clojure_core(g_test_eval_state);

    // Test: (let [rev clojure.core/reverse] (rev '(1 2 3)))
    CljObject *result = eval_string("(let [rev clojure.core/reverse] (rev '(1 2 3)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_list_type(TAG(result)));
}

// Test: Resolve clojure.core/reverse in function
TEST(test_resolve_clojure_core_reverse_in_function) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Load clojure.core
    load_clojure_core(g_test_eval_state);

    // Test: (fn [x] (clojure.core/reverse x))
    CljObject *fn_result = eval_string("(fn [x] (clojure.core/reverse x))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(fn_result);
    TEST_ASSERT_TRUE(TAG(fn_result) == CLJ_CLOSURE);

    // Call the function
    CljObject *call_result = eval_string("((fn [x] (clojure.core/reverse x)) '(1 2 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(call_result);
    TEST_ASSERT_TRUE(is_list_type(TAG(call_result)));
}

// ============================================================================
// TESTS FOR clojure.string QUALIFIED SYMBOLS
// ============================================================================

// Test: Resolve clojure.string/blank?
TEST(test_resolve_clojure_string_blank) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Load clojure.string namespace
    load_clojure_string_namespace();

    // Test direct resolution via eval_symbol
    CljSymbol *blank_sym = intern_symbol(SYM_CLOJURE_STRING, "blank?");
    TEST_ASSERT_NOT_NULL(blank_sym);
    TEST_ASSERT_NOT_NULL(blank_sym->ns_name);

    ID resolved = eval_symbol(blank_sym, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(resolved);
    TEST_ASSERT_TRUE(TAG(resolved) == CLJ_FUNC || TAG(resolved) == CLJ_CLOSURE);
}

// Test: Evaluate clojure.string/blank? in expression
TEST(test_eval_clojure_string_blank_expression) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Load clojure.string namespace
    load_clojure_string_namespace();

    // Test: (clojure.string/blank? "") => true
    CljObject *result = eval_string("(clojure.string/blank? \"\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

// Test: Resolve clojure.string/join
TEST(test_resolve_clojure_string_join) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Load clojure.string namespace
    load_clojure_string_namespace();

    // Test direct resolution via eval_symbol
    CljSymbol *join_sym = intern_symbol(SYM_CLOJURE_STRING, "join");
    TEST_ASSERT_NOT_NULL(join_sym);
    TEST_ASSERT_NOT_NULL(join_sym->ns_name);

    ID resolved = eval_symbol(join_sym, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(resolved);
    TEST_ASSERT_TRUE(TAG(resolved) == CLJ_FUNC || TAG(resolved) == CLJ_CLOSURE);
}

// Test: Evaluate clojure.string/join in expression
TEST(test_eval_clojure_string_join_expression) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Load clojure.string namespace
    load_clojure_string_namespace();

    // Test: (clojure.string/join "," '("a" "b" "c")) => "a,b,c"
    CljObject *result = eval_string("(clojure.string/join \",\" '(\"a\" \"b\" \"c\"))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_STRING);
}

// Test: Resolve clojure.string/reverse
TEST(test_resolve_clojure_string_reverse) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Load clojure.string namespace
    load_clojure_string_namespace();

    // Test direct resolution via eval_symbol
    CljSymbol *reverse_sym = intern_symbol(SYM_CLOJURE_STRING, "reverse");
    TEST_ASSERT_NOT_NULL(reverse_sym);
    TEST_ASSERT_NOT_NULL(reverse_sym->ns_name);

    ID resolved = eval_symbol(reverse_sym, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(resolved);
    TEST_ASSERT_TRUE(TAG(resolved) == CLJ_FUNC || TAG(resolved) == CLJ_CLOSURE);
}

// Test: Evaluate clojure.string/reverse in expression
TEST(test_eval_clojure_string_reverse_expression) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Load clojure.string namespace
    load_clojure_string_namespace();

    // Test: (clojure.string/reverse "hello") => "olleh"
    CljObject *result = eval_string("(clojure.string/reverse \"hello\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_STRING);
}

// ============================================================================
// TESTS FOR QUALIFIED SYMBOLS IN LOCAL FUNCTIONS
// ============================================================================

// Test: Resolve clojure.core/reverse in local function (fn)
TEST(test_resolve_clojure_core_reverse_in_local_fn) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Load clojure.core
    load_clojure_core(g_test_eval_state);

    // Test: Define a function that uses clojure.core/reverse
    CljObject *fn_result = eval_string("(fn [x] (clojure.core/reverse x))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(fn_result);
    TEST_ASSERT_TRUE(TAG(fn_result) == CLJ_CLOSURE);

    // Call the function
    CljObject *call_result = eval_string("((fn [x] (clojure.core/reverse x)) '(1 2 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(call_result);
    TEST_ASSERT_TRUE(is_list_type(TAG(call_result)));
}

// Test: Resolve clojure.core/reverse in let binding with function
TEST(test_resolve_clojure_core_reverse_in_let_with_fn) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Load clojure.core
    load_clojure_core(g_test_eval_state);

    // Test: (let [step (fn [x] (clojure.core/reverse x))] (step '(1 2 3)))
    CljObject *result = eval_string("(let [step (fn [x] (clojure.core/reverse x))] (step '(1 2 3)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_list_type(TAG(result)));
}

// Test: Resolve clojure.string/join in local function
TEST(test_resolve_clojure_string_join_in_local_fn) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Load clojure.string namespace
    load_clojure_string_namespace();

    // Test: Define a function that uses clojure.string/join
    CljObject *fn_result = eval_string("(fn [coll] (clojure.string/join \",\" coll))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(fn_result);
    TEST_ASSERT_TRUE(TAG(fn_result) == CLJ_CLOSURE);

    // Call the function
    CljObject *call_result = eval_string("((fn [coll] (clojure.string/join \",\" coll)) '(\"a\" \"b\" \"c\"))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(call_result);
    TEST_ASSERT_TRUE(TAG(call_result) == CLJ_STRING);
}

// ============================================================================
// TESTS FOR NAMESPACE ALIAS RESOLUTION
// ============================================================================

// Test: Resolve qualified symbol with namespace alias
TEST(test_resolve_qualified_symbol_with_alias) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Load clojure.string namespace
    load_clojure_string_namespace();

    // Create a namespace with alias
    eval_string("(ns test-ns (:require [clojure.string :as str]))", g_test_eval_state);

    // Test: str/blank? should resolve to clojure.string/blank?
    CljObject *result = eval_string("(str/blank? \"\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

// ============================================================================
// TESTS FOR ERROR CASES
// ============================================================================

// Test: Missing namespace should suggest require (but not for clojure.core)
TEST(test_missing_namespace_suggests_require) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    TRY {
        (void)eval_string("clojure.string/blank?", g_test_eval_state);
        TEST_FAIL_MESSAGE("Should have thrown exception for missing namespace (not required)");
    } CATCH(ex) {
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(ex->message, "(require 'clojure.string) missing?"),
                                     "Expected require hint for missing namespace");
        TEST_ASSERT_NULL_MESSAGE(strchr(ex->message, '\n'),
                                 "Exception message must not contain newlines");
    } END_TRY
}

// Test: Missing namespace via (doc <qualified-sym>) should suggest require and keep the qualified name.
TEST(test_missing_namespace_doc_suggests_require) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Ensure clojure.repl namespace is available. We call doc as a qualified var
    // because (require ...) does not :refer symbols into the current namespace.
    (void)eval_string("(require 'clojure.repl)", g_test_eval_state);

    TRY {
        // Use a namespace that is not loaded by clojure.repl itself.
        (void)eval_string("(clojure.repl/doc tiny-clj.net.mdns/close!)", g_test_eval_state);
        TEST_FAIL_MESSAGE("Should have thrown exception for missing namespace in doc");
    } CATCH(ex) {
        TEST_ASSERT_NOT_NULL(ex);
        const char *msg = ex->message;
        bool has_qualified = (strstr(msg, "tiny-clj.net.mdns/close!") != NULL);
        bool has_hint = (strstr(msg, "(require 'tiny-clj.net.mdns) missing?") != NULL);
        if (!has_qualified || !has_hint) {
            test_fprintf(stderr, "missing_namespace_doc_suggests_require: got message: %s\n", msg);
        }
        TEST_ASSERT_TRUE_MESSAGE(has_qualified, "Expected qualified symbol in error message");
        TEST_ASSERT_TRUE_MESSAGE(has_hint, "Expected require hint for missing namespace");
        TEST_ASSERT_NULL_MESSAGE(strchr(ex->message, '\n'),
                                 "Exception message must not contain newlines");
    } END_TRY
}

// Test: Non-existent qualified symbol should throw exception
TEST(test_nonexistent_qualified_symbol_throws) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Test: clojure.core/nonexistent should throw exception
    TRY {
        (void)eval_string("clojure.core/nonexistent", g_test_eval_state);
        TEST_FAIL_MESSAGE("Should have thrown exception for nonexistent symbol");
    } CATCH(ex) {
        // Expected exception
        TEST_ASSERT_NOT_NULL(ex);
        // Never suggest requiring clojure.core (it's always loaded in setUp).
        TEST_ASSERT_NULL_MESSAGE(strstr(ex->message, "(require 'clojure.core)"),
                                 "Must not suggest requiring clojure.core");
    } END_TRY
}

// Test: Parser resolves alias for symbols
TEST(test_parser_resolves_symbol_alias) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Setup: Create namespace with alias
    load_clojure_string_namespace();
    eval_string("(ns test-parser-alias (:require [clojure.string :as str]))", g_test_eval_state);
    
    // Test: Parse str/blank? - should resolve alias in parser
    ID parsed = parse("str/blank?", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(parsed);
    parsed = canonicalize_ast(parsed, g_test_eval_state);
    TEST_ASSERT_TRUE(TAG(parsed) == CLJ_SYMBOL);
    
    CljSymbol *sym = as_symbol(parsed);
    TEST_ASSERT_NOT_NULL(sym);
    TEST_ASSERT_NOT_NULL(sym->ns_name);
    
    // Verify: ns_name should be clojure.string (resolved), not str (alias)
    TEST_ASSERT_EQUAL_STRING("clojure.string", sym->ns_name->cname);
    TEST_ASSERT_EQUAL_STRING("blank?", sym->cname);
}

// Test: Common alias resolution for keywords and symbols
TEST(test_common_alias_resolution_for_keywords_and_symbols) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Setup: Create namespace with alias
    load_clojure_string_namespace();
    eval_string("(ns test-common (:require [clojure.string :as str]))", g_test_eval_state);
    
    // Test: Both keyword and symbol should resolve to same namespace
    ID kw_parsed = parse(":str/trim", g_test_eval_state);
    ID sym_parsed = parse("str/blank?", g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(kw_parsed);
    TEST_ASSERT_NOT_NULL(sym_parsed);
    
    kw_parsed = canonicalize_ast(kw_parsed, g_test_eval_state);
    sym_parsed = canonicalize_ast(sym_parsed, g_test_eval_state);
    CljSymbol *kw = as_symbol(kw_parsed);
    CljSymbol *sym = as_symbol(sym_parsed);
    
    // Both should resolve to clojure.string namespace
    TEST_ASSERT_EQUAL_STRING("clojure.string", kw->ns_name->cname);
    TEST_ASSERT_EQUAL_STRING("clojure.string", sym->ns_name->cname);
}

// Test: No runtime fallback for alias (alias not available at parse time)
TEST(test_no_runtime_fallback_for_alias) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Setup: Parse symbol BEFORE alias is set (simulating require outside ns)
    ID parsed = parse("str/blank?", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(parsed);
    
    parsed = canonicalize_ast(parsed, g_test_eval_state);
    CljSymbol *sym = as_symbol(parsed);
    TEST_ASSERT_NOT_NULL(sym);
    
    // At parse time, alias doesn't exist - should use original ns_str
    // After removing runtime fallback, this should remain as "str" (not resolved)
    TEST_ASSERT_NOT_NULL(sym->ns_name);
    TEST_ASSERT_EQUAL_STRING("str", sym->ns_name->cname);
    
    // Now set alias
    load_clojure_string_namespace();
    eval_string("(require '[clojure.string :as str])", g_test_eval_state);
    
    // Evaluate the parsed symbol - should fail (no runtime fallback)
    TRY {
        (void)eval_symbol(sym, g_test_eval_state);
        // If we get here, runtime fallback still exists (should be removed)
        TEST_FAIL_MESSAGE("Runtime fallback should be removed - eval should fail");
    } CATCH(ex) {
        // Expected: No runtime fallback, so unresolved symbol
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY
}

// Test: Non-existent namespace should throw exception
TEST(test_nonexistent_namespace_throws) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Test: nonexistent.ns/symbol should throw exception
    TRY {
        (void)eval_string("nonexistent.ns/symbol", g_test_eval_state);
        TEST_FAIL_MESSAGE("Should have thrown exception for nonexistent namespace");
    } CATCH(ex) {
        // Expected exception
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY
}

// Test: Qualified native namespaces must not auto-require during symbol resolution.
TEST(test_qualified_native_symbol_requires_explicit_require) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    (void)eval_string("(ns-unload 'tiny-fx.sound-native)", g_test_eval_state);

    bool threw_unresolved = false;
    TRY {
        (void)eval_string("tiny-fx.sound-native/sound-play-sfx!", g_test_eval_state);
    } CATCH(ex) {
        threw_unresolved = true;
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY
    TEST_ASSERT_TRUE_MESSAGE(threw_unresolved,
                             "qualified symbol lookup must not auto-require tiny-fx.sound-native");

    ID loaded = eval_string("(do (require 'tiny-fx.sound-native) (fn? tiny-fx.sound-native/sound-play-sfx!))",
                            g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, loaded);
}
