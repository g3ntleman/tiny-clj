/*
 * Seq Semantics Tests for Tiny-CLJ
 * 
 * Tests the iterator-based sequence implementation
 */

#include "minunit.h"
#include "seq.h"
#include "vector.h"
#include "list_operations.h"
#include "map.h"
#include "string.h"
#include "clj_symbols.h"
#include "memory_profiler.h"
#include "memory_hooks.h"

// ============================================================================
// SEQ CREATION TESTS
// ============================================================================

#define TEST_VECTOR_SIZE 3

static char *test_seq_create_list(void) {
    
    WITH_MEMORY_PROFILING({
        // Create a test list using convenience function
        CljObject *list = list_from_ints(2, 1, 2);
        
        // Create sequence iterator
        CljObject *seq = seq_create(list);
        mu_assert("seq creation failed", seq != NULL);
        CljSeqIterator *seq_iter = as_seq(seq);
        mu_assert("seq iterator cast failed", seq_iter != NULL);
        mu_assert("seq container mismatch", seq_iter->iter.container == list);
        mu_assert("seq type mismatch", seq_iter->iter.seq_type == CLJ_LIST);
        
        seq_release(seq);
        release(list);
    });
    
    return 0;
}

static char *test_seq_create_vector(void) {
    
    WITH_MEMORY_PROFILING({
        // Create a test vector
        CljObject *vec = make_vector(TEST_VECTOR_SIZE, 1);
        CljPersistentVector *vec_data = as_vector(vec);
        if (vec_data) {
            vec_data->data[0] = make_int(1);
            vec_data->data[1] = make_int(2);
            vec_data->data[2] = make_int(3);
            vec_data->count = TEST_VECTOR_SIZE;
        }
        
        // Create sequence iterator
        CljObject *seq = seq_create(vec);
        mu_assert("seq creation failed", seq != NULL);
        CljSeqIterator *seq_iter = as_seq(seq);
        mu_assert("seq iterator cast failed", seq_iter != NULL);
        mu_assert("seq container mismatch", seq_iter->iter.container == vec);
        mu_assert("seq type mismatch", seq_iter->iter.seq_type == CLJ_VECTOR);
        
        seq_release(seq);
        release(vec);
    });
    
    return 0;
}

static char *test_seq_create_string(void) {
    
    // Create a test string
    CljObject *str = make_string("hello");
    
    // Create sequence iterator
    CljObject *seq = seq_create(str);
    mu_assert("seq creation failed", seq != NULL);
    CljSeqIterator *seq_iter = as_seq(seq);
    mu_assert("seq iterator cast failed", seq_iter != NULL);
    mu_assert("seq container mismatch", seq_iter->iter.container == str);
    mu_assert("seq type mismatch", seq_iter->iter.seq_type == CLJ_STRING);
    
    seq_release(seq);
    release(str);
    
    return 0;
}

static char *test_seq_create_nil(void) {
    
    // Create sequence for nil - should return nil singleton
    CljObject *seq = seq_create(NULL);
    mu_assert("seq creation failed", seq != NULL);
    mu_assert("seq of nil should be nil", seq == clj_nil());
    mu_assert("seq should be nil type", seq->type == CLJ_NIL);
    
    // No seq_release needed for nil singleton
    
    return 0;
}

// ============================================================================
// SEQ OPERATION TESTS
// ============================================================================

static char *test_seq_first(void) {
    
    // Test with vector
    CljObject *vec = make_vector(2, 1);
    CljPersistentVector *vec_data = as_vector(vec);
    if (vec_data) {
        vec_data->data[0] = make_int(42);
        vec_data->data[1] = make_int(84);
        vec_data->count = 2;
    }
    
    CljObject *seq = seq_create(vec);
    mu_assert("seq creation failed", seq != NULL);
    
    CljObject *first = seq_first(seq);
    mu_assert("first element is null", first != NULL);
    mu_assert_obj_int_detailed(first, 42);
    
    seq_release(seq);
    release(vec);
    
    return 0;
}

static char *test_seq_rest(void) {
    
    // Test with vector
    CljObject *vec = make_vector(3, 1);
    CljPersistentVector *vec_data = as_vector(vec);
    if (vec_data) {
        vec_data->data[0] = make_int(1);
        vec_data->data[1] = make_int(2);
        vec_data->data[2] = make_int(3);
        vec_data->count = 3;
    }
    
    CljObject *seq = seq_create(vec);
    mu_assert("seq creation failed", seq != NULL);
    
    CljObject *rest_seq = seq_rest(seq);
    mu_assert("rest sequence is null", rest_seq != NULL);
    
    CljObject *first_rest = seq_first(rest_seq);
    mu_assert("first of rest is null", first_rest != NULL);
    mu_assert_obj_int_detailed(first_rest, 2);
    
    seq_release(seq);
    seq_release(rest_seq);
    release(vec);
    
    return 0;
}

static char *test_seq_empty(void) {
    
    // Test with empty vector
    CljObject *vec = make_vector(0, 1);
    CljObject *seq = seq_create(vec);
    mu_assert("seq creation failed", seq != NULL);
    
    mu_assert("empty sequence should be empty", seq_empty(seq) == true);
    
    seq_release(seq);
    release(vec);
    
    return 0;
}

static char *test_seq_count(void) {
    
    // Test with vector
    CljObject *vec = make_vector(3, 1);
    CljPersistentVector *vec_data = as_vector(vec);
    if (vec_data) {
        vec_data->data[0] = make_int(1);
        vec_data->data[1] = make_int(2);
        vec_data->data[2] = make_int(3);
        vec_data->count = 3;
    }
    
    CljObject *seq = seq_create(vec);
    mu_assert("seq creation failed", seq != NULL);
    
    int count = seq_count(seq);
    mu_assert("count mismatch", count == 3);
    
    seq_release(seq);
    release(vec);
    
    return 0;
}

// ============================================================================
// SEQABLE PREDICATES TESTS
// ============================================================================

static char *test_is_seqable(void) {
    
    // Test seqable types
    CljList *test_list = make_list(NULL, NULL);
    mu_assert("list should be seqable", is_seqable((CljObject*)test_list) == true);
    mu_assert("vector should be seqable", is_seqable(make_vector(1, 1)) == true);
    mu_assert("string should be seqable", is_seqable(make_string("test")) == true);
    mu_assert("nil should be seqable", is_seqable(NULL) == true);
    
    // Test non-seqable types
    mu_assert("int should not be seqable", is_seqable(make_int(42)) == false);
    mu_assert("float should not be seqable", is_seqable(make_float(3.14)) == false);
    mu_assert("bool should not be seqable", is_seqable(clj_true()) == false);
    
    return 0;
}

// ============================================================================
// SEQ TO LIST CONVERSION TESTS
// ============================================================================

static char *test_seq_to_list(void) {
    
    // Create vector with test data
    CljObject *vec = make_vector(2, 1);
    CljPersistentVector *vec_data = as_vector(vec);
    if (vec_data) {
        vec_data->data[0] = make_int(1);
        vec_data->data[1] = make_int(2);
        vec_data->count = 2;
    }
    
    // Test that lists and sequences work the same way
    // (first list1) and (first (seq list1)) should use the same codepath
    
    // Test that vectors work with seq operations
    // Vectors are not lists, so we need seq operations for them
    
    // Test that seq operations work on lists directly
    CljObject *seq = seq_create(vec);
    mu_assert("seq creation should work on lists", seq != NULL);
    
    CljObject *first_elem = seq_first(seq);
    mu_assert("seq first should work", first_elem != NULL);
    mu_assert("seq first should be integer", first_elem->type == CLJ_INT);
    
    seq_release(seq);
    release(vec);
    
    return 0;
}

static char *test_empty_list_nil_semantics(void) {
    
    // Test 1: empty-list is () (nil)
    CljList *empty_list = make_list(NULL, NULL);
    mu_assert("empty list should not be NULL", empty_list != NULL);
    mu_assert("empty list should be a list", empty_list->type == CLJ_LIST);
    
    // Test 2: (seq empty-list) is nil
    CljObject *seq = seq_create(empty_list);
    mu_assert("seq of empty list should be nil singleton", seq == clj_nil());
    
    // Test 3: (= nil nil) is true
    CljObject *nil1 = clj_nil();
    CljObject *nil2 = clj_nil();
    mu_assert("nil should equal nil", nil1 == nil2);
    
    // Test 4: (= () nil) is false
    mu_assert("empty list should not equal nil", empty_list != clj_nil());
    
    // Test 5: seq operations on empty list
    CljObject *first = list_first(empty_list);
    mu_assert("first of empty list should be nil singleton", first == clj_nil());
    
    // Test that empty list is seqable but seq is nil
    mu_assert("empty list should be seqable", is_seqable(empty_list));
    
    release(empty_list);
    
    return 0;
}

// ============================================================================
// TEST SUITE REGISTRY
// ============================================================================

static char *all_seq_tests(void) {
    mu_run_test(test_seq_create_list);
    mu_run_test(test_seq_create_vector);
    mu_run_test(test_seq_create_string);
    mu_run_test(test_seq_create_nil);
    
    mu_run_test(test_seq_first);
    mu_run_test(test_seq_rest);
    mu_run_test(test_seq_empty);
    mu_run_test(test_seq_count);
    
    mu_run_test(test_is_seqable);
    
    mu_run_test(test_seq_to_list);
    mu_run_test(test_empty_list_nil_semantics);
    
    return 0;
}

// Export for unified test runner
char *run_seq_tests(void) {
    init_special_symbols();
    return all_seq_tests();
}

#ifndef UNIFIED_TEST_RUNNER
// Standalone mode
int main(void) {
    printf("=== Tiny-CLJ Seq Semantics Tests ===\n");
    init_special_symbols();
    int result = run_minunit_tests(all_seq_tests, "Seq Semantics Tests");
    return result;
}
#endif
