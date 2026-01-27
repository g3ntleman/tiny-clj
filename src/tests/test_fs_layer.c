#include "tests_common.h"

#include "../fs_layer.h"
#include "mini_format.h"

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

    /* write a file (no explicit mkdir needed - directories are implicit) */
    uint8_t bytes[600];
    for (size_t i = 0; i < sizeof(bytes); i++) {
        bytes[i] = (uint8_t)(i & 0xFF);
    }
    fs_err_t e = fs_write_bytes(st, "/data/file.bin", bytes, sizeof(bytes));
    TEST_ASSERT_EQUAL_INT(FS_NO_ERR, e);

    /* read back */
    ID out = fs_read_bytes(st, "/data/file.bin");
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL_INT(CLJ_BYTE_ARRAY, TAG(out));
    CljByteArray *ba = as_byte_array(out);
    TEST_ASSERT_EQUAL_INT((int)sizeof(bytes), ba->length);
    TEST_ASSERT_EQUAL_MEMORY(bytes, ba->data, sizeof(bytes));

    /* stat */
    int64_t size = fs_stat_size(st, "/data/file.bin");
    TEST_ASSERT_EQUAL_INT64((int64_t)sizeof(bytes), size);

    /* list dir: contains exactly the file path */
    char last_key[FS_KEY_MAX] = {0};
    ID lst = fs_list_dir_batch(st, "/data/", NULL, 32, last_key, sizeof(last_key));
    TEST_ASSERT_NOT_NULL(lst);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(lst));
    CljPersistentVector *v = as_vector(lst);
    TEST_ASSERT_EQUAL_INT(1, vector_count(v));
    TEST_ASSERT_TRUE(last_key[0] == '\0'); // only entry, end reached

    /* delete meta only */
    TEST_ASSERT_TRUE(fs_delete(st, "/data/file.bin"));
    TEST_ASSERT_FALSE(fs_exists(st, "/data/file.bin"));

    /* chunks are still present (GC will clean later) */
    uint8_t tmp[16] = {0};
    size_t saved_len = 0;
    size_t n = fs_kv_get(st, "/data/file.bin@1#0000", tmp, sizeof(tmp), &saved_len);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(saved_len > 0);

    fs_kv_store_free(st);
}

TEST(test_fs_list_dir_batch_many_files)
{
    FsKvStore *st = fs_kv_store_new();
    TEST_ASSERT_NOT_NULL(st);

    // Create: /many/file_00.bin .. /many/file_49.bin
    for (int i = 0; i < 50; i++) {
        char path[FS_KEY_MAX];
        mini_snprintf(path, sizeof(path), "/many/file_%02d.bin", i);
        uint8_t data = (uint8_t)i;
        fs_err_t e = fs_write_bytes(st, path, &data, 1);
        TEST_ASSERT_EQUAL_INT(FS_NO_ERR, e);
    }

    // Batch 1
    char last_key[FS_KEY_MAX] = {0};
    ID batch1 = fs_list_dir_batch(st, "/many/", NULL, 32, last_key, sizeof(last_key));
    TEST_ASSERT_NOT_NULL(batch1);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(batch1));
    TEST_ASSERT_EQUAL_INT(32, vector_count(as_vector(batch1)));
    TEST_ASSERT_TRUE(last_key[0] != '\0');

    // Batch 2
    char last_key2[FS_KEY_MAX] = {0};
    ID batch2 = fs_list_dir_batch(st, "/many/", last_key, 32, last_key2, sizeof(last_key2));
    TEST_ASSERT_NOT_NULL(batch2);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(batch2));
    TEST_ASSERT_EQUAL_INT(18, vector_count(as_vector(batch2)));
    TEST_ASSERT_TRUE(last_key2[0] == '\0'); // no more

    // Correctness: combine and verify order + uniqueness.
    CljPersistentVector *v1 = as_vector(batch1);
    CljPersistentVector *v2 = as_vector(batch2);
    TEST_ASSERT_NOT_NULL(v1);
    TEST_ASSERT_NOT_NULL(v2);
    TEST_ASSERT_EQUAL_INT(32, vector_count(v1));
    TEST_ASSERT_EQUAL_INT(18, vector_count(v2));

    // Verify every expected path is present exactly once, in lexicographic order.
    // The %02d naming makes lexicographic == numeric order for 0..49.
    for (int i = 0; i < 50; i++) {
        char expected[FS_KEY_MAX];
        mini_snprintf(expected, sizeof(expected), "/many/file_%02d.bin", i);

        ID elem = (i < 32) ? vector_nth(v1, (unsigned int)i)
                           : vector_nth(v2, (unsigned int)(i - 32));
        TEST_ASSERT_NOT_NULL(elem);
        TEST_ASSERT_EQUAL_INT(CLJ_STRING, TAG(elem));
        TEST_ASSERT_EQUAL_STRING(expected, string_data((CljString*)elem));
    }

    fs_kv_store_free(st);
}

TEST(test_fs_layer_rewrite_increments_version)
{
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    FsKvStore *st = fs_kv_store_new();
    TEST_ASSERT_NOT_NULL(st);

    const uint8_t v1[3] = {1, 2, 3};
    fs_err_t e = fs_write_bytes(st, "/cfg/a.bin", v1, sizeof(v1));
    TEST_ASSERT_EQUAL_INT(FS_NO_ERR, e);

    const uint8_t v2[5] = {9, 8, 7, 6, 5};
    e = fs_write_bytes(st, "/cfg/a.bin", v2, sizeof(v2));
    TEST_ASSERT_EQUAL_INT(FS_NO_ERR, e);

    ID out = fs_read_bytes(st, "/cfg/a.bin");
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
