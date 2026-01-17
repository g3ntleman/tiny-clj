/*
 * Unity Static Keyword Tests for Tiny-CLJ
 *
 * Ensures commonly used keywords are pre-interned as static symbols and
 * returned by pointer identity via intern_symbol_global().
 */

#include "tests_common.h"

TEST(test_static_keywords_are_interned_singletons)
{
    // init_special_symbols() is executed once in unity_test_runner setUp().
    TEST_ASSERT_NOT_NULL(SYM_KW_VALUE);
    TEST_ASSERT_NOT_NULL(SYM_KW_CLOSED);
    TEST_ASSERT_NOT_NULL(SYM_KW_DATA);
    TEST_ASSERT_NOT_NULL(SYM_KW_FROM);
    TEST_ASSERT_NOT_NULL(SYM_KW_TO);
    TEST_ASSERT_NOT_NULL(SYM_KW_PORT);
    TEST_ASSERT_NOT_NULL(SYM_KW_COLUMN);
    TEST_ASSERT_NOT_NULL(SYM_KW_FN);
    TEST_ASSERT_NOT_NULL(SYM_KW_PATH);
    TEST_ASSERT_NOT_NULL(SYM_KW_HOST_OS);
    TEST_ASSERT_NOT_NULL(SYM_KW_MACRO);

    TEST_ASSERT_TRUE(IS_KEYWORD((ID)SYM_KW_VALUE));
    TEST_ASSERT_TRUE(IS_KEYWORD((ID)SYM_KW_CLOSED));
    TEST_ASSERT_TRUE(IS_KEYWORD((ID)SYM_KW_DATA));
    TEST_ASSERT_TRUE(IS_KEYWORD((ID)SYM_KW_FROM));
    TEST_ASSERT_TRUE(IS_KEYWORD((ID)SYM_KW_TO));
    TEST_ASSERT_TRUE(IS_KEYWORD((ID)SYM_KW_PORT));
    TEST_ASSERT_TRUE(IS_KEYWORD((ID)SYM_KW_COLUMN));
    TEST_ASSERT_TRUE(IS_KEYWORD((ID)SYM_KW_FN));
    TEST_ASSERT_TRUE(IS_KEYWORD((ID)SYM_KW_PATH));
    TEST_ASSERT_TRUE(IS_KEYWORD((ID)SYM_KW_HOST_OS));
    TEST_ASSERT_TRUE(IS_KEYWORD((ID)SYM_KW_MACRO));

    // Pointer identity: calling intern_symbol_global should return the same singleton.
    TEST_ASSERT_EQUAL_PTR(SYM_KW_VALUE, intern_symbol_global(":value"));
    TEST_ASSERT_EQUAL_PTR(SYM_KW_CLOSED, intern_symbol_global(":closed"));
    TEST_ASSERT_EQUAL_PTR(SYM_KW_DATA, intern_symbol_global(":data"));
    TEST_ASSERT_EQUAL_PTR(SYM_KW_FROM, intern_symbol_global(":from"));
    TEST_ASSERT_EQUAL_PTR(SYM_KW_TO, intern_symbol_global(":to"));
    TEST_ASSERT_EQUAL_PTR(SYM_KW_PORT, intern_symbol_global(":port"));
    TEST_ASSERT_EQUAL_PTR(SYM_KW_COLUMN, intern_symbol_global(":column"));
    TEST_ASSERT_EQUAL_PTR(SYM_KW_FN, intern_symbol_global(":fn"));
    TEST_ASSERT_EQUAL_PTR(SYM_KW_PATH, intern_symbol_global(":path"));
    TEST_ASSERT_EQUAL_PTR(SYM_KW_HOST_OS, intern_symbol_global(":host-os"));
    TEST_ASSERT_EQUAL_PTR(SYM_KW_MACRO, intern_symbol_global(":macro"));

    // Sanity: names match expected values.
    TEST_ASSERT_EQUAL_STRING(":value", as_symbol((ID)SYM_KW_VALUE)->cname);
    TEST_ASSERT_EQUAL_STRING(":closed", as_symbol((ID)SYM_KW_CLOSED)->cname);
    TEST_ASSERT_EQUAL_STRING(":data", as_symbol((ID)SYM_KW_DATA)->cname);
    TEST_ASSERT_EQUAL_STRING(":from", as_symbol((ID)SYM_KW_FROM)->cname);
    TEST_ASSERT_EQUAL_STRING(":to", as_symbol((ID)SYM_KW_TO)->cname);
    TEST_ASSERT_EQUAL_STRING(":port", as_symbol((ID)SYM_KW_PORT)->cname);
    TEST_ASSERT_EQUAL_STRING(":column", as_symbol((ID)SYM_KW_COLUMN)->cname);
    TEST_ASSERT_EQUAL_STRING(":fn", as_symbol((ID)SYM_KW_FN)->cname);
    TEST_ASSERT_EQUAL_STRING(":path", as_symbol((ID)SYM_KW_PATH)->cname);
    TEST_ASSERT_EQUAL_STRING(":host-os", as_symbol((ID)SYM_KW_HOST_OS)->cname);
    TEST_ASSERT_EQUAL_STRING(":macro", as_symbol((ID)SYM_KW_MACRO)->cname);
}

TEST(test_static_symbols_are_interned_singletons)
{
    // init_special_symbols() is executed once in unity_test_runner setUp().
    TEST_ASSERT_NOT_NULL(SYM_INC);
    TEST_ASSERT_NOT_NULL(SYM_MATH);
    TEST_ASSERT_NOT_NULL(SYM_PERCENT);

    TEST_ASSERT_FALSE(IS_KEYWORD((ID)SYM_INC));
    TEST_ASSERT_FALSE(IS_KEYWORD((ID)SYM_MATH));
    TEST_ASSERT_FALSE(IS_KEYWORD((ID)SYM_PERCENT));

    TEST_ASSERT_EQUAL_PTR(SYM_INC, intern_symbol_global("inc"));
    TEST_ASSERT_EQUAL_PTR(SYM_MATH, intern_symbol_global("Math"));
    TEST_ASSERT_EQUAL_PTR(SYM_PERCENT, intern_symbol_global("%"));

    TEST_ASSERT_EQUAL_STRING("inc", as_symbol((ID)SYM_INC)->cname);
    TEST_ASSERT_EQUAL_STRING("Math", as_symbol((ID)SYM_MATH)->cname);
    TEST_ASSERT_EQUAL_STRING("%", as_symbol((ID)SYM_PERCENT)->cname);
}

