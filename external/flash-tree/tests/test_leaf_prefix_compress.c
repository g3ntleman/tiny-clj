// test_leaf_prefix_compress.c - Leaf page key prefix-compression tests.

#include "unity.h"

#include "ft_leaf_page.h"

#include <string.h>

typedef struct {
    const char* const* expected_keys;
    const uint8_t* expected_vals;
    size_t expected_n;
    size_t seen;
} seen_t;

static ft_status_t verify_in_order_cb(const void* key, size_t key_len,
                                      const void* val, size_t val_len,
                                      void* arg) {
    seen_t* s = (seen_t*)arg;
    TEST_ASSERT_TRUE(s->seen < s->expected_n);

    const char* exp = s->expected_keys[s->seen];
    const size_t exp_len = strlen(exp);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)exp_len, (uint32_t)key_len);
    TEST_ASSERT_EQUAL_MEMORY(exp, key, exp_len);

    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)val_len);
    TEST_ASSERT_NOT_NULL(val);
    TEST_ASSERT_EQUAL_HEX8(s->expected_vals[s->seen], *(const uint8_t*)val);

    s->seen++;
    return FT_OK;
}

static ft_status_t count_cb(const void* key, size_t key_len, const void* val, size_t val_len, void* arg) {
    (void)key;
    (void)key_len;
    (void)val;
    (void)val_len;
    size_t* c = (size_t*)arg;
    (*c)++;
    return FT_OK;
}

static void make_key(char* out, size_t out_cap, unsigned idx) {
    // Format: /data/dir/fileNNNN
    // Avoid snprintf to keep test code portable/minimal.
    const char* p = "/data/dir/file";
    const size_t p_len = strlen(p);
    TEST_ASSERT_TRUE(out_cap >= p_len + 4 + 1);
    memcpy(out, p, p_len);

    unsigned n = idx % 10000u;
    out[p_len + 0] = (char)('0' + ((n / 1000u) % 10u));
    out[p_len + 1] = (char)('0' + ((n / 100u) % 10u));
    out[p_len + 2] = (char)('0' + ((n / 10u) % 10u));
    out[p_len + 3] = (char)('0' + (n % 10u));
    out[p_len + 4] = '\0';
}

static void test_key_compression_saves_space_and_preserves_order(void) {
    enum { N = 200 };

    static uint8_t page[4096];
    static char keys[N][32];
    static const char* key_ptrs[N];
    static uint8_t vals[N];
    static ft_leaf_entry_ref_t refs[N];

    for (unsigned i = 0; i < N; i++) {
        make_key(keys[i], sizeof(keys[i]), i);
        key_ptrs[i] = keys[i];
        vals[i] = (uint8_t)i;
        refs[i].key = keys[i];
        refs[i].key_len = strlen(keys[i]);
        refs[i].val = &vals[i];
        refs[i].val_len = 1;
    }

    ft_leaf_stats_t st = {0};
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_leaf_encode(page, sizeof(page), refs, N, &st));
    TEST_ASSERT_EQUAL_UINT32(N, (uint32_t)st.n_entries);
    TEST_ASSERT_TRUE(st.raw_key_bytes > 0);

    // Expect a visible win for path-like keys. Use integer math: stored < raw * 0.6.
    TEST_ASSERT_TRUE((st.stored_key_bytes * 10u) < (st.raw_key_bytes * 6u));

    uint8_t scratch[128];
    seen_t seen = {.expected_keys = key_ptrs, .expected_vals = vals, .expected_n = N, .seen = 0};
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_leaf_iter(page, sizeof(page), scratch, sizeof(scratch), verify_in_order_cb, &seen, NULL));
    TEST_ASSERT_EQUAL_UINT32(N, (uint32_t)seen.seen);
}

static void test_prefix_scan_works(void) {
    enum { N = 200 };

    static uint8_t page[4096];
    static char keys[N][32];
    static uint8_t vals[N];
    static ft_leaf_entry_ref_t refs[N];

    for (unsigned i = 0; i < N; i++) {
        make_key(keys[i], sizeof(keys[i]), i);
        vals[i] = (uint8_t)i;
        refs[i].key = keys[i];
        refs[i].key_len = strlen(keys[i]);
        refs[i].val = &vals[i];
        refs[i].val_len = 1;
    }

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_leaf_encode(page, sizeof(page), refs, N, NULL));

    // Prefix that matches all keys.
    const char* p_all = "/data/dir/";
    uint8_t scratch[128];
    size_t count_all = 0;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_leaf_iter_prefix(page, sizeof(page), p_all, strlen(p_all), scratch, sizeof(scratch), count_cb, &count_all, NULL));
    TEST_ASSERT_EQUAL_UINT32(N, (uint32_t)count_all);

    // Prefix that matches 0000..0099.
    const char* p_00 = "/data/dir/file00";
    size_t count_00 = 0;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_leaf_iter_prefix(page, sizeof(page), p_00, strlen(p_00), scratch, sizeof(scratch), count_cb, &count_00, NULL));
    TEST_ASSERT_EQUAL_UINT32(100, (uint32_t)count_00);
}

void ft_register_tests_leaf_prefix_compress(void) {
    RUN_TEST(test_key_compression_saves_space_and_preserves_order);
    RUN_TEST(test_prefix_scan_works);
}

