// test_runner.c - Unity runner for flash-tree tests.

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

// Forward declarations for each test file's register function.
void ft_register_tests_host_smoke(void);
void ft_register_tests_blockdev(void);
void ft_register_tests_btree_prefix(void);
void ft_register_tests_btree_rw(void);
void ft_register_tests_multi_kv(void);
void ft_register_tests_gc(void);
void ft_register_tests_gc_syskeys(void);
void ft_register_tests_mpool_o1ram(void);
void ft_register_tests_kv_edge_cases(void);
void ft_register_tests_page_policy(void);
void ft_register_tests_chunk_stream(void);
void ft_register_tests_blob(void);
void ft_register_tests_btree_pincount(void);
void ft_register_tests_btree_split_helpers(void);
void ft_register_tests_btree_split_child(void);
void ft_register_tests_btree_topdown_insert(void);
void ft_register_tests_mpool_cache_cfg(void);
void ft_register_tests_mpool_cache_stress(void);

int main(void) {
    UNITY_BEGIN();

    ft_register_tests_host_smoke();
    ft_register_tests_blockdev();
    ft_register_tests_btree_prefix();
    ft_register_tests_btree_rw();
    ft_register_tests_multi_kv();
    ft_register_tests_gc();
    ft_register_tests_gc_syskeys();
    ft_register_tests_mpool_o1ram();
    ft_register_tests_kv_edge_cases();
    ft_register_tests_page_policy();
    ft_register_tests_chunk_stream();
    ft_register_tests_blob();
    ft_register_tests_btree_pincount();
    ft_register_tests_btree_split_helpers();
    ft_register_tests_btree_split_child();
    ft_register_tests_btree_topdown_insert();
    ft_register_tests_mpool_cache_cfg();
    ft_register_tests_mpool_cache_stress();

    return UNITY_END();
}
