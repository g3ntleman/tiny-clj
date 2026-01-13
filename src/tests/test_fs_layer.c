#include "tests_common.h"

#include "../fs_layer.h"

TEST(test_fs_kv_store_roundtrip_bytes)
{
    FsKvStore *st = fs_kv_store_new();
    TEST_ASSERT_NOT_NULL(st);

    const uint8_t in[4] = {1, 2, 3, 4};
    TEST_ASSERT_TRUE(fs_kv_put(st, "/k", in, sizeof(in)));

    uint8_t out[8] = {0};
    size_t saved_len = 0;
    size_t n = fs_kv_get(st, "/k", out, sizeof(out), &saved_len);
    TEST_ASSERT_EQUAL_UINT32(sizeof(in), (uint32_t)n);
    TEST_ASSERT_EQUAL_UINT32(sizeof(in), (uint32_t)saved_len);
    TEST_ASSERT_EQUAL_MEMORY(in, out, sizeof(in));

    TEST_ASSERT_TRUE(fs_kv_del(st, "/k"));
    saved_len = 123;
    n = fs_kv_get(st, "/k", out, sizeof(out), &saved_len);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)n);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)saved_len);

    fs_kv_store_free(st);
}

TEST(test_fs_layer_write_read_stat_list_delete)
{
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    FsKvStore *st = fs_kv_store_new();
    TEST_ASSERT_NOT_NULL(st);

    /* create a directory */
    fs_err_t e = fs_mkdir(st, "/data/", NULL, NULL);
    TEST_ASSERT_EQUAL_INT(FS_NO_ERR, e);

    /* write a file */
    uint8_t bytes[600];
    for (size_t i = 0; i < sizeof(bytes); i++) {
        bytes[i] = (uint8_t)(i & 0xFF);
    }
    e = fs_write_bytes(st, g_test_eval_state, "/data/file.bin", bytes, sizeof(bytes));
    TEST_ASSERT_EQUAL_INT(FS_NO_ERR, e);

    /* read back */
    ID out = fs_read_bytes(st, g_test_eval_state, "/data/file.bin");
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL_INT(CLJ_BYTE_ARRAY, TAG(out));
    CljByteArray *ba = as_byte_array(out);
    TEST_ASSERT_EQUAL_INT((int)sizeof(bytes), ba->length);
    TEST_ASSERT_EQUAL_MEMORY(bytes, ba->data, sizeof(bytes));

    /* stat */
    ID st_map = fs_stat(st, g_test_eval_state, "/data/file.bin");
    TEST_ASSERT_NOT_NULL(st_map);
    TEST_ASSERT_EQUAL_INT(CLJ_MAP, TAG(st_map));
    ID type = map_get(as_map(st_map), (ID)intern_symbol_global(":type"));
    TEST_ASSERT_NOT_NULL(type);
    TEST_ASSERT_EQUAL_INT(CLJ_SYMBOL, TAG(type));
    TEST_ASSERT_EQUAL_STRING(":file", as_symbol(type)->cname);
    ID sizev = map_get(as_map(st_map), (ID)intern_symbol_global(":size"));
    TEST_ASSERT_TRUE(is_fixnum((CljValue)sizev));
    TEST_ASSERT_EQUAL_INT((int)sizeof(bytes), as_fixnum((CljValue)sizev));

    /* list dir: contains exactly the file */
    ID lst = fs_list_dir(st, g_test_eval_state, "/data/");
    TEST_ASSERT_NOT_NULL(lst);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, TAG(lst));
    CljVector *v = as_vector(lst);
    TEST_ASSERT_EQUAL_INT(1, vector_count(v));

    /* delete meta only */
    TEST_ASSERT_TRUE(fs_delete(st, "/data/file.bin"));
    TEST_ASSERT_NULL(fs_stat(st, g_test_eval_state, "/data/file.bin"));

    /* chunks are still present (GC will clean later) */
    uint8_t tmp[16] = {0};
    size_t saved_len = 0;
    size_t n = fs_kv_get(st, "/data/file.bin@1#0000", tmp, sizeof(tmp), &saved_len);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(saved_len > 0);

    fs_kv_store_free(st);
}

TEST(test_fs_layer_rewrite_increments_version)
{
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    FsKvStore *st = fs_kv_store_new();
    TEST_ASSERT_NOT_NULL(st);

    fs_err_t e = fs_mkdir(st, "/cfg/", NULL, NULL);
    TEST_ASSERT_EQUAL_INT(FS_NO_ERR, e);

    const uint8_t v1[3] = {1, 2, 3};
    e = fs_write_bytes(st, g_test_eval_state, "/cfg/a.bin", v1, sizeof(v1));
    TEST_ASSERT_EQUAL_INT(FS_NO_ERR, e);

    const uint8_t v2[5] = {9, 8, 7, 6, 5};
    e = fs_write_bytes(st, g_test_eval_state, "/cfg/a.bin", v2, sizeof(v2));
    TEST_ASSERT_EQUAL_INT(FS_NO_ERR, e);

    ID out = fs_read_bytes(st, g_test_eval_state, "/cfg/a.bin");
    TEST_ASSERT_NOT_NULL(out);
    CljByteArray *ba = as_byte_array(out);
    TEST_ASSERT_EQUAL_INT((int)sizeof(v2), ba->length);
    TEST_ASSERT_EQUAL_MEMORY(v2, ba->data, sizeof(v2));

    /* old version chunks still exist */
    uint8_t tmp[8] = {0};
    size_t saved_len = 0;
    size_t n = fs_kv_get(st, "/cfg/a.bin@1#0000", tmp, sizeof(tmp), &saved_len);
    TEST_ASSERT_TRUE(n > 0);

    fs_kv_store_free(st);
}

