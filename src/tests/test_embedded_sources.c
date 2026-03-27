/*
 * Embedded sources + resolver tests
 */

#include "tests_common.h"

#include "../embedded_sources.h"
#include "../fs_layer.h"
#include "../source_resolver.h"
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

ID native_slurp(ID *args, unsigned int argc);

static const char *embedded_sources_repo_root(void)
{
    const char *marker_abs = "/src/tests/test_embedded_sources.c";
    const char *marker_rel = "src/tests/test_embedded_sources.c";
    const char *pos = strstr(__FILE__, marker_abs);
    if (!pos) {
        pos = strstr(__FILE__, marker_rel);
    }
    TEST_ASSERT_NOT_NULL_MESSAGE(pos, "__FILE__ must point inside the tiny-clj repo");
    return __FILE__;
}

static void embedded_sources_repo_path(char *out, size_t out_sz, const char *repo_rel_path)
{
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_NOT_NULL(repo_rel_path);
    TEST_ASSERT_TRUE(out_sz > 0);

    const char *repo_root = embedded_sources_repo_root();
    const char *marker_abs = strstr(repo_root, "/src/tests/test_embedded_sources.c");
    size_t root_len = 0;

    if (marker_abs) {
        root_len = (size_t)(marker_abs - repo_root);
    } else {
        const char *marker_rel = strstr(repo_root, "src/tests/test_embedded_sources.c");
        TEST_ASSERT_NOT_NULL(marker_rel);
        root_len = (size_t)(marker_rel - repo_root);
    }

    if (root_len == 0u) {
        const char *rel_path = repo_rel_path[0] == '/' ? repo_rel_path + 1 : repo_rel_path;
        TEST_ASSERT_TRUE(strlen(rel_path) + 1u < out_sz);
        strcpy(out, rel_path);
        return;
    }

    TEST_ASSERT_TRUE(root_len + strlen(repo_rel_path) + 1u < out_sz);
    memcpy(out, repo_root, root_len);
    strcpy(out + root_len, repo_rel_path);
}

static void assert_resolved_bytes_match_repo_file(const char *virtual_path, const char *repo_rel_path)
{
    char abs_path[1024];
    embedded_sources_repo_path(abs_path, sizeof(abs_path), repo_rel_path);

    FILE *fp = fopen(abs_path, "rb");
    if (!fp) {
        const char *rel_path = repo_rel_path[0] == '/' ? repo_rel_path + 1 : repo_rel_path;
        char parent_path[1024];
        TEST_ASSERT_TRUE(strlen(rel_path) + 4u < sizeof(parent_path));
        strcpy(parent_path, "../");
        strcat(parent_path, rel_path);
        fp = fopen(parent_path, "rb");
        if (fp) {
            strcpy(abs_path, parent_path);
        }
    }
    TEST_ASSERT_NOT_NULL_MESSAGE(fp, abs_path);

    TEST_ASSERT_EQUAL_INT(0, fseek(fp, 0, SEEK_END));
    long file_size = ftell(fp);
    TEST_ASSERT_TRUE(file_size >= 0);
    TEST_ASSERT_EQUAL_INT(0, fseek(fp, 0, SEEK_SET));

    uint8_t *file_bytes = NULL;
    if (file_size > 0) {
        file_bytes = (uint8_t *)malloc((size_t)file_size);
        TEST_ASSERT_NOT_NULL(file_bytes);
        TEST_ASSERT_EQUAL_UINT64((uint64_t)file_size, (uint64_t)fread(file_bytes, 1, (size_t)file_size, fp));
    }
    fclose(fp);

    ID bytes = resolve_path_to_bytes(virtual_path);
    TEST_ASSERT_NOT_NULL(bytes);
    TEST_ASSERT_EQUAL_INT(CLJ_BYTE_ARRAY, TAG(bytes));

    CljByteArray *ba = as_byte_array(bytes);
    TEST_ASSERT_EQUAL_INT((int)file_size, ba->length);
    if (file_size > 0) {
        TEST_ASSERT_EQUAL_INT(0, memcmp(ba->data, file_bytes, (size_t)file_size));
    }

    free(file_bytes);
}

static void assert_test_mkdir_ok(const char *path)
{
    TEST_ASSERT_NOT_NULL(path);
    int rc = mkdir(path, 0777);
    if (rc != 0) {
        TEST_ASSERT_EQUAL_INT(EEXIST, errno);
    }
}

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

    /* Embedded core is included as raw string; content starts after delimiter. */
    const char *core_marker = "(ns clojure.core)";
    size_t marker_len = strlen(core_marker);
    TEST_ASSERT_TRUE(ba->length >= (int)marker_len);
    const uint8_t *p = ba->data;
    size_t rem = (size_t)ba->length;
    while (rem >= marker_len && memcmp(p, core_marker, marker_len) != 0) {
        p++;
        rem--;
    }
    TEST_ASSERT_TRUE(rem >= marker_len);
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

TEST(test_embedded_sources_tiny_clj_datetime)
{
    embedded_source_map_init();
    fs_global_store_reset();
    ID bytes = resolve_path_to_bytes("/libs/tiny-clj/datetime.clj");
    TEST_ASSERT_NOT_NULL(bytes);
    TEST_ASSERT_EQUAL_INT(CLJ_BYTE_ARRAY, TAG(bytes));
    CljByteArray *ba = as_byte_array(bytes);
    TEST_ASSERT_TRUE(ba->length > 0);
    const char *needle = "(ns tiny-clj.datetime)";
    size_t n = strlen(needle);
    TEST_ASSERT_TRUE(ba->length >= (int)n);
    int found = 0;
    for (int i = 0; i <= ba->length - (int)n && !found; i++)
        if (memcmp(ba->data + i, needle, n) == 0) found = 1;
    TEST_ASSERT_TRUE(found);
}

TEST(test_embedded_sources_tiny_clj_gpio)
{
    embedded_source_map_init();
    fs_global_store_reset();
    ID bytes = resolve_path_to_bytes("/libs/tiny-clj/gpio.clj");
    TEST_ASSERT_NOT_NULL(bytes);
    TEST_ASSERT_EQUAL_INT(CLJ_BYTE_ARRAY, TAG(bytes));
    CljByteArray *ba = as_byte_array(bytes);
    TEST_ASSERT_TRUE(ba->length > 0);
    const char *needle = "(ns tiny-clj.gpio)";
    size_t n = strlen(needle);
    TEST_ASSERT_TRUE(ba->length >= (int)n);
    int found = 0;
    for (int i = 0; i <= ba->length - (int)n && !found; i++) {
        if (memcmp(ba->data + i, needle, n) == 0) found = 1;
    }
    TEST_ASSERT_TRUE(found);
}

TEST(test_embedded_sources_tiny_clj_board)
{
    embedded_source_map_init();
    fs_global_store_reset();
    ID bytes = resolve_path_to_bytes("/libs/tiny-clj/board.clj");
    TEST_ASSERT_NOT_NULL(bytes);
    TEST_ASSERT_EQUAL_INT(CLJ_BYTE_ARRAY, TAG(bytes));
    CljByteArray *ba = as_byte_array(bytes);
    TEST_ASSERT_TRUE(ba->length > 0);
    const char *needle = "(ns tiny-clj.board)";
    size_t n = strlen(needle);
    TEST_ASSERT_TRUE(ba->length >= (int)n);
    int found = 0;
    for (int i = 0; i <= ba->length - (int)n && !found; i++) {
        if (memcmp(ba->data + i, needle, n) == 0) found = 1;
    }
    TEST_ASSERT_TRUE(found);
}

TEST(test_embedded_sources_tiny_clj_button)
{
    embedded_source_map_init();
    fs_global_store_reset();
    ID bytes = resolve_path_to_bytes("/libs/tiny-clj/button.clj");
    TEST_ASSERT_NOT_NULL(bytes);
    TEST_ASSERT_EQUAL_INT(CLJ_BYTE_ARRAY, TAG(bytes));
    CljByteArray *ba = as_byte_array(bytes);
    TEST_ASSERT_TRUE(ba->length > 0);
    const char *needle = "(ns tiny-clj.button";
    size_t n = strlen(needle);
    TEST_ASSERT_TRUE(ba->length >= (int)n);
    int found = 0;
    for (int i = 0; i <= ba->length - (int)n && !found; i++) {
        if (memcmp(ba->data + i, needle, n) == 0) found = 1;
    }
    TEST_ASSERT_TRUE(found);
}

TEST(test_embedded_sources_tiny_clj_event)
{
    embedded_source_map_init();
    fs_global_store_reset();
    ID bytes = resolve_path_to_bytes("/libs/tiny-clj/event.clj");
    TEST_ASSERT_NOT_NULL(bytes);
    TEST_ASSERT_EQUAL_INT(CLJ_BYTE_ARRAY, TAG(bytes));
    CljByteArray *ba = as_byte_array(bytes);
    TEST_ASSERT_TRUE(ba->length > 0);
    const char *needle = "(ns tiny-clj.event";
    size_t n = strlen(needle);
    TEST_ASSERT_TRUE(ba->length >= (int)n);
    int found = 0;
    for (int i = 0; i <= ba->length - (int)n && !found; i++) {
        if (memcmp(ba->data + i, needle, n) == 0) found = 1;
    }
    TEST_ASSERT_TRUE(found);
}

TEST(test_embedded_sources_tiny_clj_sensor)
{
    embedded_source_map_init();
    fs_global_store_reset();
    ID bytes = resolve_path_to_bytes("/libs/tiny-clj/sensor.clj");
    TEST_ASSERT_NOT_NULL(bytes);
    TEST_ASSERT_EQUAL_INT(CLJ_BYTE_ARRAY, TAG(bytes));
    CljByteArray *ba = as_byte_array(bytes);
    TEST_ASSERT_TRUE(ba->length > 0);
    const char *needle = "(ns tiny-clj.sensor";
    size_t n = strlen(needle);
    TEST_ASSERT_TRUE(ba->length >= (int)n);
    int found = 0;
    for (int i = 0; i <= ba->length - (int)n && !found; i++) {
        if (memcmp(ba->data + i, needle, n) == 0) found = 1;
    }
    TEST_ASSERT_TRUE(found);
}

TEST(test_embedded_sources_tiny_fx_startup)
{
    embedded_source_map_init();
    fs_global_store_reset();
    ID bytes = resolve_path_to_bytes("/libs/tiny-fx/startup.clj");
    TEST_ASSERT_NOT_NULL(bytes);
    TEST_ASSERT_EQUAL_INT(CLJ_BYTE_ARRAY, TAG(bytes));
    CljByteArray *ba = as_byte_array(bytes);
    TEST_ASSERT_TRUE(ba->length > 0);
    const char *needle = "(ns tiny-fx.startup";
    size_t n = strlen(needle);
    TEST_ASSERT_TRUE(ba->length >= (int)n);
    int found = 0;
    for (int i = 0; i <= ba->length - (int)n && !found; i++) {
        if (memcmp(ba->data + i, needle, n) == 0) found = 1;
    }
    TEST_ASSERT_TRUE(found);
}

TEST(test_embedded_sources_core_matches_libs_file_bytes)
{
    embedded_source_map_init();
    fs_global_store_reset();
    assert_resolved_bytes_match_repo_file("/libs/clojure/core.clj", "/libs/clojure/core.clj");
}

TEST(test_embedded_sources_tiny_db_kv_matches_libs_file_bytes)
{
    embedded_source_map_init();
    fs_global_store_reset();
    assert_resolved_bytes_match_repo_file("/libs/tiny-db/kv.clj", "/libs/tiny-db/kv.clj");
}

TEST(test_embedded_sources_tiny_fx_sound_matches_libs_file_bytes)
{
    embedded_source_map_init();
    fs_global_store_reset();
    assert_resolved_bytes_match_repo_file("/libs/tiny-fx/sound.clj", "/libs/tiny-fx/sound.clj");
}

TEST(test_embedded_sources_tiny_fx_sound_demos_matches_libs_file_bytes)
{
    embedded_source_map_init();
    fs_global_store_reset();
    assert_resolved_bytes_match_repo_file("/libs/tiny-fx/sound-demos.clj", "/libs/tiny-fx/sound-demos.clj");
}

TEST(test_source_resolver_assets_path_maps_to_repo_assets_directory)
{
    embedded_source_map_init();
    fs_global_store_reset();
    assert_resolved_bytes_match_repo_file("/assets/tiny-fx/startup.edn", "/assets/tiny-fx/startup.edn");
}

TEST(test_source_resolver_bundle_resource_root_resolves_libs_and_assets)
{
    embedded_source_map_init();
    fs_global_store_reset();

    char temp_template[] = "/tmp/tinyclj-bundle-XXXXXX";
    char *temp_dir = mkdtemp(temp_template);
    TEST_ASSERT_NOT_NULL(temp_dir);

    char contents_dir[PATH_MAX];
    char resources_dir[PATH_MAX];
    char libs_dir[PATH_MAX];
    char test_lib_dir[PATH_MAX];
    char assets_dir[PATH_MAX];
    char test_asset_dir[PATH_MAX];
    char lib_file[PATH_MAX];
    char asset_file[PATH_MAX];

    test_snprintf(contents_dir, sizeof(contents_dir), "%s/Contents", temp_dir);
    test_snprintf(resources_dir, sizeof(resources_dir), "%s/Contents/Resources", temp_dir);
    test_snprintf(libs_dir, sizeof(libs_dir), "%s/libs", resources_dir);
    test_snprintf(test_lib_dir, sizeof(test_lib_dir), "%s/test", libs_dir);
    test_snprintf(assets_dir, sizeof(assets_dir), "%s/assets", resources_dir);
    test_snprintf(test_asset_dir, sizeof(test_asset_dir), "%s/test-bundle", assets_dir);
    test_snprintf(lib_file, sizeof(lib_file), "%s/bundle-only.clj", test_lib_dir);
    test_snprintf(asset_file, sizeof(asset_file), "%s/data.edn", test_asset_dir);

    assert_test_mkdir_ok(contents_dir);
    assert_test_mkdir_ok(resources_dir);
    assert_test_mkdir_ok(libs_dir);
    assert_test_mkdir_ok(test_lib_dir);
    assert_test_mkdir_ok(assets_dir);
    assert_test_mkdir_ok(test_asset_dir);

    FILE *fp = fopen(lib_file, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    TEST_ASSERT_EQUAL_UINT64(26u, fwrite("(ns test.bundle-only)\n42\n", 1, 26u, fp));
    TEST_ASSERT_EQUAL_INT(0, fclose(fp));

    fp = fopen(asset_file, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    TEST_ASSERT_EQUAL_UINT64(11u, fwrite("{:bundle 1}\n", 1, 11u, fp));
    TEST_ASSERT_EQUAL_INT(0, fclose(fp));

    source_resolver_set_bundle_resource_root(resources_dir);

    ID lib_bytes = resolve_path_to_bytes("/libs/test/bundle-only.clj");
    TEST_ASSERT_NOT_NULL(lib_bytes);
    TEST_ASSERT_EQUAL_INT(CLJ_BYTE_ARRAY, TAG(lib_bytes));
    TEST_ASSERT_TRUE(as_byte_array(lib_bytes)->length > 0);

    ID asset_bytes = resolve_path_to_bytes("/assets/test-bundle/data.edn");
    TEST_ASSERT_NOT_NULL(asset_bytes);
    TEST_ASSERT_EQUAL_INT(CLJ_BYTE_ARRAY, TAG(asset_bytes));
    TEST_ASSERT_TRUE(as_byte_array(asset_bytes)->length > 0);

    source_resolver_clear_bundle_resource_root();
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
    // Content must match (may or may not be zero-copy)
    TEST_ASSERT_EQUAL_MEMORY(ba->data, string_data(s), ba->length);
}
