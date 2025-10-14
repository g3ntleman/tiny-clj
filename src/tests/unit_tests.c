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
    WITH_AUTORELEASE_POOL({
        // Test null pointer
        TEST_ASSERT_EQUAL_INT(0, list_count(NULL));

        // Test non-list object (this should not crash)
        CljObject *int_obj = make_int(42);
        TEST_ASSERT_EQUAL_INT(0, list_count(int_obj));
        RELEASE(int_obj);

        // Test empty list (clj_nil is not a list)
        CljObject *empty_list = clj_nil();
        TEST_ASSERT_EQUAL_INT(0, list_count(empty_list));
    });
}

void test_list_creation(void) {
    WITH_AUTORELEASE_POOL({
        // Test list creation
        CljObject *list = (CljObject*)make_list(NULL, NULL);
        TEST_ASSERT_NOT_NULL(list);
        TEST_ASSERT_EQUAL_INT(CLJ_LIST, list->type);
        // make_list(NULL, NULL) creates a list with one nil element
        int count = list_count(list);
        TEST_ASSERT_EQUAL_INT(1, count);
        
        RELEASE(list);
    });
}

void test_symbol_creation(void) {
    WITH_AUTORELEASE_POOL({
        // Test symbol creation
        CljObject *sym = make_symbol("test-symbol", "user");
        TEST_ASSERT_NOT_NULL(sym);
        TEST_ASSERT_EQUAL_INT(CLJ_SYMBOL, sym->type);
        
        RELEASE(sym);
    });
}

void test_string_creation(void) {
    WITH_AUTORELEASE_POOL({
        // Test string creation
        CljObject *str = make_string("hello world");
        TEST_ASSERT_NOT_NULL(str);
        TEST_ASSERT_EQUAL_INT(CLJ_STRING, str->type);
        
        RELEASE(str);
    });
}

void test_vector_creation(void) {
    WITH_AUTORELEASE_POOL({
        // Test vector creation
        CljObject *vec = make_vector(5, 1);
        TEST_ASSERT_NOT_NULL(vec);
        TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, vec->type);
        
        CljPersistentVector *vec_data = as_vector(vec);
        TEST_ASSERT_NOT_NULL(vec_data);
        TEST_ASSERT_EQUAL_INT(5, vec_data->capacity);
        
        RELEASE(vec);
    });
}

void test_map_creation(void) {
    WITH_AUTORELEASE_POOL({
        // Test map creation
        CljObject *map = make_map(16);
        TEST_ASSERT_NOT_NULL(map);
        TEST_ASSERT_EQUAL_INT(CLJ_MAP, map->type);
        
        RELEASE(map);
    });
}

void test_integer_creation(void) {
    WITH_AUTORELEASE_POOL({
        // Test integer creation
        CljObject *int_obj = make_int(42);
        TEST_ASSERT_NOT_NULL(int_obj);
        TEST_ASSERT_EQUAL_INT(CLJ_INT, int_obj->type);
        TEST_ASSERT_EQUAL_INT(42, int_obj->as.i);
        
        RELEASE(int_obj);
    });
}

void test_float_creation(void) {
    WITH_AUTORELEASE_POOL({
        // Test float creation
        CljObject *float_obj = make_float(3.14);
        TEST_ASSERT_NOT_NULL(float_obj);
        TEST_ASSERT_EQUAL_INT(CLJ_FLOAT, float_obj->type);
        TEST_ASSERT_EQUAL_FLOAT(3.14, float_obj->as.f);
        
        RELEASE(float_obj);
    });
}

void test_nil_creation(void) {
    WITH_AUTORELEASE_POOL({
        // Test nil creation
        CljObject *nil_obj = clj_nil();
        TEST_ASSERT_NOT_NULL(nil_obj);
        TEST_ASSERT_EQUAL_INT(CLJ_NIL, nil_obj->type);
    });
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
// TEST FUNCTIONS (no main function - called by unity_test_runner.c)
// ============================================================================
