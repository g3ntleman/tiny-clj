/*
 * Unit Tests using Unity Framework
 * 
 * Basic unit tests for Tiny-Clj core functionality migrated from MinUnit.
 */

#include "unity.h"
#include "object.h"
#include "parser.h"
#include "symbol.h"
#include "clj_string.h"
#include "exception.h"
#include "map.h"
#include "namespace.h"
#include "vector.h"
#include "value.h"
#include "memory.h"
#include "memory_profiler.h"
#include "builtins.h"
#include <stdio.h>
#include <stdlib.h>

// ============================================================================
// TEST FIXTURES (setUp/tearDown defined in unity_test_runner.c)
// ============================================================================

// ============================================================================
// BASIC FUNCTIONALITY TESTS
// ============================================================================

void test_list_count(void) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Test null pointer
        TEST_ASSERT_EQUAL_INT(0, list_count(NULL));

        // Test non-list object (this should not crash)
        CljObject *int_obj = make_int(42);
        TEST_ASSERT_EQUAL_INT(0, list_count(int_obj));
        RELEASE(int_obj);

        // Test empty list (clj_nil is not a list)
        CljObject *empty_list = clj_nil();
        TEST_ASSERT_EQUAL_INT(0, list_count(empty_list));
    }
}

void test_list_creation(void) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Test list creation
        CljObject *list = (CljObject*)make_list(NULL, NULL);
        TEST_ASSERT_NOT_NULL(list);
        TEST_ASSERT_EQUAL_INT(CLJ_LIST, list->type);
        // make_list(NULL, NULL) creates a list with one nil element
        int count = list_count(list);
        TEST_ASSERT_EQUAL_INT(1, count);
        
        RELEASE(list);
    }
}

void test_symbol_creation(void) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Test symbol creation
        CljObject *sym = make_symbol("test-symbol", "user");
        TEST_ASSERT_NOT_NULL(sym);
        TEST_ASSERT_EQUAL_INT(CLJ_SYMBOL, sym->type);
        
        RELEASE(sym);
    }
}

void test_string_creation(void) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Test string creation
        CljObject *str = make_string("hello world");
        TEST_ASSERT_NOT_NULL(str);
        TEST_ASSERT_EQUAL_INT(CLJ_STRING, str->type);
        
        RELEASE(str);
    }
}

void test_vector_creation(void) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Test vector creation
        CljObject *vec = make_vector(5, 1);
        TEST_ASSERT_NOT_NULL(vec);
        TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, vec->type);
        
        CljPersistentVector *vec_data = as_vector(vec);
        TEST_ASSERT_NOT_NULL(vec_data);
        TEST_ASSERT_EQUAL_INT(5, vec_data->capacity);
        
        RELEASE(vec);
    }
}

void test_map_creation(void) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Test map creation
        CljMap *map = make_map(16);
        TEST_ASSERT_NOT_NULL(map);
        TEST_ASSERT_EQUAL_INT(CLJ_MAP, map->base.type);
        
        RELEASE(map);
    }
}

void test_array_map_builtin(void) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        EvalState *eval_state = evalstate_new();
        register_builtins();
        
        // Test empty map: (array-map)
        CljObject *result0 = AUTORELEASE(parse("(array-map)", eval_state));
        CljObject *eval0 = AUTORELEASE(eval_expr_simple(result0, eval_state));
        TEST_ASSERT_EQUAL_INT(0, map_count(eval0));

        // Test single key-value: (array-map "a" 1)
        CljObject *result1 = AUTORELEASE(parse("(array-map \"a\" 1)", eval_state));
        CljObject *eval1 = AUTORELEASE(eval_expr_simple(result1, eval_state));
        TEST_ASSERT_EQUAL_INT(1, map_count(eval1));

        // Test multiple pairs: (array-map "a" 1 "b" 2)
        CljObject *result2 = AUTORELEASE(parse("(array-map \"a\" 1 \"b\" 2)", eval_state));
        CljObject *eval2 = AUTORELEASE(eval_expr_simple(result2, eval_state));
        TEST_ASSERT_EQUAL_INT(2, map_count(eval2));

        // Test with keywords: (array-map :a 1 :b 2)
        CljObject *result3 = AUTORELEASE(parse("(array-map :a 1 :b 2)", eval_state));
        CljObject *eval3 = AUTORELEASE(eval_expr_simple(result3, eval_state));
        TEST_ASSERT_EQUAL_INT(2, map_count(eval3));

        evalstate_free(eval_state);
    }
}

void test_integer_creation(void) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Test integer creation
        CljObject *int_obj = make_int(42);
        TEST_ASSERT_NOT_NULL(int_obj);
        TEST_ASSERT_EQUAL_INT(CLJ_INT, int_obj->type);
        TEST_ASSERT_EQUAL_INT(42, int_obj->as.i);
        
        RELEASE(int_obj);
    }
}

void test_float_creation(void) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Test float creation
        CljObject *float_obj = make_float(3.14);
        TEST_ASSERT_NOT_NULL(float_obj);
        TEST_ASSERT_EQUAL_INT(CLJ_FLOAT, float_obj->type);
        TEST_ASSERT_EQUAL_FLOAT(3.14, float_obj->as.f);
        
        RELEASE(float_obj);
    }
}

void test_nil_creation(void) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Test nil creation
        CljObject *nil_obj = clj_nil();
        TEST_ASSERT_NOT_NULL(nil_obj);
        TEST_ASSERT_EQUAL_INT(CLJ_NIL, nil_obj->type);
    }
}

// ============================================================================
// PARSER TESTS
// ============================================================================

// Parser tests moved to parser_tests.c to avoid duplication

// ============================================================================
// MEMORY MANAGEMENT TESTS
// ============================================================================

// Memory management tests moved to memory_tests.c to avoid duplication

// ============================================================================
// EXCEPTION HANDLING TESTS
// ============================================================================

// Exception handling tests moved to exception_tests.c to avoid duplication

// ============================================================================
// CLJVALUE API TESTS (Phase 0-2)
// ============================================================================

void test_cljvalue_immediate_helpers(void) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Test immediate value creation
        CljValue nil_val = make_nil();
        CljValue true_val = make_true();
        CljValue false_val = make_false();
        
        TEST_ASSERT_NOT_NULL(nil_val);
        TEST_ASSERT_NOT_NULL(true_val);
        TEST_ASSERT_NOT_NULL(false_val);
        
        // Test immediate value checking
        TEST_ASSERT_TRUE(is_nil(nil_val));
        TEST_ASSERT_TRUE(is_true(true_val));
        TEST_ASSERT_TRUE(is_false(false_val));
        TEST_ASSERT_FALSE(is_true(false_val));
        TEST_ASSERT_FALSE(is_false(true_val));
        TEST_ASSERT_FALSE(is_nil(true_val));
    }
}

void test_cljvalue_vector_api(void) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Test vector creation with CljValue API
        CljValue vec = make_vector_v(3, 0);  // capacity=3, immutable
        TEST_ASSERT_NOT_NULL(vec);
        TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, vec->type);
        
        // Test vector conj with CljValue API
        CljValue item = make_int_v(42);
        CljValue result = vector_conj_v(vec, item);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(vec != result);  // Should be new vector
        
        RELEASE(item);
        RELEASE(result);
    }
}

void test_cljvalue_transient_vector(void) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Test transient conversion
        CljValue vec = make_vector_v(3, 0);
        CljValue tvec = transient(vec);
        
        TEST_ASSERT_NOT_NULL(tvec);
        TEST_ASSERT_EQUAL_INT(CLJ_TRANSIENT_VECTOR, tvec->type);
        TEST_ASSERT_TRUE(vec != tvec);  // Should be copy
        
        // Test transient conj! (in-place mutation)
        CljValue item1 = make_int_v(1);
        CljValue item2 = make_int_v(2);
        
        CljValue result1 = conj_v(tvec, item1);
        TEST_ASSERT_EQUAL_PTR(tvec, result1);  // Same instance (in-place)
        
        CljValue result2 = conj_v(tvec, item2);
        TEST_ASSERT_EQUAL_PTR(tvec, result2);  // Same instance (in-place)
        
        // Test persistent! conversion (new instance)
        CljValue pvec = persistent_v(tvec);
        TEST_ASSERT_NOT_NULL(pvec);
        TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, pvec->type);
        TEST_ASSERT_TRUE(tvec != pvec);  // Different instance
        
        RELEASE(item1);
        RELEASE(item2);
        RELEASE(pvec);
    }
}

void test_cljvalue_clojure_semantics(void) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Test Clojure-like usage pattern
        CljValue v1 = make_vector_v(3, 0);
        CljValue tv = transient(v1);
        
        // Add elements to transient
        conj_v(tv, make_int_v(1));
        conj_v(tv, make_int_v(2));
        conj_v(tv, make_int_v(3));
        
        // Convert back to persistent
        CljValue v2 = persistent_v(tv);
        
        // v1 should be unchanged (original vector)
        TEST_ASSERT_NOT_NULL(v1);
        TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, v1->type);
        
        // v2 should be new persistent vector
        TEST_ASSERT_NOT_NULL(v2);
        TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, v2->type);
        TEST_ASSERT_TRUE(v1 != v2);
        TEST_ASSERT_TRUE(tv != v2);
        
        RELEASE(v2);
    }
}

void test_cljvalue_wrapper_functions(void) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Test wrapper functions for existing APIs
        CljValue int_val = make_int_v(42);
        CljValue float_val = make_float_v(3.14);
        CljValue str_val = make_string_v("hello");
        CljValue sym_val = make_symbol_v("test", NULL);
        
        TEST_ASSERT_NOT_NULL(int_val);
        TEST_ASSERT_NOT_NULL(float_val);
        TEST_ASSERT_NOT_NULL(str_val);
        TEST_ASSERT_NOT_NULL(sym_val);
        
        // Test that they work with existing APIs
        TEST_ASSERT_EQUAL_INT(CLJ_INT, int_val->type);
        TEST_ASSERT_EQUAL_INT(CLJ_FLOAT, float_val->type);
        TEST_ASSERT_EQUAL_INT(CLJ_STRING, str_val->type);
        TEST_ASSERT_EQUAL_INT(CLJ_SYMBOL, sym_val->type);
        
        RELEASE(int_val);
        RELEASE(float_val);
        RELEASE(str_val);
        RELEASE(sym_val);
    }
}

// ============================================================================
// TEST FUNCTIONS (no main function - called by unity_test_runner.c)
// ============================================================================
