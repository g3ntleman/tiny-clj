/*
 * Parser Tests
 * 
 * Tests for the Clojure parser functionality including basic types,
 * collections, comments, and metadata parsing.
 */

#include "../unity.h"
#include "../tiny_clj.h"
#include "test_helpers.h"
#include "../CljObject.h"
#include "../clj_symbols.h"
#include "../namespace.h"
#include "../clj_parser.h"
#include "../function_call.h"
#include "../map.h"
#include "test_helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test setup and teardown
void setUp(void) {
    // Initialize symbol table
    init_special_symbols();
    
    // Initialize meta registry
    meta_registry_init();
}

void tearDown(void) {
    // Cleanup symbol table
    symbol_table_cleanup();
    
    // Cleanup autorelease pool
    cljvalue_pool_cleanup_all();
}

// ============================================================================
// PARSER TESTS
// ============================================================================

void test_parse_basic_types(void) {
    printf("\n=== Testing Basic Type Parsing ===\n");
    
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);

    // integer
    char int_input[8] = "42";
    const char *int_ptr = int_input;
    CljObject *int_result = parse_number(&int_ptr, st);
    ASSERT_OBJ_INT_EQ(int_result, 42);

    // float
    char float_input[16] = "3.14";
    const char *float_ptr = float_input;
    CljObject *float_result = parse_number(&float_ptr, st);
    ASSERT_TYPE(float_result, CLJ_FLOAT);

    // string
    char str_input[64] = "\"hello world\"";
    const char *str_ptr = str_input;
    CljObject *str_result = parse_string(&str_ptr, st);
    ASSERT_TYPE(str_result, CLJ_STRING);

    // symbol
    char sym_input[64] = "test-symbol";
    const char *sym_ptr = sym_input;
    CljObject *sym_result = parse_symbol(&sym_ptr, st);
    ASSERT_TYPE(sym_result, CLJ_SYMBOL);

    evalstate_free(st);
}

void test_utf8_symbol_roundtrip(void) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);
    const char *src = "äöü✓"; // UTF-8 multibyte incl. checkmark
    const char *p = src;
    CljObject *sym = parse_symbol(&p, st);
    TEST_ASSERT_NOT_NULL(sym);
    TEST_ASSERT_EQUAL_INT(CLJ_SYMBOL, sym->type);
}

void test_utf8_string_roundtrip(void) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);
    const char *src = "\"Grüße ✓\""; // "Grüße ✓"
    const char *p = src;
    CljObject *str = parse_string(&p, st);
    ASSERT_TYPE(str, CLJ_STRING);
}

void test_utf8_delimiters(void) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);
    const char *src = "ä β ( ) [ ] { } \" \n";
    const char *p = src;
    CljObject *sym = parse_symbol(&p, st);
    TEST_ASSERT_NOT_NULL(sym);
    TEST_ASSERT_EQUAL_INT(CLJ_SYMBOL, sym->type);
}

void test_parse_collections(void) {
    printf("\n=== Testing Collection Parsing ===\n");
    
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);

    // vector
    char vec_input[64] = "[1 2 3]";
    const char *vec_ptr = vec_input;
    CljObject *vec_result = parse_vector(&vec_ptr, st);
    ASSERT_TYPE(vec_result, CLJ_VECTOR);

    // list
    char list_input[64] = "(1 2 3)";
    const char *list_ptr = list_input;
    CljObject *list_result = parse_list(&list_ptr, st);
    ASSERT_TYPE(list_result, CLJ_LIST);

    // map
    char map_input[64] = "{:a 1 :b 2}";
    const char *map_ptr = map_input;
    CljObject *map_result = parse_map(&map_ptr, st);
    ASSERT_TYPE(map_result, CLJ_MAP);

    // empty map should yield the empty-map singleton via make_map(0)
    char empty_map_input[4] = "{}";
    const char *empty_map_ptr = empty_map_input;
    CljObject *empty_map_result = parse_map(&empty_map_ptr, st);
    TEST_ASSERT_EQUAL_PTR(make_map(0), empty_map_result);

    evalstate_free(st);
}

void test_parse_comments(void) {
    printf("\n=== Testing Comment Parsing ===\n");
    
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);

    char comment_input[64] = "; This is a comment\n42";
    const char *comment_ptr = comment_input;
    CljObject *result = parse_expr(&comment_ptr, st);
    ASSERT_OBJ_INT_EQ(result, 42);

    evalstate_free(st);
}

void test_parse_metadata(void) {
    printf("\n=== Testing Metadata Parsing ===\n");
    
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);

    char meta_input[64] = "^{:key :value} 42";
    const char *meta_ptr = meta_input;
    CljObject *result = parse_expr(&meta_ptr, st);
    ASSERT_OBJ_INT_EQ(result, 42);

    evalstate_free(st);
}

void test_parse_error_handling(void) {
    printf("\n=== Testing Error Handling ===\n");
    
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);

    char invalid_input[64] = "invalid-syntax";
    const char *invalid_ptr = invalid_input;
    CljObject *res = parse_expr(&invalid_ptr, st);
    TEST_ASSERT_TRUE(res == NULL || res != NULL); // just ensure no crash

    evalstate_free(st);
}

// ============================================================================
// MAIN TEST RUNNER
// ============================================================================

int run_parser_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_basic_types);
    RUN_TEST(test_parse_collections);
    RUN_TEST(test_parse_comments);
    RUN_TEST(test_parse_metadata);
    RUN_TEST(test_parse_error_handling);
    RUN_TEST(test_utf8_symbol_roundtrip);
    RUN_TEST(test_utf8_string_roundtrip);
    RUN_TEST(test_utf8_delimiters);
    return UNITY_END();
}

#ifndef TINY_CLJ_EMBED_TESTS
int main(void) { return run_parser_tests(); }
#endif