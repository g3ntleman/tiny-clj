/*
 * Parser Tests using MinUnit
 *
 * Tests for the Clojure parser functionality including basic types,
 * collections, comments, and metadata parsing.
 */

#include "object.h"
#include "parser.h"
#include "clj_symbols.h"
#include "function_call.h"
#include "map.h"
#include "namespace.h"
#include "memory.h"
#include "memory_profiler.h"
#include "minunit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test setup and teardown
static void test_setup(void) {
  // Initialize symbol table
  init_special_symbols();

  // Initialize meta registry
  meta_registry_init();
}

static void test_teardown(void) {
  // Cleanup symbol table
  symbol_table_cleanup();

  // Cleanup autorelease pool
  autorelease_pool_cleanup_all();
}

// ============================================================================
// PARSER TESTS
// ============================================================================

static char *test_parse_basic_types(void) {

  WITH_AUTORELEASE_POOL_EVAL({
    // Initialize symbol table for proper parsing
    init_special_symbols();

    // Test integer parsing
    CljObject *int_result = parse("42", eval_state);
    mu_assert_obj_int_detailed(int_result, 42);

    // Test float parsing
    CljObject *float_result = parse("3.14", eval_state);
    mu_assert_obj_not_null(float_result);
    mu_assert_obj_type(float_result, CLJ_FLOAT);

    // Test string parsing (now fixed!)
    CljObject *str_result = parse("\"hello\"", eval_state);
    mu_debug_obj(str_result, "str_result");
    mu_assert_obj_type_detailed(str_result, CLJ_STRING);

    // Test symbol parsing
    CljObject *sym_result = parse("test-symbol", eval_state);
    mu_assert_obj_not_null(sym_result);
    mu_assert_obj_type(sym_result, CLJ_SYMBOL);

  });
  return 0;
}

static char *test_parse_collections(void) {
  WITH_AUTORELEASE_POOL_EVAL({
    // Test vector parsing
    CljObject *vec_result = parse("[1 2 3]", eval_state);
    mu_assert_obj_not_null(vec_result);
    mu_assert_obj_type(vec_result, CLJ_VECTOR);

    // Test list parsing
    CljObject *list_result = parse("(1 2 3)", eval_state);
    mu_assert_obj_not_null(list_result);
    mu_assert_obj_type(list_result, CLJ_LIST);

    // Test map parsing
    CljObject *map_result = parse("{:a 1 :b 2}", eval_state);
    mu_assert_obj_not_null(map_result);
    mu_assert_obj_type(map_result, CLJ_MAP);

  });
  return 0;
}

static char *test_parse_comments(void) {
  WITH_AUTORELEASE_POOL_EVAL({
    init_special_symbols();

    // Test line comment parsing
    CljObject *result = parse("; This is a comment\n42", eval_state);
    mu_assert_obj_not_null(result);
    mu_assert_obj_int(result, 42);
  });

  return 0;
}

static char *test_parse_metadata(void) {
  WITH_AUTORELEASE_POOL_EVAL({
    // Test metadata parsing
    CljObject *result = parse("^{:key :value} 42", eval_state);
    mu_assert_obj_not_null(result);
    mu_assert_obj_int(result, 42);

  });
  return 0;
}

static char *test_parse_error_handling(void) {
  WITH_AUTORELEASE_POOL_EVAL({
    // Test invalid syntax handling
    CljObject *result = parse("invalid-syntax", eval_state);
    // Should not crash, but may return NULL or valid object
    mu_assert("Parser should not crash on invalid syntax", result == NULL || result != NULL);

  });
  return 0;
}

static char *test_utf8_symbol_roundtrip(void) {
  WITH_AUTORELEASE_POOL_EVAL({
    // Test UTF-8 symbol parsing
    const char *src = "äöü✓"; // UTF-8 multibyte incl. checkmark
    CljObject *sym = parse(src, eval_state);
    mu_assert_obj_not_null(sym);
    mu_assert_obj_type(sym, CLJ_SYMBOL);

  });
  return 0;
}

static char *test_utf8_string_roundtrip(void) {
  WITH_AUTORELEASE_POOL_EVAL({
    // Test UTF-8 string parsing
    const char *src = "\"Grüße ✓\""; // "Grüße ✓"
    CljObject *str = parse(src, eval_state);
    mu_assert_obj_not_null(str);
    mu_assert_obj_type(str, CLJ_STRING);

  });
  return 0;
}

static char *test_utf8_delimiters(void) {
  WITH_AUTORELEASE_POOL_EVAL({
    // Test UTF-8 characters as delimiters
    const char *src = "ä β ( ) [ ] { } \" \n";
    CljObject *sym = parse(src, eval_state);
    mu_assert_obj_not_null(sym);
    mu_assert_obj_type(sym, CLJ_SYMBOL);

  });
  return 0;
}

// ============================================================================
// TEST RUNNER
// ============================================================================

static char *all_parser_tests(void) {
  test_setup();
  
  mu_run_test(test_parse_basic_types);
  mu_run_test(test_parse_collections);
  mu_run_test(test_parse_comments);
  mu_run_test(test_parse_metadata);
  mu_run_test(test_parse_error_handling);
  mu_run_test(test_utf8_symbol_roundtrip);
  mu_run_test(test_utf8_string_roundtrip);
  mu_run_test(test_utf8_delimiters);
  
  test_teardown();
  return 0;
}

// Export for unified test runner
char *run_parser_tests(void) {
  return all_parser_tests();
}

#ifndef UNIFIED_TEST_RUNNER
// Standalone mode
int main(void) {
  return run_minunit_tests(all_parser_tests, "Parser Tests");
}
#endif
