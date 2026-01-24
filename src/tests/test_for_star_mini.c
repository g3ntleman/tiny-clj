/*
 * Low-level test for for* thunk executor with minimal example
 * 
 * Tests the sequence advancement logic directly by calling
 * native_for_star_thunk_executor with a simple state.
 */

#include "tests_common.h"
#include "../eval.h"
#include "../vector.h"
#include "../map.h"
#include "../symbol.h"
#include "../seq.h"
#include "../builtins.h"

// ============================================================================
// TEST: for* with [x [1 2] y [3 4]] [x y] - minimal cartesian product
// ============================================================================

TEST_SHARED(test_for_star_mini_cartesian) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create symbols
    CljSymbol *x_sym = intern_symbol_global("x");
    CljSymbol *y_sym = intern_symbol_global("y");
    
    // Create collections: [1 2] and [3 4]
    CljVector *coll1 = make_vector(2, CLJ_VECTOR);
    ASSIGN(coll1, vector_conj(coll1, fixnum(1)));
    ASSIGN(coll1, vector_conj(coll1, fixnum(2)));
    
    CljVector *coll2 = make_vector(2, CLJ_VECTOR);
    ASSIGN(coll2, vector_conj(coll2, fixnum(3)));
    ASSIGN(coll2, vector_conj(coll2, fixnum(4)));
    
    // Create sequences from collections
    CljSeqIterator *seq1 = make_seq((ID)coll1);
    CljSeqIterator *seq2 = make_seq((ID)coll2);
    TEST_ASSERT_NOT_NULL(seq1);
    TEST_ASSERT_NOT_NULL(seq2);
    
    // Create vectors for state
    CljVector *seqs_vec = make_vector(2, CLJ_VECTOR);
    ASSIGN(seqs_vec, vector_conj(seqs_vec, (ID)seq1));
    ASSIGN(seqs_vec, vector_conj(seqs_vec, (ID)seq2));
    
    CljVector *vars_vec = make_vector(2, CLJ_VECTOR);
    ASSIGN(vars_vec, vector_conj(vars_vec, (ID)x_sym));
    ASSIGN(vars_vec, vector_conj(vars_vec, (ID)y_sym));
    
    CljVector *initial_colls_vec = make_vector(2, CLJ_VECTOR);
    ASSIGN(initial_colls_vec, vector_conj(initial_colls_vec, (ID)RETAIN(coll1)));
    ASSIGN(initial_colls_vec, vector_conj(initial_colls_vec, (ID)RETAIN(coll2)));
    
    // Create ops_vec: [0, NULL, 0, NULL] (two BINDING operations)
    CljVector *ops_vec = make_vector(4, CLJ_VECTOR);
    ASSIGN(ops_vec, vector_conj(ops_vec, fixnum(0))); // FOR_OP_BINDING
    ASSIGN(ops_vec, vector_conj(ops_vec, NULL));      // placeholder
    ASSIGN(ops_vec, vector_conj(ops_vec, fixnum(0))); // FOR_OP_BINDING
    ASSIGN(ops_vec, vector_conj(ops_vec, NULL));      // placeholder
    
    // Create body: [x y]
    CljVector *body = make_vector(2, CLJ_VECTOR);
    ASSIGN(body, vector_conj(body, (ID)x_sym));
    ASSIGN(body, vector_conj(body, (ID)y_sym));
    
    // Create state map
    CljSymbol *k_seqs = intern_symbol_global("__for_star_seqs__");
    CljSymbol *k_vars = intern_symbol_global("__for_star_vars__");
    CljSymbol *k_initial_colls = intern_symbol_global("__for_star_initial_colls__");
    CljSymbol *k_ops = intern_symbol_global("__for_star_ops__");
    CljSymbol *k_body = intern_symbol_global("__for_star_body__");
    CljSymbol *k_env_stack = intern_symbol_global("__for_star_env_stack__");
    CljSymbol *k_binding_count = intern_symbol_global("__for_star_binding_count__");
    CljMap *state = map_assoc(map_empty(), (ID)k_seqs, (ID)seqs_vec);
    ASSIGN(state, map_assoc(state, (ID)k_vars, (ID)vars_vec));
    ASSIGN(state, map_assoc(state, (ID)k_initial_colls, (ID)initial_colls_vec));
    ASSIGN(state, map_assoc(state, (ID)k_ops, (ID)ops_vec));
    ASSIGN(state, map_assoc(state, (ID)k_body, (ID)body));
    ASSIGN(state, map_assoc(state, (ID)k_binding_count, fixnum(2)));
    
    // Create empty env_stack
    CljVector *empty_stack = make_vector(0, CLJ_VECTOR);
    ASSIGN(state, map_assoc(state, (ID)k_env_stack, (ID)empty_stack));
    
    // Call native_for_star_thunk_executor directly
    ID args[1] = { (ID)state };
    ID result = native_for_star_thunk_executor(args, 1);
    
    TEST_ASSERT_NOT_NULL_MESSAGE(result, "native_for_star_thunk_executor should return a result");
    if (!result) return;
    
    TEST_ASSERT_TRUE_MESSAGE(is_list_type(TAG(result)), "Result should be a list");
    if (!is_list_type(TAG(result))) return;
    
    CljList *result_list = as_list(result);
    ID first = LIST_FIRST(result_list);
    ID rest = LIST_REST(result_list);
    
    // Check first element: should be [1 3]
    TEST_ASSERT_NOT_NULL_MESSAGE(first, "First element should not be NULL");
    if (!first) return;
    
    TEST_ASSERT_TRUE_MESSAGE(is_vector(first), "First element should be a vector");
    if (!is_vector(first)) return;
    
    CljVector *first_vec = as_vector(first);
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, vector_count(first_vec), "First vector should have 2 elements");
    if (vector_count(first_vec) != 2) return;
    
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, as_fixnum(vector_nth(first_vec, 0)), "First element should be 1");
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, as_fixnum(vector_nth(first_vec, 1)), "First element should be 3");
    
    // Check that rest is a lazy sequence
    TEST_ASSERT_NOT_NULL_MESSAGE(rest, "Rest should not be NULL");
    if (!rest) {
        return;
    }
    
    TEST_ASSERT_TRUE_MESSAGE(TAG(rest) == CLJ_LAZY_SEQ, "Rest should be a lazy sequence");
    if (TAG(rest) != CLJ_LAZY_SEQ) {
        return;
    }
    
    // Realize the rest to get next element
    CljSeqIterator *rest_seq = make_seq(rest);
    TEST_ASSERT_NOT_NULL_MESSAGE(rest_seq, "rest_seq should not be NULL");
    if (!rest_seq) return;
    
    ID rest_first = seq_first((ID)rest_seq);
    RELEASE(rest_seq);
    TEST_ASSERT_NOT_NULL_MESSAGE(rest_first, "Rest first should not be NULL after realization");
    if (!rest_first) return;
    
    // rest_first should be the body_result (a vector)
    TEST_ASSERT_TRUE_MESSAGE(is_vector(rest_first), "Rest first should be a vector (the body_result)");
    if (!is_vector(rest_first)) return;
    
    // Check second element: should be [1 4]
    CljVector *second_vec = as_vector(rest_first);
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, vector_count(second_vec), "Second vector should have 2 elements");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, as_fixnum(vector_nth(second_vec, 0)), "Second element should be 1");
    TEST_ASSERT_EQUAL_INT_MESSAGE(4, as_fixnum(vector_nth(second_vec, 1)), "Second element should be 4");
    
    // Get third element: should be [2 3]
    // Use lazy->cached_rest directly (it's a LazySeq)
    CljLazySeq *lazy_after = as_lazy_seq(rest);
    ID rest_second = NULL;
    if (lazy_after && lazy_after->cached_rest != NOT_FOUND) {
        rest_second = lazy_after->cached_rest;
    }
    TEST_ASSERT_NOT_NULL_MESSAGE(rest_second, "Rest second should not be NULL");
    if (rest_second) {
        // rest_second is a LazySeq, realize it
        CljSeqIterator *rest_second_seq = make_seq(rest_second);
        if (rest_second_seq) {
            ID third = seq_first((ID)rest_second_seq);
            RELEASE(rest_second_seq);
            CljLazySeq *lazy_second = as_lazy_seq(rest_second);
            TEST_ASSERT_NOT_NULL_MESSAGE(third, "Third element should not be NULL");
            if (third && is_vector(third)) {
                CljVector *third_vec = as_vector(third);
                TEST_ASSERT_EQUAL_INT_MESSAGE(2, vector_count(third_vec), "Third vector should have 2 elements");
                TEST_ASSERT_EQUAL_INT_MESSAGE(2, as_fixnum(vector_nth(third_vec, 0)), "Third element should be 2");
                TEST_ASSERT_EQUAL_INT_MESSAGE(3, as_fixnum(vector_nth(third_vec, 1)), "Third element should be 3");
            }
            // Get fourth element: should be [2 4]
            if (lazy_second && lazy_second->cached_rest != NOT_FOUND) {
                ID rest_third = lazy_second->cached_rest;
                if (rest_third) {
                    CljSeqIterator *rest_third_seq = make_seq(rest_third);
                    if (rest_third_seq) {
                        ID fourth = seq_first((ID)rest_third_seq);
                        TEST_ASSERT_NOT_NULL_MESSAGE(fourth, "Fourth element should not be NULL");
                        if (fourth && is_vector(fourth)) {
                            CljVector *fourth_vec = as_vector(fourth);
                            TEST_ASSERT_EQUAL_INT_MESSAGE(2, vector_count(fourth_vec), "Fourth vector should have 2 elements");
                            TEST_ASSERT_EQUAL_INT_MESSAGE(2, as_fixnum(vector_nth(fourth_vec, 0)), "Fourth element should be 2");
                            TEST_ASSERT_EQUAL_INT_MESSAGE(4, as_fixnum(vector_nth(fourth_vec, 1)), "Fourth element should be 4");
                        }
                        RELEASE(rest_third_seq);
                    }
                }
            }
        }
    }
    
    // Cleanup
    RELEASE(seq1);
    RELEASE(seq2);
    RELEASE(coll1);
    RELEASE(coll2);
}
