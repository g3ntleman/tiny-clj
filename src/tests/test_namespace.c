#include "../unity.h"
#include "../tiny_clj.h"
#include "../CljObject.h"
#include "../clj_symbols.h"
#include "../namespace.h"
#include "../map.h"
#include <stdlib.h>

void setUp(void) {
    // Setup before each test
    init_special_symbols();
    meta_registry_init();
}

void tearDown(void) {
    // Cleanup after each test
    meta_registry_cleanup();
    cljvalue_pool_cleanup_all();
}

void test_evalstate_creation(void) {
    EvalState *state = evalstate_new();
    
    TEST_ASSERT_NOT_NULL(state);
    TEST_ASSERT_NOT_NULL(state->current_ns);  // evalstate_new() initializes default "user" namespace
    TEST_ASSERT_NULL(state->last_error);
    
    evalstate_free(state);
}

void test_evalstate_set_ns(void) {
    EvalState *state = evalstate_new();
    
    // Test setting namespace
    evalstate_set_ns(state, "test.namespace");
    TEST_ASSERT_NOT_NULL(state->current_ns);
    // Check that namespace name is a symbol
    TEST_ASSERT_EQUAL(CLJ_SYMBOL, state->current_ns->name->type);
    
    evalstate_free(state);
}

void test_ns_get_or_create(void) {
    // Test creating new namespace
    CljNamespace *ns1 = ns_get_or_create("new.namespace", "test_namespace_simple_unity.c");
    TEST_ASSERT_NOT_NULL(ns1);
    // Check that namespace name is a symbol
    TEST_ASSERT_EQUAL(CLJ_SYMBOL, ns1->name->type);
    TEST_ASSERT_NOT_NULL(ns1->mappings);
    
    // Test getting existing namespace
    CljNamespace *ns2 = ns_get_or_create("new.namespace", "test_namespace_simple_unity.c");
    TEST_ASSERT_EQUAL_PTR(ns1, ns2); // Should be the same pointer
    
    // Test creating different namespace
    CljNamespace *ns3 = ns_get_or_create("other.namespace", "test_namespace_simple_unity.c");
    TEST_ASSERT_NOT_NULL(ns3);
    TEST_ASSERT_TRUE(ns1 != ns3);
}

void test_map_operations(void) {
    // Test map creation and basic operations
    CljObject *map = autorelease(make_map(4));
    CljMap *map_data = as_map(map);
    
    TEST_ASSERT_EQUAL_INT(0, map_data->count);
    TEST_ASSERT_EQUAL_INT(4, map_data->capacity);
    
    // Test map_assoc
    CljObject *key = autorelease(make_string("test-key"));
    CljObject *value = autorelease(make_int(42));
    
    map_assoc(map, key, value);
    TEST_ASSERT_EQUAL_INT(1, map_data->count);
    
    // Test map_get
    CljObject *retrieved = map_get(map, key);
    TEST_ASSERT_NOT_NULL(retrieved);
    TEST_ASSERT_TRUE(clj_equal(retrieved, value));
    
    // Test map_get with non-existent key
    CljObject *other_key = autorelease(make_string("other-key"));
    CljObject *not_found = map_get(map, other_key);
    TEST_ASSERT_NULL(not_found);
}

void test_namespace_isolation(void) {
    // Test that different namespaces are isolated
    CljNamespace *ns1 = ns_get_or_create("namespace1", "test_namespace_simple_unity.c");
    CljNamespace *ns2 = ns_get_or_create("namespace2", "test_namespace_simple_unity.c");
    
    // Add same symbol to both namespaces with different values
    CljObject *sym = autorelease(make_symbol("shared-symbol", NULL));
    CljObject *val1 = autorelease(make_int(100));
    CljObject *val2 = autorelease(make_int(200));
    
    map_assoc(ns1->mappings, sym, val1);
    map_assoc(ns2->mappings, sym, val2);
    
    // Test that values are different
    CljObject *found1 = map_get(ns1->mappings, sym);
    CljObject *found2 = map_get(ns2->mappings, sym);
    
    TEST_ASSERT_NOT_NULL(found1);
    TEST_ASSERT_NOT_NULL(found2);
    TEST_ASSERT_FALSE(clj_equal(found1, found2));
    TEST_ASSERT_TRUE(clj_equal(found1, val1));
    TEST_ASSERT_TRUE(clj_equal(found2, val2));
}

void test_eval_expr_simple_atoms(void) {
    EvalState *state = evalstate_new();
    evalstate_set_ns(state, "test.eval");
    
    // Test integer evaluation
    CljObject *int_obj = autorelease(make_int(42));
    CljObject *result = eval_expr_simple(int_obj, state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(clj_equal(int_obj, result));
    
    // Test string evaluation
    CljObject *str_obj = autorelease(make_string("hello"));
    result = eval_expr_simple(str_obj, state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(clj_equal(str_obj, result));
    
    // Test nil evaluation
    result = eval_expr_simple(clj_nil(), state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(clj_equal(clj_nil(), result));
    
    evalstate_free(state);
}

void test_eval_expr_simple_symbols(void) {
    EvalState *state = evalstate_new();
    evalstate_set_ns(state, "test.eval");
    
    // Add symbol to namespace
    CljObject *sym = autorelease(make_symbol("test-symbol", NULL));
    CljObject *value = autorelease(make_int(123));
    map_assoc(state->current_ns->mappings, sym, value);
    
    // Test symbol evaluation
    CljObject *result = eval_expr_simple(sym, state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(clj_equal(value, result));
    
    // Test undefined symbol
    CljObject *undefined = autorelease(make_symbol("undefined", NULL));
    result = eval_expr_simple(undefined, state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(clj_equal(undefined, result)); // Should return symbol itself
    
    evalstate_free(state);
}

int main(void) {
    printf("=== Unity Test Suite for Namespace (Simple) ===\n");
    
    UNITY_BEGIN();
    
    // EvalState tests
    RUN_TEST(test_evalstate_creation);
    RUN_TEST(test_evalstate_set_ns);
    
    // Namespace management tests
    RUN_TEST(test_ns_get_or_create);
    RUN_TEST(test_namespace_isolation);
    
    // Map operation tests
    RUN_TEST(test_map_operations);
    
    // Expression evaluation tests
    RUN_TEST(test_eval_expr_simple_atoms);
    RUN_TEST(test_eval_expr_simple_symbols);
    
    return UNITY_END();
}
