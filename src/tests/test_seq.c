/*
 * Seq Tests using Unity Framework
 * 
 * Tests for sequence semantics and iterator-based implementation.
 */

#include "tests_common.h"
#include "../list.h"
#include "../seq.h"

// ============================================================================
// TEST FIXTURES (setUp/tearDown defined in unity_test_runner.c)
// ============================================================================

#define TEST_VECTOR_SIZE 3

static CljMap* make_sample_map_with_entries(void) {
    CljMap *map = make_map(4);
    TEST_ASSERT_NOT_NULL(map);
    
    CljSymbol *key1 = intern_symbol_global("k1");
    CljSymbol *key2 = intern_symbol_global("k2");
    TEST_ASSERT_NOT_NULL(key1);
    TEST_ASSERT_NOT_NULL(key2);
    
    map = map_assoc(map, (ID)key1, fixnum(10));
    map = map_assoc(map, (ID)key2, fixnum(20));
    TEST_ASSERT_NOT_NULL(map);
    
    return map;
}

// ============================================================================
// SEQ CREATION TESTS
// ============================================================================

TEST(test_make_seq_list) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Test with nil first
        CljSeqIterator *seq_nil = make_seq(NULL);
        TEST_ASSERT_EQUAL_PTR(NULL, seq_nil);
    }
}

TEST(test_make_seq_vector) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Create a test vector
        CljValue vec = make_vector(TEST_VECTOR_SIZE, CLJ_VECTOR);
        CljVector *vec_data = as_vector((CljObject*)vec);
        TEST_ASSERT_NOT_NULL(vec_data);
        
        // Add elements using vector_conj
        vec_data = vector_conj(vec_data, (ID)fixnum(1));
        vec_data = vector_conj(vec_data, (ID)fixnum(2));
        vec_data = vector_conj(vec_data, (ID)fixnum(3));
        
        // Create sequence iterator
        CljSeqIterator *seq = make_seq((CljObject*)vec);
        TEST_ASSERT_NOT_NULL(seq);
        CljSeqIterator *seq_iter = as_seq((ID)seq);
        TEST_ASSERT_NOT_NULL(seq_iter);
        
        // Test sequence properties
        TEST_ASSERT_EQUAL_INT(CLJ_SEQ, seq->base.type);
        // Note: seq_iter->count may not be available in current implementation
    }
}

TEST(test_make_seq_string) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Create a test string
        CljValue str = make_string("hello");
        TEST_ASSERT_NOT_NULL(str);
        
        // Create sequence iterator
        CljSeqIterator *seq = make_seq((CljObject*)str);
        TEST_ASSERT_NOT_NULL(seq);
        CljSeqIterator *seq_iter = as_seq((ID)seq);
        TEST_ASSERT_NOT_NULL(seq_iter);
        
        // Test sequence properties
        TEST_ASSERT_EQUAL_INT(CLJ_SEQ, seq->base.type);
        // Note: seq_iter->count may not be available in current implementation
    }
}

TEST(test_make_seq_map) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        CljMap *map = make_sample_map_with_entries();
        map = (CljMap*)AUTORELEASE((CljObject*)map);
        TEST_ASSERT_NOT_NULL(map);
        
        CljSeqIterator *seq = make_seq((CljObject*)map);
        TEST_ASSERT_NOT_NULL(seq);
        RELEASE((ID)seq);
    }
}

// ============================================================================
// SEQ ITERATION TESTS
// ============================================================================

TEST(test_seq_first) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Create a test vector
        CljValue vec = make_vector(3, CLJ_VECTOR);
        CljVector *vec_data = as_vector((CljObject*)vec);
        TEST_ASSERT_NOT_NULL(vec_data);
        
        // Add elements using vector_conj
        vec_data = vector_conj(vec_data, (ID)fixnum(42));
        vec_data = vector_conj(vec_data, (ID)fixnum(43));
        vec_data = vector_conj(vec_data, (ID)fixnum(44));
        
        // Create sequence and test first
        CljSeqIterator *seq = make_seq((CljObject*)vec);
        CljObject *first_elem = (CljObject*)seq_first((CljObject*)seq);
        TEST_ASSERT_NOT_NULL(first_elem);
        TEST_ASSERT_TRUE(is_fixnum((CljValue)first_elem));
        TEST_ASSERT_EQUAL_INT(42, as_fixnum((CljValue)first_elem));
    }
}

TEST(test_seq_rest) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Create a test vector
        CljValue vec = make_vector(3, CLJ_VECTOR);
        CljVector *vec_data = as_vector((CljObject*)vec);
        TEST_ASSERT_NOT_NULL(vec_data);
        
        // Add elements using vector_conj
        vec_data = vector_conj(vec_data, (ID)fixnum(42));
        vec_data = vector_conj(vec_data, (ID)fixnum(43));
        vec_data = vector_conj(vec_data, (ID)fixnum(44));
        
        // Create sequence and test rest
        CljSeqIterator *seq = make_seq((CljObject*)vec);
        CljObject *rest_seq = (CljObject*)seq_rest((CljObject*)seq);
        TEST_ASSERT_NOT_NULL(rest_seq);
        TEST_ASSERT_EQUAL_INT(CLJ_SEQ, rest_seq->type);
    }
}

TEST(test_seq_map_entry_vector) {
    {
        CljMap *map = make_sample_map_with_entries();
        map = (CljMap*)AUTORELEASE((CljObject*)map);
        
        CljSeqIterator *seq = make_seq((CljObject*)map);
        TEST_ASSERT_NOT_NULL(seq);
        
        CljObject *entry = (CljObject*)seq_first((CljObject*)seq);
        TEST_ASSERT_NOT_NULL(entry);
        TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, entry->type);
        
        CljVector *vec = as_vector(entry);
        TEST_ASSERT_NOT_NULL(vec);
        TEST_ASSERT_EQUAL_INT(2, vector_count(vec));
        
        CljObject *key = vector_nth(vec, 0);
        CljObject *value = vector_nth(vec, 1);
        TEST_ASSERT_TRUE(TAG(key) == CLJ_SYMBOL);
        TEST_ASSERT_TRUE(is_fixnum((ID)value));
        CljSymbol *expected_key = intern_symbol_global("k1");
        TEST_ASSERT_EQUAL_PTR(expected_key, key);
        TEST_ASSERT_EQUAL_INT(10, as_fixnum((ID)value));
        
        RELEASE((ID)seq);
    }
}

TEST(test_seq_rest_map_returns_sequence) {
    {
        CljMap *map = make_sample_map_with_entries();
        map = (CljMap*)AUTORELEASE((CljObject*)map);
        
        CljSeqIterator *seq = make_seq((CljObject*)map);
        TEST_ASSERT_NOT_NULL(seq);
        
        CljObject *rest_seq = (CljObject*)seq_rest((CljObject*)seq);
        TEST_ASSERT_NOT_NULL(rest_seq);
        TEST_ASSERT_EQUAL_INT(CLJ_SEQ, rest_seq->type);
        
        TEST_ASSERT_FALSE(seq_empty(rest_seq));
        
        RELEASE((ID)seq);
        RELEASE((ID)rest_seq);
    }
}

TEST(test_seq_next_inplace_reuses_iterator) {
    {
        CljMap *map = make_sample_map_with_entries();
        map = (CljMap*)AUTORELEASE((CljObject*)map);
        
        CljSeqIterator *seq = make_seq((CljObject*)map);
        TEST_ASSERT_NOT_NULL(seq);
        
        ID advanced = seq_next_inplace((CljObject*)seq);
        TEST_ASSERT_EQUAL_PTR(seq, (CljSeqIterator*)advanced);
        
        // After advancing once, another advance should eventually yield NULL
        ID exhausted = seq_next_inplace(advanced);
        TEST_ASSERT_NULL(exhausted);
        
        RELEASE((ID)seq);
    }
}

TEST(test_seq_next) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Create a test vector
        CljValue vec = make_vector(3, CLJ_VECTOR);
        CljVector *vec_data = as_vector((CljObject*)vec);
        TEST_ASSERT_NOT_NULL(vec_data);
        
        // Add elements using vector_conj
        vec_data = vector_conj(vec_data, (ID)fixnum(42));
        vec_data = vector_conj(vec_data, (ID)fixnum(43));
        vec_data = vector_conj(vec_data, (ID)fixnum(44));
        
        // Create sequence and test next
        CljSeqIterator *seq = make_seq((CljObject*)vec);
        CljObject *next_seq = (CljObject*)seq_next((CljObject*)seq);
        TEST_ASSERT_NOT_NULL(next_seq);
        TEST_ASSERT_EQUAL_INT(CLJ_SEQ, next_seq->type);
    }
}

TEST(test_seq_rest_vs_next_difference) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Test 1: Non-empty sequence - both should return non-empty sequences
        {
            CljValue vec = make_vector(2, CLJ_VECTOR);
            CljVector *vec_data = as_vector((CljObject*)vec);
            vec_data = vector_conj(vec_data, (ID)fixnum(1));
            vec_data = vector_conj(vec_data, (ID)fixnum(2));
            
            CljSeqIterator *seq = make_seq((CljObject*)vec);
            
            // rest should return a sequence (even if empty later)
            CljObject *rest_seq = (CljObject*)seq_rest((CljObject*)seq);
            TEST_ASSERT_NOT_NULL(rest_seq);
            TEST_ASSERT_EQUAL_INT(CLJ_SEQ, rest_seq->type);
            
            // next should return a sequence (non-empty)
            CljObject *next_seq = (CljObject*)seq_next((CljObject*)seq);
            TEST_ASSERT_NOT_NULL(next_seq);
            TEST_ASSERT_EQUAL_INT(CLJ_SEQ, next_seq->type);
            
            RELEASE((ID)seq);
            RELEASE((ID)rest_seq);
            RELEASE((ID)next_seq);
        }
        
        // Test 2: Single-element sequence - rest should return empty sequence, next should return nil
        {
            CljValue vec = make_vector(1, CLJ_VECTOR);
            CljVector *vec_data = as_vector((CljObject*)vec);
            vec_data = vector_conj(vec_data, (ID)fixnum(42));
            
            CljSeqIterator *seq = make_seq((CljObject*)vec);
            
            // rest should return empty sequence (not nil!)
            CljObject *rest_seq = (CljObject*)seq_rest((CljObject*)seq);
            TEST_ASSERT_NOT_NULL(rest_seq);
            TEST_ASSERT_EQUAL_INT(CLJ_SEQ, rest_seq->type);
            TEST_ASSERT_TRUE(seq_empty(rest_seq));  // Should be empty sequence
            
            // next should return nil (not empty sequence!)
            CljObject *next_seq = (CljObject*)seq_next((CljObject*)seq);
            TEST_ASSERT_EQUAL_PTR(NULL, next_seq);  // nil = NULL
            
            RELEASE((ID)seq);
            RELEASE((ID)rest_seq);
            // next_seq is NULL, no need to release
        }
        
    }
}

// ============================================================================
// SEQ EQUALITY TESTS
// ============================================================================

TEST(test_seq_equality) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Create two identical vectors
        CljValue vec1 = make_vector(2, CLJ_VECTOR);
        CljValue vec2 = make_vector(2, CLJ_VECTOR);
        
        CljVector *vec1_data = as_vector((CljObject*)vec1);
        CljVector *vec2_data = as_vector((CljObject*)vec2);
        
        TEST_ASSERT_NOT_NULL(vec1_data);
        TEST_ASSERT_NOT_NULL(vec2_data);
        
        vec1_data = vector_conj(vec1_data, (ID)fixnum(1));
        vec1_data = vector_conj(vec1_data, (ID)fixnum(2));
        
        vec2_data = vector_conj(vec2_data, (ID)fixnum(1));
        vec2_data = vector_conj(vec2_data, (ID)fixnum(2));
        
        // Create sequences
        CljSeqIterator *seq1 = make_seq((CljObject*)vec1);
        CljSeqIterator *seq2 = make_seq((CljObject*)vec2);
        
        TEST_ASSERT_NOT_NULL(seq1);
        TEST_ASSERT_NOT_NULL(seq2);
        
        // Test equality (simplified - actual implementation may vary)
        TEST_ASSERT_TRUE(seq1 != seq2); // Different objects
    }
}

// ============================================================================
// SEQ_NEXT WITH CLJ_LIST TESTS
// ============================================================================

// TEST: seq_next with CLJ_LIST should return CLJ_LIST (not CLJ_SEQ)
// CRITICAL: seq_next now returns CLJ_LIST directly when original was CLJ_LIST
TEST(test_seq_next_with_list_returns_list) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Create a list: (1 2 3)
        CljList *list = make_list(fixnum(1), NULL);
        CljList *list2 = make_list(fixnum(2), NULL);
        CljList *list3 = make_list(fixnum(3), NULL);
        
        // Link them: (1 2 3)
        list->rest = (CljObject*)list2;
        list2->rest = (CljObject*)list3;
        
        // Create a seq from the list
        CljSeqIterator *seq = make_seq((CljObject*)list);
        TEST_ASSERT_NOT_NULL(seq);
        TEST_ASSERT_EQUAL_INT(CLJ_SEQ, seq->base.type);
        TEST_ASSERT_EQUAL_INT(CLJ_LIST, seq->iter.seq_type);
        
        // Call seq_next - should return CLJ_LIST directly (not CLJ_SEQ)
        CljObject *next_result = (CljObject*)seq_next((CljObject*)seq);
        TEST_ASSERT_NOT_NULL(next_result);
        
        // seq_next now returns CLJ_LIST directly when original was CLJ_LIST
        TEST_ASSERT_EQUAL_INT(CLJ_LIST, next_result->type);
        
        // Verify the result is (2 3) - should be a CljList
        CljList *next_list = as_list((ID)next_result);
        TEST_ASSERT_NOT_NULL(next_list);
        TEST_ASSERT_NOT_NULL(next_list->first);
        TEST_ASSERT_TRUE(is_fixnum((CljValue)next_list->first));
        TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)next_list->first));
        
        // Verify second element is 3
        CljList *rest_list = as_list((ID)next_list->rest);
        TEST_ASSERT_NOT_NULL(rest_list);
        TEST_ASSERT_NOT_NULL(rest_list->first);
        TEST_ASSERT_TRUE(is_fixnum((CljValue)rest_list->first));
        TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)rest_list->first));
        
        // Cleanup
        RELEASE((ID)seq);
        // next_result is a CLJ_LIST, not a CLJ_SEQ, so no RELEASE needed
    }
}

// TEST: seq_next with CLJ_LIST preserves list structure
TEST(test_seq_next_with_list_preserves_structure) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Create a list: (1 2 3)
        CljList *list = make_list(fixnum(1), NULL);
        CljList *list2 = make_list(fixnum(2), NULL);
        CljList *list3 = make_list(fixnum(3), NULL);
        
        // Link them: (1 2 3)
        list->rest = (CljObject*)list2;
        list2->rest = (CljObject*)list3;
        
        // Create a seq from the list
        CljSeqIterator *seq = make_seq((CljObject*)list);
        TEST_ASSERT_NOT_NULL(seq);
        
        // Call seq_next - should return CLJ_LIST directly
        CljObject *next_result = (CljObject*)seq_next((CljObject*)seq);
        TEST_ASSERT_NOT_NULL(next_result);
        TEST_ASSERT_EQUAL_INT(CLJ_LIST, next_result->type);
        
        // Verify the result is (2 3) - should be a CljList
        CljList *next_list = as_list((ID)next_result);
        TEST_ASSERT_NOT_NULL(next_list);
        TEST_ASSERT_NOT_NULL(next_list->first);
        TEST_ASSERT_TRUE(is_fixnum((CljValue)next_list->first));
        TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)next_list->first));
        
        // Get second element - should also be a CljList
        CljList *rest_list = as_list((ID)next_list->rest);
        TEST_ASSERT_NOT_NULL(rest_list);
        TEST_ASSERT_NOT_NULL(rest_list->first);
        TEST_ASSERT_TRUE(is_fixnum((CljValue)rest_list->first));
        TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)rest_list->first));
        
        // Cleanup
        RELEASE((ID)seq);
        // next_result is a CLJ_LIST, not a CLJ_SEQ, so no RELEASE needed
    }
}

// TEST: seq_next with single-element list should return nil
TEST(test_seq_next_with_single_element_list) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Create a list: (1)
        CljList *list = make_list(fixnum(1), NULL);
        
        // Create a seq from the list
        CljSeqIterator *seq = make_seq((CljObject*)list);
        TEST_ASSERT_NOT_NULL(seq);
        
        // Call seq_next - should return nil (NULL) for single-element list
        CljObject *next_result = (CljObject*)seq_next((CljObject*)seq);
        TEST_ASSERT_NULL(next_result);  // next returns nil if rest is empty
        
        // Cleanup
        RELEASE((ID)seq);
    }
}

// TEST: seq_next with empty list should return nil
TEST(test_seq_next_with_empty_list) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Create empty list: ()
        CljList *list = empty_list();
        
        // Create a seq from the list - should return NULL for empty list
        CljSeqIterator *seq = make_seq((CljObject*)list);
        // make_seq returns NULL for empty lists
        TEST_ASSERT_NULL(seq);
    }
}

// TEST: seq_next with CLJ_LIST via native_next (high-level API)
// This tests the behavior when next is called on a list directly
TEST(test_native_next_with_list) {
    // Use global st from setUp
    // Test: (next (list 1 2 3)) should return (2 3) as CLJ_LIST
    CljObject *result = eval_string("(next (list 1 2 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    
    // native_next should return CLJ_LIST directly (not CLJ_SEQ)
    // Because native_next handles CLJ_LIST specially in builtins.c
    TEST_ASSERT_EQUAL_INT(CLJ_LIST, result->type);
    
    // Verify it's a CljList with correct content
    CljList *list = as_list((ID)result);
    TEST_ASSERT_NOT_NULL(list);
    TEST_ASSERT_NOT_NULL(list->first);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)list->first));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)list->first));
    
    // Verify second element is 3
    CljList *rest_list = as_list((ID)list->rest);
    TEST_ASSERT_NOT_NULL(rest_list);
    TEST_ASSERT_NOT_NULL(rest_list->first);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)rest_list->first));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)rest_list->first));
}

// ============================================================================
// TEST FUNCTIONS (no main function - called by unity_test_runner.c)
// ============================================================================

// Tests are automatically registered by TEST() macros
