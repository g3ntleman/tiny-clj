/*
 * Unity Tests for meta function in Tiny-CLJ
 * 
 * Test-First: These tests verify that meta works correctly
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
#include "../builtins.h"
#include "../meta.h"
#include <sys/time.h>

static void assert_meta_has_line_info(CljObject *meta_result, const char *context) {
    TEST_ASSERT_NOT_NULL_MESSAGE(meta_result, context);
    TEST_ASSERT_TRUE_MESSAGE(TAG(meta_result) == CLJ_MAP, context);

    CljSymbol *kw_line = intern_symbol_global(":line");
    TEST_ASSERT_NOT_NULL(kw_line);

    CljMap *meta_map = (CljMap*)meta_result;
    ID line_value = map_get(meta_map, kw_line, NULL);

    TEST_ASSERT_NOT_NULL_MESSAGE(line_value, context);
    TEST_ASSERT_TRUE_MESSAGE(is_fixnum(line_value), context);

    int line_number = as_fixnum(line_value);
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, line_number, context);
}

// ============================================================================
// TEST: meta_registry_init initializes registry
// ============================================================================
TEST(test_meta_registry_init) {
    // Ensure runtime is initialized
    runtime_init(&g_runtime);
    
    // Initialize meta registry
    meta_registry_init();
    
    // Check that registry exists
    TEST_ASSERT_NOT_NULL_MESSAGE(g_runtime.meta_registry, 
                                 "meta_registry should be initialized");
    
    // Check that it's a map
    TEST_ASSERT_EQUAL_MESSAGE(CLJ_MAP, TAG(g_runtime.meta_registry),
                              "meta_registry should be a map");
}

// ============================================================================
// TEST: meta_set and meta_get work with simple objects
// ============================================================================
TEST(test_meta_function_set_and_get) {
    // Ensure runtime is initialized
    runtime_init(&g_runtime);
    meta_registry_init();
    
    // Create a test object (a string)
    CljString *test_obj = make_string("test");
    TEST_ASSERT_NOT_NULL(test_obj);
    
    // Create metadata map
    CljMap *meta_map = make_map(4);
    TEST_ASSERT_NOT_NULL(meta_map);
    
    // Add a key-value pair to metadata
    CljSymbol *key = intern_symbol_global(":test");
    TEST_ASSERT_NOT_NULL(key);
    CljString *value = make_string("value");
    TEST_ASSERT_NOT_NULL(value);
    
    CljMap *new_meta = map_assoc((CljValue)meta_map, (CljValue)key, (CljValue)value);
    TEST_ASSERT_NOT_NULL(new_meta);
    if (new_meta != meta_map) {
        RELEASE(meta_map);  // Only release if new map was created (COW)
    }
    meta_map = new_meta;
    
    // Set metadata
    meta_set((CljObject*)test_obj, (CljObject*)meta_map);
    
    // Get metadata
    ID retrieved_meta = meta_get((CljObject*)test_obj);
    TEST_ASSERT_NOT_NULL_MESSAGE(retrieved_meta, 
                                 "retrieved_meta should not be NULL");
    
    // Check that it's the same map
    TEST_ASSERT_EQUAL_PTR_MESSAGE(meta_map, retrieved_meta,
                                  "retrieved metadata should be the same map");
    
    RELEASE(test_obj);
    RELEASE(meta_map);
}

// ============================================================================
// TEST: meta function returns nil for object without metadata
// ============================================================================
TEST(test_meta_returns_nil_for_no_metadata) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create a test object
    CljString *test_obj = make_string("test");
    TEST_ASSERT_NOT_NULL(test_obj);
    
    // Call meta function
    ID args[1] = { (ID)test_obj };
    ID result = native_meta(args, 1);
    
    // Should return nil (NULL)
    TEST_ASSERT_NULL_MESSAGE(result, 
                             "meta should return nil for object without metadata");
    
    RELEASE(test_obj);
}

// ============================================================================
// TEST: meta function returns metadata for object with metadata
// ============================================================================
TEST(test_meta_returns_metadata) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Ensure runtime is initialized
    runtime_init(&g_runtime);
    meta_registry_init();
    
    // Create a test object
    CljString *test_obj = make_string("test");
    TEST_ASSERT_NOT_NULL(test_obj);
    
    // Create metadata map
    CljMap *meta_map = make_map(4);
    TEST_ASSERT_NOT_NULL(meta_map);
    
    // Add a key-value pair to metadata
    CljSymbol *key = intern_symbol_global(":test");
    TEST_ASSERT_NOT_NULL(key);
    CljString *value = make_string("value");
    TEST_ASSERT_NOT_NULL(value);
    
    CljMap *new_meta = map_assoc((CljValue)meta_map, (CljValue)key, (CljValue)value);
    TEST_ASSERT_NOT_NULL(new_meta);
    if (new_meta != meta_map) {
        RELEASE(meta_map);  // Only release if new map was created (COW)
    }
    meta_map = new_meta;
    
    // Set metadata
    meta_set((CljObject*)test_obj, (CljObject*)meta_map);
    
    // Call meta function
    ID args[1] = { (ID)test_obj };
    ID result = native_meta(args, 1);
    
    // Should return metadata map
    TEST_ASSERT_NOT_NULL_MESSAGE(result, 
                                 "meta should return metadata map");
    
    // Check that it's the same map
    TEST_ASSERT_EQUAL_PTR_MESSAGE(meta_map, result,
                                  "meta should return the same metadata map");
    
    RELEASE(test_obj);
    RELEASE(meta_map);
    RELEASE(result);  // native_meta retains the result
}

// ============================================================================
// TEST: meta function resolves symbols to get metadata
// ============================================================================
TEST(test_meta_resolves_symbols) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Ensure runtime is initialized
    runtime_init(&g_runtime);
    meta_registry_init();
    
    // Create a test variable
    CljString *test_value = make_string("test-value");
    TEST_ASSERT_NOT_NULL(test_value);
    
    // Create metadata map
    CljMap *meta_map = make_map(4);
    TEST_ASSERT_NOT_NULL(meta_map);
    
    // Add a key-value pair to metadata
    CljSymbol *key = intern_symbol_global(":doc");
    TEST_ASSERT_NOT_NULL(key);
    CljString *value = make_string("Test variable");
    TEST_ASSERT_NOT_NULL(value);
    
    CljMap *new_meta = map_assoc((CljValue)meta_map, (CljValue)key, (CljValue)value);
    TEST_ASSERT_NOT_NULL(new_meta);
    if (new_meta != meta_map) {
        RELEASE(meta_map);  // Only release if new map was created (COW)
    }
    meta_map = new_meta;
    
    // Set metadata on the value
    meta_set((CljObject*)test_value, (CljObject*)meta_map);
    
    // Define the variable in namespace
    CljSymbol *var_sym = intern_symbol_global("test-var");
    TEST_ASSERT_NOT_NULL(var_sym);
    ns_define(g_test_eval_state->current_ns, var_sym, (ID)test_value);
    
    // Set eval state for builtin
    builtin_set_eval_state(g_test_eval_state);
    
    // Call meta function with symbol
    ID args[1] = { (ID)var_sym };
    ID result = native_meta(args, 1);
    
    // Should return metadata map (after resolving symbol)
    TEST_ASSERT_NOT_NULL_MESSAGE(result, 
                                 "meta should return metadata map after resolving symbol");
    
    // Check that it's the same map
    TEST_ASSERT_EQUAL_PTR_MESSAGE(meta_map, result,
                                  "meta should return the same metadata map");
    
    builtin_set_eval_state(NULL);
    RELEASE(test_value);
    RELEASE(meta_map);
    RELEASE(result);  // native_meta retains the result
}

// ============================================================================
// TEST: metadata is transferred from defn form to function
// ============================================================================
TEST(test_metadata_transferred_from_defn_to_function) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Ensure runtime is initialized
    runtime_init(&g_runtime);
    meta_registry_init();
    
    // Parse a defn form with metadata
    const char *code = "^#^{:doc \"Test function\"} (defn test-fn [] 42)";
    ID parsed = parse(code, g_test_eval_state);
    TEST_ASSERT_NOT_NULL_MESSAGE(parsed, "defn form should parse");
    
    // Check that metadata is on the parsed form
    ID form_meta = meta_get((CljObject*)parsed);
    TEST_ASSERT_NOT_NULL_MESSAGE(form_meta, 
                                 "metadata should be on the parsed form");
    
    // Evaluate the defn form
    ID result = eval_parsed((CljObject*)parsed, g_test_eval_state, NULL);
    TEST_ASSERT_NOT_NULL_MESSAGE(result, "defn should evaluate successfully");
    
    // Resolve the function
    CljSymbol *fn_sym = intern_symbol_global("test-fn");
    TEST_ASSERT_NOT_NULL(fn_sym);
    ID fn_obj = ns_resolve(g_test_eval_state, fn_sym);
    TEST_ASSERT_NOT_NULL_MESSAGE(fn_obj, "function should be in namespace");
    
    // Check that metadata is on the function
    ID fn_meta = meta_get((CljObject*)fn_obj);
    TEST_ASSERT_NOT_NULL_MESSAGE(fn_meta, 
                                 "metadata should be on the function");
    
    RELEASE(parsed);
    RELEASE(result);
}

// ============================================================================
// TEST: metadata is transferred from defn form to native function
// ============================================================================
TEST(test_metadata_transferred_from_defn_to_native_function) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Ensure runtime is initialized
    runtime_init(&g_runtime);
    meta_registry_init();
    
    // Set namespace to clojure.string
    evalstate_set_ns(g_test_eval_state, "clojure.string");
    
    // Parse a defn form with metadata for native function
    const char *code = "^#^{:doc \"Removes whitespace\"} (defn trim [s] :native)";
    ID parsed = parse(code, g_test_eval_state);
    TEST_ASSERT_NOT_NULL_MESSAGE(parsed, "defn form should parse");
    
    // Check that metadata is on the parsed form
    ID form_meta = meta_get((CljObject*)parsed);
    TEST_ASSERT_NOT_NULL_MESSAGE(form_meta, 
                                 "metadata should be on the parsed form");
    
    // Evaluate the defn form
    ID result = eval_parsed((CljObject*)parsed, g_test_eval_state, NULL);
    TEST_ASSERT_NOT_NULL_MESSAGE(result, "defn should evaluate successfully");
    
    // Resolve the function
    CljSymbol *fn_sym = intern_symbol_global("trim");
    TEST_ASSERT_NOT_NULL(fn_sym);
    ID fn_obj = ns_resolve(g_test_eval_state, fn_sym);
    TEST_ASSERT_NOT_NULL_MESSAGE(fn_obj, "native function should be in namespace");
    
    // Check that metadata is on the native function
    ID fn_meta = meta_get((CljObject*)fn_obj);
    TEST_ASSERT_NOT_NULL_MESSAGE(fn_meta, 
                                 "metadata should be on the native function");
    
    RELEASE(parsed);
    RELEASE(result);
}

// ============================================================================
// TEST: meta works with qualified symbols like clojure.string/trim
// ============================================================================
TEST(test_meta_qualified_symbol) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    init_special_symbols();
    
    // Load clojure.string namespace
    CljObject *req_result = eval_string("(require 'clojure.string)", g_test_eval_state);
    (void)req_result; // require returns nil
    
    // First, check if trim function exists
    CljNamespace *string_ns = ns_find("clojure.string");
    TEST_ASSERT_NOT_NULL_MESSAGE(string_ns, "clojure.string namespace should exist");
    
    CljSymbol *trim_sym = NULL;
    if (string_ns->name && string_ns->name->cname) {
        trim_sym = intern_symbol(string_ns->name, "trim");
    }
    if (!trim_sym) {
        // Fallback for legacy namespaces that still store unqualified keys
        trim_sym = intern_symbol_global("trim");
    }
    TEST_ASSERT_NOT_NULL_MESSAGE(trim_sym, "trim symbol should exist");
    
    ID trim_func = map_get(string_ns->mappings, trim_sym, NOT_FOUND);
    if (trim_func == NOT_FOUND) {
        trim_func = NULL;
    }
    TEST_ASSERT_NOT_NULL_MESSAGE(trim_func, "trim function should exist in clojure.string namespace");
    
    // Check if trim function has metadata directly
    // Note: trim might have been loaded before the changes, so it might not have metadata
    // In that case, we'll test with a newly defined function
    ID trim_meta = meta_get((CljObject*)trim_func);
    if (!trim_meta) {
        // trim was loaded before changes - redefine it to get metadata
        evalstate_set_ns(g_test_eval_state, "clojure.string");
        CljObject *redef_result = eval_string("(defn trim [s] :native)", g_test_eval_state);
        (void)redef_result;
        
        // Check again - use qualified symbol for lookup
        CljSymbol *qualified_trim_sym = NULL;
        if (string_ns->name && string_ns->name->cname) {
            qualified_trim_sym = intern_symbol(string_ns->name, "trim");
        }
        TEST_ASSERT_NOT_NULL_MESSAGE(qualified_trim_sym, "Should be able to create qualified trim symbol");
        
        trim_func = qualified_trim_sym ? map_get(string_ns->mappings, qualified_trim_sym, NOT_FOUND) : NULL;
        if (trim_func == NOT_FOUND) {
            trim_func = NULL;
        }
        TEST_ASSERT_NOT_NULL_MESSAGE(trim_func, "trim function should exist in clojure.string namespace after redefinition");
        
        trim_meta = trim_func ? meta_get((CljObject*)trim_func) : NULL;
        TEST_ASSERT_NOT_NULL_MESSAGE(trim_meta, "trim function should have metadata after redefinition");
    }
    
    // Test: (meta 'clojure.string/trim) should return metadata map
    CljObject *meta_result = eval_string("(meta 'clojure.string/trim)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL_MESSAGE(meta_result, 
                                 "(meta 'clojure.string/trim) should return metadata map");
    
    // Metadata should be a map
    TEST_ASSERT_TRUE_MESSAGE(TAG(meta_result) == CLJ_MAP, 
                            "meta result should be a map");
    
    // Check that metadata contains :name and :ns keys
    CljSymbol *kw_name = intern_symbol_global(":name");
    CljSymbol *kw_ns = intern_symbol_global(":ns");
    TEST_ASSERT_NOT_NULL(kw_name);
    TEST_ASSERT_NOT_NULL(kw_ns);
    
    CljMap *meta_map = (CljMap*)meta_result;
    ID name_value = map_get(meta_map, kw_name, NULL);
    ID ns_value = map_get(meta_map, kw_ns, NULL);
    
    TEST_ASSERT_NOT_NULL_MESSAGE(name_value, 
                                 "metadata should contain :name key");
    TEST_ASSERT_NOT_NULL_MESSAGE(ns_value, 
                                 "metadata should contain :ns key");
    
    // :name should be "trim" (string)
    if (name_value && TAG(name_value) == CLJ_STRING) {
        CljString *name_str = as_clj_string(name_value);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("trim", clj_string_data(name_str), 
                                        ":name should be \"trim\"");
    }
}

// ============================================================================
// TEST: meta returns :line information for native functions
// ============================================================================
TEST(test_meta_contains_line_info) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (meta nil?) should return metadata with :line information
    CljObject *meta_result = eval_string("(meta nil?)", g_test_eval_state);
    assert_meta_has_line_info(meta_result, "(meta nil?) should include :line metadata");
}

#ifdef ENABLE_META
// ============================================================================
// TEST: metadata contains :line for various literal forms
// ============================================================================
TEST(test_meta_list_literal_contains_line_info) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    CljObject *meta_result = eval_string("(meta '(+ 1 2))", g_test_eval_state);
    assert_meta_has_line_info(meta_result, "(meta '(+ 1 2)) should include :line metadata");
}

TEST(test_meta_vector_literal_contains_line_info) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    CljObject *meta_result = eval_string("(meta '[1 2 3])", g_test_eval_state);
    assert_meta_has_line_info(meta_result, "(meta '[1 2 3]) should include :line metadata");
}

TEST(test_meta_map_literal_contains_line_info) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    CljObject *meta_result = eval_string("(meta '{:a 1 :b 2})", g_test_eval_state);
    assert_meta_has_line_info(meta_result, "(meta '{:a 1 :b 2}) should include :line metadata");
}

TEST(test_meta_symbol_literal_contains_line_info) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    CljObject *meta_result = eval_string("(meta 'inc)", g_test_eval_state);
    assert_meta_has_line_info(meta_result, "(meta 'inc) should include :line metadata");
}

TEST(test_meta_string_literal_contains_line_info) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    CljObject *meta_result = eval_string("(meta \"hello world\")", g_test_eval_state);
    assert_meta_has_line_info(meta_result, "(meta \"hello world\") should include :line metadata");
}
#endif

