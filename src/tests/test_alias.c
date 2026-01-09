/*
 * Unity Tests for namespace alias resolution in Tiny-CLJ
 *
 * Tests for alias resolution in parser and runtime behavior
 */

#include "tests_common.h"
#include "symbol.h"
#include "namespace.h"
#include "function_call.h"
#include "value.h"
#include <sys/stat.h>
#include <errno.h>

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

static void load_clojure_string_namespace(void) {
    CljObject *req_result = eval_string("(require 'clojure.string)", g_test_eval_state);
    (void)req_result; // require returns nil
}

// Helper functions for file operations
static int ensure_dir(const char *path) {
    // Create directory if it does not exist (0777 perms)
    // Ignore EEXIST
    if (mkdir(path, 0777) == 0) return 0;
    if (errno == EEXIST) return 0;
    return -1;
}

static int write_file(const char *path, const char *content) {
    FILE *fp = fopen(path, "w");
    if (!fp) return -1;
    if (content && *content) fputs(content, fp);
    fclose(fp);
    return 0;
}

// ============================================================================
// TESTS FOR ALIAS RESOLUTION
// ============================================================================

// Note: test_require_with_alias and test_require_alias_resolution are defined in test_namespace.c
// to avoid duplicate symbols

// ============================================================================
// SYSTEMATIC TESTS FOR ALIAS RESOLUTION HYPOTHESES
// ============================================================================

// Hypothesis 1: Alias is set correctly when using (ns ... (:require ...))
TEST(test_hypothesis_ns_require_sets_alias) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Setup: Create namespace with alias using (ns ... (:require ...))
    load_clojure_string_namespace();
    eval_string("(ns test-hypothesis-ns (:require [clojure.string :as str]))", g_test_eval_state);
    
    // Verify: Alias should be set in the new namespace
    CljSymbol *str_alias = intern_symbol_global("str");
    TEST_ASSERT_NOT_NULL(str_alias);
    
    CljObject *ns_name = ns_get_alias(g_test_eval_state->current_ns, (CljObject *)str_alias);
    TEST_ASSERT_NOT_NULL_MESSAGE(ns_name, "Alias 'str' should be set when using (ns ... (:require ...))");
    
    if (ns_name && TAG(ns_name) == CLJ_SYMBOL) {
        CljSymbol *ns_sym = as_symbol(ns_name);
        TEST_ASSERT_NOT_NULL(ns_sym);
        TEST_ASSERT_NOT_NULL(ns_sym->cname);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("clojure.string", ns_sym->cname, 
            "Alias should resolve to clojure.string");
    }
    
    // Verify: Current namespace should be test-hypothesis-ns
    TEST_ASSERT_NOT_NULL(g_test_eval_state->current_ns);
    TEST_ASSERT_NOT_NULL(g_test_eval_state->current_ns->name);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("test-hypothesis-ns", 
        g_test_eval_state->current_ns->name->cname,
        "Current namespace should be test-hypothesis-ns");
}

// Hypothesis 2: Alias is set correctly when using (require ...) directly
TEST(test_hypothesis_require_direct_sets_alias) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Setup: Use (require ...) directly (not in ns form)
    load_clojure_string_namespace();
    CljObject *req_result = eval_string("(require '[clojure.string :as str])", g_test_eval_state);
    (void)req_result;
    
    // Verify: Alias should be set in current namespace
    CljSymbol *str_alias = intern_symbol_global("str");
    TEST_ASSERT_NOT_NULL(str_alias);
    
    CljObject *ns_name = ns_get_alias(g_test_eval_state->current_ns, (CljObject *)str_alias);
    TEST_ASSERT_NOT_NULL_MESSAGE(ns_name, "Alias 'str' should be set when using (require ...) directly");
    
    if (ns_name && TAG(ns_name) == CLJ_SYMBOL) {
        CljSymbol *ns_sym = as_symbol(ns_name);
        TEST_ASSERT_NOT_NULL(ns_sym);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("clojure.string", ns_sym->cname,
            "Alias should resolve to clojure.string");
    }
}

// Hypothesis 3: Parser resolves alias for :alias/keyword
TEST(test_hypothesis_parser_resolves_keyword_alias) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Setup: Create namespace with alias
    load_clojure_string_namespace();
    eval_string("(ns test-parser-keyword (:require [clojure.string :as str]))", g_test_eval_state);
    
    // Test: Parse :str/trim - should resolve alias in parser
    CljObject *parsed = parse(":str/trim", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(parsed);
    TEST_ASSERT_TRUE(TAG(parsed) == CLJ_SYMBOL);
    TEST_ASSERT_TRUE(IS_KEYWORD(parsed));
    
    CljSymbol *kw = as_symbol(parsed);
    TEST_ASSERT_NOT_NULL(kw);
    TEST_ASSERT_NOT_NULL(kw->ns_name);
    
    // Verify: ns_name should be clojure.string (resolved), not str (alias)
    TEST_ASSERT_EQUAL_STRING_MESSAGE("clojure.string", kw->ns_name->cname,
        "Parser should resolve alias for :alias/keyword");
    TEST_ASSERT_EQUAL_STRING(":trim", kw->cname);
}

// Hypothesis 4: Parser resolves alias for ::alias/keyword (auto-qualified)
TEST(test_hypothesis_parser_resolves_auto_qualified_keyword_alias) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Setup: Create namespace with alias
    load_clojure_string_namespace();
    eval_string("(ns test-auto-qualified (:require [clojure.string :as str]))", g_test_eval_state);
    
    // Test: Parse ::str/trim - should resolve alias in parser
    CljObject *parsed = parse("::str/trim", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(parsed);
    TEST_ASSERT_TRUE(TAG(parsed) == CLJ_SYMBOL);
    TEST_ASSERT_TRUE(IS_KEYWORD(parsed));
    
    CljSymbol *kw = as_symbol(parsed);
    TEST_ASSERT_NOT_NULL(kw);
    TEST_ASSERT_NOT_NULL(kw->ns_name);
    
    // Verify: ns_name should be clojure.string (resolved), not str (alias)
    TEST_ASSERT_EQUAL_STRING_MESSAGE("clojure.string", kw->ns_name->cname,
        "Parser should resolve alias for ::alias/keyword");
    TEST_ASSERT_EQUAL_STRING(":trim", kw->cname);
}

// Hypothesis 5: Parser resolves alias for alias/symbol
TEST(test_hypothesis_parser_resolves_symbol_alias) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Setup: Create namespace with alias
    load_clojure_string_namespace();
    eval_string("(ns test-parser-symbol (:require [clojure.string :as str]))", g_test_eval_state);
    
    // Test: Parse str/blank? - should resolve alias in parser
    CljObject *parsed = parse("str/blank?", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(parsed);
    TEST_ASSERT_TRUE(TAG(parsed) == CLJ_SYMBOL);
    
    CljSymbol *sym = as_symbol(parsed);
    TEST_ASSERT_NOT_NULL(sym);
    TEST_ASSERT_NOT_NULL(sym->ns_name);
    
    // Verify: ns_name should be clojure.string (resolved), not str (alias)
    TEST_ASSERT_EQUAL_STRING_MESSAGE("clojure.string", sym->ns_name->cname,
        "Parser should resolve alias for alias/symbol");
    TEST_ASSERT_EQUAL_STRING("blank?", sym->cname);
}

// Hypothesis 6: resolve_alias_in_namespace works correctly
TEST(test_hypothesis_resolve_alias_in_namespace_function) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Setup: Create namespace with alias
    load_clojure_string_namespace();
    eval_string("(ns test-resolve-fn (:require [clojure.string :as str]))", g_test_eval_state);
    
    // Verify: Alias map exists
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_eval_state->current_ns->aliases,
        "Aliases map should exist after require");
    
    // Verify: Alias can be retrieved
    CljSymbol *str_alias = intern_symbol_global("str");
    TEST_ASSERT_NOT_NULL(str_alias);
    
    CljObject *ns_name = ns_get_alias(g_test_eval_state->current_ns, (CljObject *)str_alias);
    TEST_ASSERT_NOT_NULL_MESSAGE(ns_name, "ns_get_alias should return namespace name");
    
    if (ns_name && TAG(ns_name) == CLJ_SYMBOL) {
        CljSymbol *ns_sym = as_symbol(ns_name);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("clojure.string", ns_sym->cname,
            "ns_get_alias should return correct namespace name");
    }
}

// Hypothesis 7: st->current_ns is correct when alias is set
TEST(test_hypothesis_current_ns_correct_when_alias_set) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Remember original namespace
    CljNamespace *original_ns = g_test_eval_state->current_ns;
    TEST_ASSERT_NOT_NULL(original_ns);
    
    // Setup: Create new namespace with alias
    load_clojure_string_namespace();
    eval_string("(ns test-current-ns (:require [clojure.string :as str]))", g_test_eval_state);
    
    // Verify: Current namespace changed
    TEST_ASSERT_NOT_NULL(g_test_eval_state->current_ns);
    TEST_ASSERT_NOT_NULL(g_test_eval_state->current_ns->name);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("test-current-ns",
        g_test_eval_state->current_ns->name->cname,
        "Current namespace should be test-current-ns");
    
    // Verify: Alias is in the new namespace, not the original
    CljSymbol *str_alias = intern_symbol_global("str");
    CljObject *ns_name_new = ns_get_alias(g_test_eval_state->current_ns, (CljObject *)str_alias);
    TEST_ASSERT_NOT_NULL_MESSAGE(ns_name_new,
        "Alias should be in new namespace");
    
    // Verify: Alias is NOT in original namespace
    CljObject *ns_name_orig = ns_get_alias(original_ns, (CljObject *)str_alias);
    TEST_ASSERT_NULL_MESSAGE(ns_name_orig,
        "Alias should NOT be in original namespace");
}

// Hypothesis 8: Alias resolution works when namespace is already loaded
TEST(test_hypothesis_alias_resolution_when_namespace_already_loaded) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Setup: Load namespace first
    load_clojure_string_namespace();
    
    // Then create namespace with alias (namespace already exists)
    eval_string("(ns test-already-loaded (:require [clojure.string :as str]))", g_test_eval_state);
    
    // Verify: Alias should still be set correctly
    CljSymbol *str_alias = intern_symbol_global("str");
    CljObject *ns_name = ns_get_alias(g_test_eval_state->current_ns, (CljObject *)str_alias);
    TEST_ASSERT_NOT_NULL_MESSAGE(ns_name,
        "Alias should be set even when namespace is already loaded");
    
    if (ns_name && TAG(ns_name) == CLJ_SYMBOL) {
        CljSymbol *ns_sym = as_symbol(ns_name);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("clojure.string", ns_sym->cname,
            "Alias should resolve correctly when namespace already loaded");
    }
}

// Hypothesis 9: Parser does NOT resolve alias if alias doesn't exist
TEST(test_hypothesis_parser_no_resolution_when_alias_missing) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Setup: Create namespace WITHOUT alias
    eval_string("(ns test-no-alias)", g_test_eval_state);
    
    // Test: Parse :nonexistent/keyword - should NOT resolve (no alias)
    CljObject *parsed = parse(":nonexistent/keyword", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(parsed);
    TEST_ASSERT_TRUE(TAG(parsed) == CLJ_SYMBOL);
    
    CljSymbol *kw = as_symbol(parsed);
    TEST_ASSERT_NOT_NULL(kw);
    TEST_ASSERT_NOT_NULL(kw->ns_name);
    
    // Verify: ns_name should be nonexistent (not resolved, because no alias exists)
    TEST_ASSERT_EQUAL_STRING_MESSAGE("nonexistent", kw->ns_name->cname,
        "Parser should NOT resolve when alias doesn't exist");
}

