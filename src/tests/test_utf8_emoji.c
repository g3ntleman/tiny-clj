/*
 * Test UTF-8 Emoji Support in Strings
 * 
 * Tests that UTF-8 emojis are correctly parsed and stored in strings.
 */

#include "tests_common.h"
#include "strings.h"

static int32_t as_unsigned_byte(ID value) {
    TEST_ASSERT_NOT_NULL(value);
    TEST_ASSERT_TRUE_MESSAGE(is_fixnum(value) || is_character(value),
                             "nth result should be a fixnum or character");
    int32_t raw = is_character(value) ? (int32_t)as_character(value)
                                      : as_fixnum(value);
    return (raw < 0) ? (raw + 256) : raw;
}

TEST(test_utf8_emoji_in_string) {
    EvalState *st = g_test_eval_state;
    
    // Test 1: Simple emoji (rocket 🚀)
    const char *test1 = "\"🚀\"";
    ID result1 = eval_string(test1, st);
    TEST_ASSERT_NOT_NULL_MESSAGE(result1, "Rocket emoji should parse successfully");
    TEST_ASSERT_EQUAL(CLJ_STRING, TAG(result1));
    CljString *str1 = (CljString *)result1;
    TEST_ASSERT_NOT_NULL_MESSAGE(str1, "String object should exist");
    TEST_ASSERT_NOT_NULL_MESSAGE(str1->data, "String data should exist");
    // Check that the emoji bytes are present (F0 9F 9A 80)
    TEST_ASSERT_EQUAL(0xF0, (unsigned char)str1->data[0]);
    TEST_ASSERT_EQUAL(0x9F, (unsigned char)str1->data[1]);
    TEST_ASSERT_EQUAL(0x9A, (unsigned char)str1->data[2]);
    TEST_ASSERT_EQUAL(0x80, (unsigned char)str1->data[3]);
    TEST_ASSERT_EQUAL('\0', str1->data[4]);
    
    // Test 2: Emoji in text
    const char *test2 = "\"🚀 Simple Benchmark Test\"";
    ID result2 = eval_string(test2, st);
    TEST_ASSERT_NOT_NULL_MESSAGE(result2, "Emoji in text should parse successfully");
    TEST_ASSERT_EQUAL(CLJ_STRING, TAG(result2));
    CljString *str2 = (CljString *)result2;
    TEST_ASSERT_NOT_NULL_MESSAGE(str2, "String object should exist");
    TEST_ASSERT_NOT_NULL_MESSAGE(str2->data, "String data should exist");
    // Check that the emoji bytes are at the start
    TEST_ASSERT_EQUAL(0xF0, (unsigned char)str2->data[0]);
    TEST_ASSERT_EQUAL(0x9F, (unsigned char)str2->data[1]);
    TEST_ASSERT_EQUAL(0x9A, (unsigned char)str2->data[2]);
    TEST_ASSERT_EQUAL(0x80, (unsigned char)str2->data[3]);
    TEST_ASSERT_EQUAL(' ', str2->data[4]);
    
    // Test 3: Checkmark emoji (✅)
    const char *test3 = "\"✅\"";
    ID result3 = eval_string(test3, st);
    TEST_ASSERT_NOT_NULL_MESSAGE(result3, "Checkmark emoji should parse successfully");
    TEST_ASSERT_EQUAL(CLJ_STRING, TAG(result3));
    CljString *str3 = (CljString *)result3;
    TEST_ASSERT_NOT_NULL_MESSAGE(str3, "String object should exist");
    TEST_ASSERT_NOT_NULL_MESSAGE(str3->data, "String data should exist");
    // Checkmark emoji: E2 9C 85
    TEST_ASSERT_EQUAL(0xE2, (unsigned char)str3->data[0]);
    TEST_ASSERT_EQUAL(0x9C, (unsigned char)str3->data[1]);
    TEST_ASSERT_EQUAL(0x85, (unsigned char)str3->data[2]);
    TEST_ASSERT_EQUAL('\0', str3->data[3]);
    
    // Test 4: Multiple emojis
    const char *test4 = "\"🚀 ✅\"";
    ID result4 = eval_string(test4, st);
    TEST_ASSERT_NOT_NULL_MESSAGE(result4, "Multiple emojis should parse successfully");
    TEST_ASSERT_EQUAL(CLJ_STRING, TAG(result4));
}

TEST(test_string_nth_ascii_and_utf8_bytes) {
    EvalState *st = g_test_eval_state;
    TEST_ASSERT_NOT_NULL(st);

    ID ascii_count = eval_string("(count \"G5\")", st);
    assert_fixnum(ascii_count, 2);
    ID ascii0 = eval_string("(nth \"G5\" 0)", st);
    ID ascii1 = eval_string("(nth \"G5\" 1)", st);
    TEST_ASSERT_EQUAL_INT(0x47, as_unsigned_byte(ascii0)); // 'G'
    TEST_ASSERT_EQUAL_INT(0x35, as_unsigned_byte(ascii1)); // '5'

    ID umlaut_count = eval_string("(count \"ä\")", st);
    assert_fixnum(umlaut_count, 2);
    ID umlaut0 = eval_string("(nth \"ä\" 0)", st);
    ID umlaut1 = eval_string("(nth \"ä\" 1)", st);
    TEST_ASSERT_EQUAL_INT(0xC3, as_unsigned_byte(umlaut0));
    TEST_ASSERT_EQUAL_INT(0xA4, as_unsigned_byte(umlaut1));

    ID emoji_count = eval_string("(count \"🙂\")", st);
    assert_fixnum(emoji_count, 4);
    ID emoji0 = eval_string("(nth \"🙂\" 0)", st);
    ID emoji1 = eval_string("(nth \"🙂\" 1)", st);
    ID emoji2 = eval_string("(nth \"🙂\" 2)", st);
    ID emoji3 = eval_string("(nth \"🙂\" 3)", st);
    TEST_ASSERT_EQUAL_INT(0xF0, as_unsigned_byte(emoji0));
    TEST_ASSERT_EQUAL_INT(0x9F, as_unsigned_byte(emoji1));
    TEST_ASSERT_EQUAL_INT(0x99, as_unsigned_byte(emoji2));
    TEST_ASSERT_EQUAL_INT(0x82, as_unsigned_byte(emoji3));
}
