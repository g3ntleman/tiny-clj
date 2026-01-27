/*
 * Low-Level Tests for Alias Resolution
 *
 * Direct tests of alias setting and resolution functions
 */

#include "tests_common.h"
#include "symbol.h"
#include "namespace.h"
#include "builtins.h"
#include "vector.h"

// Forward declaration - we need to access process_require_spec
// Since it's static, we'll test through native_require instead
extern ID native_require(ID *args, unsigned int argc);
extern void builtin_set_eval_state(EvalState *st);

// Helper functions
static void load_clojure_string_namespace(void) {
    ID req_result = eval_string("(require 'clojure.string)", g_test_eval_state);
    (void)req_result;
}

// ============================================================================
// LOW-LEVEL TESTS FOR ALIAS HYPOTHESES
// ============================================================================

// Hypothesis 1: alias_sym is correctly extracted from vector
TEST(test_lowlevel_alias_sym_extraction) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create a vector: [clojure.string :as str]
    CljSymbol *ns_sym = AUTORELEASE(intern_symbol_global("clojure.string"));
    CljSymbol *as_kw = AUTORELEASE(intern_symbol_global(":as"));
    CljSymbol *str_alias = AUTORELEASE(intern_symbol_global("str"));
    
    TEST_ASSERT_NOT_NULL(ns_sym);
    TEST_ASSERT_NOT_NULL(as_kw);
    TEST_ASSERT_NOT_NULL(str_alias);
    
    // Create vector
    CljVector *vec = AUTORELEASE(make_vector(3, CLJ_VECTOR_PERSISTENT));
    vec = AUTORELEASE(vector_conj(vec, ns_sym));
    vec = AUTORELEASE(vector_conj(vec, as_kw));
    vec = AUTORELEASE(vector_conj(vec, str_alias));
    
    TEST_ASSERT_NOT_NULL(vec);
    TEST_ASSERT_EQUAL_INT(3, vector_count(vec));
    
    // Verify elements
    CljObject *elem0 = (CljObject*)vector_nth(vec, 0);
    CljObject *elem1 = (CljObject*)vector_nth(vec, 1);
    CljObject *elem2 = (CljObject*)vector_nth(vec, 2);
    
    TEST_ASSERT_NOT_NULL(elem0);
    TEST_ASSERT_NOT_NULL(elem1);
    TEST_ASSERT_NOT_NULL(elem2);
    TEST_ASSERT_TRUE(TAG(elem0) == CLJ_SYMBOL);
    TEST_ASSERT_TRUE(TAG(elem1) == CLJ_SYMBOL);
    TEST_ASSERT_TRUE(TAG(elem2) == CLJ_SYMBOL);
    
    CljSymbol *ns = as_symbol(elem0);
    CljSymbol *as = as_symbol(elem1);
    CljSymbol *alias = as_symbol(elem2);
    
    TEST_ASSERT_EQUAL_STRING("clojure.string", ns->cname);
    TEST_ASSERT_EQUAL_STRING(":as", as->cname);
    TEST_ASSERT_EQUAL_STRING("str", alias->cname);
}

// Hypothesis 2: st->current_ns is correct when native_require is called
TEST(test_lowlevel_current_ns_correct) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Remember original namespace
    CljNamespace *original_ns = g_test_eval_state->current_ns;
    TEST_ASSERT_NOT_NULL(original_ns);
    
    // Create a new namespace
    evalstate_set_ns(g_test_eval_state, "test-lowlevel-ns");
    
    // Verify namespace changed
    TEST_ASSERT_NOT_NULL(g_test_eval_state->current_ns);
    TEST_ASSERT_NOT_NULL(g_test_eval_state->current_ns->name);
    TEST_ASSERT_EQUAL_STRING("test-lowlevel-ns", g_test_eval_state->current_ns->name->cname);
    
    // Verify it's different from original
    TEST_ASSERT_TRUE(g_test_eval_state->current_ns != original_ns);
}

// Hypothesis 3: native_require sets alias when namespace is already loaded
TEST(test_lowlevel_native_require_sets_alias_preloaded) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Setup: Load clojure.string first
    load_clojure_string_namespace();
    
    // Create a new namespace
    evalstate_set_ns(g_test_eval_state, "test-lowlevel-preloaded");
    
    // Create vector: [clojure.string :as str]
    CljSymbol *ns_sym = AUTORELEASE(intern_symbol_global("clojure.string"));
    CljSymbol *as_kw = AUTORELEASE(intern_symbol_global(":as"));
    CljSymbol *str_alias = AUTORELEASE(intern_symbol_global("str"));
    
    CljVector *vec = AUTORELEASE(make_vector(3, CLJ_VECTOR_PERSISTENT));
    vec = AUTORELEASE(vector_conj(vec, ns_sym));
    vec = AUTORELEASE(vector_conj(vec, as_kw));
    vec = AUTORELEASE(vector_conj(vec, str_alias));
    
    // Call native_require
    builtin_set_eval_state(g_test_eval_state);
    ID args[1] = { vec };
    ID result = native_require(args, 1);
    builtin_set_eval_state(NULL);
    
    (void)result; // require returns nil
    
    // Verify: Alias should be set in current namespace
    CljObject *ns_name = ns_get_alias(g_test_eval_state->current_ns, (CljObject *)str_alias);
    TEST_ASSERT_NOT_NULL_MESSAGE(ns_name, "Alias 'str' should be set after native_require");
    
    if (ns_name && TAG(ns_name) == CLJ_SYMBOL) {
        CljSymbol *ns_sym_resolved = as_symbol(ns_name);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("clojure.string", ns_sym_resolved->cname,
            "Alias should resolve to clojure.string");
    }
}

// Hypothesis 4: ns_set_alias actually stores the alias
TEST(test_lowlevel_ns_set_alias_stores) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create a test namespace
    CljNamespace *test_ns = AUTORELEASE(ns_get_or_create("test-set-alias", NULL));
    TEST_ASSERT_NOT_NULL(test_ns);
    
    // Create alias symbol and namespace name symbol
    CljSymbol *alias_sym = AUTORELEASE(intern_symbol_global("test-alias"));
    CljSymbol *ns_name_sym = AUTORELEASE(intern_symbol_global("test.target"));
    
    TEST_ASSERT_NOT_NULL(alias_sym);
    TEST_ASSERT_NOT_NULL(ns_name_sym);
    
    // Set alias
    ns_set_alias(test_ns, alias_sym, ns_name_sym);
    
    // Verify: aliases map should exist
    TEST_ASSERT_NOT_NULL_MESSAGE(test_ns->aliases, "aliases map should be created");
    
    // Verify: alias should be retrievable
    CljObject *retrieved = ns_get_alias(test_ns, (CljObject *)alias_sym);
    TEST_ASSERT_NOT_NULL_MESSAGE(retrieved, "Alias should be retrievable after setting");
    
    if (retrieved && TAG(retrieved) == CLJ_SYMBOL) {
        CljSymbol *retrieved_sym = as_symbol(retrieved);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("test.target", retrieved_sym->cname,
            "Retrieved alias should match set value");
    }
}

// Hypothesis 5: alias_sym is not NULL when needs_loading is false
TEST(test_lowlevel_alias_sym_not_null_when_preloaded) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Setup: Load clojure.string first
    load_clojure_string_namespace();
    
    // Create a new namespace
    evalstate_set_ns(g_test_eval_state, "test-alias-not-null");
    
    // Create vector: [clojure.string :as str]
    CljSymbol *ns_sym = AUTORELEASE(intern_symbol_global("clojure.string"));
    CljSymbol *as_kw = AUTORELEASE(intern_symbol_global(":as"));
    CljSymbol *str_alias = AUTORELEASE(intern_symbol_global("str"));
    
    CljVector *vec = AUTORELEASE(make_vector(3, CLJ_VECTOR_PERSISTENT));
    vec = AUTORELEASE(vector_conj(vec, ns_sym));
    vec = AUTORELEASE(vector_conj(vec, as_kw));
    vec = AUTORELEASE(vector_conj(vec, str_alias));
    
    // Verify vector is correct before calling native_require
    TEST_ASSERT_NOT_NULL(vec);
    TEST_ASSERT_EQUAL_INT(3, vector_count(vec));
    
    CljObject *alias_elem = (CljObject*)vector_nth(vec, 2);
    TEST_ASSERT_NOT_NULL_MESSAGE(alias_elem, "Alias element should exist in vector");
    TEST_ASSERT_TRUE_MESSAGE(TAG(alias_elem) == CLJ_SYMBOL, "Alias element should be a symbol");
    
    CljSymbol *alias = as_symbol(alias_elem);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("str", alias->cname, "Alias should be 'str'");
    
    // Call native_require - this should extract alias_sym and set it
    builtin_set_eval_state(g_test_eval_state);
    ID args[1] = { vec };
    ID result = native_require(args, 1);
    builtin_set_eval_state(NULL);
    
    (void)result;
    
    // After native_require, alias should be set
    CljObject *ns_name = ns_get_alias(g_test_eval_state->current_ns, (CljObject *)str_alias);
    TEST_ASSERT_NOT_NULL_MESSAGE(ns_name, "Alias should be set after native_require with preloaded namespace");
}

// Hypothesis 6: st->current_ns is correct namespace when alias is set
TEST(test_lowlevel_current_ns_correct_when_alias_set) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Remember original namespace
    CljNamespace *original_ns = g_test_eval_state->current_ns;
    TEST_ASSERT_NOT_NULL(original_ns);
    
    // Create a new namespace
    evalstate_set_ns(g_test_eval_state, "test-current-ns-alias");
    CljNamespace *new_ns = g_test_eval_state->current_ns;
    TEST_ASSERT_NOT_NULL(new_ns);
    TEST_ASSERT_TRUE(new_ns != original_ns);
    
    // Load clojure.string
    load_clojure_string_namespace();
    
    // Create vector: [clojure.string :as str]
    CljSymbol *ns_sym = AUTORELEASE(intern_symbol_global("clojure.string"));
    CljSymbol *as_kw = AUTORELEASE(intern_symbol_global(":as"));
    CljSymbol *str_alias = AUTORELEASE(intern_symbol_global("str"));
    
    CljVector *vec = AUTORELEASE(make_vector(3, CLJ_VECTOR_PERSISTENT));
    vec = AUTORELEASE(vector_conj(vec, ns_sym));
    vec = AUTORELEASE(vector_conj(vec, as_kw));
    vec = AUTORELEASE(vector_conj(vec, str_alias));
    
    // Call native_require
    builtin_set_eval_state(g_test_eval_state);
    ID args[1] = { vec };
    ID result = native_require(args, 1);
    builtin_set_eval_state(NULL);
    
    (void)result;
    
    // Verify: current_ns should still be the new namespace
    TEST_ASSERT_EQUAL_MESSAGE(new_ns, g_test_eval_state->current_ns,
        "current_ns should remain the same after native_require");
    
    // Verify: Alias should be in new namespace, not original
    CljObject *ns_name_new = ns_get_alias(new_ns, (CljObject *)str_alias);
    CljObject *ns_name_orig = ns_get_alias(original_ns, (CljObject *)str_alias);
    
    TEST_ASSERT_NOT_NULL_MESSAGE(ns_name_new, "Alias should be in new namespace");
    TEST_ASSERT_NULL_MESSAGE(ns_name_orig, "Alias should NOT be in original namespace");
}

