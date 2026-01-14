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
void ft_register_tests_tsdb_basic(void);
void ft_register_tests_tsdb_agg(void);
void ft_register_tests_gc(void);
void ft_register_tests_kv_edge_cases(void);

int main(void) {
    UNITY_BEGIN();

    ft_register_tests_host_smoke();
    ft_register_tests_blockdev();
    ft_register_tests_btree_prefix();
    ft_register_tests_btree_rw();
    ft_register_tests_multi_kv();
    ft_register_tests_tsdb_basic();
    ft_register_tests_tsdb_agg();
    ft_register_tests_gc();
    ft_register_tests_kv_edge_cases();

    return UNITY_END();
}

