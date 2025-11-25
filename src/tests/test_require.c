/*
 * Unity Tests for require functionality and clojure.string namespace in Tiny-CLJ
 * 
 * Tests for require special form and clojure.string functions
 */

#include "tests_common.h"
#include "strings.h"  // For to_cstring
#include <sys/time.h>

// Forward declarations
int load_clojure_core(EvalState *st);
bool eval_multiform_string(const char *code, EvalState *st);

// ============================================================================
// REQUIRE TESTS
// ============================================================================

TEST(test_require_clojure_string) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (require 'clojure.string) should load the namespace
    CljObject *req_result = eval_string("(require 'clojure.string)", g_test_eval_state);
    (void)req_result; // require returns nil
    
    // Verify that clojure.string namespace exists
    CljNamespace *string_ns = ns_find("clojure.string");
    TEST_ASSERT_NOT_NULL(string_ns);
    
    // Verify that functions are available in the namespace
    // Check if blank? is defined
    CljSymbol *blank_sym = intern_symbol_global("blank?");
    TEST_ASSERT_NOT_NULL(blank_sym);
    
    // Try to resolve blank? from clojure.string namespace
    EvalState *temp_st = evalstate_new(false);
    evalstate_set_ns(temp_st, "clojure.string");
    (void)ns_resolve(temp_st, blank_sym);
    evalstate_free(temp_st);
    
    // blank? should be available (either as function or as nil if not yet loaded)
    // We just check that namespace exists and can be queried
    TEST_ASSERT_NOT_NULL(string_ns->mappings);
}

// Test: Check if :native functions are registered differently than regular functions
TEST(test_require_native_vs_regular_functions) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    init_special_symbols();
    
    // Load clojure.string namespace
    (void)eval_string("(require 'clojure.string)", g_test_eval_state);
    
    CljNamespace *string_ns = ns_find("clojure.string");
    TEST_ASSERT_NOT_NULL_MESSAGE(string_ns, "clojure.string namespace should exist");
    TEST_ASSERT_NOT_NULL_MESSAGE(string_ns->mappings, "clojure.string namespace should have mappings");
    
    // Test 1: Check blank? (regular function, NOT :native) - should be registered
    CljSymbol *blank_sym = intern_symbol_global("blank?");
    TEST_ASSERT_NOT_NULL_MESSAGE(blank_sym, "blank? symbol should be interned");
    static CljObject not_found_sentinel = { .type = CLJ_NIL, .rc = SINGLETON_RC };
    ID blank_func = map_get(string_ns->mappings, blank_sym, (ID)&not_found_sentinel);
    if (blank_func == (ID)&not_found_sentinel) {
        TEST_FAIL_MESSAGE("blank? (regular function) not found in clojure.string namespace");
    } else {
        TEST_ASSERT_NOT_NULL_MESSAGE(blank_func, "blank? should be registered");
    }
    
    // Test 2: Check trim (:native function) - might not be registered
    CljSymbol *trim_sym = intern_symbol_global("trim");
    TEST_ASSERT_NOT_NULL_MESSAGE(trim_sym, "trim symbol should be interned");
    ID trim_func = map_get(string_ns->mappings, trim_sym, (ID)&not_found_sentinel);
    if (trim_func == (ID)&not_found_sentinel) {
        TEST_FAIL_MESSAGE("trim (:native function) not found in clojure.string namespace - only :native functions are not registered");
    } else {
        TEST_ASSERT_NOT_NULL_MESSAGE(trim_func, "trim should be registered");
        TEST_ASSERT_TRUE_MESSAGE(TAG(trim_func) == CLJ_FUNC, "trim should be a native function");
    }
}

// Test: Verify that native functions are available in clojure.string
TEST(test_require_native_trim_available) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    (void)eval_string("(require 'clojure.string)", g_test_eval_state);
    
    // Test: trim should be available as native function
    CljObject *trim_result = eval_string("(clojure.string/trim \"  hello  \")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(trim_result);
    TEST_ASSERT_TRUE(TAG(trim_result) == CLJ_STRING);
    CljString *trim_str = as_clj_string(trim_result);
    TEST_ASSERT_EQUAL_STRING("hello", clj_string_data(trim_str));
}

// Test: Verify that require actually loads functions into namespace
TEST(test_require_loads_functions) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    CljObject *req_result = eval_string("(require 'clojure.string)", g_test_eval_state);
    (void)req_result; // require returns nil
    
    // Verify namespace exists
    CljNamespace *string_ns = ns_find("clojure.string");
    TEST_ASSERT_NOT_NULL(string_ns);
    TEST_ASSERT_NOT_NULL(string_ns->mappings);
    
    // First verify trim works (native function)
    CljObject *trim_result = eval_string("(clojure.string/trim \"  hello  \")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(trim_result);
    TEST_ASSERT_TRUE(TAG(trim_result) == CLJ_STRING);
}

// Test: Verify that blank? is loaded and can be resolved
TEST(test_require_blank_resolution) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    CljObject *req_result = eval_string("(require 'clojure.string)", g_test_eval_state);
    (void)req_result; // require returns nil
    
    // Verify namespace exists
    CljNamespace *string_ns = ns_find("clojure.string");
    TEST_ASSERT_NOT_NULL_MESSAGE(string_ns, "clojure.string namespace should exist after require");
    TEST_ASSERT_NOT_NULL_MESSAGE(string_ns->mappings, "clojure.string namespace should have mappings");
    
    // Check if blank? is defined in the namespace
    CljSymbol *blank_sym = intern_symbol_global("blank?");
    TEST_ASSERT_NOT_NULL_MESSAGE(blank_sym, "blank? symbol should be interned");
    
    // Try to resolve blank? from clojure.string namespace using ns_resolve
    EvalState *temp_st = evalstate_new(false);
    evalstate_set_ns(temp_st, "clojure.string");
    ID blank_func = ns_resolve(temp_st, blank_sym);
    evalstate_free(temp_st);
    
    // blank? should be available if it was loaded correctly
    TEST_ASSERT_NOT_NULL_MESSAGE(blank_func, "blank? should be resolvable from clojure.string namespace");
    
    // Verify it's a function
    if (blank_func) {
        TEST_ASSERT_TRUE_MESSAGE(TAG(blank_func) == CLJ_FUNC || TAG(blank_func) == CLJ_CLOSURE,
                                 "blank? should resolve to a function");
    }
}

// Test: Verify that blank? can be called via qualified symbol
TEST(test_require_blank_call) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    CljObject *req_result = eval_string("(require 'clojure.string)", g_test_eval_state);
    (void)req_result; // require returns nil
    
    // Test: (clojure.string/blank? "") => true
    CljObject *blank_result = eval_string("(clojure.string/blank? \"\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL_MESSAGE(blank_result, "blank? should return a result");
    TEST_ASSERT_TRUE_MESSAGE(blank_result == clj_true, "blank? should return true for empty string");
    
    // Test: (clojure.string/blank? "abc") => false
    CljObject *blank_result2 = eval_string("(clojure.string/blank? \"abc\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL_MESSAGE(blank_result2, "blank? should return a result");
    TEST_ASSERT_TRUE_MESSAGE(blank_result2 == clj_false, "blank? should return false for non-empty string");
}

// ============================================================================
// CLOJURE.STRING TESTS (after require)
// ============================================================================

TEST(test_string_blank_after_require) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    (void)eval_string("(require 'clojure.string)", g_test_eval_state);
    
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

TEST(test_string_capitalize_after_require) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    (void)eval_string("(require 'clojure.string)", g_test_eval_state);
    
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

TEST(test_string_ends_with_after_require) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    (void)eval_string("(require 'clojure.string)", g_test_eval_state);
    
    // Test: (clojure.string/ends-with? "hello" "lo") => true
    CljObject *result1 = eval_string("(clojure.string/ends-with? \"hello\" \"lo\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(result1 == clj_true);
    
    // Test: (clojure.string/ends-with? "hello" "x") => false
    CljObject *result2 = eval_string("(clojure.string/ends-with? \"hello\" \"x\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(result2 == clj_false);
}

TEST(test_string_includes_after_require) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    (void)eval_string("(require 'clojure.string)", g_test_eval_state);
    
    // Test: (clojure.string/includes? "hello" "ell") => true
    CljObject *result1 = eval_string("(clojure.string/includes? \"hello\" \"ell\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(result1 == clj_true);
    
    // Test: (clojure.string/includes? "hello" "xyz") => false
    CljObject *result2 = eval_string("(clojure.string/includes? \"hello\" \"xyz\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(result2 == clj_false);
}

TEST(test_string_index_of_after_require) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    (void)eval_string("(require 'clojure.string)", g_test_eval_state);
    
    // Test: (clojure.string/index-of "hello" "l" 0) => 2
    CljObject *result1 = eval_string("(clojure.string/index-of \"hello\" \"l\" 0)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(is_fixnum(result1));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(result1));
}

TEST(test_string_reverse_after_require) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    (void)eval_string("(require 'clojure.string)", g_test_eval_state);
    
    // Test: (clojure.string/reverse "abc") => "cba"
    CljObject *result1 = eval_string("(clojure.string/reverse \"abc\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(result1 && TAG(result1) == CLJ_STRING);
    CljString *str1 = as_clj_string(result1);
    TEST_ASSERT_EQUAL_STRING("cba", clj_string_data(str1));
}

// ============================================================================
// REVERSE CONFLICT TESTS
// ============================================================================

TEST(test_require_reverse_conflict_clojure_core) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    (void)eval_string("(require 'clojure.string)", g_test_eval_state);
    
    // Test: (clojure.core/reverse (list 1 2 3)) => (3 2 1)
    // This tests if clojure.core/reverse still works after loading clojure.string
    CljObject *result = eval_string("(clojure.core/reverse (list 1 2 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result && TAG(result) == CLJ_LIST);
    
    // Verify first element is 3
    CljList *list = as_list((ID)result);
    TEST_ASSERT_NOT_NULL(list);
    TEST_ASSERT_NOT_NULL(list->first);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)list->first));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)list->first));
}

TEST(test_require_reverse_conflict_clojure_string) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    (void)eval_string("(require 'clojure.string)", g_test_eval_state);
    
    // Test: (clojure.string/reverse "abc") => "cba"
    // This tests if clojure.string/reverse works for strings
    CljObject *result = eval_string("(clojure.string/reverse \"abc\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result && TAG(result) == CLJ_STRING);
    CljString *str = as_clj_string(result);
    TEST_ASSERT_EQUAL_STRING("cba", clj_string_data(str));
}

TEST(test_require_reverse_in_let_after_require) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    (void)eval_string("(require 'clojure.string)", g_test_eval_state);
    
    // Test: (let [step (fn [coll] (clojure.core/reverse coll))] (step (list 1 2 3)))
    // This tests if clojure.core/reverse works in let bindings after loading clojure.string
    CljObject *result = eval_string("(let [step (fn [coll] (clojure.core/reverse coll))] (step (list 1 2 3)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result && TAG(result) == CLJ_LIST);
    
    // Verify first element is 3
    CljList *list = as_list((ID)result);
    TEST_ASSERT_NOT_NULL(list);
    TEST_ASSERT_NOT_NULL(list->first);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)list->first));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)list->first));
}

TEST(test_require_reverse_in_recursive_function) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    (void)eval_string("(require 'clojure.string)", g_test_eval_state);
    
    // Test: (let [step (fn [coll acc] (if (empty? coll) (clojure.core/reverse acc) (step (rest coll) (cons (first coll) acc))))] (step (list 1 2 3) (list)))
    // This tests if clojure.core/reverse works in recursive functions after loading clojure.string
    CljObject *result = eval_string("(let [step (fn [coll acc] (if (empty? coll) (clojure.core/reverse acc) (step (rest coll) (cons (first coll) acc))))] (step (list 1 2 3) (list)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result && TAG(result) == CLJ_LIST);
    
    // Verify result is (1 2 3)
    CljList *list = as_list((ID)result);
    TEST_ASSERT_NOT_NULL(list);
    TEST_ASSERT_NOT_NULL(list->first);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)list->first));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum((CljValue)list->first));
}

TEST(test_require_both_reverse_functions) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    (void)eval_string("(require 'clojure.string)", g_test_eval_state);
    
    // Test both reverse functions work correctly
    // clojure.core/reverse for collections
    CljObject *core_result = eval_string("(clojure.core/reverse (list 1 2 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(core_result);
    TEST_ASSERT_TRUE(core_result && TAG(core_result) == CLJ_LIST);
    
    // clojure.string/reverse for strings
    CljObject *string_result = eval_string("(clojure.string/reverse \"abc\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(string_result);
    TEST_ASSERT_TRUE(string_result && TAG(string_result) == CLJ_STRING);
    CljString *str = as_clj_string(string_result);
    TEST_ASSERT_EQUAL_STRING("cba", clj_string_data(str));
}

// ============================================================================
// TEST: Funktionen mit Metadaten werden über require nicht registriert
// ============================================================================
TEST(test_require_metadata_functions_registered) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    init_special_symbols();
    
    // Create test directory structure
    const char *test_dir = "libs/test/meta";
    const char *test_file = "libs/test/meta/require.clj";
    
    // Create directory if it doesn't exist
    char mkdir_cmd[256];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", test_dir);
    (void)system(mkdir_cmd);
    
    // Create test file with function that has metadata
    const char *test_content = 
        "(ns test.meta.require)\n"
        "^#^{:doc \"test\"}\n"
        "(defn trim [s] :native)\n";
    
    FILE *fp = fopen(test_file, "w");
    if (!fp) {
        TEST_IGNORE_MESSAGE("Cannot create test file - skipping metadata test");
        return;
    }
    fprintf(fp, "%s", test_content);
    fclose(fp);
    
    // Test: Load via require
    CljObject *req_result = eval_string("(require 'test.meta.require)", g_test_eval_state);
    (void)req_result; // require returns nil
    
    // Check if namespace exists
    CljNamespace *test_ns_obj = ns_find("test.meta.require");
    TEST_ASSERT_NOT_NULL_MESSAGE(test_ns_obj, "test.meta.require namespace should exist after require");
    
    // Check if trim is registered
    CljSymbol *trim_sym = intern_symbol_global("trim");
    TEST_ASSERT_NOT_NULL_MESSAGE(trim_sym, "trim symbol should be interned");
    
    CljObject not_found_sentinel = { .type = CLJ_NIL, .rc = SINGLETON_RC };
    ID trim_func_require = map_get(test_ns_obj->mappings, trim_sym, (ID)&not_found_sentinel);
    
    // This test should FAIL if the bug exists (trim not found)
    if (trim_func_require == (ID)&not_found_sentinel) {
        TEST_FAIL_MESSAGE("trim not found in test.meta.require namespace after require - functions with metadata are not registered via require");
    } else {
        TEST_ASSERT_NOT_NULL_MESSAGE(trim_func_require, "trim should be registered after require");
        TEST_ASSERT_TRUE_MESSAGE(TAG(trim_func_require) == CLJ_FUNC, "trim should be a native function");
    }
    
    // Cleanup: remove test file
    remove(test_file);
}

// ============================================================================
// TEST: Vergleich: Funktionen mit Metadaten funktionieren mit direkter Datei-Ladung
// ============================================================================
TEST(test_file_load_metadata_functions_work) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    init_special_symbols();
    
    // Create test file with function that has metadata
    const char *test_file = "/tmp/test_meta_file.clj";
    
    const char *test_content = 
        "(ns test.meta.file)\n"
        "^#^{:doc \"test\"}\n"
        "(defn trim [s] :native)\n";
    
    FILE *fp = fopen(test_file, "w");
    if (!fp) {
        TEST_IGNORE_MESSAGE("Cannot create test file - skipping metadata file test");
        return;
    }
    fprintf(fp, "%s", test_content);
    fclose(fp);
    
    // Load file using eval_multiform_string (simulating -f FILE)
    // Read entire file
    FILE *read_fp = fopen(test_file, "r");
    if (!read_fp) {
        remove(test_file);
        TEST_IGNORE_MESSAGE("Cannot open test file for reading");
        return;
    }
    
    fseek(read_fp, 0, SEEK_END);
    long sz = ftell(read_fp);
    rewind(read_fp);
    char *buffer = (char*)malloc((size_t)sz + 1);
    if (!buffer) {
        fclose(read_fp);
        remove(test_file);
        TEST_IGNORE_MESSAGE("Cannot allocate buffer");
        return;
    }
    size_t n = fread(buffer, 1, (size_t)sz, read_fp);
    buffer[n] = '\0';
    fclose(read_fp);
    
    // Evaluate file content directly (like -f FILE does)
    // Use eval_multiform_string to evaluate all forms in the file
    bool load_success = eval_multiform_string(buffer, g_test_eval_state);
    free(buffer);
    if (!load_success) {
        remove(test_file);
        TEST_FAIL_MESSAGE("Failed to load test file");
        return;
    }
    
    // Check if namespace exists
    CljNamespace *test_ns_obj = ns_find("test.meta.file");
    TEST_ASSERT_NOT_NULL_MESSAGE(test_ns_obj, "test.meta.file namespace should exist after file load");
    
    // Check if trim is registered
    CljSymbol *trim_sym = intern_symbol_global("trim");
    TEST_ASSERT_NOT_NULL_MESSAGE(trim_sym, "trim symbol should be interned");
    
    CljObject not_found_sentinel = { .type = CLJ_NIL, .rc = SINGLETON_RC };
    ID trim_func_file = map_get(test_ns_obj->mappings, trim_sym, (ID)&not_found_sentinel);
    
    // This should PASS (file loading works)
    if (trim_func_file == (ID)&not_found_sentinel) {
        remove(test_file);
        TEST_FAIL_MESSAGE("trim not found in test.meta.file namespace after file load - file loading should work");
    } else {
        TEST_ASSERT_NOT_NULL_MESSAGE(trim_func_file, "trim should be registered after file load");
        TEST_ASSERT_TRUE_MESSAGE(TAG(trim_func_file) == CLJ_FUNC, "trim should be a native function");
    }
    
    // Cleanup: remove test file
    remove(test_file);
}

// Test: Verify that req-test namespace can be required and trim is available
TEST(test_require_req_test_trim) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load req-test namespace
    CljObject *req_result = eval_string("(require 'req-test)", g_test_eval_state);
    (void)req_result; // require returns nil
    
    // Verify that req-test namespace exists
    CljNamespace *req_test_ns = ns_find("req-test");
    TEST_ASSERT_NOT_NULL_MESSAGE(req_test_ns, "req-test namespace should exist after require");
    TEST_ASSERT_NOT_NULL_MESSAGE(req_test_ns->mappings, "req-test namespace should have mappings");
    
    // Check if join symbol exists in req-test namespace (now first function)
    CljSymbol *join_sym = intern_symbol_global("join");
    TEST_ASSERT_NOT_NULL_MESSAGE(join_sym, "join symbol should be interned");
    
    // Check if join is in namespace mappings
    static CljObject not_found_sentinel = { .type = CLJ_NIL, .rc = SINGLETON_RC };
    ID join_func = map_get(req_test_ns->mappings, join_sym, (ID)&not_found_sentinel);
    
    if (join_func == (ID)&not_found_sentinel) {
        TEST_FAIL_MESSAGE("join not found in req-test namespace mappings after require");
    } else {
        TEST_ASSERT_NOT_NULL_MESSAGE(join_func, "join should be in req-test namespace mappings");
        // join should be a Clojure function (not native)
        TEST_ASSERT_TRUE_MESSAGE(TAG(join_func) == CLJ_CLOSURE, "join should be a Clojure function in req-test");
    }
    
    // Test: Verify join can be called via qualified symbol
    CljObject *join_result = eval_string("(req-test/join \", \" [\"a\" \"b\" \"c\"])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL_MESSAGE(join_result, "join should be callable via req-test/join");
    TEST_ASSERT_TRUE_MESSAGE(TAG(join_result) == CLJ_STRING, "join result should be a string");
    CljString *join_str = as_clj_string(join_result);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("a, b, c", clj_string_data(join_str), "join should return correct result");
    
    // Check if trim symbol exists in req-test namespace (now second function)
    CljSymbol *trim_sym = intern_symbol_global("trim");
    TEST_ASSERT_NOT_NULL_MESSAGE(trim_sym, "trim symbol should be interned");
    
    // Check if trim is in namespace mappings
    ID trim_func = map_get(req_test_ns->mappings, trim_sym, (ID)&not_found_sentinel);
    
    if (trim_func == (ID)&not_found_sentinel) {
        TEST_FAIL_MESSAGE("trim not found in req-test namespace mappings after require");
    } else {
        TEST_ASSERT_NOT_NULL_MESSAGE(trim_func, "trim should be in req-test namespace mappings");
        // trim should be a Clojure function (not native, since it's a stub)
        TEST_ASSERT_TRUE_MESSAGE(TAG(trim_func) == CLJ_CLOSURE, "trim should be a Clojure function in req-test");
    }
    
    // Test: Verify trim can be called via qualified symbol
    CljObject *trim_result = eval_string("(req-test/trim \"  hello  \")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL_MESSAGE(trim_result, "trim should be callable via req-test/trim");
    // Note: The stub implementation just returns the string unchanged, so result should be "  hello  "
    TEST_ASSERT_TRUE_MESSAGE(TAG(trim_result) == CLJ_STRING, "trim result should be a string");
}

