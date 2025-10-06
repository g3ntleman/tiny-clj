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

// ============================================================================
// SEQ CREATION TESTS
// ============================================================================

static char *test_seq_create_list(void) {
    printf("\n=== Testing Seq Creation for Lists ===\n");
    
    MEMORY_TEST_START("Seq Creation for Lists");
    
    // Create a test list
    CljObject *list = make_list();
    CljList *list_data = as_list(list);
    if (list_data) {
        list_data->head = make_int(1);
        list_data->tail = make_list();
        CljList *tail_data = as_list(list_data->tail);
        if (tail_data) {
            tail_data->head = make_int(2);
            tail_data->tail = NULL;
        }
    }
    
    // Create sequence iterator
    SeqIterator *seq = seq_create(list);
    mu_assert("seq creation failed", seq != NULL);
    mu_assert("seq container mismatch", seq->container == list);
    mu_assert("seq type mismatch", seq->seq_type == CLJ_LIST);
    
    seq_release(seq);
    release(list);
    
    MEMORY_TEST_END("Seq Creation for Lists");
    
    printf("✓ List seq creation test passed\n");
    return 0;
}

static char *test_seq_create_vector(void) {
    printf("\n=== Testing Seq Creation for Vectors ===\n");
    
    MEMORY_TEST_START("Seq Creation for Vectors");
    
    // Create a test vector
    CljObject *vec = make_vector(3, 1);
    CljPersistentVector *vec_data = as_vector(vec);
    if (vec_data) {
        vec_data->data[0] = make_int(1);
        vec_data->data[1] = make_int(2);
        vec_data->data[2] = make_int(3);
        vec_data->count = 3;
    }
    
    // Create sequence iterator
    SeqIterator *seq = seq_create(vec);
    mu_assert("seq creation failed", seq != NULL);
    mu_assert("seq container mismatch", seq->container == vec);
    mu_assert("seq type mismatch", seq->seq_type == CLJ_VECTOR);
    
    seq_release(seq);
    release(vec);
    
    MEMORY_TEST_END("Seq Creation for Vectors");
    
    printf("✓ Vector seq creation test passed\n");
    return 0;
}

static char *test_seq_create_string(void) {
    printf("\n=== Testing Seq Creation for Strings ===\n");
    
    // Create a test string
    CljObject *str = make_string("hello");
    
    // Create sequence iterator
    SeqIterator *seq = seq_create(str);
    mu_assert("seq creation failed", seq != NULL);
    mu_assert("seq container mismatch", seq->container == str);
    mu_assert("seq type mismatch", seq->seq_type == CLJ_STRING);
    
    seq_release(seq);
    release(str);
    
    printf("✓ String seq creation test passed\n");
    return 0;
}

static char *test_seq_create_nil(void) {
    printf("\n=== Testing Seq Creation for Nil ===\n");
    
    // Create sequence iterator for nil
    SeqIterator *seq = seq_create(NULL);
    mu_assert("seq creation failed", seq != NULL);
    mu_assert("seq type mismatch", seq->seq_type == CLJ_NIL);
    
    seq_release(seq);
    
    printf("✓ Nil seq creation test passed\n");
    return 0;
}

// ============================================================================
// SEQ OPERATION TESTS
// ============================================================================

static char *test_seq_first(void) {
    printf("\n=== Testing Seq First ===\n");
    
    // Test with vector
    CljObject *vec = make_vector(2, 1);
    CljPersistentVector *vec_data = as_vector(vec);
    if (vec_data) {
        vec_data->data[0] = make_int(42);
        vec_data->data[1] = make_int(84);
        vec_data->count = 2;
    }
    
    SeqIterator *seq = seq_create(vec);
    mu_assert("seq creation failed", seq != NULL);
    
    CljObject *first = seq_first(seq);
    mu_assert("first element is null", first != NULL);
    mu_assert_obj_int_detailed(first, 42);
    
    seq_release(seq);
    release(vec);
    
    printf("✓ Seq first test passed\n");
    return 0;
}

static char *test_seq_rest(void) {
    printf("\n=== Testing Seq Rest ===\n");
    
    // Test with vector
    CljObject *vec = make_vector(3, 1);
    CljPersistentVector *vec_data = as_vector(vec);
    if (vec_data) {
        vec_data->data[0] = make_int(1);
        vec_data->data[1] = make_int(2);
        vec_data->data[2] = make_int(3);
        vec_data->count = 3;
    }
    
    SeqIterator *seq = seq_create(vec);
    mu_assert("seq creation failed", seq != NULL);
    
    SeqIterator *rest_seq = seq_rest(seq);
    mu_assert("rest sequence is null", rest_seq != NULL);
    
    CljObject *first_rest = seq_first(rest_seq);
    mu_assert("first of rest is null", first_rest != NULL);
    mu_assert_obj_int_detailed(first_rest, 2);
    
    seq_release(seq);
    seq_release(rest_seq);
    release(vec);
    
    printf("✓ Seq rest test passed\n");
    return 0;
}

static char *test_seq_empty(void) {
    printf("\n=== Testing Seq Empty ===\n");
    
    // Test with empty vector
    CljObject *vec = make_vector(0, 1);
    SeqIterator *seq = seq_create(vec);
    mu_assert("seq creation failed", seq != NULL);
    
    mu_assert("empty sequence should be empty", seq_empty(seq) == true);
    
    seq_release(seq);
    release(vec);
    
    printf("✓ Seq empty test passed\n");
    return 0;
}

static char *test_seq_count(void) {
    printf("\n=== Testing Seq Count ===\n");
    
    // Test with vector
    CljObject *vec = make_vector(3, 1);
    CljPersistentVector *vec_data = as_vector(vec);
    if (vec_data) {
        vec_data->data[0] = make_int(1);
        vec_data->data[1] = make_int(2);
        vec_data->data[2] = make_int(3);
        vec_data->count = 3;
    }
    
    SeqIterator *seq = seq_create(vec);
    mu_assert("seq creation failed", seq != NULL);
    
    int count = seq_count(seq);
    mu_assert("count mismatch", count == 3);
    
    seq_release(seq);
    release(vec);
    
    printf("✓ Seq count test passed\n");
    return 0;
}

// ============================================================================
// SEQABLE PREDICATES TESTS
// ============================================================================

static char *test_is_seqable(void) {
    printf("\n=== Testing is_seqable ===\n");
    
    // Test seqable types
    mu_assert("list should be seqable", is_seqable(make_list()) == true);
    mu_assert("vector should be seqable", is_seqable(make_vector(1, 1)) == true);
    mu_assert("string should be seqable", is_seqable(make_string("test")) == true);
    mu_assert("nil should be seqable", is_seqable(NULL) == true);
    
    // Test non-seqable types
    mu_assert("int should not be seqable", is_seqable(make_int(42)) == false);
    mu_assert("float should not be seqable", is_seqable(make_float(3.14)) == false);
    mu_assert("bool should not be seqable", is_seqable(clj_true()) == false);
    
    printf("✓ is_seqable test passed\n");
    return 0;
}

// ============================================================================
// SEQ TO LIST CONVERSION TESTS
// ============================================================================

static char *test_seq_to_list(void) {
    printf("\n=== Testing Seq to List Conversion ===\n");
    
    // Create vector with test data
    CljObject *vec = make_vector(2, 1);
    CljPersistentVector *vec_data = as_vector(vec);
    if (vec_data) {
        vec_data->data[0] = make_int(1);
        vec_data->data[1] = make_int(2);
        vec_data->count = 2;
    }
    
    SeqIterator *seq = seq_create(vec);
    mu_assert("seq creation failed", seq != NULL);
    
    CljObject *list = seq_to_list(seq);
    mu_assert("list conversion failed", list != NULL);
    mu_assert("list should be a list", list->type == CLJ_LIST);
    
    seq_release(seq);
    release(vec);
    release(list);
    
    printf("✓ Seq to list conversion test passed\n");
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
    
    return 0;
}

int main(void) {
    printf("=== Tiny-CLJ Seq Semantics Tests ===\n");
    
    // Initialize symbol table
    init_special_symbols();
    
    int result = run_minunit_tests(all_seq_tests, "Seq Semantics Tests");
    
    return result;
}
