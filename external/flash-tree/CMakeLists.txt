cmake_minimum_required(VERSION 3.10)
project(flash-tree C)

add_library(flash-tree STATIC
    src/flash_tree.c
    src/ft_blockdev.c
    src/ft_crc32.c
    src/ft_log.c
    src/ft_btree.c
)

target_include_directories(flash-tree
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/inc
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
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

