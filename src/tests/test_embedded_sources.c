/*
 * Embedded sources + resolver tests
 */

#include "tests_common.h"

#include "../embedded_sources.h"
#include "../fs_layer.h"
#include "../source_resolver.h"

ID native_slurp(ID *args, unsigned int argc);

TEST(test_embedded_sources_kv_precedes_embedded)
{
    embedded_source_map_init();
    fs_global_store_reset();

    FsKvStore *st = fs_global_store();
    TEST_ASSERT_NOT_NULL(st);

    const uint8_t b = 'X';
    fs_err_t e = fs_write_bytes(st, "/libs/clojure/core.clj", &b, 1);
    TEST_ASSERT_EQUAL_INT(FS_NO_ERR, e);

    ID bytes = resolve_path_to_bytes("/libs/clojure/core.clj");
    TEST_ASSERT_NOT_NULL(bytes);
    TEST_ASSERT_EQUAL_INT(CLJ_BYTE_ARRAY, TAG(bytes));
    CljByteArray *ba = as_byte_array(bytes);
    TEST_ASSERT_EQUAL_INT(1, ba->length);
    TEST_ASSERT_EQUAL_UINT8('X', ba->data[0]);
}

TEST(test_embedded_sources_fallback)
{
    embedded_source_map_init();
    fs_global_store_reset();

    ID bytes = resolve_path_to_bytes("/libs/clojure/core.clj");
    TEST_ASSERT_NOT_NULL(bytes);
    TEST_ASSERT_EQUAL_INT(CLJ_BYTE_ARRAY, TAG(bytes));
    CljByteArray *ba = as_byte_array(bytes);
    TEST_ASSERT_TRUE(ba->length > 0);

    const char *prefix = "(ns clojure.core";
    size_t prefix_len = strlen(prefix);
    TEST_ASSERT_TRUE(ba->length >= (int)prefix_len);
    TEST_ASSERT_EQUAL_MEMORY(prefix, ba->data, prefix_len);
}

TEST(test_embedded_sources_tiny_db_kv)
{
    embedded_source_map_init();
    fs_global_store_reset();
    ID bytes = resolve_path_to_bytes("/libs/tiny-db/kv.clj");
    TEST_ASSERT_NOT_NULL(bytes);
    TEST_ASSERT_EQUAL_INT(CLJ_BYTE_ARRAY, TAG(bytes));
    CljByteArray *ba = as_byte_array(bytes);
    TEST_ASSERT_TRUE(ba->length > 0);
    const char *needle = "(ns tiny-db.kv)";
    size_t n = strlen(needle);
    TEST_ASSERT_TRUE(ba->length >= (int)n);
    int found = 0;
    for (int i = 0; i <= ba->length - (int)n && !found; i++)
        if (memcmp(ba->data + i, needle, n) == 0) found = 1;
    TEST_ASSERT_TRUE(found);
}

TEST(test_slurp_returns_string_view)
{
    embedded_source_map_init();
    fs_global_store_reset();

    ID bytes = resolve_path_to_bytes("/libs/clojure/core.clj");
    TEST_ASSERT_NOT_NULL(bytes);
    CljByteArray *ba = as_byte_array(bytes);

    ID arg = (ID)make_string("/libs/clojure/core.clj");
    ID args[1] = { arg };
    ID s = native_slurp(args, 1);
    RELEASE(arg);

    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_INT(CLJ_STRING, TAG(s));
    TEST_ASSERT_EQUAL_INT(ba->length, (int)string_length(s));
    TEST_ASSERT_EQUAL_PTR(ba->data, (const uint8_t*)string_data(s));
}
