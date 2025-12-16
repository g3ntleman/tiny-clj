#include "test_common.h"
#include "public/symbol.h"
#include <string.h>

// Test that predefined special symbols have correct names
TEST(test_special_symbols_have_correct_names) {
    // These symbols are defined in test_support_stubs.c
    TEST_ASSERT_NOT_NULL(SYM_DEF);
    TEST_ASSERT_NOT_NULL(SYM_TRY);
    TEST_ASSERT_NOT_NULL(SYM_IF);
    TEST_ASSERT_NOT_NULL(SYM_LET);
    TEST_ASSERT_NOT_NULL(SYM_FN);
    
    // Verify each symbol has the correct name
    TEST_ASSERT_EQUAL_STRING("def", SYM_DEF->cname);
    TEST_ASSERT_EQUAL_STRING("try", SYM_TRY->cname);
    TEST_ASSERT_EQUAL_STRING("if", SYM_IF->cname);
    TEST_ASSERT_EQUAL_STRING("let", SYM_LET->cname);
    TEST_ASSERT_EQUAL_STRING("fn", SYM_FN->cname);
}

// Test that all special symbols are unique
TEST(test_special_symbols_are_unique) {
    CljSymbol *symbols[] = {
        SYM_IF, SYM_LET, SYM_DEFN, SYM_DEF, SYM_FN, SYM_DO,
        SYM_COND, SYM_WHEN, SYM_WHILE, SYM_QUOTE, SYM_RECUR,
        SYM_AND, SYM_OR, SYM_NS, SYM_TRY, SYM_CATCH, SYM_THROW,
        SYM_FINALLY, SYM_VAR, SYM_LOOP, SYM_GO, SYM_TIME
    };
    const size_t count = sizeof(symbols) / sizeof(symbols[0]);
    
    // Verify all are non-NULL
    for (size_t i = 0; i < count; i++) {
        TEST_ASSERT_NOT_NULL(symbols[i]);
    }
    
    // Verify all are different pointers
    for (size_t i = 0; i < count; i++) {
        for (size_t j = i + 1; j < count; j++) {
            TEST_ASSERT_NOT_EQUAL(symbols[i], symbols[j]);
        }
    }
}

// Test symbol names are not corrupted
TEST(test_symbol_name_integrity) {
    TEST_ASSERT_EQUAL_STRING("if", SYM_IF->cname);
    TEST_ASSERT_EQUAL_STRING("let", SYM_LET->cname);
    TEST_ASSERT_EQUAL_STRING("defn", SYM_DEFN->cname);
    TEST_ASSERT_EQUAL_STRING("def", SYM_DEF->cname);
    TEST_ASSERT_EQUAL_STRING("fn", SYM_FN->cname);
    TEST_ASSERT_EQUAL_STRING("do", SYM_DO->cname);
    TEST_ASSERT_EQUAL_STRING("cond", SYM_COND->cname);
    TEST_ASSERT_EQUAL_STRING("when", SYM_WHEN->cname);
    TEST_ASSERT_EQUAL_STRING("while", SYM_WHILE->cname);
    TEST_ASSERT_EQUAL_STRING("quote", SYM_QUOTE->cname);
    TEST_ASSERT_EQUAL_STRING("recur", SYM_RECUR->cname);
    TEST_ASSERT_EQUAL_STRING("and", SYM_AND->cname);
    TEST_ASSERT_EQUAL_STRING("or", SYM_OR->cname);
    TEST_ASSERT_EQUAL_STRING("ns", SYM_NS->cname);
    TEST_ASSERT_EQUAL_STRING("try", SYM_TRY->cname);
    TEST_ASSERT_EQUAL_STRING("catch", SYM_CATCH->cname);
    TEST_ASSERT_EQUAL_STRING("throw", SYM_THROW->cname);
    TEST_ASSERT_EQUAL_STRING("finally", SYM_FINALLY->cname);
    TEST_ASSERT_EQUAL_STRING("var", SYM_VAR->cname);
    TEST_ASSERT_EQUAL_STRING("loop", SYM_LOOP->cname);
    TEST_ASSERT_EQUAL_STRING("go", SYM_GO->cname);
    TEST_ASSERT_EQUAL_STRING("time", SYM_TIME->cname);
}

