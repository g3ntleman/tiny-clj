#include "test_common.h"

static void expect_string_equals(CljString *s, const char *expected) {
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_STRING(expected, string_data(s));
}

TEST(test_make_clj_string_basic) {
    CljString *s = make_clj_string("abc");
    expect_string_equals(s, "abc");
    TEST_ASSERT_EQUAL_UINT16(3, s->length);
    RELEASE((CljObject*)s);
}

TEST(test_make_string_returns_singleton_for_empty) {
    CljString *s = make_string("");
    TEST_ASSERT_EQUAL_PTR(string_empty_singleton, s);
}

TEST(test_make_string_buffer_zero_filled) {
    CljString *buf = make_string_buffer(5);
    TEST_ASSERT_NOT_NULL(buf);
    TEST_ASSERT_EQUAL_UINT16(5, buf->length);
    for (size_t i = 0; i < 5; ++i) {
        TEST_ASSERT_EQUAL_CHAR(0, buf->data[i]);
    }
    RELEASE((CljObject*)buf);
}

// Tests for to_string, pr_str, strings_register_special_form, etc. 
// have been moved to src/tests/ since these functions are now in src/object.c and src/symbol.c

