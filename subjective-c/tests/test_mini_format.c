/**
 * @file test_mini_format.c
 * @brief Tests for mini_format.h utility functions
 */

#include "test_common.h"
#include "mini_format.h"

#include <string.h>

// ---------------------------------------------------------------------------
// Tests for mini_snprintf
// ---------------------------------------------------------------------------

TEST(test_mini_snprintf_basic_string)
{
    char buf[32];
    int n = mini_snprintf(buf, sizeof(buf), "hello %s", "world");
    TEST_ASSERT_EQUAL_STRING("hello world", buf);
    TEST_ASSERT_EQUAL_INT(11, n);
}

TEST(test_mini_snprintf_integer)
{
    char buf[32];
    int n = mini_snprintf(buf, sizeof(buf), "value=%d", 42);
    TEST_ASSERT_EQUAL_STRING("value=42", buf);
    TEST_ASSERT_EQUAL_INT(8, n);
}

TEST(test_mini_snprintf_unsigned)
{
    char buf[32];
    int n = mini_snprintf(buf, sizeof(buf), "val=%u", 12345u);
    TEST_ASSERT_EQUAL_STRING("val=12345", buf);
    TEST_ASSERT_EQUAL_INT(9, n);
}

// ---------------------------------------------------------------------------
// Tests for zero-padding in format strings (%04u, %02d, etc.)
// ---------------------------------------------------------------------------

TEST(test_mini_snprintf_zeropad_unsigned)
{
    char buf[32];
    int n = mini_snprintf(buf, sizeof(buf), "%04u", 42u);
    TEST_ASSERT_EQUAL_STRING("0042", buf);
    TEST_ASSERT_EQUAL_INT(4, n);
}

TEST(test_mini_snprintf_zeropad_unsigned_no_pad_needed)
{
    char buf[32];
    int n = mini_snprintf(buf, sizeof(buf), "%04u", 12345u);
    TEST_ASSERT_EQUAL_STRING("12345", buf);
    TEST_ASSERT_EQUAL_INT(5, n);
}

TEST(test_mini_snprintf_zeropad_signed)
{
    char buf[32];
    int n = mini_snprintf(buf, sizeof(buf), "%04d", 7);
    TEST_ASSERT_EQUAL_STRING("0007", buf);
    TEST_ASSERT_EQUAL_INT(4, n);
}

TEST(test_mini_snprintf_zeropad_signed_negative)
{
    char buf[32];
    int n = mini_snprintf(buf, sizeof(buf), "%04d", -7);
    TEST_ASSERT_EQUAL_STRING("-007", buf);
    TEST_ASSERT_EQUAL_INT(4, n);
}

TEST(test_mini_snprintf_zeropad_hex)
{
    char buf[32];
    int n = mini_snprintf(buf, sizeof(buf), "%04x", 0xABu);
    TEST_ASSERT_EQUAL_STRING("00ab", buf);
    TEST_ASSERT_EQUAL_INT(4, n);
}

TEST(test_mini_snprintf_zeropad_hex_upper)
{
    char buf[32];
    int n = mini_snprintf(buf, sizeof(buf), "%08X", 0xCAFEu);
    TEST_ASSERT_EQUAL_STRING("0000CAFE", buf);
    TEST_ASSERT_EQUAL_INT(8, n);
}

TEST(test_mini_snprintf_iso_date_style)
{
    char buf[32];
    // Typical ISO-8601 date formatting: 2024-01-07
    int n = mini_snprintf(buf, sizeof(buf), "%04u-%02u-%02u", 2024u, 1u, 7u);
    TEST_ASSERT_EQUAL_STRING("2024-01-07", buf);
    TEST_ASSERT_EQUAL_INT(10, n);
}

// ---------------------------------------------------------------------------
// Tests for format_append helpers
// ---------------------------------------------------------------------------

TEST(test_format_append_basic)
{
    char buf[32];
    memset(buf, 0, sizeof(buf));

    size_t pos = format_append(buf, 0, sizeof(buf), "hello");
    TEST_ASSERT_EQUAL_UINT(5, pos);
    TEST_ASSERT_EQUAL_STRING("hello", buf);

    pos = format_append(buf, pos, sizeof(buf), " world");
    TEST_ASSERT_EQUAL_UINT(11, pos);
    TEST_ASSERT_EQUAL_STRING("hello world", buf);
}

TEST(test_format_append_uint)
{
    char buf[32];
    memset(buf, 0, sizeof(buf));

    size_t pos = format_append(buf, 0, sizeof(buf), "n=");
    pos = format_append_uint(buf, pos, sizeof(buf), 123);
    TEST_ASSERT_EQUAL_STRING("n=123", buf);
}

TEST(test_format_append_int_negative)
{
    char buf[32];
    memset(buf, 0, sizeof(buf));

    size_t pos = format_append(buf, 0, sizeof(buf), "n=");
    pos = format_append_int(buf, pos, sizeof(buf), -42);
    TEST_ASSERT_EQUAL_STRING("n=-42", buf);
}
