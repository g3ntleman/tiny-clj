cmake_minimum_required(VERSION 3.10)
project(flash-tree C)

add_library(flash-tree STATIC
    src/flash_tree.c
    src/ft_blockdev.c
    src/ft_crc32.c
    src/ft_log.c
    src/ft_btree.c
    src/ft_leaf_page.c
    src/ft_tsdb_flashdb.c
    src/flashdb/ft_port.c
    src/flashdb/ft_kvdb.c
    src/flashdb/ft_tsdb.c
    src/flashdb/ft_utils.c
)

# 4.4BSD btree/mpool (reference build target)
# Not linked into flash-tree by default; built on demand.
add_library(bsd-btree STATIC
    src/bt_close.c
    src/bt_conv.c
    src/bt_debug.c
    src/bt_delete.c
    src/bt_get.c
    src/bt_open.c
    src/bt_overflow.c
    src/bt_page.c
    src/bt_put.c
    src/bt_search.c
    src/bt_seq.c
    src/bt_split.c
    src/bt_stack.c
    src/bt_utils.c
    src/db_panic.c
    src/mpool.c
)

target_include_directories(bsd-btree PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(bsd-btree PUBLIC c_std_99)
target_compile_options(bsd-btree PRIVATE -Wall -Wextra)
target_compile_definitions(bsd-btree PRIVATE __DBINTERFACE_PRIVATE)

target_include_directories(flash-tree
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/inc
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/flashdb
)

target_compile_features(flash-tree PUBLIC c_std_99)
target_compile_options(flash-tree PRIVATE -Wall -Wextra)
target_compile_definitions(flash-tree PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
)

# Single-config generators fallback (older CMake workflows).
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(flash-tree PRIVATE DEBUG)
endif()

# Host tests (Unity)
add_executable(flash-tree-tests
    tests/test_runner.c
    tests/test_host_smoke.c
    tests/test_blockdev.c
    tests/test_log_checkpoint.c
    tests/test_btree_prefix.c
    tests/test_btree_rw.c
    tests/test_leaf_prefix_compress.c
    tests/test_tsdb_basic.c
    tests/test_tsdb_agg.c
    tests/test_gc.c
    ${CMAKE_SOURCE_DIR}/external/unity/src/unity.c
)

target_include_directories(flash-tree-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external/unity/src
)

target_link_libraries(flash-tree-tests PRIVATE flash-tree)

target_compile_definitions(flash-tree-tests PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
)

