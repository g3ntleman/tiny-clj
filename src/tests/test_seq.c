/*
 * Seq Tests using Unity Framework
 * 
 * Tests for sequence semantics and iterator-based implementation.
 */

#include "tests_common.h"
#include "../list.h"
#include "../seq.h"
#include "../function.h"
#include "../builtins.h"

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
// LAZY-SEQ TESTS
// ============================================================================

// Generator-Funktion für Tests: gibt nächste lazy-seq zurück
static ID test_lazy_seq_generator(ID *args, unsigned int argc) {
    (void)args; (void)argc;
    // Generator gibt lazy-seq mit next value zurück
    return make_lazy_seq(fixnum(43), NULL);
}

// Generator-Funktion für Tests: gibt normale Sequenz zurück
static ID test_normal_seq_generator(ID *args, unsigned int argc) __attribute__((unused));
static ID test_normal_seq_generator(ID *args, unsigned int argc) {
    (void)args; (void)argc;
    // Generator gibt normale Sequenz zurück (CLJ_SEQ)
    ID vec = make_vector(1, CLJ_VECTOR);
    CljVector *v = as_vector(vec);
    v = vector_conj(v, fixnum(43));
    return make_seq(vec);
}

TEST_SHARED(test_make_lazy_seq_creation) {
    ID first = fixnum(42);
    ID rest_fn = NULL; // Generator-Funktion später
    ID lazy = make_lazy_seq(first, rest_fn);
    TEST_ASSERT_NOT_NULL(lazy);
    TEST_ASSERT_EQUAL_INT(CLJ_LAZY_SEQ, TAG(lazy));
    CljLazySeq *ls = as_lazy_seq(lazy);
    TEST_ASSERT_NOT_NULL(ls);
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(ls->first));
    TEST_ASSERT_NULL(ls->rest_fn);
    TEST_ASSERT_NULL(ls->cached_rest);
    RELEASE(lazy);
}

TEST_SHARED(test_seq_first_lazy_seq) {
    ID first = fixnum(100);
    ID lazy = make_lazy_seq(first, NULL);
    ID result = seq_first(lazy);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(100, as_fixnum(result));
    RELEASE(result);
    RELEASE(lazy);
}

TEST_SHARED(test_seq_first_lazy_seq_nil) {
    ID lazy = make_lazy_seq(NULL, NULL);
    ID result = seq_first(lazy);
    TEST_ASSERT_NULL(result);
    RELEASE(lazy);
}

TEST_SHARED(test_seq_empty_lazy_seq) {
    ID lazy_empty = make_lazy_seq(NULL, NULL);
    TEST_ASSERT_TRUE(seq_empty(lazy_empty));
    RELEASE(lazy_empty);
    
    ID lazy_nonempty = make_lazy_seq(fixnum(42), NULL);
    TEST_ASSERT_FALSE(seq_empty(lazy_nonempty));
    RELEASE(lazy_nonempty);
}

TEST_SHARED(test_seq_rest_lazy_seq_calls_generator) {
    // Erstelle Generator-Funktion, die eine neue Sequenz zurückgibt
    ID rest_fn = make_named_func(test_lazy_seq_generator, "test-gen");
    ID lazy = make_lazy_seq(fixnum(42), rest_fn);
    
    ID rest1 = seq_rest(lazy);
    TEST_ASSERT_NOT_NULL(rest1);
    // rest1 ist eine NEUE Sequenz (nicht die gleiche lazy-seq)
    TEST_ASSERT_TRUE(rest1 != lazy);
    TEST_ASSERT_EQUAL_INT(43, as_fixnum(seq_first(rest1)));
    
    // Caching: Zweiter Aufruf sollte gecachtes Ergebnis zurückgeben
    ID rest2 = seq_rest(lazy);
    TEST_ASSERT_EQUAL_PTR(rest1, rest2); // Gleiche Sequenz (gecacht)
    
    RELEASE(rest1);
    RELEASE(rest2);
    RELEASE(rest_fn);
    RELEASE(lazy);
}

TEST_SHARED(test_seq_rest_lazy_seq_memory_management) {
    // Test Memory Management: cached_rest sollte korrekt gecacht werden
    ID rest_fn = make_named_func(test_lazy_seq_generator, "test-gen");
    ID lazy = make_lazy_seq(fixnum(42), rest_fn);
    
    ID rest1 = seq_rest(lazy);
    CljLazySeq *ls = as_lazy_seq(lazy);
    TEST_ASSERT_NOT_NULL(ls->cached_rest);
    // cached_rest sollte mit RETAIN gesetzt sein (Memory Policy)
    TEST_ASSERT_EQUAL_PTR(rest1, ls->cached_rest);
    
    // Zweiter Aufruf sollte gecachtes Ergebnis zurückgeben
    ID rest2 = seq_rest(lazy);
    TEST_ASSERT_EQUAL_PTR(rest1, rest2);
    
    RELEASE(rest1);
    RELEASE(rest2);
    RELEASE(rest_fn);
    RELEASE(lazy);
}

TEST_SHARED(test_seq_next_lazy_seq) {
    ID rest_fn = make_named_func(test_lazy_seq_generator, "test-gen");
    ID lazy = make_lazy_seq(fixnum(42), rest_fn);
    
    ID next = seq_next(lazy);
    TEST_ASSERT_NOT_NULL(next);
    TEST_ASSERT_EQUAL_INT(43, as_fixnum(seq_first(next)));
    
    // Leere lazy-seq
    ID empty_lazy = make_lazy_seq(NULL, NULL);
    TEST_ASSERT_NULL(seq_next(empty_lazy));
    
    RELEASE(next);
    RELEASE(rest_fn);
    RELEASE(lazy);
    RELEASE(empty_lazy);
}

TEST_SHARED(test_seq_release_lazy_seq) {
    ID first = fixnum(42);
    ID rest_fn = make_named_func(test_lazy_seq_generator, "test-gen");
    ID lazy = make_lazy_seq(RETAIN(first), RETAIN(rest_fn));
    
    // Test Memory Management
    seq_release(lazy);
    // Objekte sollten freigegeben sein (Memory Profiler prüft)
    
    RELEASE(first);
    RELEASE(rest_fn);
}

TEST_SHARED(test_is_seqable_lazy_seq) {
    ID lazy = make_lazy_seq(fixnum(42), NULL);
    TEST_ASSERT_TRUE(is_seqable(lazy));
    TEST_ASSERT_TRUE(is_seq(lazy));
    RELEASE(lazy);
}

TEST_SHARED(test_clj_type_name_lazy_seq) {
    TEST_ASSERT_EQUAL_STRING("LazySeq", clj_type_name(CLJ_LAZY_SEQ));
}

TEST_SHARED(test_seq_next_inplace_lazy_seq_recycles) {
    // Erstelle Generator-Funktion, die eine neue lazy-seq zurückgibt
    ID rest_fn = make_named_func(test_lazy_seq_generator, "test-gen");
    ID lazy = make_lazy_seq(fixnum(42), rest_fn);
    TEST_ASSERT_EQUAL_INT(1, ((CljLazySeq*)lazy)->base.rc);
    
    // seq_next_inplace sollte lazy-seq recyclen (rc==1 und nächste Sequenz ist lazy-seq)
    ID result = seq_next_inplace(lazy);
    TEST_ASSERT_EQUAL_PTR(lazy, result);
    
    // Lazy-seq sollte jetzt auf nächste Sequenz zeigen
    CljLazySeq *ls = as_lazy_seq(lazy);
    TEST_ASSERT_EQUAL_INT(43, as_fixnum(ls->first));
    TEST_ASSERT_NULL(ls->cached_rest);
    
    RELEASE(rest_fn);
    RELEASE(lazy);
}

TEST_SHARED(test_seq_next_inplace_lazy_seq_no_recycle_rc_greater_one) {
    ID rest_fn = make_named_func(test_lazy_seq_generator, "test-gen");
    ID lazy = make_lazy_seq(fixnum(42), rest_fn);
    RETAIN(lazy); // rc wird jetzt 2
    
    // Bei rc>1 sollte kein Recycling stattfinden
    ID result = seq_next_inplace(lazy);
    TEST_ASSERT_TRUE(lazy != result);
    
    RELEASE(rest_fn);
    RELEASE(lazy);
    RELEASE(result);
}

TEST_SHARED(test_lazy_seq_memoization_two_branches) {
    if (!g_test_eval_state) {
        TEST_FAIL_MESSAGE("Failed to create EvalState");
        return;
    }
    
    // Erstelle eine lazy-seq und speichere sie in einer Variable
    // Dann verwenden wir zwei verschiedene Zweige, die beide auf die gleiche seq zugreifen
    const char *code = "(let [s (repeat 100)] "
                       "(let [branch1 (rest s) "
                       "      branch2 (rest s)] "
                       "(and (= (first branch1) 100) "
                       "     (= (first branch2) 100) "
                       "     (= (first branch1) (first branch2)))))";
    
    CljObject *result = eval_string(code, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(clj_is_truthy(result));
    
    // Test: Beide Zweige sollten das gleiche gecachte Ergebnis erhalten
    // Das bedeutet, dass rest nur einmal evaluiert wurde und beide branch1 und branch2
    // auf das gleiche gecachte Objekt zeigen
    const char *code2 = "(let [s (repeat 100) "
                        "      branch1 (rest s) "
                        "      branch2 (rest s)] "
                        "(identical? branch1 branch2))";
    
    CljObject *identical_result = eval_string(code2, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(identical_result);
    TEST_ASSERT_TRUE(clj_is_truthy(identical_result));
    
    // Test: Beide Zweige können unabhängig weiter verwendet werden
    const char *code3 = "(let [s (repeat 100) "
                        "      branch1 (rest s) "
                        "      branch2 (rest s)] "
                        "(and (= (first (rest branch1)) 100) "
                        "     (= (first (rest branch2)) 100)))";
    
    CljObject *independent_result = eval_string(code3, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(independent_result);
    TEST_ASSERT_TRUE(clj_is_truthy(independent_result));
}

TEST_SHARED(test_lazy_seq_memoization_different_rest_counts) {
    if (!g_test_eval_state) {
        TEST_FAIL_MESSAGE("Failed to create EvalState");
        return;
    }
    
    // Erstelle eine lazy-seq und verwende zwei Branches mit unterschiedlich vielen rest-Aufrufen
    // Branch1: rest einmal, dann nochmal rest
    // Branch2: rest einmal
    // Beide sollten korrekt funktionieren und die Memoization sollte funktionieren
    const char *code = "(let [s (repeat 42) "
                       "      branch1 (rest s) "
                       "      branch2 (rest s)] "
                       "(let [branch1_next (rest branch1)] "
                       "(and (= (first branch1) 42) "
                       "     (= (first branch2) 42) "
                       "     (= (first branch1_next) 42) "
                       "     (= (first branch1) (first branch2)))))";
    
    CljObject *result = eval_string(code, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(clj_is_truthy(result));
    
    // Test: Branch1 wird weiter verwendet (mehrfach rest), Branch2 bleibt auf erstem rest
    // Beide sollten unabhängig korrekt funktionieren
    const char *code2 = "(let [s (repeat 100) "
                        "      branch1 (rest s) "
                        "      branch2 (rest s)] "
                        "(let [branch1_rest2 (rest branch1) "
                        "      branch1_rest3 (rest branch1_rest2)] "
                        "(and (= (first branch1) 100) "
                        "     (= (first branch2) 100) "
                        "     (= (first branch1_rest2) 100) "
                        "     (= (first branch1_rest3) 100))))";
    
    CljObject *result2 = eval_string(code2, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(clj_is_truthy(result2));
    
    // Test: Prüfe, dass beide Branches identisch sind beim ersten rest
    // (gleiche Objektreferenz durch Memoization)
    const char *code3 = "(let [s (repeat 200) "
                        "      branch1 (rest s) "
                        "      branch2 (rest s)] "
                        "(identical? branch1 branch2))";
    
    CljObject *identical_result = eval_string(code3, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(identical_result);
    TEST_ASSERT_TRUE(clj_is_truthy(identical_result));
}

TEST_SHARED(test_range_infinite_lazy_seq) {
    if (!g_test_eval_state) {
        TEST_FAIL_MESSAGE("Failed to create EvalState");
        return;
    }

    // Test: (range) ohne Argumente gibt infinite lazy-seq zurück
    const char *code = "(first (range))";
    CljObject *result = eval_string(code, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_INT);
    TEST_ASSERT_EQUAL_INT(0, AS_FIXNUM(result));

    // Test: (take 5 (range)) gibt (0 1 2 3 4) zurück
    const char *code2 = "(take 5 (range))";
    CljObject *result2 = eval_string(code2, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(TAG(result2) == CLJ_LIST);

    // Test: (first (rest (range))) gibt 1 zurück
    const char *code3 = "(first (rest (range)))";
    CljObject *result3 = eval_string(code3, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result3);
    TEST_ASSERT_TRUE(TAG(result3) == CLJ_INT);
    TEST_ASSERT_EQUAL_INT(1, AS_FIXNUM(result3));
}

TEST_SHARED(test_range_memoization) {
    if (!g_test_eval_state) {
        TEST_FAIL_MESSAGE("Failed to create EvalState");
        return;
    }

    // Test: Memoization bei mehreren rest-Aufrufen auf derselben range
    const char *code = "(let [r (range) "
                       "      branch1 (rest r) "
                       "      branch2 (rest r)] "
                       "(and (= (first branch1) 1) "
                       "     (= (first branch2) 1) "
                       "     (identical? branch1 branch2)))";

    CljObject *result = eval_string(code, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(clj_is_truthy(result));
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
    CljVector *vec = as_vector(entry);
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
    ID vec1 = AUTORELEASE(make_vector(2, CLJ_VECTOR));
    ID vec2 = AUTORELEASE(make_vector(2, CLJ_VECTOR));
    CljVector *v1 = as_vector(vec1), *v2 = as_vector(vec2);
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
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, TAG(result));
    CljVector *vec = as_vector(result);
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
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, TAG(eval_string("(first (seq {:a 1 :b 2}))", g_test_eval_state)));
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
    CljVector *vec = as_vector(entry);
    TEST_ASSERT_EQUAL_INT(2, vector_count(vec));
    TEST_ASSERT_EQUAL_INT(CLJ_SYMBOL, TAG(vector_nth(vec, 0)));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(vector_nth(vec, 1)));
}

// ============================================================================
// COW TESTS FOR SEGITERATOR
// ============================================================================

TEST_SHARED(test_seq_cow_multiple_sequences_same_container) {
    ID vec = AUTORELEASE(make_vector(4, CLJ_VECTOR));
    CljVector *v = as_vector(vec);
    v = vector_conj(vector_conj(vector_conj(v, fixnum(1)), fixnum(2)), fixnum(3));
    ID seq1 = AUTORELEASE(make_seq(vec));
    ID seq2 = AUTORELEASE(make_seq(vec));
    RETAIN(vec);
    CljVector *new_vec = vector_conj(v, fixnum(4));
    TEST_ASSERT_TRUE(v != new_vec);
    TEST_ASSERT_EQUAL_INT(3, vector_count(v));
    TEST_ASSERT_EQUAL_PTR(v, as_seq(seq1)->iter.container);
    TEST_ASSERT_EQUAL_PTR(v, as_seq(seq2)->iter.container);
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(seq_first(seq1)));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(seq_first(seq2)));
    RELEASE(vec);
}

TEST_SHARED(test_seq_cow_rc_one_inplace) {
    ID vec = AUTORELEASE(make_vector(4, CLJ_VECTOR));
    CljVector *v = as_vector(vec);
    v = vector_conj(vector_conj(v, fixnum(1)), fixnum(2));
    ID seq = AUTORELEASE(make_seq(vec));
    CljVector *new_vec = vector_conj(v, fixnum(3));
    TEST_ASSERT_TRUE(v == new_vec);
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(seq_first(seq)));
}

TEST_SHARED(test_seq_cow_rc_greater_one_copy_on_write) {
    ID vec = AUTORELEASE(make_vector(4, CLJ_VECTOR));
    CljVector *v = as_vector(vec);
    v = vector_conj(vector_conj(v, fixnum(1)), fixnum(2));
    ID seq = AUTORELEASE(make_seq(vec));
    RETAIN(vec);
    CljVector *new_vec = vector_conj(v, fixnum(3));
    TEST_ASSERT_TRUE(v != new_vec);
    TEST_ASSERT_EQUAL_INT(2, vector_count(v));
    TEST_ASSERT_EQUAL_PTR(v, as_seq(seq)->iter.container);
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(seq_first(seq)));
    RELEASE(vec);
}

TEST_SHARED(test_seq_cow_multiple_sequences_preserved) {
    ID vec = AUTORELEASE(make_vector(4, CLJ_VECTOR));
    CljVector *v = as_vector(vec);
    v = vector_conj(vector_conj(vector_conj(v, fixnum(10)), fixnum(20)), fixnum(30));
    ID seq1 = AUTORELEASE(make_seq(vec));
    ID seq2 = AUTORELEASE(make_seq(vec));
    ID seq3 = AUTORELEASE(make_seq(vec));
    RETAIN(vec); RETAIN(vec);
    CljVector *new_vec = vector_conj(v, fixnum(40));
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
    ID vec = AUTORELEASE(make_vector(4, CLJ_VECTOR));
    CljVector *v = as_vector(vec);
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
