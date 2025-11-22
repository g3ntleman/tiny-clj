/*
 * Unity Tests for clojure.string functions in Tiny-CLJ
 * 
 * Tests for string manipulation functions from clojure.string namespace
 */

#include "tests_common.h"
#include "namespace.h"
#include "symbol.h"
#include "map.h"
#include "object.h"
#include "kv_macros.h"

// Forward declaration
int load_clojure_core(EvalState *st);

// ============================================================================
// HELPER: Load clojure.string namespace
// ============================================================================

static void load_clojure_string_namespace(void) {
    // Load clojure.string namespace using require
    // CRITICAL: This must be called before any clojure.string function tests
    CljObject *req_result = eval_string("(require 'clojure.string)", g_test_eval_state);
    (void)req_result; // require returns nil
}

// ============================================================================
// TESTS - Test hypotheses about namespace loading and symbol resolution
// ============================================================================

// Test: Verify that clojure.string namespace exists after require
TEST(test_string_debug_namespace_exists) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    load_clojure_string_namespace();
    
    // Check if namespace exists
    CljNamespace *string_ns = ns_find("clojure.string");
    TEST_ASSERT_NOT_NULL_MESSAGE(string_ns, "clojure.string namespace should exist after require");
    
    // Check if namespace has mappings
    TEST_ASSERT_NOT_NULL_MESSAGE(string_ns->mappings, "clojure.string namespace should have mappings");
    
    // Check mapping count
    if (string_ns->mappings) {
        int count = ((CljMap*)string_ns->mappings)->count;
        TEST_ASSERT_TRUE_MESSAGE(count > 0, "clojure.string namespace should have at least one mapping");
    }
}

// Test: Verify that blank? symbol exists in namespace mappings
TEST(test_string_debug_blank_symbol_in_mappings) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    load_clojure_string_namespace();
    
    // Get namespace
    CljNamespace *string_ns = ns_find("clojure.string");
    TEST_ASSERT_NOT_NULL(string_ns);
    TEST_ASSERT_NOT_NULL(string_ns->mappings);
    
    // Create blank? symbol for lookup
    CljSymbol *blank_sym = intern_symbol_global("blank?");
    TEST_ASSERT_NOT_NULL(blank_sym);
    
    // Try to find blank? in namespace mappings
    static CljObject not_found_sentinel = { .type = CLJ_NIL, .rc = SINGLETON_RC };
    ID blank_func = map_get(string_ns->mappings, blank_sym, (ID)&not_found_sentinel);
    
    // Check if blank? was found
    if (blank_func == (ID)&not_found_sentinel) {
        // Not found - try to find it by iterating through mappings
        CljMap *mappings = string_ns->mappings;
        bool found = false;
        for (int i = 0; i < mappings->count; i++) {
            CljObject *key = KV_KEY(mappings->data, i);
            if (key && TAG(key) == CLJ_SYMBOL) {
                CljSymbol *key_sym = as_symbol(key);
                if (key_sym && key_sym->name && strcmp(key_sym->name, "blank?") == 0) {
                    found = true;
                    // Found it - check if it's equal to our lookup symbol
                    TEST_ASSERT_TRUE_MESSAGE(clj_equal((ID)key, (ID)blank_sym), 
                        "blank? symbol should be structurally equal to interned symbol");
                    break;
                }
            }
        }
        TEST_ASSERT_TRUE_MESSAGE(found, "blank? symbol should exist in namespace mappings");
    } else {
        // Found it - this is the expected case
        TEST_ASSERT_NOT_NULL(blank_func);
    }
}

// Test: Verify that qualified symbol parsing works correctly
TEST(test_string_debug_qualified_symbol_parsing) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Parse a qualified symbol
    CljObject *parsed = eval_string("'clojure.string/blank?", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(parsed);
    TEST_ASSERT_TRUE(TAG(parsed) == CLJ_SYMBOL);
    
    CljSymbol *sym = as_symbol(parsed);
    TEST_ASSERT_NOT_NULL(sym);
    TEST_ASSERT_NOT_NULL(sym->ns);
    TEST_ASSERT_NOT_NULL(sym->ns->name);
    TEST_ASSERT_EQUAL_STRING("clojure.string", sym->ns->name);
    TEST_ASSERT_NOT_NULL(sym->name);
    TEST_ASSERT_EQUAL_STRING("blank?", sym->name);
}

// Test: Verify that eval_symbol can resolve qualified symbols
TEST(test_string_debug_eval_symbol_resolution) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    load_clojure_string_namespace();
    
    // Create qualified symbol
    CljSymbol *blank_sym = intern_symbol("clojure.string", "blank?");
    TEST_ASSERT_NOT_NULL(blank_sym);
    TEST_ASSERT_NOT_NULL(blank_sym->ns);
    TEST_ASSERT_NOT_NULL(blank_sym->name);
    
    // Try to resolve it using eval_symbol
    ID resolved = eval_symbol(blank_sym, g_test_eval_state);
    TEST_ASSERT_NOT_NULL_MESSAGE(resolved, "eval_symbol should resolve clojure.string/blank?");
    
    // Verify it's a function
    if (resolved) {
        TEST_ASSERT_TRUE(TAG(resolved) == CLJ_FUNC || TAG(resolved) == CLJ_CLOSURE);
    }
}

// Test: Verify that join symbol exists in namespace mappings
TEST(test_string_debug_join_symbol_in_mappings) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    load_clojure_string_namespace();
    
    // Get namespace
    CljNamespace *string_ns = ns_find("clojure.string");
    TEST_ASSERT_NOT_NULL(string_ns);
    TEST_ASSERT_NOT_NULL(string_ns->mappings);
    
    // Create join symbol for lookup
    CljSymbol *join_sym = intern_symbol_global("join");
    TEST_ASSERT_NOT_NULL(join_sym);
    
    // Try to find join in namespace mappings
    static CljObject not_found_sentinel = { .type = CLJ_NIL, .rc = SINGLETON_RC };
    ID join_func = map_get(string_ns->mappings, join_sym, (ID)&not_found_sentinel);
    
    // Check if join was found
    if (join_func == (ID)&not_found_sentinel) {
        // Not found - try to find it by iterating through mappings
        CljMap *mappings = string_ns->mappings;
        bool found = false;
        for (int i = 0; i < mappings->count; i++) {
            CljObject *key = KV_KEY(mappings->data, i);
            if (key && TAG(key) == CLJ_SYMBOL) {
                CljSymbol *key_sym = as_symbol(key);
                if (key_sym && key_sym->name && strcmp(key_sym->name, "join") == 0) {
                    found = true;
                    break;
                }
            }
        }
        TEST_ASSERT_TRUE_MESSAGE(found, "join symbol should exist in namespace mappings");
    } else {
        // Found it - this is the expected case
        TEST_ASSERT_NOT_NULL(join_func);
    }
}

// Test: Verify that eval_symbol can resolve join
TEST(test_string_debug_eval_symbol_resolution_join) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    load_clojure_string_namespace();
    
    // Create qualified symbol
    CljSymbol *join_sym = intern_symbol("clojure.string", "join");
    TEST_ASSERT_NOT_NULL(join_sym);
    TEST_ASSERT_NOT_NULL(join_sym->ns);
    TEST_ASSERT_NOT_NULL(join_sym->name);
    
    // Try to resolve it using eval_symbol
    ID resolved = eval_symbol(join_sym, g_test_eval_state);
    TEST_ASSERT_NOT_NULL_MESSAGE(resolved, "eval_symbol should resolve clojure.string/join");
    
    // Verify it's a function
    if (resolved) {
        TEST_ASSERT_TRUE(TAG(resolved) == CLJ_FUNC || TAG(resolved) == CLJ_CLOSURE);
    }
}

// Test: Verify that join can be called directly
TEST(test_string_debug_join_direct_call) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    load_clojure_string_namespace();
    
    // Test: (clojure.string/join "," '("a" "b")) => "a,b"
    CljObject *result = eval_string("(clojure.string/join \",\" '(\"a\" \"b\"))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    
    if (result) {
        if (TAG(result) == CLJ_STRING) {
            CljString *str = as_clj_string(result);
            TEST_ASSERT_EQUAL_STRING("a,b", clj_string_data(str));
        } else if (TAG(result) == CLJ_NIL || result == NULL) {
            TEST_FAIL_MESSAGE("join returned nil instead of string");
        } else {
            // Unexpected type
            char msg[256];
            snprintf(msg, sizeof(msg), "join returned unexpected type: %d", TAG(result));
            TEST_FAIL_MESSAGE(msg);
        }
    } else {
        TEST_FAIL_MESSAGE("join returned NULL");
    }
}

// Test: Verify that join function is actually defined in namespace
TEST(test_string_debug_join_function_defined) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    load_clojure_string_namespace();
    
    // Get namespace
    CljNamespace *string_ns = ns_find("clojure.string");
    TEST_ASSERT_NOT_NULL(string_ns);
    TEST_ASSERT_NOT_NULL(string_ns->mappings);
    
    // Create join symbol for lookup
    CljSymbol *join_sym = intern_symbol_global("join");
    TEST_ASSERT_NOT_NULL(join_sym);
    
    // Try to find join in namespace mappings
    static CljObject not_found_sentinel = { .type = CLJ_NIL, .rc = SINGLETON_RC };
    ID join_func = map_get(string_ns->mappings, join_sym, (ID)&not_found_sentinel);
    
    // Check if join was found
    TEST_ASSERT_TRUE_MESSAGE(join_func != (ID)&not_found_sentinel, "join should be found in namespace mappings");
    TEST_ASSERT_NOT_NULL(join_func);
    
    // Verify it's a function
    if (join_func) {
        TEST_ASSERT_TRUE_MESSAGE(TAG(join_func) == CLJ_FUNC || TAG(join_func) == CLJ_CLOSURE, 
                                 "join should be a function or closure");
    }
}

// Test: Hypothesis 1 - Test if nested functions work in general
TEST(test_string_debug_nested_functions_hypothesis) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: Define a function with nested functions similar to join
    CljObject *result = eval_string(
        "(let [outer-fn (fn [x] "
        "  (let [inner-fn (fn [y] (+ y 1))] "
        "    (inner-fn x)))] "
        "(outer-fn 5))", 
        g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(6, as_fixnum(result));
}

// Test: Hypothesis 2 - Test if recursive nested functions work
TEST(test_string_debug_recursive_nested_functions_hypothesis) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: Define a function with recursive nested function
    CljObject *result = eval_string(
        "(let [outer-fn (fn [coll] "
        "  (let [inner-fn (fn [lst acc] "
        "    (if (empty? lst) "
        "      acc "
        "      (inner-fn (rest lst) (+ acc (first lst)))))] "
        "    (inner-fn coll 0)))] "
        "(outer-fn '(1 2 3)))", 
        g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(6, as_fixnum(result));
}

// Test: Hypothesis 3 - Test if defn with nested functions works when defined in namespace
TEST(test_string_debug_defn_nested_functions_hypothesis) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: Define a function with nested functions using defn
    CljObject *defn_result = eval_string(
        "(defn test-join [sep coll] "
        "  (if (empty? coll) "
        "    \"\" "
        "    (let [concat-fn (fn [str-list] "
        "      (if (empty? str-list) "
        "        \"\" "
        "        (let [first-str (first str-list) "
        "              rest-list (rest str-list)] "
        "          (if (empty? rest-list) "
        "            first-str "
        "            (let [next-result (concat-fn rest-list)] "
        "              (str first-str next-result))))))] "
        "      (concat-fn (list \"a\" \"b\")))))", 
        g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(defn_result);
    
    // Now call the function
    CljObject *call_result = eval_string("(test-join \",\" '(\"a\" \"b\"))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(call_result);
    TEST_ASSERT_TRUE(TAG(call_result) == CLJ_STRING);
    
    if (call_result && TAG(call_result) == CLJ_STRING) {
        CljString *str = as_clj_string(call_result);
        TEST_ASSERT_EQUAL_STRING("ab", clj_string_data(str));
    }
}

// Test: Hypothesis 4 - Test if build-list logic works in isolation
TEST(test_string_debug_build_list_hypothesis) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: Test build-list logic similar to join
    CljObject *result = eval_string(
        "(let [build-list (fn [separator coll acc] "
        "  (if (empty? coll) "
        "    (if (empty? acc) "
        "      nil "
        "      (clojure.core/reverse acc)) "
        "    (let [first-elem (first coll) "
        "          rest-coll (rest coll)] "
        "      (if (empty? rest-coll) "
        "        (if (empty? acc) "
        "          (list (str first-elem)) "
        "          (clojure.core/reverse (cons (str first-elem) acc))) "
        "        (build-list separator rest-coll (cons separator (cons (str first-elem) acc)))))))] "
        "(build-list \",\" '(\"a\" \"b\") (list)))", 
        g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_LIST);
    
    // Verify result is ("a" "," "b")
    if (result && TAG(result) == CLJ_LIST) {
        CljList *list = as_list((ID)result);
        TEST_ASSERT_NOT_NULL(list);
        TEST_ASSERT_NOT_NULL(list->first);
        
        // Check first element
        CljObject *first = (CljObject*)list->first;
        TEST_ASSERT_TRUE(TAG(first) == CLJ_STRING);
        if (TAG(first) == CLJ_STRING) {
            CljString *str = as_clj_string(first);
            TEST_ASSERT_EQUAL_STRING("a", clj_string_data(str));
        }
    }
}

// Test: Hypothesis 5 - Test if concat-strings works with result from build-list
TEST(test_string_debug_concat_strings_hypothesis) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: Test concat-strings with a list similar to what build-list would produce
    CljObject *result = eval_string(
        "(let [concat-strings (fn [str-list] "
        "  (if (empty? str-list) "
        "    \"\" "
        "    (let [first-str (first str-list) "
        "          rest-list (rest str-list)] "
        "      (if (empty? rest-list) "
        "        first-str "
        "        (let [next-result (concat-strings rest-list)] "
        "          (str first-str next-result))))))] "
        "(concat-strings '(\"a\" \",\" \"b\")))", 
        g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_STRING);
    
    if (result && TAG(result) == CLJ_STRING) {
        CljString *str = as_clj_string(result);
        TEST_ASSERT_EQUAL_STRING("a,b", clj_string_data(str));
    }
}

// Test: Hypothesis 6 - Test complete join logic in a single let (simulating what defn should do)
TEST(test_string_debug_complete_join_logic_hypothesis) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: Complete join logic in a single let expression
    CljObject *result = eval_string(
        "(let [join-impl (fn [separator coll] "
        "  (if (empty? coll) "
        "    \"\" "
        "    (let [sep (if (nil? separator) \"\" separator) "
        "          build-list (fn [separator coll acc] "
        "            (if (empty? coll) "
        "              (if (empty? acc) "
        "                nil "
        "                (clojure.core/reverse acc)) "
        "              (let [first-elem (first coll) "
        "                    rest-coll (rest coll)] "
        "                (if (empty? rest-coll) "
        "                  (if (empty? acc) "
        "                    (list (str first-elem)) "
        "                    (clojure.core/reverse (cons (str first-elem) acc))) "
        "                  (build-list separator rest-coll (cons separator (cons (str first-elem) acc))))))) "
        "          concat-strings (fn [str-list] "
        "            (if (empty? str-list) "
        "              \"\" "
        "              (let [first-str (first str-list) "
        "                    rest-list (rest str-list)] "
        "                (if (empty? rest-list) "
        "                  first-str "
        "                  (let [next-result (concat-strings rest-list)] "
        "                    (str first-str next-result))))))] "
        "      (let [result (build-list sep coll (list))] "
        "        (if (nil? result) "
        "          \"\" "
        "          (concat-strings result))))))] "
        "(join-impl \",\" '(\"a\" \"b\")))", 
        g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(result);
    
    if (result) {
        if (TAG(result) == CLJ_STRING) {
            CljString *str = as_clj_string(result);
            TEST_ASSERT_EQUAL_STRING("a,b", clj_string_data(str));
        } else if (TAG(result) == CLJ_NIL || result == NULL) {
            TEST_FAIL_MESSAGE("join-impl returned nil instead of string");
        } else {
            char msg[256];
            snprintf(msg, sizeof(msg), "join-impl returned unexpected type: %d", TAG(result));
            TEST_FAIL_MESSAGE(msg);
        }
    } else {
        TEST_FAIL_MESSAGE("join-impl returned NULL");
    }
}

// Test: Hypothesis 7 - Test if manually defining join in clojure.string namespace works
TEST(test_string_debug_manual_join_definition_hypothesis) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace first
    load_clojure_string_namespace();
    
    // Manually define a simplified join in clojure.string namespace
    // First switch to clojure.string namespace
    CljObject *ns_result = eval_string("(ns clojure.string)", g_test_eval_state);
    (void)ns_result;
    
    // Define a simplified version that uses the existing join function
    CljObject *defn_result = eval_string(
        "(defn my-join [separator coll] "
        "  (if (empty? coll) "
        "    \"\" "
        "    (clojure.string/join separator coll)))", 
        g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(defn_result);
    
    // Now call the manually defined function
    CljObject *call_result = eval_string("(clojure.string/my-join \",\" '(\"a\" \"b\"))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(call_result);
    
    if (call_result) {
        if (TAG(call_result) == CLJ_STRING) {
            CljString *str = as_clj_string(call_result);
            TEST_ASSERT_EQUAL_STRING("a,b", clj_string_data(str));
        } else if (TAG(call_result) == CLJ_NIL || call_result == NULL) {
            TEST_FAIL_MESSAGE("my-join returned nil instead of string");
        } else {
            char msg[256];
            snprintf(msg, sizeof(msg), "my-join returned unexpected type: %d", TAG(call_result));
            TEST_FAIL_MESSAGE(msg);
        }
    } else {
        TEST_FAIL_MESSAGE("my-join returned NULL");
    }
}

// Test: Hypothesis 8 - Test if calling join function object directly works
TEST(test_string_debug_direct_function_call_hypothesis) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    load_clojure_string_namespace();
    
    // Get the join function object
    CljSymbol *join_sym = intern_symbol("clojure.string", "join");
    TEST_ASSERT_NOT_NULL(join_sym);
    
    ID join_func = eval_symbol(join_sym, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(join_func);
    TEST_ASSERT_TRUE(TAG(join_func) == CLJ_FUNC || TAG(join_func) == CLJ_CLOSURE);
    
    // Try to call it directly via eval_string with the function object
    // First, let's test if we can bind it to a variable and call it
    CljObject *result = eval_string(
        "(let [join-fn clojure.string/join] "
        "(join-fn \",\" '(\"a\" \"b\")))", 
        g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(result);
    
    if (result) {
        if (TAG(result) == CLJ_STRING) {
            CljString *str = as_clj_string(result);
            TEST_ASSERT_EQUAL_STRING("a,b", clj_string_data(str));
        } else if (TAG(result) == CLJ_NIL || result == NULL) {
            TEST_FAIL_MESSAGE("join-fn returned nil instead of string");
        } else {
            char msg[256];
            snprintf(msg, sizeof(msg), "join-fn returned unexpected type: %d", TAG(result));
            TEST_FAIL_MESSAGE(msg);
        }
    } else {
        TEST_FAIL_MESSAGE("join-fn returned NULL");
    }
}

// Test: Hypothesis 9 - Check if join is actually defined when clojure.string is loaded
TEST(test_string_debug_join_defined_after_load_hypothesis) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    load_clojure_string_namespace();
    
    // Get namespace
    CljNamespace *string_ns = ns_find("clojure.string");
    TEST_ASSERT_NOT_NULL(string_ns);
    TEST_ASSERT_NOT_NULL(string_ns->mappings);
    
    // Check mapping count - should have multiple functions including join
    CljMap *mappings = string_ns->mappings;
    int count = mappings->count;
    TEST_ASSERT_TRUE_MESSAGE(count > 0, "clojure.string namespace should have mappings");
    
    // Try to find join using map_get (safer than iterating)
    CljSymbol *join_sym = intern_symbol_global("join");
    TEST_ASSERT_NOT_NULL(join_sym);
    
    static CljObject not_found_sentinel = { .type = CLJ_NIL, .rc = SINGLETON_RC };
    ID join_func = map_get(string_ns->mappings, join_sym, (ID)&not_found_sentinel);
    
    bool found_join = (join_func != (ID)&not_found_sentinel);
    TEST_ASSERT_TRUE_MESSAGE(found_join, "join should be found in namespace mappings after loading");
    TEST_ASSERT_NOT_NULL(join_func);
    
    if (join_func) {
        TEST_ASSERT_TRUE_MESSAGE(TAG(join_func) == CLJ_FUNC || TAG(join_func) == CLJ_CLOSURE, 
                                 "join should be a function or closure");
    }
}

// Test: Hypothesis 10 - Check if join's closure environment is correct
TEST(test_string_debug_join_closure_env_hypothesis) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    load_clojure_string_namespace();
    
    // Get join function
    CljSymbol *join_sym = intern_symbol("clojure.string", "join");
    TEST_ASSERT_NOT_NULL(join_sym);
    
    ID join_func = eval_symbol(join_sym, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(join_func);
    TEST_ASSERT_TRUE(TAG(join_func) == CLJ_CLOSURE);
    
    if (TAG(join_func) == CLJ_CLOSURE) {
        CljFunction *func = as_function(join_func);
        TEST_ASSERT_NOT_NULL(func);
        TEST_ASSERT_NOT_NULL(func->closure_env);
        TEST_ASSERT_TRUE(TAG(func->closure_env) == CLJ_MAP);
        
        // Check if closure_env contains necessary functions like reverse, str, etc.
        CljSymbol *reverse_sym = intern_symbol_global("reverse");
        CljSymbol *str_sym = intern_symbol_global("str");
        CljSymbol *empty_sym = intern_symbol_global("empty?");
        
        static CljObject not_found_sentinel = { .type = CLJ_NIL, .rc = SINGLETON_RC };
        
        ID reverse_func = map_get(func->closure_env, reverse_sym, (ID)&not_found_sentinel);
        ID str_func = map_get(func->closure_env, str_sym, (ID)&not_found_sentinel);
        ID empty_func = map_get(func->closure_env, empty_sym, (ID)&not_found_sentinel);
        
        // These should be found in closure_env (they're in clojure.core)
        TEST_ASSERT_TRUE_MESSAGE(reverse_func != (ID)&not_found_sentinel, 
                                 "reverse should be in join's closure_env");
        TEST_ASSERT_TRUE_MESSAGE(str_func != (ID)&not_found_sentinel, 
                                 "str should be in join's closure_env");
        TEST_ASSERT_TRUE_MESSAGE(empty_func != (ID)&not_found_sentinel, 
                                 "empty? should be in join's closure_env");
    }
}

// Test: Hypothesis 11 - Test if nested functions in join can access their closure
TEST(test_string_debug_join_nested_functions_closure_hypothesis) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: Create a function similar to join with nested functions
    // and check if nested functions can access outer closure
    CljObject *result = eval_string(
        "(let [outer-fn (fn [x] "
        "  (let [inner-fn (fn [y] "
        "    (let [deep-fn (fn [z] (+ x y z))] "
        "      (deep-fn 1)))] "
        "    (inner-fn 2)))] "
        "(outer-fn 3))", 
        g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(6, as_fixnum(result));
}

// Test: Hypothesis 12 - Test if join works with a simpler version (without nested recursion)
TEST(test_string_debug_join_simple_version_hypothesis) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: Simple version of join without nested recursive functions
    CljObject *result = eval_string(
        "(let [simple-join (fn [sep coll] "
        "  (if (empty? coll) "
        "    \"\" "
        "    (let [first-elem (first coll) "
        "          rest-coll (rest coll)] "
        "      (if (empty? rest-coll) "
        "        (str first-elem) "
        "        (str first-elem sep (simple-join sep rest-coll))))))] "
        "(simple-join \",\" '(\"a\" \"b\" \"c\")))", 
        g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_STRING);
    
    if (result && TAG(result) == CLJ_STRING) {
        CljString *str = as_clj_string(result);
        TEST_ASSERT_EQUAL_STRING("a,b,c", clj_string_data(str));
    }
}

// Test: Hypothesis 13 - Test if the problem is with calling join from different namespace
TEST(test_string_debug_join_from_different_namespace_hypothesis) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    load_clojure_string_namespace();
    
    // Switch to a different namespace and try to call join
    eval_string("(ns test-ns)", g_test_eval_state);
    
    // Try to call join from different namespace
    CljObject *result = eval_string("(clojure.string/join \",\" '(\"a\" \"b\"))", g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(result);
    
    if (result) {
        if (TAG(result) == CLJ_STRING) {
            CljString *str = as_clj_string(result);
            TEST_ASSERT_EQUAL_STRING("a,b", clj_string_data(str));
        } else if (TAG(result) == CLJ_NIL || result == NULL) {
            TEST_FAIL_MESSAGE("join returned nil when called from different namespace");
        } else {
            char msg[256];
            snprintf(msg, sizeof(msg), "join returned unexpected type: %d", TAG(result));
            TEST_FAIL_MESSAGE(msg);
        }
    } else {
        TEST_FAIL_MESSAGE("join returned NULL when called from different namespace");
    }
}

// Test: Hypothesis 14 - Test if join works when defined in current namespace
TEST(test_string_debug_join_in_current_namespace_hypothesis) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace first
    load_clojure_string_namespace();
    
    // Ensure we're in user namespace
    CljObject *ns_result = eval_string("(ns user)", g_test_eval_state);
    (void)ns_result;
    
    // Define a simplified join in current namespace (user) that uses clojure.string/join
    CljObject *defn_result = eval_string(
        "(defn my-join [separator coll] "
        "  (if (empty? coll) "
        "    \"\" "
        "    (clojure.string/join separator coll)))", 
        g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(defn_result);
    
    // Now call the function
    CljObject *call_result = eval_string("(my-join \",\" '(\"a\" \"b\"))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(call_result);
    
    if (call_result) {
        if (TAG(call_result) == CLJ_STRING) {
            CljString *str = as_clj_string(call_result);
            TEST_ASSERT_EQUAL_STRING("a,b", clj_string_data(str));
        } else if (TAG(call_result) == CLJ_NIL || call_result == NULL) {
            TEST_FAIL_MESSAGE("my-join returned nil instead of string");
        } else {
            char msg[256];
            snprintf(msg, sizeof(msg), "my-join returned unexpected type: %d", TAG(call_result));
            TEST_FAIL_MESSAGE(msg);
        }
    } else {
        TEST_FAIL_MESSAGE("my-join returned NULL");
    }
}

// ============================================================================
// BLANK? TESTS
// ============================================================================

TEST(test_string_blank) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace first
    load_clojure_string_namespace();
    
    // Test: (clojure.string/blank? nil) => true
    CljObject *result1 = eval_string("(clojure.string/blank? nil)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(result1 == clj_true);
    
    // Test: (clojure.string/blank? "") => true
    CljObject *result2 = eval_string("(clojure.string/blank? \"\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(result2 == clj_true);
    
    // Test: (clojure.string/blank? "   ") => true
    CljObject *result3 = eval_string("(clojure.string/blank? \"   \")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result3);
    TEST_ASSERT_TRUE(result3 == clj_true);
    
    // Test: (clojure.string/blank? "abc") => false
    CljObject *result4 = eval_string("(clojure.string/blank? \"abc\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result4);
    TEST_ASSERT_TRUE(result4 == clj_false);
}

// ============================================================================
// CAPITALIZE TESTS
// ============================================================================

TEST(test_string_capitalize) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace first
    load_clojure_string_namespace();
    
    // Test: (clojure.string/capitalize "hello") => "Hello"
    CljObject *result1 = eval_string("(clojure.string/capitalize \"hello\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(result1 && TAG(result1) == CLJ_STRING);
    CljString *str1 = as_clj_string(result1);
    TEST_ASSERT_EQUAL_STRING("Hello", clj_string_data(str1));
    
    // Test: (clojure.string/capitalize "HELLO") => "Hello"
    CljObject *result2 = eval_string("(clojure.string/capitalize \"HELLO\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(result2 && TAG(result2) == CLJ_STRING);
    CljString *str2 = as_clj_string(result2);
    TEST_ASSERT_EQUAL_STRING("Hello", clj_string_data(str2));
}

// ============================================================================
// ENDS-WITH? TESTS
// ============================================================================

TEST(test_string_ends_with) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace first
    load_clojure_string_namespace();
    
    // Test: (clojure.string/ends-with? "hello" "lo") => true
    CljObject *result1 = eval_string("(clojure.string/ends-with? \"hello\" \"lo\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(result1 == clj_true);
    
    // Test: (clojure.string/ends-with? "hello" "x") => false
    CljObject *result2 = eval_string("(clojure.string/ends-with? \"hello\" \"x\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(result2 == clj_false);
}

// ============================================================================
// ESCAPE TESTS
// ============================================================================

TEST(test_string_escape) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace first
    load_clojure_string_namespace();
    
    // Test: (clojure.string/escape "abc" {}) => "abc"
    CljObject *result1 = eval_string("(clojure.string/escape \"abc\" {})", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(result1 && TAG(result1) == CLJ_STRING);
    CljString *str1 = as_clj_string(result1);
    TEST_ASSERT_EQUAL_STRING("abc", clj_string_data(str1));
}

// ============================================================================
// INCLUDES? TESTS
// ============================================================================

TEST(test_string_includes) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace first
    load_clojure_string_namespace();
    
    // Test: (clojure.string/includes? "hello" "ell") => true
    CljObject *result1 = eval_string("(clojure.string/includes? \"hello\" \"ell\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(result1 == clj_true);
    
    // Test: (clojure.string/includes? "hello" "xyz") => false
    CljObject *result2 = eval_string("(clojure.string/includes? \"hello\" \"xyz\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(result2 == clj_false);
}

// ============================================================================
// INDEX-OF TESTS
// ============================================================================

TEST(test_string_index_of) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace first
    load_clojure_string_namespace();
    
    // Test: (clojure.string/index-of "hello" "l" 0) => 2
    CljObject *result1 = eval_string("(clojure.string/index-of \"hello\" \"l\" 0)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(is_fixnum(result1));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(result1));
}

// ============================================================================
// REVERSE TESTS (clojure.string/reverse for strings)
// ============================================================================

TEST(test_string_reverse) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace first
    load_clojure_string_namespace();
    
    // Test: (clojure.string/reverse "abc") => "cba"
    CljObject *result1 = eval_string("(clojure.string/reverse \"abc\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(result1 && TAG(result1) == CLJ_STRING);
    CljString *str1 = as_clj_string(result1);
    TEST_ASSERT_EQUAL_STRING("cba", clj_string_data(str1));
}

// ============================================================================
// TESTS FOR REVERSE CONFLICTS
// ============================================================================

TEST(test_string_debug_reverse_direct_resolution) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace first
    load_clojure_string_namespace();
    
    // Test: (clojure.core/reverse (list 1 2 3)) => (3 2 1)
    // This tests if clojure.core/reverse is accessible after loading clojure.string
    CljObject *result = eval_string("(clojure.core/reverse (list 1 2 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result && TAG(result) == CLJ_LIST);
}

TEST(test_string_debug_reverse_in_let_binding) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace first
    load_clojure_string_namespace();
    
    // Test: (let [step (fn [coll] (clojure.core/reverse coll))] (step (list 1 2 3)))
    // This tests if clojure.core/reverse works in let bindings
    CljObject *result = eval_string("(let [step (fn [coll] (clojure.core/reverse coll))] (step (list 1 2 3)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result && TAG(result) == CLJ_LIST);
}

TEST(test_string_debug_reverse_after_require_string) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace first
    load_clojure_string_namespace();
    
    // Test: (clojure.core/reverse (list 1 2 3)) => (3 2 1)
    // This tests if clojure.core/reverse works after requiring clojure.string
    CljObject *result = eval_string("(clojure.core/reverse (list 1 2 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result && TAG(result) == CLJ_LIST);
}

TEST(test_string_debug_reverse_in_function) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace first
    load_clojure_string_namespace();
    
    // Test: (let [step (fn [coll acc] (if (empty? coll) (clojure.core/reverse acc) (step (rest coll) (cons (first coll) acc))))] (step (list 1 2 3) (list)))
    // This tests if clojure.core/reverse works in recursive functions
    CljObject *result = eval_string("(let [step (fn [coll acc] (if (empty? coll) (clojure.core/reverse acc) (step (rest coll) (cons (first coll) acc))))] (step (list 1 2 3) (list)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result && TAG(result) == CLJ_LIST);
}

TEST(test_string_debug_reverse_in_simple_let) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace first
    load_clojure_string_namespace();
    
    // Test: (let [x (list 1 2 3)] (clojure.core/reverse x))
    // This tests if clojure.core/reverse works in simple let bindings
    CljObject *result = eval_string("(let [x (list 1 2 3)] (clojure.core/reverse x))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result && TAG(result) == CLJ_LIST);
    
    // Verify result is (3 2 1)
    CljList *list = as_list((ID)result);
    TEST_ASSERT_NOT_NULL(list);
    TEST_ASSERT_NOT_NULL(list->first);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)list->first));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)list->first));
}

