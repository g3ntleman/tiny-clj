cmake_minimum_required(VERSION 3.10)
project(flash-tree C)

set(FLASH_TREE_CORE_SRCS
    src/ft_kv.c
    src/ft_kv_bind.c
    src/ft_blockdev.c
    src/ft_crc32.c
    src/ft_tsdb.c
)

set(FLASHDB_SRCS
    src/flashdb/ft_port.c
    src/flashdb/ft_tsdb.c
    src/flashdb/ft_utils.c
)

set(BSD_BTREE_SRCS
    src/ft_bt_close.c
    src/ft_bt_conv.c
    src/ft_bt_debug.c
    src/ft_bt_delete.c
    src/ft_bt_get.c
    src/ft_bt_open.c
    src/ft_bt_overflow.c
    src/ft_bt_page.c
    src/ft_bt_put.c
    src/ft_bt_search.c
    src/ft_bt_seq.c
    src/ft_bt_split.c
    src/ft_bt_stack.c
    src/ft_bt_utils.c
    src/ft_bsd_db_panic.c
    src/ft_bsd_mpool.c
)

# Build as OBJECT libraries so we can:
# - produce a monolithic libflash-tree.a (for existing consumers)
# - also produce split archives for size accounting (flashdb vs flash-tree).
add_library(flashdb-obj OBJECT ${FLASHDB_SRCS})
add_library(flash-tree-obj OBJECT ${FLASH_TREE_CORE_SRCS})
add_library(bsd-btree-obj OBJECT ${BSD_BTREE_SRCS})

add_library(flashdb-core STATIC $<TARGET_OBJECTS:flashdb-obj>)
add_library(flash-tree-core STATIC $<TARGET_OBJECTS:flash-tree-obj>)
add_library(bsd-btree STATIC $<TARGET_OBJECTS:bsd-btree-obj>)

# Monolithic archive (backwards compatible target name).
add_library(flash-tree STATIC
    $<TARGET_OBJECTS:flash-tree-obj>
    $<TARGET_OBJECTS:flashdb-obj>
    $<TARGET_OBJECTS:bsd-btree-obj>
)

# 4.4BSD btree/mpool (reference build target)
# Not linked into flash-tree by default; built on demand.
# (The actual compilation is done by bsd-btree-obj above.)

foreach(tgt flashdb-obj flash-tree-obj bsd-btree-obj flashdb-core flash-tree-core bsd-btree flash-tree)
    target_include_directories(${tgt}
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/inc
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/src/flashdb
    )
    target_compile_features(${tgt} PUBLIC c_std_99)
endforeach()

target_compile_options(flash-tree-obj PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_compile_options(flashdb-obj PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_compile_options(bsd-btree-obj PRIVATE -Wall -Wextra)

target_compile_definitions(bsd-btree-obj PRIVATE
    __DBINTERFACE_PRIVATE
)

target_compile_definitions(flash-tree-obj PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
)
target_compile_definitions(flashdb-obj PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
)

# Single-config generators fallback (older CMake workflows).
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(flash-tree-obj PRIVATE DEBUG)
    target_compile_definitions(flashdb-obj PRIVATE DEBUG)
endif()

target_include_directories(bsd-btree PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
)

# Host tests (Unity) - find Unity in various locations
set(UNITY_SEARCH_PATHS
    "${CMAKE_SOURCE_DIR}/external/unity/src/unity.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../unity/src/unity.c"
)

set(UNITY_SRC "")
set(UNITY_INC "")
foreach(path ${UNITY_SEARCH_PATHS})
    if(EXISTS "${path}")
        set(UNITY_SRC "${path}")
        get_filename_component(UNITY_INC "${path}" DIRECTORY)
        break()
    endif()
endforeach()

if(UNITY_SRC)
    add_executable(flash-tree-tests
        tests/test_runner.c
        tests/test_host_smoke.c
        tests/test_blockdev.c
        tests/test_btree_prefix.c
        tests/test_btree_rw.c
        tests/test_multi_kv.c
        tests/test_kv_edge_cases.c
        tests/test_tsdb_basic.c
        tests/test_tsdb_agg.c
        tests/test_gc.c
        src/ft_btree.c
        ${UNITY_SRC}
    )

    target_include_directories(flash-tree-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/inc
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${UNITY_INC}
    )

    target_link_libraries(flash-tree-tests PRIVATE flash-tree)

    target_compile_definitions(flash-tree-tests PRIVATE
        $<$<CONFIG:Debug>:DEBUG>
    )
endif()

