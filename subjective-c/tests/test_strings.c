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

TEST(test_to_string_special_forms_flag) {
    WITH_AUTORELEASE_POOL({
        bool prev = strings_set_special_form_rendering(true);
        CljString *test_str = (CljString*)AUTORELEASE(make_string("test-string"));
        CljString *str1 = (CljString*)AUTORELEASE(to_string((ID)test_str));
        TEST_ASSERT_NOT_NULL(str1);
        const char *str1_data = string_data(str1);
        TEST_ASSERT_NOT_NULL(str1_data);
        TEST_ASSERT_EQUAL_STRING("test-string", str1_data);
        
        strings_set_special_form_rendering(false);
        CljString *str2 = (CljString*)AUTORELEASE(to_string((ID)test_str));
        TEST_ASSERT_NOT_NULL(str2);
        TEST_ASSERT_EQUAL_STRING("test-string", string_data(str2));
        
        strings_set_special_form_rendering(prev);
    });
}

TEST(test_to_string_namespace_rendering) {
    WITH_AUTORELEASE_POOL({
        CljString *test_str = (CljString*)AUTORELEASE(make_string("my.ns/my-symbol"));
        CljString *str = (CljString*)AUTORELEASE(to_string((ID)test_str));
        TEST_ASSERT_NOT_NULL(str);
        const char *result = string_data(str);
        TEST_ASSERT_NOT_NULL(strstr(result, "my.ns"));
        TEST_ASSERT_NOT_NULL(strstr(result, "my-symbol"));
        TEST_ASSERT_NOT_NULL(strstr(result, "/"));
    });
}

TEST(test_pr_str_with_escape) {
    WITH_AUTORELEASE_POOL({
        CljString *test_str = (CljString*)AUTORELEASE(make_string("test\"quote\\backslash"));
        TEST_ASSERT_NOT_NULL(test_str);
        
        CljString *escaped = (CljString*)AUTORELEASE(pr_str((ID)test_str));
        TEST_ASSERT_NOT_NULL(escaped);
        const char *escaped_data = string_data(escaped);
        TEST_ASSERT_NOT_NULL(escaped_data);
        TEST_ASSERT_NOT_NULL(strstr(escaped_data, "\\\""));
        TEST_ASSERT_NOT_NULL(strstr(escaped_data, "\\\\"));
        
        CljString *unescaped = (CljString*)AUTORELEASE(to_string((ID)test_str));
        TEST_ASSERT_NOT_NULL(unescaped);
        const char *unescaped_data = string_data(unescaped);
        TEST_ASSERT_EQUAL_STRING("test\"quote\\backslash", unescaped_data);
        
        CljString *escaped_direct = (CljString*)AUTORELEASE(to_string_with_escape((ID)test_str, true));
        TEST_ASSERT_NOT_NULL(escaped_direct);
        const char *direct_data = string_data(escaped_direct);
        TEST_ASSERT_NOT_NULL(strstr(direct_data, "\\\""));
    });
}

TEST(test_special_form_registry) {
    WITH_AUTORELEASE_POOL({
        strings_clear_special_forms();
        
        CljString *test_str = (CljString*)AUTORELEASE(make_string("custom-form"));
        CljString *str1 = (CljString*)AUTORELEASE(to_string((ID)test_str));
        TEST_ASSERT_NOT_NULL(str1);
        const char *str1_data = string_data(str1);
        TEST_ASSERT_NOT_NULL(str1_data);
        TEST_ASSERT_EQUAL_STRING("custom-form", str1_data);
        
        strings_register_special_form("custom-form");
        
        bool prev = strings_set_special_form_rendering(true);
        CljString *str2 = (CljString*)AUTORELEASE(to_string((ID)test_str));
        TEST_ASSERT_NOT_NULL(str2);
        const char *str2_data = string_data(str2);
        TEST_ASSERT_NOT_NULL(str2_data);
        TEST_ASSERT_EQUAL_STRING("custom-form", str2_data);
        
        strings_register_special_form("another-form");
        
        CljString *another_str = (CljString*)AUTORELEASE(make_string("another-form"));
        CljString *str3 = (CljString*)AUTORELEASE(to_string((ID)another_str));
        TEST_ASSERT_NOT_NULL(str3);
        const char *str3_data = string_data(str3);
        TEST_ASSERT_NOT_NULL(str3_data);
        TEST_ASSERT_EQUAL_STRING("another-form", str3_data);
        
        strings_set_special_form_rendering(prev);
    });
}

