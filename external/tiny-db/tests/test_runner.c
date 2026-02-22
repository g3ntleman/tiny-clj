// test_runner.c - Unity runner for tiny-db tests.

#include "unity.h"

/**
 * @brief setUp.
 */
void setUp(void) {}
/**
 * @brief tearDown.
 */
void tearDown(void) {}

// Forward declarations for each test file's register function.
void tdb_register_tests_host_smoke(void);
void tdb_register_tests_blockdev(void);
void tdb_register_tests_btree_prefix(void);
void tdb_register_tests_btree_rw(void);
void tdb_register_tests_multi_kv(void);
void tdb_register_tests_gc(void);
void tdb_register_tests_gc_syskeys(void);
void tdb_register_tests_mpool_o1ram(void);
void tdb_register_tests_kv_edge_cases(void);
void tdb_register_tests_page_policy(void);
void tdb_register_tests_chunk_stream(void);
void tdb_register_tests_blob(void);
void tdb_register_tests_btree_pincount(void);
void tdb_register_tests_btree_split_helpers(void);
void tdb_register_tests_btree_split_child(void);
void tdb_register_tests_btree_topdown_insert(void);
void tdb_register_tests_mpool_cache_cfg(void);
void tdb_register_tests_mpool_cache_stress(void);

/**
 * @brief main.
 * @return Unity framework exit code.
 */
int main(void) {
    UNITY_BEGIN();

    tdb_register_tests_host_smoke();
    tdb_register_tests_blockdev();
    tdb_register_tests_btree_prefix();
    tdb_register_tests_btree_rw();
    tdb_register_tests_multi_kv();
    tdb_register_tests_gc();
    tdb_register_tests_gc_syskeys();
    tdb_register_tests_mpool_o1ram();
    tdb_register_tests_kv_edge_cases();
    tdb_register_tests_page_policy();
    tdb_register_tests_chunk_stream();
    tdb_register_tests_blob();
    tdb_register_tests_btree_pincount();
    tdb_register_tests_btree_split_helpers();
    tdb_register_tests_btree_split_child();
    tdb_register_tests_btree_topdown_insert();
    tdb_register_tests_mpool_cache_cfg();
    tdb_register_tests_mpool_cache_stress();

    return UNITY_END();
}
