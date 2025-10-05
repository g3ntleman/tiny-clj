/*
 * Simple Unit Tests using MinUnit
 *
 * Basic unit tests for Tiny-Clj core functionality
 */

#include "../CljObject.h"
#include "../clj_parser.h"
#include "../clj_symbols.h"
#include "../exception.h"
#include "../map.h"
#include "../namespace.h"
#include "../vector.h"
#include "minunit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test setup and teardown functions
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
// BASIC FUNCTIONALITY TESTS
// ============================================================================

static char *test_basic_creation(void) {
  printf("\n=== Testing Basic Object Creation ===\n");

  // Test integer creation
  CljObject *int_obj = make_int(42);
  mu_assert_obj_type(int_obj, CLJ_INT);
  mu_assert_obj_int(int_obj, 42);

  // Test string creation
  CljObject *str_obj = make_string("hello");
  mu_assert_obj_type(str_obj, CLJ_STRING);
  mu_assert_obj_string(str_obj, "hello");
  
  // Test float creation
  CljObject *float_obj = make_float(3.14);
  mu_assert_obj_type(float_obj, CLJ_FLOAT);
  
  printf("✓ Basic object creation tests passed\n");
  return 0;
}

static char *test_boolean_creation(void) {
  // Test boolean creation using singleton
  CljObject *bool_obj = clj_true();
  mu_assert_obj_type(bool_obj, CLJ_BOOL);
  mu_assert_obj_bool(bool_obj, true);

  printf("✓ Boolean creation tests passed\n");
  return 0;
}

static char *test_singleton_objects(void) {
  printf("\n=== Testing Singleton Objects ===\n");

  // Test nil singleton
  CljObject *nil1 = clj_nil();
  CljObject *nil2 = clj_nil();
  mu_assert_obj_ptr_equal(nil1, nil2);

  // Test boolean singletons
  CljObject *true1 = clj_true();
  CljObject *true2 = clj_true();
  mu_assert_obj_ptr_equal(true1, true2);

  CljObject *false1 = clj_false();
  CljObject *false2 = clj_false();
  mu_assert_obj_ptr_equal(false1, false2);

  printf("✓ Singleton objects tests passed\n");
  return 0;
}

static char *test_empty_vector_singleton(void) {
  // make_vector(0, ...) returns the empty-vector singleton
  CljObject *v0 = make_vector(0, 0);
  mu_assert_obj_not_null(v0);
  mu_assert_obj_type(v0, CLJ_VECTOR);

  // capacity <= 0 also returns the same singleton
  CljObject *vneg = make_vector(-1, 1);
  mu_assert_obj_ptr_equal(v0, vneg);

  // Parsing [] should yield the same singleton pointer
  EvalState st;
  memset(&st, 0, sizeof(EvalState));
  CljObject *parsed = parse("[]", &st);
  mu_assert_obj_ptr_equal(v0, parsed);

  printf("✓ Empty vector singleton tests passed\n");
  return 0;
}

static char *test_empty_map_singleton(void) {
  // make_map(0) returns the empty-map singleton
  CljObject *m0 = make_map(0);
  mu_assert_obj_ptr_equal(m0, make_map(0));
  mu_assert_obj_ptr_equal(m0, make_map(-1));

  printf("✓ Empty map singleton tests passed\n");
  return 0;
}

// ============================================================================
// PARSER TESTS
// ============================================================================

static char *test_parser_basic_types(void) {
  printf("\n=== Testing Parser Basic Types ===\n");

  EvalState st;
  memset(&st, 0, sizeof(EvalState));

  // Test integer parsing
  CljObject *int_result = parse("42", &st);
  if (int_result == NULL) return "parse returned NULL for integer";
  if (int_result->type != CLJ_INT) return "wrong type for integer";
  if (int_result->as.i != 42) return "wrong integer value";

  // Test float parsing
  CljObject *float_result = parse("3.14", &st);
  if (float_result == NULL) return "parse returned NULL for float";
  if (float_result->type != CLJ_FLOAT) return "wrong type for float";

  // Test string parsing
  CljObject *str_result = parse("\"hello\"", &st);
  if (str_result == NULL) return "parse returned NULL for string";
  if (str_result->type != CLJ_STRING) return "wrong type for string";

  // Test symbol parsing
  CljObject *sym_result = parse("test-symbol", &st);
  if (sym_result == NULL) return "parse returned NULL for symbol";
  if (sym_result->type != CLJ_SYMBOL) return "wrong type for symbol";

  printf("✓ Parser basic types tests passed\n");
  return 0;
}

static char *test_parser_collections(void) {
  EvalState st;
  memset(&st, 0, sizeof(EvalState));

  // Test vector parsing
  CljObject *vec_result = parse("[1 2 3]", &st);
  mu_assert_obj_type(vec_result, CLJ_VECTOR);

  // Test list parsing
  CljObject *list_result = parse("(1 2 3)", &st);
  mu_assert_obj_type(list_result, CLJ_LIST);

  // Test map parsing
  CljObject *map_result = parse("{:a 1 :b 2}", &st);
  mu_assert_obj_type(map_result, CLJ_MAP);

  printf("✓ Parser collections tests passed\n");
  return 0;
}

// ============================================================================
// TEST RUNNER
// ============================================================================

static char *all_unit_tests(void) {
  test_setup();
  
  mu_run_test(test_basic_creation);
  mu_run_test(test_boolean_creation);
  mu_run_test(test_singleton_objects);
  mu_run_test(test_empty_vector_singleton);
  mu_run_test(test_empty_map_singleton);
  mu_run_test(test_parser_basic_types);
  mu_run_test(test_parser_collections);
  
  test_teardown();
  return 0;
}

int main(void) {
  return run_minunit_tests(all_unit_tests, "Unit Tests");
}
