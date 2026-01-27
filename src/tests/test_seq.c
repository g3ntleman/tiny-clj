/*
 * Seq Tests using Unity Framework
 * 
 * Tests for sequence semantics and iterator-based implementation.
 */

#include "tests_common.h"
#include "../list.h"
#include "../seq.h"

// is_list_like is defined in list.h as static inline

// ============================================================================
// TEST FIXTURES (setUp/tearDown defined in unity_test_runner.c)
// ============================================================================

#define TEST_VECTOR_SIZE 3

static ID make_sample_map_with_entries(void) {
    ID map = AUTORELEASE(make_map(4));
    map = map_assoc(as_map(map), intern_symbol_global("k1"), fixnum(10));
    map = map_assoc(as_map(map), intern_symbol_global("k2"), fixnum(20));
    return map;
}

// ============================================================================
// SEQ CREATION TESTS
// ============================================================================

TEST_SHARED(test_make_seq_list) {
    TEST_ASSERT_EQUAL_PTR(NULL, make_seq(NULL));
}

TEST_SHARED(test_make_seq_vector) {
    TEST_ASSERT_EQUAL_INT(CLJ_SEQ, TAG(eval_string("(seq [1 2 3])", g_test_eval_state)));
}

TEST_SHARED(test_make_seq_string) {
    TEST_ASSERT_EQUAL_INT(CLJ_SEQ, TAG(eval_string("(seq \"hello\")", g_test_eval_state)));
}

TEST_SHARED(test_make_seq_map) {
    TEST_ASSERT_NOT_NULL(eval_string("(seq {:k1 10 :k2 20})", g_test_eval_state));
}

// ============================================================================
// SEQ ITERATION TESTS
// ============================================================================

TEST_SHARED(test_seq_first) {
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(eval_string("(first (seq [42 43 44]))", g_test_eval_state)));
}

TEST_SHARED(test_seq_rest) {
    TEST_ASSERT_EQUAL_INT(CLJ_SEQ, TAG(eval_string("(rest (seq [42 43 44]))", g_test_eval_state)));
}

TEST_SHARED(test_seq_map_entry_vector) {
    ID entry = eval_string("(first (seq {:k1 10 :k2 20}))", g_test_eval_state);
    CljPersistentVector *vec = as_vector(entry);
    TEST_ASSERT_EQUAL_INT(2, vector_count(vec));
    TEST_ASSERT_EQUAL_INT(10, as_fixnum(vector_nth(vec, 1)));
}

TEST_SHARED(test_seq_rest_map_returns_sequence) {
    ID rest = eval_string("(rest (seq {:k1 10 :k2 20}))", g_test_eval_state);
    TEST_ASSERT_EQUAL_INT(CLJ_SEQ, TAG(rest));
    TEST_ASSERT_FALSE(seq_empty(rest));
}

TEST_SHARED(test_seq_next_inplace_reuses_iterator) {
    ID map = make_sample_map_with_entries();
    ID seq = AUTORELEASE(make_seq(map));
    TEST_ASSERT_EQUAL_PTR(seq, seq_next_inplace(seq));
    TEST_ASSERT_NULL(seq_next_inplace(seq));
}

TEST_SHARED(test_seq_next) {
    TEST_ASSERT_EQUAL_INT(CLJ_SEQ, TAG(eval_string("(next (seq [42 43 44]))", g_test_eval_state)));
}

TEST_SHARED(test_seq_rest_vs_next_difference) {
    TEST_ASSERT_TRUE(is_seqable(eval_string("(rest (seq [1 2]))", g_test_eval_state)));
    TEST_ASSERT_TRUE(is_seqable(eval_string("(next (seq [1 2]))", g_test_eval_state)));
    ID rest = eval_string("(rest (seq [42]))", g_test_eval_state);
    TEST_ASSERT_TRUE(is_seqable(rest));
    TEST_ASSERT_NULL(eval_string("(first (rest (seq [42])))", g_test_eval_state));
    TEST_ASSERT_NULL(eval_string("(next (seq [42]))", g_test_eval_state));
}

// ============================================================================
// SEQ EQUALITY TESTS
// ============================================================================

TEST_SHARED(test_seq_equality) {
    ID vec1 = AUTORELEASE(make_vector(2, CLJ_VECTOR_PERSISTENT));
    ID vec2 = AUTORELEASE(make_vector(2, CLJ_VECTOR_PERSISTENT));
    CljPersistentVector *v1 = as_vector(vec1), *v2 = as_vector(vec2);
    v1 = vector_conj(vector_conj(v1, fixnum(1)), fixnum(2));
    v2 = vector_conj(vector_conj(v2, fixnum(1)), fixnum(2));
    ID seq1 = AUTORELEASE(make_seq(vec1));
    ID seq2 = AUTORELEASE(make_seq(vec2));
    TEST_ASSERT_TRUE(seq1 != seq2);
}

// ============================================================================
// SEQ_NEXT WITH CLJ_LIST TESTS
// ============================================================================

TEST_SHARED(test_seq_next_with_list_returns_list) {
    CljList *list = make_list(fixnum(1), NULL);
    CljList *list2 = make_list(fixnum(2), NULL);
    CljList *list3 = make_list(fixnum(3), NULL);
    list->rest = (CljObject*)list2;
    list2->rest = (CljObject*)list3;
    ID seq = AUTORELEASE(make_seq(list));
    TEST_ASSERT_EQUAL_INT(CLJ_LIST, as_seq(seq)->iter.seq_type);
    ID next = seq_next(seq);
    TEST_ASSERT_EQUAL_INT(CLJ_LIST, TAG(next));
    CljList *l = as_list(next);
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(l->first));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(as_list(l->rest)->first));
}

TEST_SHARED(test_seq_next_with_list_preserves_structure) {
    CljList *list = make_list(fixnum(1), NULL);
    CljList *list2 = make_list(fixnum(2), NULL);
    CljList *list3 = make_list(fixnum(3), NULL);
    list->rest = (CljObject*)list2;
    list2->rest = (CljObject*)list3;
    ID next = seq_next(AUTORELEASE(make_seq(list)));
    CljList *l = as_list(next);
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(l->first));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(as_list(l->rest)->first));
}

TEST_SHARED(test_seq_next_with_single_element_list) {
    TEST_ASSERT_NULL(eval_string("(next (seq (list 1)))", g_test_eval_state));
}

TEST_SHARED(test_seq_next_with_empty_list) {
    ID result = eval_string("(seq (list))", g_test_eval_state);
    TEST_ASSERT_TRUE(result == NULL || seq_empty(result));
}

TEST_SHARED(test_native_next_with_list) {
    ID result = eval_string("(next (list 1 2 3))", g_test_eval_state);
    TEST_ASSERT_EQUAL_INT(CLJ_LIST, TAG(result));
    CljList *l = as_list(result);
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(l->first));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(as_list(l->rest)->first));
}

// ============================================================================
// HIGH-LEVEL SEQ TESTS FOR MAPS
// ============================================================================

TEST_SHARED(test_seq_map_returns_sequence) {
    TEST_ASSERT_EQUAL_INT(CLJ_SEQ, TAG(eval_string("(seq {:a 1 :b 2})", g_test_eval_state)));
}

TEST_SHARED(test_seq_empty_map_returns_nil) {
    ID result = eval_string("(seq {})", g_test_eval_state);
    TEST_ASSERT_TRUE(result == NULL || seq_empty(result));
}

TEST_SHARED(test_seq_nil_returns_nil) {
    ID result = eval_string("(seq nil)", g_test_eval_state);
    TEST_ASSERT_TRUE(result == NULL || seq_empty(result));
}

TEST_SHARED(test_seq_map_first_returns_vector) {
    ID result = eval_string("(first (seq {:a 1 :b 2}))", g_test_eval_state);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(result));
    CljPersistentVector *vec = as_vector(result);
    TEST_ASSERT_EQUAL_INT(2, vector_count(vec));
    TEST_ASSERT_EQUAL_INT(CLJ_SYMBOL, TAG(vector_nth(vec, 0)));
    TEST_ASSERT_TRUE(is_fixnum(vector_nth(vec, 1)));
}

TEST_SHARED(test_seq_map_count) {
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(eval_string("(count (seq {:a 1 :b 2}))", g_test_eval_state)));
}

TEST_SHARED(test_seq_map_next_returns_sequence) {
    TEST_ASSERT_EQUAL_INT(CLJ_SEQ, TAG(eval_string("(next (seq {:a 1 :b 2}))", g_test_eval_state)));
}

TEST_SHARED(test_seq_map_rest_returns_sequence) {
    TEST_ASSERT_EQUAL_INT(CLJ_SEQ, TAG(eval_string("(rest (seq {:a 1 :b 2}))", g_test_eval_state)));
}

TEST_SHARED(test_seq_map_iteration) {
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(eval_string("(first (seq {:a 1 :b 2}))", g_test_eval_state)));
    TEST_ASSERT_EQUAL_INT(CLJ_SEQ, TAG(eval_string("(next (seq {:a 1 :b 2}))", g_test_eval_state)));
    TEST_ASSERT_EQUAL_INT(2, vector_count(as_vector(eval_string("(first (next (seq {:a 1 :b 2})))", g_test_eval_state))));
}

TEST_SHARED(test_seq_map_single_entry_next) {
    TEST_ASSERT_NULL(eval_string("(next (seq {:a 1}))", g_test_eval_state));
}

TEST_SHARED(test_seq_map_single_entry_rest) {
    TEST_ASSERT_TRUE(is_seqable(eval_string("(rest (seq {:a 1}))", g_test_eval_state)));
}

TEST_SHARED(test_seq_map_entry_structure) {
    ID entry = eval_string("(first (seq {:a 1}))", g_test_eval_state);
    CljPersistentVector *vec = as_vector(entry);
    TEST_ASSERT_EQUAL_INT(2, vector_count(vec));
    TEST_ASSERT_EQUAL_INT(CLJ_SYMBOL, TAG(vector_nth(vec, 0)));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(vector_nth(vec, 1)));
}

// ============================================================================
// COW TESTS FOR SEGITERATOR
// ============================================================================

TEST_SHARED(test_seq_cow_multiple_sequences_same_container) {
    ID vec = AUTORELEASE(make_vector(4, CLJ_VECTOR_PERSISTENT));
    CljPersistentVector *v = as_vector(vec);
    v = vector_conj(vector_conj(vector_conj(v, fixnum(1)), fixnum(2)), fixnum(3));
    ID seq1 = AUTORELEASE(make_seq(vec));
    ID seq2 = AUTORELEASE(make_seq(vec));
    RETAIN(vec);
    CljPersistentVector *new_vec = vector_conj(v, fixnum(4));
    TEST_ASSERT_TRUE(v != new_vec);
    TEST_ASSERT_EQUAL_INT(3, vector_count(v));
    TEST_ASSERT_EQUAL_PTR(v, as_seq(seq1)->iter.container);
    TEST_ASSERT_EQUAL_PTR(v, as_seq(seq2)->iter.container);
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(seq_first(seq1)));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(seq_first(seq2)));
    RELEASE(vec);
}

TEST_SHARED(test_seq_cow_rc_one_inplace) {
    ID vec = AUTORELEASE(make_vector(4, CLJ_VECTOR_PERSISTENT));
    CljPersistentVector *v = as_vector(vec);
    v = vector_conj(vector_conj(v, fixnum(1)), fixnum(2));
    ID seq = AUTORELEASE(make_seq(vec));
    CljPersistentVector *new_vec = vector_conj(v, fixnum(3));
    TEST_ASSERT_TRUE(v == new_vec);
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(seq_first(seq)));
}

TEST_SHARED(test_seq_cow_rc_greater_one_copy_on_write) {
    ID vec = AUTORELEASE(make_vector(4, CLJ_VECTOR_PERSISTENT));
    CljPersistentVector *v = as_vector(vec);
    v = vector_conj(vector_conj(v, fixnum(1)), fixnum(2));
    ID seq = AUTORELEASE(make_seq(vec));
    RETAIN(vec);
    CljPersistentVector *new_vec = vector_conj(v, fixnum(3));
    TEST_ASSERT_TRUE(v != new_vec);
    TEST_ASSERT_EQUAL_INT(2, vector_count(v));
    TEST_ASSERT_EQUAL_PTR(v, as_seq(seq)->iter.container);
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(seq_first(seq)));
    RELEASE(vec);
}

TEST_SHARED(test_seq_cow_multiple_sequences_preserved) {
    ID vec = AUTORELEASE(make_vector(4, CLJ_VECTOR_PERSISTENT));
    CljPersistentVector *v = as_vector(vec);
    v = vector_conj(vector_conj(vector_conj(v, fixnum(10)), fixnum(20)), fixnum(30));
    ID seq1 = AUTORELEASE(make_seq(vec));
    ID seq2 = AUTORELEASE(make_seq(vec));
    ID seq3 = AUTORELEASE(make_seq(vec));
    RETAIN(vec); RETAIN(vec);
    CljPersistentVector *new_vec = vector_conj(v, fixnum(40));
    TEST_ASSERT_TRUE(v != new_vec);
    TEST_ASSERT_EQUAL_INT(3, vector_count(v));
    TEST_ASSERT_EQUAL_PTR(v, as_seq(seq1)->iter.container);
    TEST_ASSERT_EQUAL_PTR(v, as_seq(seq2)->iter.container);
    TEST_ASSERT_EQUAL_PTR(v, as_seq(seq3)->iter.container);
    TEST_ASSERT_EQUAL_INT(10, as_fixnum(seq_first(seq1)));
    TEST_ASSERT_EQUAL_INT(10, as_fixnum(seq_first(seq2)));
    TEST_ASSERT_EQUAL_INT(10, as_fixnum(seq_first(seq3)));
    RELEASE(vec); RELEASE(vec);
}

TEST_SHARED(test_seq_cow_iteration_after_cow) {
    ID vec = AUTORELEASE(make_vector(4, CLJ_VECTOR_PERSISTENT));
    CljPersistentVector *v = as_vector(vec);
    v = vector_conj(vector_conj(vector_conj(v, fixnum(1)), fixnum(2)), fixnum(3));
    ID seq = AUTORELEASE(make_seq(vec));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(seq_first(seq)));
    RETAIN(vec);
    TEST_ASSERT_TRUE(v != vector_conj(v, fixnum(4)));
    TEST_ASSERT_EQUAL_PTR(v, as_seq(seq)->iter.container);
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(seq_first(seq_rest(seq))));
    RELEASE(vec);
}

// ============================================================================
// TEST FUNCTIONS (no main function - called by unity_test_runner.c)
// ============================================================================

// Tests are automatically registered by TEST_SHARED() macros
