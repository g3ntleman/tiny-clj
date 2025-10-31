/*
 * Simple Unit Tests using MinUnit
 *
 * Basic unit tests for Tiny-Clj core functionality
 */

#include "../object.h"
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

  CLJVALUE_POOL_SCOPE(pool) {
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
    
  }
  return 0;
}

static char *test_boolean_creation(void) {
  // Test boolean creation using singleton
  CljObject *bool_obj = clj_true();
  mu_assert_obj_type(bool_obj, CLJ_BOOL);
  mu_assert_obj_bool(bool_obj, true);

  return 0;
}

static char *test_singleton_objects(void) {

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

  return 0;
}

static char *test_empty_vector_singleton(void) {
  // make_vector(0, ...) returns the empty-vector singleton
  CljObject *v0 = make_vector(0, 0);
  mu_assert_obj_type(v0, CLJ_VECTOR);

  // capacity <= 0 also returns the same singleton
  CljObject *vneg = make_vector(-1, 1);
  mu_assert_obj_ptr_equal(v0, vneg);

  // Parsing [] should yield the same singleton pointer
  EvalState st;
  memset(&st, 0, sizeof(EvalState));
  CljObject *parsed = parse("[]", &st);
  mu_assert_obj_ptr_equal(v0, parsed);

  return 0;
}

static char *test_empty_map_singleton(void) {
  CLJVALUE_POOL_SCOPE(pool) {
    // make_map(0) returns the empty-map singleton
    CljObject *m0 = make_map(0);
    mu_assert_obj_ptr_equal(m0, make_map(0));
    mu_assert_obj_ptr_equal(m0, make_map(-1));

  }
  return 0;
}

// ============================================================================
// PARSER TESTS
// ============================================================================

static char *test_parser_basic_types(void) {

  CLJVALUE_POOL_SCOPE(pool) {
    EvalState st;
    memset(&st, 0, sizeof(EvalState));

    // Test integer parsing
    CljObject *int_result = parse("42", &st);
    mu_assert_obj_type(int_result, CLJ_INT);
    mu_assert_obj_int(int_result, 42);

    // Test float parsing
    CljObject *float_result = parse("3.14", &st);
    mu_assert_obj_type(float_result, CLJ_FLOAT);

    // Test string parsing
    CljObject *str_result = parse(R"("hello")", &st);
    mu_assert_obj_type(str_result, CLJ_STRING);

    // Test symbol parsing
    CljObject *sym_result = parse("test-symbol", &st);
    mu_assert_obj_type(sym_result, CLJ_SYMBOL);

  }
  return 0;
}

static char *test_parser_collections(void) {
  CLJVALUE_POOL_SCOPE(pool) {
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

  }
  return 0;
}

static char *test_variable_definition(void) {
  EvalState *st = evalstate();
  // Assertion moved to evalstate()
  
  // Test defining a variable
  CljObject *result = eval_string("(def x 42)", st);
  mu_assert_obj_type(result, CLJ_SYMBOL);  // def returns symbol
  
  // Test retrieving the variable
  CljObject *var_result = eval_string("x", st);
  mu_assert_obj_int(var_result, 42);
  
  evalstate_free(st);
  return 0;
}

static char *test_variable_redefinition(void) {
  EvalState *st = evalstate();
  // Assertion moved to evalstate()
  
  // Define variable first time
  CljObject *result1 = eval_string("(def x 42)", st);
  mu_assert_obj_type(result1, CLJ_SYMBOL);  // def returns symbol
  
  // Redefine variable
  CljObject *result2 = eval_string("(def x 100)", st);
  mu_assert_obj_type(result2, CLJ_SYMBOL);  // def returns symbol
  
  // Test that variable now has new value
  CljObject *var_result = eval_string("x", st);
  mu_assert_obj_int(var_result, 100);
  
  evalstate_free(st);
  return 0;
}

static char *test_variable_with_string(void) {
  EvalState *st = evalstate();
  // Assertion moved to evalstate()
  
  // Test defining a string variable
  CljObject *result = eval_string(R"((def message "Hello, World!"))", st);
  mu_assert_obj_type(result, CLJ_SYMBOL);  // def returns symbol
  
  // Test resolving the string variable - get from namespace mappings directly
  CljSymbol *sym = as_symbol(result);
  mu_assert("Symbol should have name", sym != NULL && sym->name != NULL);
  
  mu_assert("Namespace should have mappings", st->current_ns != NULL && st->current_ns->mappings != NULL);
  CljObject *var_result = map_get(st->current_ns->mappings, result);
  mu_assert_obj_type(var_result, CLJ_STRING);
  mu_assert_obj_string(var_result, "Hello, World!");
  
  evalstate_free(st);
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
  mu_run_test(test_variable_definition);
  mu_run_test(test_variable_redefinition);
  mu_run_test(test_variable_with_string);
  
  test_teardown();
  return 0;
}

// Export for unified test runner
char *run_unit_tests(void) {
  return all_unit_tests();
}

#ifndef UNIFIED_TEST_RUNNER
// Standalone mode - keep main() for backward compatibility
int main(void) {
  return run_minunit_tests(all_unit_tests, "Unit Tests");
}
#endif
