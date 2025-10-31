// Vector-spezifische Tests
#include "tests_common.h"

TEST(test_vector_builtin_basic) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);

        // (vector) => []
    CljObject *v0 = eval_string("(vector)", st);
    TEST_ASSERT_NOT_NULL(v0);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, v0->type);
    CljObject *c0 = eval_string("(count (vector))", st);
    TEST_ASSERT_NOT_NULL(c0);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)c0));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum((CljValue)c0));

        // (vector 1 2 3) => [1 2 3]
    CljObject *v3 = eval_string("(vector 1 2 3)", st);
    TEST_ASSERT_NOT_NULL(v3);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, v3->type);
    CljObject *n0 = eval_string("(nth (vector 1 2 3) 0)", st);
    TEST_ASSERT_NOT_NULL(n0);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)n0));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum((CljValue)n0));
    CljObject *n2 = eval_string("(nth (vector 1 2 3) 2)", st);
    TEST_ASSERT_NOT_NULL(n2);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)n2));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)n2));

    evalstate_free(st);
}

TEST(test_nth_with_default_and_bounds) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);

        // In-bounds ohne Default
    CljObject *x = eval_string("(nth [10 20 30] 1)", st);
    TEST_ASSERT_NOT_NULL(x);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)x));
    TEST_ASSERT_EQUAL_INT(20, as_fixnum((CljValue)x));

        // Out-of-bounds mit Default
    CljObject *d = eval_string("(nth [10 20 30] 5 :na)", st);
    TEST_ASSERT_NOT_NULL(d);
    TEST_ASSERT_TRUE(is_type(d, CLJ_SYMBOL));

    evalstate_free(st);
}

TEST(test_peek_and_pop_vector) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);

        // peek
    CljObject *p1 = eval_string("(peek [1 2 3])", st);
    TEST_ASSERT_NOT_NULL(p1);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)p1));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)p1));
    CljObject *p0 = eval_string("(peek [])", st);
    TEST_ASSERT_NULL(p0);

        // pop
    CljObject *pop1 = eval_string("(pop [1 2 3])", st);
    TEST_ASSERT_NOT_NULL(pop1);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, pop1->type);
    CljObject *cnt = eval_string("(count (pop [1 2 3]))", st);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)cnt));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)cnt));

    evalstate_free(st);
}

TEST(test_subvec_bounds_and_slices) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);

    CljObject *s1 = eval_string("(subvec [1 2 3 4] 1 3)", st);
    TEST_ASSERT_NOT_NULL(s1);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, s1->type);
    CljObject *s1n0 = eval_string("(nth (subvec [1 2 3 4] 1 3) 0)", st);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)s1n0));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)s1n0));

        // start-only
    CljObject *s2c = eval_string("(count (subvec [1 2 3 4] 2))", st);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)s2c));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)s2c));

    evalstate_free(st);
}

TEST(test_vec_from_list_and_vector_id) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);

    CljObject *v = eval_string("(vec '(1 2 3))", st);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, v->type);
    CljObject *c = eval_string("(count (vec [1 2 3]))", st);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)c));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)c));

    evalstate_free(st);
}

