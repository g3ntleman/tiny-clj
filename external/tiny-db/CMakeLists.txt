cmake_minimum_required(VERSION 3.10)
project(tiny-db C)

#
# NOTE: TDB_MPOOL_O1_RAM is always enabled (no build option).
#

set(TINY_DB_CORE_SRCS
    src/tdb_kv.c
    src/tdb_kv_bind.c
    src/tdb_alloc.c
    src/tdb_blockdev.c
    src/tdb_crc32.c
    src/tdb_page_policy.c
    src/tdb_blob.c
)

set(BSD_BTREE_SRCS
    src/tdb_bt_close.c
    src/tdb_bt_conv.c
    src/tdb_bt_delete.c
    src/tdb_bt_get.c
    src/tdb_bt_open.c
    src/tdb_bt_page.c
    src/tdb_bt_put.c
    src/tdb_bt_search.c
    src/tdb_bt_seq.c
    src/tdb_bt_split.c
    src/tdb_bt_utils.c
    src/tdb_bsd_db_panic.c
    src/tdb_bsd_mpool.c
)

# Build as OBJECT libraries so we can:
# - produce a monolithic libtiny-db.a (for existing consumers)
# - also produce split archives for size accounting.
add_library(tiny-db-obj OBJECT ${TINY_DB_CORE_SRCS})
add_library(bsd-btree-obj OBJECT ${BSD_BTREE_SRCS})

add_library(tiny-db-core STATIC $<TARGET_OBJECTS:tiny-db-obj>)
add_library(bsd-btree STATIC $<TARGET_OBJECTS:bsd-btree-obj>)

# Monolithic archive (backwards compatible target name).
add_library(tiny-db STATIC
    $<TARGET_OBJECTS:tiny-db-obj>
    $<TARGET_OBJECTS:bsd-btree-obj>
)

# 4.4BSD btree/mpool (reference build target)
# Not linked into tiny-db by default; built on demand.
# (The actual compilation is done by bsd-btree-obj above.)

foreach(tgt tiny-db-obj bsd-btree-obj tiny-db-core bsd-btree tiny-db)
    target_include_directories(${tgt}
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/inc
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_compile_features(${tgt} PUBLIC c_std_99)
endforeach()

target_compile_options(tiny-db-obj PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_compile_options(bsd-btree-obj PRIVATE -Wall -Wextra)

target_compile_definitions(bsd-btree-obj PRIVATE
    __DBINTERFACE_PRIVATE
)

target_compile_definitions(tiny-db-obj PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
)

# Cross-compile with ESP32 toolchain: propagate ESP32_BUILD into tiny-db.
if(DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES "esp32\\.cmake")
    foreach(tgt tiny-db-obj bsd-btree-obj tiny-db-core bsd-btree tiny-db)
        if(TARGET ${tgt})
            target_compile_definitions(${tgt} PRIVATE ESP32_BUILD)
        endif()
    endforeach()
endif()

#
# O(1)-RAM mode is always enabled (TDB_MPOOL_O1_RAM=1).
#

# Single-config generators fallback (older CMake workflows).
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(tiny-db-obj PRIVATE DEBUG)
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
    # Host tests are built on demand (not part of default 'all').
    add_executable(tiny-db-tests EXCLUDE_FROM_ALL
        tests/test_runner.c
        tests/test_host_smoke.c
        tests/test_blockdev.c
        tests/test_btree_prefix.c
        tests/test_btree_rw.c
        tests/test_btree_pincount.c
        tests/test_btree_split_helpers.c
        tests/test_btree_split_child.c
        tests/test_btree_topdown_insert.c
        tests/test_mpool_cache_cfg.c
        tests/test_mpool_cache_stress.c
        tests/test_multi_kv.c
        tests/test_kv_edge_cases.c
        tests/test_gc.c
        tests/test_gc_syskeys.c
        tests/test_mpool_o1ram.c
        tests/test_page_policy.c
        tests/test_chunk_stream.c
        tests/test_blob.c
        src/tdb_btree.c
        ${UNITY_SRC}
    )

    target_include_directories(tiny-db-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/inc
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${UNITY_INC}
    )

    target_link_libraries(tiny-db-tests PRIVATE tiny-db)

    target_compile_definitions(tiny-db-tests PRIVATE
        $<$<CONFIG:Debug>:DEBUG>
    )
    # Force scan-miss coverage in tests (small cache), while keeping library defaults intact.
    target_compile_definitions(tiny-db-tests PRIVATE TDB_MPOOL_PG_CACHE_ENTRIES=1)
endif()
