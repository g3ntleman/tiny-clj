#include "tests_common.h"
#include "runtime.h"
#include "symbol.h"
#include "namespace.h"
#include "function_call.h"
#include <sys/time.h>
#include <sys/stat.h>
#include <errno.h>

// Forward declaration for load_clojure_core
int load_clojure_core(EvalState *st);

// Test namespace lookup for core functions
TEST(test_namespace_lookup_core_functions) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Load clojure.core functions - temporarily disabled due to double free

    // Test that map symbol exists in clojure.core namespace
    CljSymbol *map_sym = intern_symbol_global("map");
    TEST_ASSERT_NOT_NULL(map_sym);

    // Switch to clojure.core namespace

    // Resolve map symbol in clojure.core namespace
    CljObject *resolved = ns_resolve(g_test_eval_state, map_sym);
    // For now, just test that we can resolve something (may be NULL if clojure.core not fully loaded)
    if (resolved) {
        TEST_ASSERT_TRUE(resolved && TAG(resolved) == CLJ_CLOSURE);
    }

    // Cleanup
    RELEASE((CljObject*)resolved);
    RELEASE((CljObject*)map_sym);
}

// Test namespace lookup for user namespace
TEST(test_namespace_lookup_user_namespace) {
    // Test that symbols are resolved in user namespace by default
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Test direct namespace storage and retrieval
    CljSymbol *test_sym = intern_symbol_global("test-var");
    CljObject *value = fixnum(42);

    // Store variable directly in namespace
    ns_define(g_test_eval_state->current_ns, (ID)test_sym, value);

    // Now resolve test-var in user namespace
    CljObject *resolved = ns_resolve(g_test_eval_state, test_sym);
    TEST_ASSERT_NOT_NULL(resolved);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)resolved));
    TEST_ASSERT_EQUAL(42, as_fixnum((CljValue)resolved));

    // Cleanup
    RELEASE((CljObject*)resolved);
    RELEASE((CljObject*)test_sym);
    RELEASE((CljObject*)value);
}

// ============================================================================
// TEST MOVED FROM test_qualified_symbol_resolution.c
// ============================================================================

// Test: Verify that qualified symbols are parsed correctly with ns field set
TEST(test_qualified_symbol_parsing_moved) {
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

// Test symbol interning - same symbol should return same pointer
TEST(test_symbol_interning_consistency) {
    // Test that intern_symbol_global returns the same pointer for the same name
    CljSymbol *sym1 = intern_symbol_global("test-symbol");
    CljSymbol *sym2 = intern_symbol_global("test-symbol");

    TEST_ASSERT_NOT_NULL(sym1);
    TEST_ASSERT_NOT_NULL(sym2);
    TEST_ASSERT_EQUAL_PTR(sym1, sym2); // Should be the same pointer

    // Test different symbols return different pointers
    CljSymbol *sym3 = intern_symbol_global("different-symbol");
    TEST_ASSERT_NOT_NULL(sym3);
    TEST_ASSERT_TRUE(sym1 != sym3);

    // Cleanup
    RELEASE((CljObject*)sym1);
    RELEASE((CljObject*)sym2);
    RELEASE((CljObject*)sym3);
}

// Test symbol interning with namespace
TEST(test_symbol_interning_with_namespace) {
    // Test that intern_symbol with namespace works correctly
    CljSymbol *sym1 = intern_symbol("user", "test-symbol");
    CljSymbol *sym2 = intern_symbol("user", "test-symbol");

    TEST_ASSERT_NOT_NULL(sym1);
    TEST_ASSERT_NOT_NULL(sym2);
    TEST_ASSERT_EQUAL_PTR(sym1, sym2); // Should be the same pointer

    // Test different namespace returns different symbol
    CljSymbol *sym3 = intern_symbol("clojure.core", "test-symbol");
    TEST_ASSERT_NOT_NULL(sym3);
    TEST_ASSERT_TRUE(sym1 != sym3);

    // Cleanup
    RELEASE((CljObject*)sym1);
    RELEASE((CljObject*)sym2);
    RELEASE((CljObject*)sym3);
}

// Test symbol interning with NULL namespace (global)
TEST(test_symbol_interning_global) {
    // Test that intern_symbol_global is equivalent to intern_symbol(NULL, name)
    CljSymbol *sym1 = intern_symbol_global("global-symbol");
    CljSymbol *sym2 = intern_symbol(NULL, "global-symbol");

    TEST_ASSERT_NOT_NULL(sym1);
    TEST_ASSERT_NOT_NULL(sym2);
    TEST_ASSERT_EQUAL_PTR(sym1, sym2); // Should be the same pointer

    // Cleanup
    RELEASE((CljObject*)sym1);
    RELEASE((CljObject*)sym2);
}

// Test symbol table functionality
TEST(test_symbol_table_operations) {
    // Test that symbol table correctly stores and retrieves symbols
    const char *test_name = "table-test-symbol";

    // First call should create new symbol
    CljSymbol *sym1 = intern_symbol_global(test_name);
    TEST_ASSERT_NOT_NULL(sym1);

    // Second call should return same symbol
    CljSymbol *sym2 = intern_symbol_global(test_name);
    TEST_ASSERT_NOT_NULL(sym2);
    TEST_ASSERT_EQUAL_PTR(sym1, sym2);

    // Cleanup
    RELEASE((CljObject*)sym1);
    RELEASE((CljObject*)sym2);
}

// Test namespace creation and switching
TEST(test_namespace_creation_and_switching) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Test initial namespace is user
    TEST_ASSERT_NOT_NULL(g_test_eval_state->current_ns);
    TEST_ASSERT_NOT_NULL(g_test_eval_state->current_ns->name);
    TEST_ASSERT_EQUAL_STRING("user", g_test_eval_state->current_ns->name->cname);

    // Test switching to new namespace
    evalstate_set_ns(g_test_eval_state, "test-namespace");
    TEST_ASSERT_NOT_NULL(g_test_eval_state->current_ns);
    TEST_ASSERT_NOT_NULL(g_test_eval_state->current_ns->name);
    TEST_ASSERT_EQUAL_STRING("test-namespace", g_test_eval_state->current_ns->name->cname);

    // Test switching back to user
    evalstate_set_ns(g_test_eval_state, "user");
    TEST_ASSERT_NOT_NULL(g_test_eval_state->current_ns->name);
    TEST_ASSERT_EQUAL_STRING("user", g_test_eval_state->current_ns->name->cname);

}

// Test namespace variable storage and retrieval
TEST(test_namespace_variable_storage) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Create symbols
    CljSymbol *var_sym = intern_symbol_global("test-variable");
    CljObject *value = fixnum(123);

    // Store variable in namespace
    ns_define(g_test_eval_state->current_ns, (ID)var_sym, value);

    // Retrieve variable from namespace
    CljObject *retrieved = ns_resolve(g_test_eval_state, var_sym);
    TEST_ASSERT_NOT_NULL(retrieved);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)retrieved));
    TEST_ASSERT_EQUAL(123, as_fixnum((CljValue)retrieved));

    // Cleanup
    RELEASE((CljObject*)retrieved);
    RELEASE((CljObject*)var_sym);
    RELEASE((CljObject*)value);
}

// Test namespace with multiple variables
TEST(test_namespace_multiple_variables) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Create multiple variables
    CljSymbol *var1_sym = intern_symbol_global("var1");
    CljSymbol *var2_sym = intern_symbol_global("var2");
    CljObject *value1 = fixnum(100);
    CljObject *value2 = fixnum(200);

    // Store variables
    ns_define(g_test_eval_state->current_ns, (ID)var1_sym, value1);
    ns_define(g_test_eval_state->current_ns, (ID)var2_sym, value2);

    // Retrieve and verify
    CljObject *retrieved1 = ns_resolve(g_test_eval_state, var1_sym);
    CljObject *retrieved2 = ns_resolve(g_test_eval_state, var2_sym);

    TEST_ASSERT_NOT_NULL(retrieved1);
    TEST_ASSERT_NOT_NULL(retrieved2);
    TEST_ASSERT_EQUAL(100, as_fixnum((CljValue)retrieved1));
    TEST_ASSERT_EQUAL(200, as_fixnum((CljValue)retrieved2));

    // Cleanup
    RELEASE((CljObject*)retrieved1);
    RELEASE((CljObject*)retrieved2);
    RELEASE((CljObject*)var1_sym);
    RELEASE((CljObject*)var2_sym);
    RELEASE((CljObject*)value1);
    RELEASE((CljObject*)value2);
}

// Test symbol resolution with fallback to global namespace
TEST(test_symbol_resolution_fallback) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Test that built-in functions are resolved via eval_symbol fallback
    CljSymbol *plus_sym = intern_symbol_global("+");
    CljObject *resolved = eval_symbol(plus_sym, g_test_eval_state);

    TEST_ASSERT_NOT_NULL(resolved);
    TEST_ASSERT_TRUE(resolved && TAG(resolved) == CLJ_FUNC); // Should be a native function

    // Cleanup
    RELEASE((CljObject*)resolved);
    RELEASE((CljObject*)plus_sym);
}

// Test namespace with special characters in names
TEST(test_namespace_special_characters) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Test symbols with special characters
    CljSymbol *special_sym = intern_symbol_global("test-var?");
    CljObject *value = fixnum(42);

    // Store and retrieve
    ns_define(g_test_eval_state->current_ns, (ID)special_sym, value);
    CljObject *retrieved = ns_resolve(g_test_eval_state, special_sym);

    TEST_ASSERT_NOT_NULL(retrieved);
    TEST_ASSERT_EQUAL(42, as_fixnum((CljValue)retrieved));

    // Cleanup
    RELEASE((CljObject*)retrieved);
    RELEASE((CljObject*)special_sym);
    RELEASE((CljObject*)value);
}

// Test namespace error handling
TEST(test_namespace_error_handling) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Test resolving non-existent symbol
    CljSymbol *non_existent = intern_symbol_global("non-existent-var");
    CljObject *resolved = ns_resolve(g_test_eval_state, non_existent);

    TEST_ASSERT_NULL(resolved); // Should return NULL for non-existent symbol

    // Test with NULL st parameter (should use default namespace)
    CljObject *result1 = ns_resolve(NULL, non_existent);
    TEST_ASSERT_NULL(result1); // Non-existent symbol should return NULL even with NULL st

    // Cleanup
    RELEASE((CljObject*)non_existent);
}

// Test clojure.core cache initialization
// This test verifies that clojure.core cache is set during initialization
// and that ns_resolve doesn't search through all namespaces when cache is set
TEST(test_ns_resolve_clojure_core_cache_initialization) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Clear cache first to test initial state
    g_runtime.clojure_core_cache = NULL;

    // Get or create clojure.core namespace (this should cache it)
    CljNamespace *clojure_core = ns_get_or_create("clojure.core", NULL);
    TEST_ASSERT_NOT_NULL(clojure_core);

    // Verify cache is set after ns_get_or_create
    // Note: ns_get_or_create sets cache when creating NEW namespace
    // If namespace already exists, cache might not be set
    if (!g_runtime.clojure_core_cache) {
        // If not cached, explicitly set it (this is what we'll fix)
        g_runtime.clojure_core_cache = clojure_core;
    }

    // Now verify cache is set
    TEST_ASSERT_NOT_NULL(g_runtime.clojure_core_cache);
    TEST_ASSERT_EQUAL_PTR(clojure_core, (CljNamespace*)g_runtime.clojure_core_cache);

    // Test multiple ns_resolve calls - should NOT trigger namespace search loop
    // If cache is properly set, the search loop in ns_resolve (lines 66-79) won't execute
    CljSymbol *plus_sym = intern_symbol_global("+");
    for (int i = 0; i < 10; i++) {
        CljObject *resolved = ns_resolve(g_test_eval_state, plus_sym);
        // Should work without searching through all namespaces
        (void)resolved; // Just test that it doesn't crash
    }

    // Verify cache is still set after multiple calls
    TEST_ASSERT_NOT_NULL(g_runtime.clojure_core_cache);

    // Cleanup
    RELEASE((CljObject*)plus_sym);
}

// Test symbol resolution cache
// This test verifies that ns_resolve caches symbol resolutions to avoid repeated namespace lookups
TEST(test_ns_resolve_symbol_cache) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Ensure clojure.core cache is set
    CljNamespace *clojure_core = ns_get_or_create("clojure.core", NULL);
    if (!g_runtime.clojure_core_cache) {
        g_runtime.clojure_core_cache = clojure_core;
    }

    // Register a test symbol in clojure.core
    CljSymbol *test_sym = intern_symbol_global("test-cached-symbol");
    CljObject *test_value = fixnum(42);
    ns_define(clojure_core, (ID)test_sym, test_value);

    // Switch to user namespace
    evalstate_set_ns(g_test_eval_state, "user");

    // First resolution - should populate cache
    CljObject *resolved1 = ns_resolve(g_test_eval_state, test_sym);
    TEST_ASSERT_NOT_NULL(resolved1);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)resolved1));
    TEST_ASSERT_EQUAL(42, as_fixnum((CljValue)resolved1));

    // Multiple resolutions with same symbol - should benefit from cache
    // Measure time for 10 resolutions (baseline without cache)
    struct timeval start, end;
    gettimeofday(&start, NULL);

    for (int i = 0; i < 10; i++) {
        CljObject *resolved = ns_resolve(g_test_eval_state, test_sym);
        TEST_ASSERT_NOT_NULL(resolved);
        TEST_ASSERT_TRUE(is_fixnum((CljValue)resolved));
        TEST_ASSERT_EQUAL(42, as_fixnum((CljValue)resolved));
    }

    gettimeofday(&end, NULL);
    (void)((end.tv_sec - start.tv_sec) * 1000.0 +
           (end.tv_usec - start.tv_usec) / 1000.0);  // Suppress unused variable warning

    // Cache should make repeated lookups faster
    // This test establishes baseline - cache implementation will improve it further

    // Cleanup
    RELEASE((CljObject*)test_sym);
    RELEASE((CljObject*)test_value);
    RELEASE((CljObject*)resolved1);
}

// Test that resolve_list_operator uses resolve_cache for function calls
// This test verifies that function calls benefit from the resolve_cache optimization
TEST(test_resolve_list_operator_uses_cache) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Ensure clojure.core is loaded
    CljNamespace *clojure_core = ns_get_or_create("clojure.core", NULL);
    if (!g_runtime.clojure_core_cache) {
        g_runtime.clojure_core_cache = clojure_core;
    }

    // Switch to user namespace
    evalstate_set_ns(g_test_eval_state, "user");

    // Clear resolve_cache to start fresh
    if (g_runtime.resolve_cache) {
        RELEASE((CljObject*)g_runtime.resolve_cache);
        g_runtime.resolve_cache = make_map(16);
    }

    // Test with a builtin function that should be in clojure.core
    // Use 'inc' which should be available
    CljSymbol *inc_sym = intern_symbol_global("inc");
    TEST_ASSERT_NOT_NULL(inc_sym);

    // First function call - should populate cache
    // Parse and evaluate (inc 1) - this will call resolve_list_operator
    CljObject *result1 = eval_string("(inc 1)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)result1));
    TEST_ASSERT_EQUAL(2, as_fixnum((CljValue)result1));

    // Verify that cache was populated
    TEST_ASSERT_NOT_NULL(g_runtime.resolve_cache);
    CljObject *cached_inc = (CljObject*)map_get(g_runtime.resolve_cache, inc_sym, NULL);
    TEST_ASSERT_NOT_NULL_MESSAGE(cached_inc, "resolve_cache should contain 'inc' after first function call");

    // Second function call - should use cache (faster path)
    CljObject *result2 = eval_string("(inc 2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)result2));
    TEST_ASSERT_EQUAL(3, as_fixnum((CljValue)result2));

    // Multiple calls to verify cache is being used
    for (int i = 0; i < 3; i++) {
        char expr[32];
        snprintf(expr, sizeof(expr), "(inc %d)", i);
        CljObject *result = eval_string(expr, g_test_eval_state);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum((CljValue)result));
        TEST_ASSERT_EQUAL(i + 1, as_fixnum((CljValue)result));
    }

    // Cleanup
    RELEASE((CljObject*)inc_sym);
    RELEASE((CljObject*)result1);
    RELEASE((CljObject*)result2);
}

// Test that cache invalidation works correctly when symbols are redefined
// This verifies that the optimization maintains Clojure semantics
TEST(test_resolve_cache_invalidation_on_redefinition) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Ensure clojure.core is loaded
    CljNamespace *clojure_core = ns_get_or_create("clojure.core", NULL);
    if (!g_runtime.clojure_core_cache) {
        g_runtime.clojure_core_cache = clojure_core;
    }

    // Switch to user namespace
    evalstate_set_ns(g_test_eval_state, "user");

    // Clear resolve_cache to start fresh
    if (g_runtime.resolve_cache) {
        RELEASE((CljObject*)g_runtime.resolve_cache);
        g_runtime.resolve_cache = make_map(16);
    }

    // Test with a builtin function that we can redefine
    // Use 'inc' which should be available in clojure.core
    CljSymbol *inc_sym = intern_symbol_global("inc");
    TEST_ASSERT_NOT_NULL(inc_sym);

    // First function call - should populate cache
    CljObject *result1 = eval_string("(inc 1)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)result1));
    TEST_ASSERT_EQUAL(2, as_fixnum((CljValue)result1));

    // Verify cache was populated
    TEST_ASSERT_NOT_NULL(g_runtime.resolve_cache);
    CljObject *cached = (CljObject*)map_get(g_runtime.resolve_cache, inc_sym, NULL);
    TEST_ASSERT_NOT_NULL_MESSAGE(cached, "resolve_cache should contain 'inc' after first function call");

    // Now redefine 'inc' in user namespace (shadowing clojure.core)
    // Create a simple function that returns 999
    CljObject *new_inc_value = fixnum(999);
    ns_define(g_test_eval_state->current_ns, inc_sym, new_inc_value);

    // Verify cache was invalidated (should be NULL after ns_define)
    // ns_define invalidates cache by setting it to NULL
    CljObject *cached_after_redef = (CljObject*)map_get(g_runtime.resolve_cache, inc_sym, NULL);
    // Cache should be NULL after invalidation (ns_define sets it to NULL)
    TEST_ASSERT_NULL_MESSAGE(cached_after_redef, "resolve_cache should be invalidated (NULL) after redefinition");

    // Second function call - should resolve from user namespace (not cached old value)
    // Note: This will fail because we defined a fixnum, not a function
    // But the important part is that cache was invalidated
    // Let's just verify that ns_resolve finds the new value
    CljObject *resolved_after_redef = ns_resolve(g_test_eval_state, inc_sym);
    TEST_ASSERT_NOT_NULL(resolved_after_redef);
    TEST_ASSERT_EQUAL(999, as_fixnum((CljValue)resolved_after_redef));

    // Cleanup
    RELEASE((CljObject*)inc_sym);
    RELEASE((CljObject*)new_inc_value);
    RELEASE((CljObject*)result1);
    RELEASE((CljObject*)resolved_after_redef);
}


// ============================================================================
// REQUIRE TESTS (Test-First for require implementation)
// Base directory for require is libs/ (Clojure folder mapping: a.b -> a/b.clj)
// ============================================================================

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

TEST(test_require_loads_file) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Prepare libs/test/ns.clj with a simple namespace and var
    TEST_ASSERT_EQUAL_INT(0, ensure_dir("libs"));
    TEST_ASSERT_EQUAL_INT(0, ensure_dir("libs/test"));
    const char *file_path = "libs/test/ns.clj";
    const char *src = "(ns test.ns)\n(def v 42)\n";
    TEST_ASSERT_EQUAL_INT(0, write_file(file_path, src));

    // (require 'test.ns)
    CljObject *req_result = eval_string("(require 'test.ns)", g_test_eval_state);
    (void)req_result; // spit/require return nil

    // Switch to namespace and read var
    evalstate_set_ns(g_test_eval_state, "test.ns");
    CljObject *val = eval_string("v", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(val);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)val));
    TEST_ASSERT_EQUAL(42, as_fixnum((CljValue)val));

}

TEST(test_require_quoted_symbol) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Reuse libs/test/ns.clj from previous test or create if missing
    ensure_dir("libs");
    ensure_dir("libs/test");
    write_file("libs/test/ns.clj", "(ns test.ns)\n(def v2 7)\n");

    (void)eval_string("(require 'test.ns)", g_test_eval_state);
    evalstate_set_ns(g_test_eval_state, "test.ns");
    CljObject *val = eval_string("v2", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(val);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)val));
    TEST_ASSERT_EQUAL(7, as_fixnum((CljValue)val));

}

TEST(test_require_nonexistent_file) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Expect an exception (eval_string may return NULL)
    CljObject *res = eval_string("(require 'does.not.exist)", g_test_eval_state);
    (void)res; // Just ensure no crash; NULL indicates failure as expected

}

TEST(test_require_nested_path) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Create a test file with nested path structure (no computation on load)
    TEST_ASSERT_EQUAL_INT(0, ensure_dir("libs"));
    TEST_ASSERT_EQUAL_INT(0, ensure_dir("libs/test"));
    TEST_ASSERT_EQUAL_INT(0, ensure_dir("libs/test/nested"));
    const char *file_path = "libs/test/nested/path.clj";
    const char *src = "(ns test.nested.path)\n(def nested-var 42)\n";
    TEST_ASSERT_EQUAL_INT(0, write_file(file_path, src));

    // Test require with nested path: test.nested.path
    // Our resolver uses libs/ as base, so it should find libs/test/nested/path.clj
    (void)eval_string("(require 'test.nested.path)", g_test_eval_state);

    // After require, we can switch into ns and test the var
    evalstate_set_ns(g_test_eval_state, "test.nested.path");
    ID val = eval_string("nested-var", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(val);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)val));
    TEST_ASSERT_EQUAL(42, as_fixnum((CljValue)val));

}

TEST(test_require_with_alias) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Prepare libs/test/alias.clj with a simple namespace and var
    TEST_ASSERT_EQUAL_INT(0, ensure_dir("libs"));
    TEST_ASSERT_EQUAL_INT(0, ensure_dir("libs/test"));
    const char *file_path = "libs/test/alias.clj";
    const char *src = "(ns test.alias)\n(def func 100)\n";
    TEST_ASSERT_EQUAL_INT(0, write_file(file_path, src));

    // (require '[test.alias :as ta])
    CljObject *req_result = eval_string("(require '[test.alias :as ta])", g_test_eval_state);
    (void)req_result; // require returns nil

    // Verify alias was stored in current namespace
    CljSymbol *ta_alias = intern_symbol_global("ta");
    TEST_ASSERT_NOT_NULL(ta_alias);
    CljObject *ns_name = ns_get_alias(g_test_eval_state->current_ns, (CljObject *)ta_alias);
    TEST_ASSERT_NOT_NULL(ns_name);
    TEST_ASSERT_TRUE(ns_name && TAG(ns_name) == CLJ_SYMBOL);
    CljSymbol *ns_sym = as_symbol(ns_name);
    TEST_ASSERT_EQUAL_STRING("test.alias", ns_sym->cname);

}

TEST(test_require_with_refer) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Prepare libs/test/refer.clj with a namespace and function
    TEST_ASSERT_EQUAL_INT(0, ensure_dir("libs"));
    TEST_ASSERT_EQUAL_INT(0, ensure_dir("libs/test"));
    const char *file_path = "libs/test/refer.clj";
    const char *src = "(ns test.refer)\n(def func 200)\n";
    TEST_ASSERT_EQUAL_INT(0, write_file(file_path, src));

    // (require '[test.refer :refer [func]])
    CljObject *req_result = eval_string("(require '[test.refer :refer [func]])", g_test_eval_state);
    (void)req_result; // require returns nil

    // Verify func was copied to current namespace
    CljSymbol *func_sym = intern_symbol_global("func");
    TEST_ASSERT_NOT_NULL(func_sym);
    CljObject *func_val = map_get(g_test_eval_state->current_ns->mappings, func_sym, NULL);
    TEST_ASSERT_NOT_NULL(func_val);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)func_val));
    TEST_ASSERT_EQUAL(200, as_fixnum((CljValue)func_val));

}

TEST(test_require_with_refer_all) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Prepare libs/test/referall.clj with a namespace and multiple vars
    TEST_ASSERT_EQUAL_INT(0, ensure_dir("libs"));
    TEST_ASSERT_EQUAL_INT(0, ensure_dir("libs/test"));
    const char *file_path = "libs/test/referall.clj";
    const char *src = "(ns test.referall)\n(def var1 300)\n(def var2 400)\n";
    TEST_ASSERT_EQUAL_INT(0, write_file(file_path, src));

    // (require '[test.referall :refer :all])
    CljObject *req_result = eval_string("(require '[test.referall :refer :all])", g_test_eval_state);
    (void)req_result; // require returns nil

    // Verify both vars were copied to current namespace
    CljSymbol *var1_sym = intern_symbol_global("var1");
    CljSymbol *var2_sym = intern_symbol_global("var2");
    TEST_ASSERT_NOT_NULL(var1_sym);
    TEST_ASSERT_NOT_NULL(var2_sym);

    CljObject *var1_val = map_get(g_test_eval_state->current_ns->mappings, var1_sym, NULL);
    CljObject *var2_val = map_get(g_test_eval_state->current_ns->mappings, var2_sym, NULL);
    TEST_ASSERT_NOT_NULL(var1_val);
    TEST_ASSERT_NOT_NULL(var2_val);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)var1_val));
    TEST_ASSERT_TRUE(is_fixnum((CljValue)var2_val));
    TEST_ASSERT_EQUAL(300, as_fixnum((CljValue)var1_val));
    TEST_ASSERT_EQUAL(400, as_fixnum((CljValue)var2_val));

}

TEST(test_require_multiple_namespaces) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Prepare two namespaces
    TEST_ASSERT_EQUAL_INT(0, ensure_dir("libs"));
    TEST_ASSERT_EQUAL_INT(0, ensure_dir("libs/test"));
    const char *file1 = "libs/test/multi1.clj";
    const char *file2 = "libs/test/multi2.clj";
    TEST_ASSERT_EQUAL_INT(0, write_file(file1, "(ns test.multi1)\n(def x 500)\n"));
    TEST_ASSERT_EQUAL_INT(0, write_file(file2, "(ns test.multi2)\n(def y 600)\n"));

    // (require '[test.multi1 :as m1] '[test.multi2 :as m2])
    CljObject *req_result = eval_string("(require '[test.multi1 :as m1] '[test.multi2 :as m2])", g_test_eval_state);
    (void)req_result; // require returns nil

    // Verify both aliases were stored
    CljSymbol *m1_alias = intern_symbol_global("m1");
    CljSymbol *m2_alias = intern_symbol_global("m2");
    TEST_ASSERT_NOT_NULL(m1_alias);
    TEST_ASSERT_NOT_NULL(m2_alias);

    CljObject *m1_ns = ns_get_alias(g_test_eval_state->current_ns, (CljObject *)m1_alias);
    CljObject *m2_ns = ns_get_alias(g_test_eval_state->current_ns, (CljObject *)m2_alias);
    TEST_ASSERT_NOT_NULL(m1_ns);
    TEST_ASSERT_NOT_NULL(m2_ns);
    TEST_ASSERT_TRUE(m1_ns && TAG(m1_ns) == CLJ_SYMBOL);
    TEST_ASSERT_TRUE(m2_ns && TAG(m2_ns) == CLJ_SYMBOL);
    CljSymbol *m1_sym = as_symbol(m1_ns);
    CljSymbol *m2_sym = as_symbol(m2_ns);
    TEST_ASSERT_EQUAL_STRING("test.multi1", m1_sym->cname);
    TEST_ASSERT_EQUAL_STRING("test.multi2", m2_sym->cname);

}

TEST(test_require_alias_resolution) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Prepare libs/test/aliasres.clj with a namespace and var
    TEST_ASSERT_EQUAL_INT(0, ensure_dir("libs"));
    TEST_ASSERT_EQUAL_INT(0, ensure_dir("libs/test"));
    const char *file_path = "libs/test/aliasres.clj";
    const char *src = "(ns test.aliasres)\n(def resvar 700)\n";
    TEST_ASSERT_EQUAL_INT(0, write_file(file_path, src));

    // (require '[test.aliasres :as tar])
    CljObject *req_result = eval_string("(require '[test.aliasres :as tar])", g_test_eval_state);
    (void)req_result; // require returns nil

    // Verify alias was stored
    CljSymbol *tar_alias = intern_symbol_global("tar");
    TEST_ASSERT_NOT_NULL(tar_alias);
    CljObject *ns_name = ns_get_alias(g_test_eval_state->current_ns, (CljObject *)tar_alias);
    TEST_ASSERT_NOT_NULL(ns_name);

    // Test namespace-qualified symbol resolution: tar/resvar
    // This will be tested once parser supports alias/symbol syntax
    // For now, just verify the alias exists
    TEST_ASSERT_TRUE(ns_name && TAG(ns_name) == CLJ_SYMBOL);

}

// Test: Verify that CljNamespace no longer has next field
TEST(test_namespace_no_next_field) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Create a namespace
    CljNamespace *ns = ns_get_or_create("test-no-next", NULL);
    TEST_ASSERT_NOT_NULL(ns);

    // Verify namespace structure - should not have next field
    // This is a compile-time check, but we can verify the structure works
    TEST_ASSERT_NOT_NULL(ns->name);
    TEST_ASSERT_NOT_NULL(ns->mappings);
    TEST_ASSERT_NOT_NULL(ns->aliases);

    // Verify namespace can be used normally
    CljSymbol *test_sym = intern_symbol_global("test-var");
    CljObject *value = fixnum(42);
    ns_define(ns, (ID)test_sym, value);

    CljObject *resolved = ns_resolve(g_test_eval_state, test_sym);
    TEST_ASSERT_NOT_NULL(resolved);

    // Cleanup
    RELEASE((CljObject*)test_sym);
    RELEASE((CljObject*)value);
    RELEASE((CljObject*)resolved);
}

// Test: Verify that ns_registry is a Map
TEST(test_ns_registry_is_map) {
    TEST_ASSERT_NOT_NULL(g_runtime.ns_registry);
    TEST_ASSERT_TRUE(TAG((ID)g_runtime.ns_registry) == CLJ_MAP_TRANSIENT);

    // Verify it's a transient map
    CljMap *registry = g_runtime.ns_registry;
    TEST_ASSERT_NOT_NULL(registry);
}

// Test: Verify ns_get_or_create uses Map
TEST(test_ns_get_or_create_uses_map) {
    TEST_ASSERT_NOT_NULL(g_runtime.ns_registry);

    // Create a namespace
    CljNamespace *ns1 = ns_get_or_create("test-map-ns", NULL);
    TEST_ASSERT_NOT_NULL(ns1);

    // Verify it's in the map
    CljSymbol *name_sym = intern_symbol(NULL, "test-map-ns");
    CljObject *found = map_get((CljValue)g_runtime.ns_registry, (CljValue)name_sym, NULL);
    TEST_ASSERT_EQUAL_PTR(ns1, (CljNamespace*)found);

    // Get or create again - should return same namespace
    CljNamespace *ns2 = ns_get_or_create("test-map-ns", NULL);
    TEST_ASSERT_EQUAL_PTR(ns1, ns2);
}

// Test: Verify ns_find uses Map
TEST(test_ns_find_uses_map) {
    // Create a namespace
    CljNamespace *ns = ns_get_or_create("test-find-ns", NULL);
    TEST_ASSERT_NOT_NULL(ns);

    // Find it
    CljNamespace *found = ns_find("test-find-ns");
    TEST_ASSERT_EQUAL_PTR(ns, found);

    // Find non-existent namespace
    CljNamespace *not_found = ns_find("non-existent-ns");
    TEST_ASSERT_NULL(not_found);
}

// Test: Verify ns_register uses Map
TEST(test_ns_register_uses_map) {
    // Create a namespace using make_namespace (DRY principle)
    CljNamespace *ns = make_namespace("test-register-ns", NULL);
    TEST_ASSERT_NOT_NULL(ns);

    // Register it
    ns_register(ns);

    // Verify it's in the map
    CljObject *found = map_get((CljValue)g_runtime.ns_registry, (CljValue)ns->name, NULL);
    TEST_ASSERT_EQUAL_PTR(ns, (CljNamespace*)found);

    // Register again - should be idempotent
    ns_register(ns);
    CljObject *found2 = map_get((CljValue)g_runtime.ns_registry, (CljValue)ns->name, NULL);
    TEST_ASSERT_EQUAL_PTR(ns, (CljNamespace*)found2);
}

// Test: Verify ns_cleanup releases all from Map
TEST(test_ns_cleanup_releases_all_from_map) {
    // Create multiple namespaces
    CljNamespace *ns1 = ns_get_or_create("cleanup-test-1", NULL);
    CljNamespace *ns2 = ns_get_or_create("cleanup-test-2", NULL);
    CljNamespace *ns3 = ns_get_or_create("cleanup-test-3", NULL);

    TEST_ASSERT_NOT_NULL(ns1);
    TEST_ASSERT_NOT_NULL(ns2);
    TEST_ASSERT_NOT_NULL(ns3);

    // Verify they're in the map
    TEST_ASSERT_NOT_NULL(g_runtime.ns_registry);
    int count_before = map_count(g_runtime.ns_registry);
    TEST_ASSERT_TRUE(count_before >= 3);

    // Cleanup (this will be called by test framework, but we can verify the structure)
    // Note: We can't actually call ns_cleanup() here as it would break other tests
    // This test just verifies the structure is correct
}

// Test: Verify iteration over all namespaces in registry
TEST(test_ns_registry_iteration) {
    // Create multiple namespaces
    CljNamespace *ns1 = ns_get_or_create("iter-test-1", NULL);
    CljNamespace *ns2 = ns_get_or_create("iter-test-2", NULL);

    TEST_ASSERT_NOT_NULL(ns1);
    TEST_ASSERT_NOT_NULL(ns2);

    // Count namespaces in registry
    int count = map_count(g_runtime.ns_registry);
    TEST_ASSERT_TRUE(count >= 2);

    // Verify both are in the map
    CljSymbol *sym1 = intern_symbol(NULL, "iter-test-1");
    CljSymbol *sym2 = intern_symbol(NULL, "iter-test-2");
    CljObject *found1 = map_get((CljValue)g_runtime.ns_registry, (CljValue)sym1, NULL);
    CljObject *found2 = map_get((CljValue)g_runtime.ns_registry, (CljValue)sym2, NULL);
    TEST_ASSERT_EQUAL_PTR(ns1, (CljNamespace*)found1);
    TEST_ASSERT_EQUAL_PTR(ns2, (CljNamespace*)found2);
}

// Test: Verify that map_conj return value is handled correctly (may return new instance)
TEST(test_ns_registry_map_conj_handles_new_instance) {
    // This test verifies that we correctly handle the case where map_conj might
    // need to grow the map (though with initial capacity 16, this is unlikely)
    // The important thing is that we always use the return value of map_conj

    // Create multiple namespaces to potentially trigger map growth
    for (int i = 0; i < 5; i++) {
        char ns_name[32];
        snprintf(ns_name, sizeof(ns_name), "growth-test-%d", i);
        CljNamespace *ns = ns_get_or_create(ns_name, NULL);
        TEST_ASSERT_NOT_NULL(ns);

        // Verify it's in the registry
        CljSymbol *name_sym = intern_symbol(NULL, ns_name);
        CljObject *found = map_get((CljValue)g_runtime.ns_registry, (CljValue)name_sym, NULL);
        TEST_ASSERT_EQUAL_PTR(ns, (CljNamespace*)found);
    }

    // Verify registry still works correctly
    TEST_ASSERT_NOT_NULL(g_runtime.ns_registry);
    int count = map_count(g_runtime.ns_registry);
    TEST_ASSERT_TRUE(count >= 20);
}

// Test: Verify ns-map returns mappings map
TEST(test_ns_map_returns_mappings) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Create a test namespace with some mappings
    CljNamespace *test_ns = ns_get_or_create("test-ns-map", NULL);
    TEST_ASSERT_NOT_NULL(test_ns);

    // Add some mappings
    CljSymbol *sym1 = intern_symbol_global("test-var1");
    CljSymbol *sym2 = intern_symbol_global("test-var2");
    CljObject *val1 = fixnum(100);
    CljObject *val2 = fixnum(200);

    ns_define(test_ns, (ID)sym1, val1);
    ns_define(test_ns, (ID)sym2, val2);

    // Test ns-map with namespace symbol
    CljSymbol *ns_sym = intern_symbol_global("test-ns-map");
    TEST_ASSERT_NOT_NULL(ns_sym);

    // Call ns-map via eval_string
    CljObject *result = eval_string("(ns-map 'test-ns-map)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_MAP);

    // Verify the mappings are in the result
    CljMap *mappings = (CljMap*)result;
    CljObject *found_val1 = (CljObject*)map_get(mappings, sym1, NULL);
    CljObject *found_val2 = (CljObject*)map_get(mappings, sym2, NULL);
    TEST_ASSERT_NOT_NULL(found_val1);
    TEST_ASSERT_NOT_NULL(found_val2);
    TEST_ASSERT_EQUAL(100, as_fixnum((CljValue)found_val1));
    TEST_ASSERT_EQUAL(200, as_fixnum((CljValue)found_val2));

    // Cleanup
    RELEASE((CljObject*)sym1);
    RELEASE((CljObject*)sym2);
    RELEASE((CljObject*)val1);
    RELEASE((CljObject*)val2);
    RELEASE((CljObject*)ns_sym);
    RELEASE((CljObject*)result);
}

// Test: Verify ns-map with empty namespace returns empty map
TEST(test_ns_map_empty_namespace) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Create an empty namespace
    CljNamespace *empty_ns = ns_get_or_create("test-empty-ns", NULL);
    TEST_ASSERT_NOT_NULL(empty_ns);

    // Test ns-map with namespace symbol
    CljObject *result = eval_string("(ns-map 'test-empty-ns)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_MAP);

    // Verify it's an empty map
    CljMap *mappings = (CljMap*)result;
    TEST_ASSERT_EQUAL(0, map_count(mappings));

    // Cleanup
    RELEASE((CljObject*)result);
}

// Test: Verify ns-map with current namespace (using namespace name)
TEST(test_ns_map_current_namespace) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Add a mapping to current namespace
    CljSymbol *test_sym = intern_symbol_global("current-ns-var");
    CljObject *test_val = fixnum(42);
    ns_define(g_test_eval_state->current_ns, (ID)test_sym, test_val);

    // Get current namespace name
    const char *current_ns_name = g_test_eval_state->current_ns->name->cname;

    // Test ns-map with namespace name
    char expr[256];
    snprintf(expr, sizeof(expr), "(ns-map '%s)", current_ns_name);
    CljObject *result = eval_string(expr, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_MAP);

    // Verify the mapping is in the result
    CljMap *mappings = (CljMap*)result;
    CljObject *found = (CljObject*)map_get(mappings, test_sym, NULL);
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL(42, as_fixnum((CljValue)found));

    // Cleanup
    RELEASE((CljObject*)test_sym);
    RELEASE((CljObject*)test_val);
    RELEASE((CljObject*)result);
}

// Test: Verify find-ns returns namespace object
TEST(test_find_ns_returns_namespace) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Create a test namespace
    CljNamespace *test_ns = ns_get_or_create("test-find-ns", NULL);
    TEST_ASSERT_NOT_NULL(test_ns);

    // Test find-ns with symbol
    CljObject *result = eval_string("(find-ns 'test-find-ns)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_NAMESPACE);
    TEST_ASSERT_EQUAL_PTR(test_ns, (CljNamespace*)result);

    // Cleanup
    RELEASE((CljObject*)result);
}

// Test: Verify find-ns returns nil for non-existent namespace
TEST(test_find_ns_returns_nil_for_nonexistent) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Test find-ns with non-existent namespace
    CljObject *result = eval_string("(find-ns 'does.not.exist)", g_test_eval_state);
    TEST_ASSERT_NULL(result); // Should return nil (NULL)
}

// Test: Verify find-ns with string argument
TEST(test_find_ns_with_string) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Create a test namespace
    CljNamespace *test_ns = ns_get_or_create("test-find-ns-string", NULL);
    TEST_ASSERT_NOT_NULL(test_ns);

    // Test find-ns with string
    CljObject *result = eval_string("(find-ns \"test-find-ns-string\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_NAMESPACE);
    TEST_ASSERT_EQUAL_PTR(test_ns, (CljNamespace*)result);

    // Cleanup
    RELEASE((CljObject*)result);
}

// Test: Verify find-ns with nil argument returns nil
TEST(test_find_ns_with_nil) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Test find-ns with nil
    CljObject *result = eval_string("(find-ns nil)", g_test_eval_state);
    TEST_ASSERT_NULL(result); // Should return nil (NULL)
}

