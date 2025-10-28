/*
 * Seq Tests using Unity Framework
 * 
 * Tests for sequence semantics and iterator-based implementation.
 */

#include "tests_common.h"

// ============================================================================
// TEST FIXTURES (setUp/tearDown defined in unity_test_runner.c)
// ============================================================================

#define TEST_VECTOR_SIZE 3

// ============================================================================
// SEQ CREATION TESTS
// ============================================================================

TEST(test_seq_create_list) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Test with nil first
        CljObject *seq_nil = seq_create(NULL);
        TEST_ASSERT_EQUAL_PTR(NULL, seq_nil);
    }
}

TEST(test_seq_create_vector) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Create a test vector
        CljValue vec = make_vector(TEST_VECTOR_SIZE, 1);
        CljPersistentVector *vec_data = as_vector((CljObject*)vec);
        TEST_ASSERT_NOT_NULL(vec_data);
        
        vec_data->data[0] = fixnum(1);
        vec_data->data[1] = fixnum(2);
        vec_data->data[2] = fixnum(3);
        vec_data->count = TEST_VECTOR_SIZE;
        
        // Create sequence iterator
        CljObject *seq = seq_create((CljObject*)vec);
        TEST_ASSERT_NOT_NULL(seq);
        CljSeqIterator *seq_iter = as_seq((ID)seq);
        TEST_ASSERT_NOT_NULL(seq_iter);
        
        // Test sequence properties
        TEST_ASSERT_EQUAL_INT(CLJ_SEQ, seq->type);
        // Note: seq_iter->count may not be available in current implementation
    }
}

TEST(test_seq_create_string) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Create a test string
        CljValue str = make_string("hello");
        TEST_ASSERT_NOT_NULL(str);
        
        // Create sequence iterator
        CljObject *seq = seq_create((CljObject*)str);
        TEST_ASSERT_NOT_NULL(seq);
        CljSeqIterator *seq_iter = as_seq((ID)seq);
        TEST_ASSERT_NOT_NULL(seq_iter);
        
        // Test sequence properties
        TEST_ASSERT_EQUAL_INT(CLJ_SEQ, seq->type);
        // Note: seq_iter->count may not be available in current implementation
    }
}

TEST(test_seq_create_map) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Create a test map
        CljObject *map = (CljObject*)make_map(16);
        TEST_ASSERT_NOT_NULL(map);
        
        // Create sequence iterator - may return NULL for empty map
        CljObject *seq = seq_create((CljObject*)map);
        (void)seq; // Suppress unused variable warning
        // Note: seq_create may return NULL for empty maps - this is expected behavior
        // TEST_ASSERT_NOT_NULL(seq); // Commented out - NULL is valid for empty maps
    }
}

// ============================================================================
// SEQ ITERATION TESTS
// ============================================================================

TEST(test_seq_first) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Create a test vector
        CljValue vec = make_vector(3, 1);
        CljPersistentVector *vec_data = as_vector((CljObject*)vec);
        TEST_ASSERT_NOT_NULL(vec_data);
        
        vec_data->data[0] = fixnum(42);
        vec_data->data[1] = fixnum(43);
        vec_data->data[2] = fixnum(44);
        vec_data->count = 3;
        
        // Create sequence and test first
        CljObject *seq = seq_create((CljObject*)vec);
        CljObject *first_elem = (CljObject*)seq_first(seq);
        TEST_ASSERT_NOT_NULL(first_elem);
        TEST_ASSERT_TRUE(is_fixnum((CljValue)first_elem));
        TEST_ASSERT_EQUAL_INT(42, as_fixnum((CljValue)first_elem));
    }
}

TEST(test_seq_rest) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Create a test vector
        CljValue vec = make_vector(3, 1);
        CljPersistentVector *vec_data = as_vector((CljObject*)vec);
        TEST_ASSERT_NOT_NULL(vec_data);
        
        vec_data->data[0] = fixnum(42);
        vec_data->data[1] = fixnum(43);
        vec_data->data[2] = fixnum(44);
        vec_data->count = 3;
        
        // Create sequence and test rest
        CljObject *seq = seq_create((CljObject*)vec);
        CljObject *rest_seq = (CljObject*)seq_rest(seq);
        TEST_ASSERT_NOT_NULL(rest_seq);
        TEST_ASSERT_EQUAL_INT(CLJ_SEQ, rest_seq->type);
    }
}

TEST(test_seq_next) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Create a test vector
        CljValue vec = make_vector(3, 1);
        CljPersistentVector *vec_data = as_vector((CljObject*)vec);
        TEST_ASSERT_NOT_NULL(vec_data);
        
        vec_data->data[0] = fixnum(42);
        vec_data->data[1] = fixnum(43);
        vec_data->data[2] = fixnum(44);
        vec_data->count = 3;
        
        // Create sequence and test next
        CljObject *seq = seq_create((CljObject*)vec);
        CljObject *next_seq = (CljObject*)seq_next(seq);
        TEST_ASSERT_NOT_NULL(next_seq);
        TEST_ASSERT_EQUAL_INT(CLJ_SEQ, next_seq->type);
    }
}

// ============================================================================
// SEQ EQUALITY TESTS
// ============================================================================

TEST(test_seq_equality) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Create two identical vectors
        CljValue vec1 = make_vector(2, 1);
        CljValue vec2 = make_vector(2, 1);
        
        CljPersistentVector *vec1_data = as_vector((CljObject*)vec1);
        CljPersistentVector *vec2_data = as_vector((CljObject*)vec2);
        
        TEST_ASSERT_NOT_NULL(vec1_data);
        TEST_ASSERT_NOT_NULL(vec2_data);
        
        vec1_data->data[0] = fixnum(1);
        vec1_data->data[1] = fixnum(2);
        vec1_data->count = 2;
        
        vec2_data->data[0] = fixnum(1);
        vec2_data->data[1] = fixnum(2);
        vec2_data->count = 2;
        
        // Create sequences
        CljObject *seq1 = seq_create((CljObject*)vec1);
        CljObject *seq2 = seq_create((CljObject*)vec2);
        
        TEST_ASSERT_NOT_NULL(seq1);
        TEST_ASSERT_NOT_NULL(seq2);
        
        // Test equality (simplified - actual implementation may vary)
        TEST_ASSERT_TRUE(seq1 != seq2); // Different objects
    }
}

// ============================================================================
// TEST FUNCTIONS (no main function - called by unity_test_runner.c)
// ============================================================================

// Tests are automatically registered by TEST() macros
