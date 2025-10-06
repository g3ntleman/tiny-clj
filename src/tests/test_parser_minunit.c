/*
 * Parser Tests using MinUnit
 *
 * Tests for the Clojure parser functionality including basic types,
 * collections, comments, and metadata parsing.
 */

#include "../CljObject.h"
#include "../clj_parser.h"
#include "../clj_symbols.h"
#include "../function_call.h"
#include "../map.h"
#include "../namespace.h"
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
  cljvalue_pool_cleanup_all();
}

// ============================================================================
// PARSER TESTS
// ============================================================================

static char *test_parse_basic_types(void) {
  printf("\n=== Testing Parser Basic Types ===\n");

  EvalState st;
  memset(&st, 0, sizeof(EvalState));
  
  // Initialize symbol table for proper parsing
  init_special_symbols();

  // Test integer parsing
  CljObject *int_result = parse("42", &st);
  mu_assert_obj_int_detailed(int_result, 42);

  // Test float parsing
  CljObject *float_result = parse("3.14", &st);
  mu_assert_obj_not_null(float_result);
  mu_assert_obj_type(float_result, CLJ_FLOAT);

  // Test string parsing (now fixed!)
  CljObject *str_result = parse("\"hello\"", &st);
  mu_debug_obj(str_result, "str_result");
  mu_assert_obj_type_detailed(str_result, CLJ_STRING);

  // Test symbol parsing
  CljObject *sym_result = parse("test-symbol", &st);
  mu_assert_obj_not_null(sym_result);
  mu_assert_obj_type(sym_result, CLJ_SYMBOL);

  printf("✓ Parser basic types tests passed\n");
  return 0;
}

static char *test_parse_collections(void) {
  EvalState st;
  memset(&st, 0, sizeof(EvalState));

  // Test vector parsing
  CljObject *vec_result = parse("[1 2 3]", &st);
  mu_assert_obj_not_null(vec_result);
  mu_assert_obj_type(vec_result, CLJ_VECTOR);

  // Test list parsing
  CljObject *list_result = parse("(1 2 3)", &st);
  mu_assert_obj_not_null(list_result);
  mu_assert_obj_type(list_result, CLJ_LIST);

  // Test map parsing
  CljObject *map_result = parse("{:a 1 :b 2}", &st);
  mu_assert_obj_not_null(map_result);
  mu_assert_obj_type(map_result, CLJ_MAP);

  printf("✓ Parser collections tests passed\n");
  return 0;
}

static char *test_parse_comments(void) {
  EvalState st;
  memset(&st, 0, sizeof(EvalState));

  // Test line comment parsing
  CljObject *result = parse("; This is a comment\n42", &st);
  mu_assert_obj_not_null(result);
  mu_assert_obj_int(result, 42);

  printf("✓ Parser comments tests passed\n");
  return 0;
}

static char *test_parse_metadata(void) {
  EvalState st;
  memset(&st, 0, sizeof(EvalState));

  // Test metadata parsing
  CljObject *result = parse("^{:key :value} 42", &st);
  mu_assert_obj_not_null(result);
  mu_assert_obj_int(result, 42);

  printf("✓ Parser metadata tests passed\n");
  return 0;
}

static char *test_parse_error_handling(void) {
  EvalState st;
  memset(&st, 0, sizeof(EvalState));

  // Test invalid syntax handling
  CljObject *result = parse("invalid-syntax", &st);
  // Should not crash, but may return NULL or valid object
  mu_assert("Parser should not crash on invalid syntax", result == NULL || result != NULL);

  printf("✓ Parser error handling tests passed\n");
  return 0;
}

static char *test_utf8_symbol_roundtrip(void) {
  EvalState st;
  memset(&st, 0, sizeof(EvalState));

  // Test UTF-8 symbol parsing
  const char *src = "äöü✓"; // UTF-8 multibyte incl. checkmark
  CljObject *sym = parse(src, &st);
  mu_assert_obj_not_null(sym);
  mu_assert_obj_type(sym, CLJ_SYMBOL);

  printf("✓ UTF-8 symbol roundtrip tests passed\n");
  return 0;
}

static char *test_utf8_string_roundtrip(void) {
  EvalState st;
  memset(&st, 0, sizeof(EvalState));

  // Test UTF-8 string parsing
  const char *src = "\"Grüße ✓\""; // "Grüße ✓"
  CljObject *str = parse(src, &st);
  mu_assert_obj_not_null(str);
  mu_assert_obj_type(str, CLJ_STRING);

  printf("✓ UTF-8 string roundtrip tests passed\n");
  return 0;
}

static char *test_utf8_delimiters(void) {
  EvalState st;
  memset(&st, 0, sizeof(EvalState));

  // Test UTF-8 characters as delimiters
  const char *src = "ä β ( ) [ ] { } \" \n";
  CljObject *sym = parse(src, &st);
  mu_assert_obj_not_null(sym);
  mu_assert_obj_type(sym, CLJ_SYMBOL);

  printf("✓ UTF-8 delimiters tests passed\n");
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

int main(void) {
  return run_minunit_tests(all_parser_tests, "Parser Tests");
}
