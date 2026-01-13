// test_runner.c - Unity runner for flash-tree tests.

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

// Forward declarations for each test file's register function.
void ft_register_tests_host_smoke(void);
void ft_register_tests_blockdev(void);
void ft_register_tests_log_checkpoint(void);
void ft_register_tests_btree_prefix(void);
void ft_register_tests_btree_rw(void);
void ft_register_tests_leaf_prefix_compress(void);
void ft_register_tests_tsdb_basic(void);
void ft_register_tests_tsdb_agg(void);
void ft_register_tests_gc(void);

int main(void) {
    UNITY_BEGIN();

    ft_register_tests_host_smoke();
    ft_register_tests_blockdev();
    ft_register_tests_log_checkpoint();
    ft_register_tests_btree_prefix();
    ft_register_tests_btree_rw();
    ft_register_tests_leaf_prefix_compress();
    ft_register_tests_tsdb_basic();
    ft_register_tests_tsdb_agg();
    ft_register_tests_gc();

    return UNITY_END();
}

