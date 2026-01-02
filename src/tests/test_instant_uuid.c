#include "tests_common.h"

#include "../hash.h"
#include "../to_string.h"

#include <subjective-c/instant.h>
#include <subjective-c/uuid.h>

TEST(test_now_returns_instant) {
    ID result = eval_string("(now)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_INSTANT, TAG(result));
}

TEST(test_instant_equality_and_hash) {
    ID a = AUTORELEASE(clj_make_instant(123, 456));
    ID b = AUTORELEASE(clj_make_instant(123, 456));
    ID c = AUTORELEASE(clj_make_instant(123, 457));

    TEST_ASSERT_TRUE(clj_equal(a, b));
    TEST_ASSERT_FALSE(clj_equal(a, c));
    TEST_ASSERT_EQUAL_UINT32(clj_hash_full(a), clj_hash_full(b));
}

TEST(test_instant_pr_str_shape) {
    ID a = AUTORELEASE(clj_make_instant(1, 2));
    CljString *s = pr_str(a);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_STRING("#inst {:days 1 :ms 2}", clj_string_data(s));
    RELEASE(s);
}

TEST(test_uuid_parse_print_equal) {
    ID u1 = AUTORELEASE(clj_uuid_from_string("f81d4fae-7dec-11d0-a765-00a0c91e6bf6"));
    ID u2 = AUTORELEASE(clj_uuid_from_string("f81d4fae-7dec-11d0-a765-00a0c91e6bf6"));
    ID u3 = AUTORELEASE(clj_uuid_from_string("00000000-0000-0000-0000-000000000000"));

    TEST_ASSERT_NOT_NULL(u1);
    TEST_ASSERT_EQUAL_INT(CLJ_UUID, TAG(u1));
    TEST_ASSERT_TRUE(clj_equal(u1, u2));
    TEST_ASSERT_FALSE(clj_equal(u1, u3));
    TEST_ASSERT_EQUAL_UINT32(clj_hash_full(u1), clj_hash_full(u2));

    CljString *s = pr_str(u1);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_STRING("#uuid \"f81d4fae-7dec-11d0-a765-00a0c91e6bf6\"", clj_string_data(s));
    RELEASE(s);
}
