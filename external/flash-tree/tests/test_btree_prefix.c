// test_btree_prefix.c - Prefix order and lower_bound tests.

#include "unity.h"

#include "ft_btree.h"

#include <string.h>

typedef struct {
    const char* keys[16];
    size_t n;
} seen_keys_t;

static ft_status_t collect_keys_cb(const void* key, size_t key_len,
                                  const void* val, size_t val_len,
                                  void* arg) {
    (void)val; (void)val_len;
    seen_keys_t* s = (seen_keys_t*)arg;
    if (s->n >= 16) return FT_ERR_NO_MEMORY;
    // keys are NUL-terminated in these tests
    TEST_ASSERT_EQUAL_UINT(strlen((const char*)key), key_len);
    s->keys[s->n++] = (const char*)key;
    return FT_OK;
}

static void test_lower_bound_basic(void) {
    // Already sorted lex-bytes.
    const char* k0 = "a";
    const char* k1 = "aa";
    const char* k2 = "ab";
    const char* k3 = "b";
    ft_kv_ref_t e[] = {
        {.key = k0, .key_len = 1, .val = "0", .val_len = 1},
        {.key = k1, .key_len = 2, .val = "1", .val_len = 1},
        {.key = k2, .key_len = 2, .val = "2", .val_len = 1},
        {.key = k3, .key_len = 1, .val = "3", .val_len = 1},
    };

    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)ft_lower_bound_kv(e, 4, "a", 1));
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)ft_lower_bound_kv(e, 4, "aa", 2));
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)ft_lower_bound_kv(e, 4, "ab", 2));
    TEST_ASSERT_EQUAL_UINT32(3, (uint32_t)ft_lower_bound_kv(e, 4, "b", 1));
    TEST_ASSERT_EQUAL_UINT32(4, (uint32_t)ft_lower_bound_kv(e, 4, "c", 1));
}

static void test_iter_prefix_order(void) {
    const char* k0 = "ab";
    const char* k1 = "a";
    const char* k2 = "aa";
    const char* k3 = "aba";
    const char* k4 = "b";

    // Sort order must be: a, aa, ab, aba, b
    ft_kv_ref_t e[] = {
        {.key = k1, .key_len = 1, .val = "v", .val_len = 1},
        {.key = k2, .key_len = 2, .val = "v", .val_len = 1},
        {.key = k0, .key_len = 2, .val = "v", .val_len = 1},
        {.key = k3, .key_len = 3, .val = "v", .val_len = 1},
        {.key = k4, .key_len = 1, .val = "v", .val_len = 1},
    };

    seen_keys_t s = {0};
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_iter_prefix_kv(e, 5, "a", 1, collect_keys_cb, &s));
    TEST_ASSERT_EQUAL_UINT32(4, (uint32_t)s.n);
    TEST_ASSERT_EQUAL_STRING("a", s.keys[0]);
    TEST_ASSERT_EQUAL_STRING("aa", s.keys[1]);
    TEST_ASSERT_EQUAL_STRING("ab", s.keys[2]);
    TEST_ASSERT_EQUAL_STRING("aba", s.keys[3]);

    seen_keys_t s2 = {0};
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_iter_prefix_kv(e, 5, "ab", 2, collect_keys_cb, &s2));
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)s2.n);
    TEST_ASSERT_EQUAL_STRING("ab", s2.keys[0]);
    TEST_ASSERT_EQUAL_STRING("aba", s2.keys[1]);
}

void ft_register_tests_btree_prefix(void) {
    RUN_TEST(test_lower_bound_basic);
    RUN_TEST(test_iter_prefix_order);
}

