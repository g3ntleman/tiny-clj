#include "test_common.h"

TEST(test_fixnum_helpers) {
    CljValue v = fixnum(12345);
    TEST_ASSERT_TRUE(is_fixnum(v));
    TEST_ASSERT_EQUAL_INT(12345, as_fixnum(v));
    TEST_ASSERT_TRUE(IS_IMMEDIATE(v));
}

TEST(test_character_helpers) {
    CljValue ch = character('A');
    TEST_ASSERT_TRUE(is_character(ch));
    TEST_ASSERT_EQUAL_UINT32('A', as_character(ch));
}

TEST(test_fixed_point_helpers) {
    CljValue fx = fixed(12.5f);
    TEST_ASSERT_TRUE(is_fixed(fx));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 12.5f, as_fixed(fx));
}

TEST(test_immediate_detection) {
    CljValue immediate = fixnum(1);
    TEST_ASSERT_TRUE(is_immediate(immediate));

    CljString *s = make_string("heap");
    TEST_ASSERT_FALSE(is_immediate((CljValue)s));
    RELEASE(s);
}
