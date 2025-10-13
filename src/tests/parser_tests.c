/*
 * Parser Tests using Unity Framework
 * 
 * Tests for the Clojure parser functionality including basic types,
 * collections, comments, and metadata parsing.
 */

#include "unity.h"
#include "object.h"
#include "parser.h"
#include "symbol.h"
#include "function_call.h"
#include "map.h"
#include "namespace.h"
#include "memory.h"
#include "memory_profiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// TEST FIXTURES (setUp/tearDown defined in unity_test_runner.c)
// ============================================================================

// ============================================================================
// PARSER TESTS
// ============================================================================

void test_parse_basic_types(void) {
    WITH_AUTORELEASE_POOL({
        EvalState *eval_state = evalstate_new();
        
        // Test integer parsing
        CljObject *int_result = parse("42", eval_state);
        TEST_ASSERT_NOT_NULL(int_result);
        TEST_ASSERT_EQUAL_INT(42, int_result->as.i);
        
        // Test float parsing
        CljObject *float_result = parse("3.14", eval_state);
        TEST_ASSERT_NOT_NULL(float_result);
        TEST_ASSERT_EQUAL_INT(CLJ_FLOAT, float_result->type);
        TEST_ASSERT_EQUAL_FLOAT(3.14, float_result->as.f);
        
        // Test string parsing
        CljObject *str_result = parse("\"hello\"", eval_state);
        TEST_ASSERT_NOT_NULL(str_result);
        TEST_ASSERT_EQUAL_INT(CLJ_STRING, str_result->type);
        
        // Test symbol parsing
        CljObject *sym_result = parse("test-symbol", eval_state);
        TEST_ASSERT_NOT_NULL(sym_result);
        TEST_ASSERT_EQUAL_INT(CLJ_SYMBOL, sym_result->type);
        
        evalstate_free(eval_state);
    });
}

void test_parse_collections(void) {
    WITH_AUTORELEASE_POOL({
        EvalState *eval_state = evalstate_new();
        
        // Test vector parsing
        CljObject *vec_result = parse("[1 2 3]", eval_state);
        TEST_ASSERT_NOT_NULL(vec_result);
        TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, vec_result->type);
        
        // Test list parsing
        CljObject *list_result = parse("(1 2 3)", eval_state);
        TEST_ASSERT_NOT_NULL(list_result);
        TEST_ASSERT_EQUAL_INT(CLJ_LIST, list_result->type);
        
        // Test map parsing
        CljObject *map_result = parse("{:a 1 :b 2}", eval_state);
        TEST_ASSERT_NOT_NULL(map_result);
        TEST_ASSERT_EQUAL_INT(CLJ_MAP, map_result->type);
        
        evalstate_free(eval_state);
    });
}

void test_parse_comments(void) {
    WITH_AUTORELEASE_POOL({
        EvalState *eval_state = evalstate_new();
        
        // Test line comment parsing
        CljObject *result = parse("; This is a comment\n42", eval_state);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_EQUAL_INT(42, result->as.i);
        
        evalstate_free(eval_state);
    });
}

void test_parse_metadata(void) {
    WITH_AUTORELEASE_POOL({
        EvalState *eval_state = evalstate_new();
        
        // Test metadata parsing
        CljObject *result = parse("^{:key :value} 42", eval_state);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_EQUAL_INT(42, result->as.i);
        
        evalstate_free(eval_state);
    });
}

void test_parse_utf8_symbols(void) {
    WITH_AUTORELEASE_POOL({
        EvalState *eval_state = evalstate_new();
        
        // Test UTF-8 symbol parsing
        const char *src = "äöü✓"; // UTF-8 multibyte incl. checkmark
        CljObject *sym = parse(src, eval_state);
        TEST_ASSERT_NOT_NULL(sym);
        TEST_ASSERT_EQUAL_INT(CLJ_SYMBOL, sym->type);
        
        evalstate_free(eval_state);
    });
}

// ============================================================================
// TEST GROUPS
// ============================================================================

static void test_group_basic_parsing(void) {
    RUN_TEST(test_parse_basic_types);
    RUN_TEST(test_parse_collections);
}

static void test_group_advanced_parsing(void) {
    RUN_TEST(test_parse_comments);
    RUN_TEST(test_parse_metadata);
    RUN_TEST(test_parse_utf8_symbols);
}

// ============================================================================
// COMMAND LINE INTERFACE
// ============================================================================

static void print_usage(const char *program_name) {
    printf("Parser Tests for Tiny-CLJ\n");
    printf("Usage: %s [test_name]\n\n", program_name);
    printf("Available tests:\n");
    printf("  basic-types     Test basic type parsing\n");
    printf("  collections     Test collection parsing\n");
    printf("  comments        Test comment parsing\n");
    printf("  metadata        Test metadata parsing\n");
    printf("  utf8            Test UTF-8 symbol parsing\n");
    printf("  basic           Run basic parsing tests\n");
    printf("  advanced        Run advanced parsing tests\n");
    printf("  all             Run all tests (default)\n");
}

static void run_specific_test(const char *test_name) {
    if (strcmp(test_name, "basic-types") == 0) {
        RUN_TEST(test_parse_basic_types);
    } else if (strcmp(test_name, "collections") == 0) {
        RUN_TEST(test_parse_collections);
    } else if (strcmp(test_name, "comments") == 0) {
        RUN_TEST(test_parse_comments);
    } else if (strcmp(test_name, "metadata") == 0) {
        RUN_TEST(test_parse_metadata);
    } else if (strcmp(test_name, "utf8") == 0) {
        RUN_TEST(test_parse_utf8_symbols);
    } else if (strcmp(test_name, "basic") == 0) {
        RUN_TEST(test_group_basic_parsing);
    } else if (strcmp(test_name, "advanced") == 0) {
        RUN_TEST(test_group_advanced_parsing);
    } else if (strcmp(test_name, "all") == 0) {
        RUN_TEST(test_parse_basic_types);
        RUN_TEST(test_parse_collections);
        RUN_TEST(test_parse_comments);
        RUN_TEST(test_parse_metadata);
        RUN_TEST(test_parse_utf8_symbols);
    } else {
        printf("Unknown test: %s\n", test_name);
        printf("Use --help to see available tests\n");
    }
}

// ============================================================================
// TEST FUNCTIONS (no main function - called by unity_test_runner.c)
// ============================================================================
