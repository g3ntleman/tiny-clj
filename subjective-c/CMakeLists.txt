cmake_minimum_required(VERSION 3.10)

project(subjective-c C)

# Disallow in-source builds for the subjective-c subproject to prevent CMake
# artifacts from polluting the source tree (CMakeCache.txt, CMakeFiles/, etc.).
# Use: cmake -S subjective-c -B subjective-c/build
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not supported for subjective-c. Please run: cmake -S subjective-c -B subjective-c/build")
endif()

# Set C standard consistently with main project
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# -----------------------------------------------------------------------------
# Subjective-C static library
# -----------------------------------------------------------------------------

set(SUBJECTIVE_C_SOURCES
    src/value.c
    src/types.c
    src/memory.c
    src/strings.c
    src/vector.c
    src/exception.c
    src/map.c
    src/byte_array.c
    src/hashmap.c
    src/callbacks.c
    src/datetime_utc.c
    src/instant.c
    src/uuid.c
    src/regex.c

        # Regex engine (tiny-regex-c) compiled into subjective-c
        ${CMAKE_SOURCE_DIR}/external/tiny_regex.c
)

add_library(subjective-c STATIC ${SUBJECTIVE_C_SOURCES})
set(SUBJECTIVE_C_PUBLIC_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/subjective-c/src")
set(SUBJECTIVE_C_PRIVATE_INCLUDE_DIR "${SUBJECTIVE_C_PUBLIC_INCLUDE_DIR}/subjective-c")

target_include_directories(subjective-c
    PUBLIC
        # Provides <subjective-c/...> includes to consumers (headers live in src/subjective-c/)
        ${SUBJECTIVE_C_PUBLIC_INCLUDE_DIR}
    PRIVATE
        # Allows subjective-c sources/tests to include "types.h", "subjective-c.h", etc.
        ${SUBJECTIVE_C_PRIVATE_INCLUDE_DIR}
        # subjective-c test runner compiles some tiny-clj sources (memory_profiler, ast, etc.)
        ${CMAKE_SOURCE_DIR}/src
)
target_compile_features(subjective-c PUBLIC c_std_99)

# Inherit ALL build flags from parent project to ensure consistent compilation
# This is critical for struct layout compatibility (e.g., CLJException size)
target_compile_options(subjective-c PRIVATE
    -Wall -Wextra
    $<$<CONFIG:Debug>:-g;-O0;-DDEBUG;-DZOMBIE_ENABLED;-fno-omit-frame-pointer>
    $<$<CONFIG:Release>:-O3;-DNDEBUG;-ffunction-sections;-fdata-sections;-fvisibility=hidden>
    $<$<CONFIG:MinSizeRel>:-Os;-DNDEBUG;-ffunction-sections;-fdata-sections;-fvisibility=hidden>
    $<$<CONFIG:Embedded>:-Os;-DNDEBUG;-ffunction-sections;-fdata-sections;-fvisibility=hidden;-flto;-fno-unwind-tables;-fno-asynchronous-unwind-tables;-fno-stack-protector;-fomit-frame-pointer;-fno-ident;-fno-common>
)

# Ensure DEBUG is defined for Debug builds (critical for CLJException struct layout)
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR NOT CMAKE_BUILD_TYPE)
    target_compile_definitions(subjective-c PUBLIC DEBUG ZOMBIE_ENABLED)
    message(STATUS "subjective-c: DEBUG and ZOMBIE_ENABLED (CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})")
else()
    message(STATUS "subjective-c: DEBUG disabled (CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})")
endif()

# Unity compatibility: flush stdout for error messages
target_compile_definitions(subjective-c PUBLIC UNITY_USE_FLUSH_STDOUT)

# Size-focused embedded builds: avoid pulling in printf-style formatting code.
# Enable this for the embedded configuration and when cross-compiling with the ESP32 toolchain.
if(CMAKE_BUILD_TYPE STREQUAL "Embedded" OR (DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES "esp32\\.cmake"))
    target_compile_definitions(subjective-c PUBLIC STRING_FORMATTING_ENABLED=0)
    message(STATUS "subjective-c: STRING_FORMATTING_ENABLED=0")
endif()

# ESP32-specific linker flags (if using ESP32 toolchain)
if(NOT APPLE AND (CMAKE_BUILD_TYPE STREQUAL "Embedded" OR (DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES "esp32\\.cmake")))
    target_compile_options(subjective-c PRIVATE -mlongcalls)
    message(STATUS "subjective-c: ESP32 -mlongcalls enabled")
endif()

# -----------------------------------------------------------------------------
# Unity-based test executable (runs very early in migration)
# -----------------------------------------------------------------------------

set(SUBJECTIVE_C_TEST_SOURCES
    tests/test_runner.c
    tests/test_registry.c
    tests/test_strings.c
    tests/test_vectors.c
    tests/test_map.c
    tests/test_memory.c
    tests/test_value.c
    tests/test_byte_array.c
    tests/test_exception.c
    tests/test_hashmap.c
    tests/test_hashmap_id_keys.c
    tests/test_hash.c
    tests/test_support_stubs.c
    ${CMAKE_SOURCE_DIR}/external/unity/src/unity.c
    ${CMAKE_SOURCE_DIR}/src/memory_profiler.c
    ${CMAKE_SOURCE_DIR}/src/validation.c
    ${CMAKE_SOURCE_DIR}/src/ast.c
    ${CMAKE_SOURCE_DIR}/src/equality.c
    ${CMAKE_SOURCE_DIR}/src/error_messages.c
    ${CMAKE_SOURCE_DIR}/src/object.c
    ${CMAKE_SOURCE_DIR}/subjective-c/src/types.c
    ${CMAKE_SOURCE_DIR}/src/list.c
)

add_executable(subjective-c-tests ${SUBJECTIVE_C_TEST_SOURCES})
target_include_directories(subjective-c-tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/external/unity/src
        ${CMAKE_SOURCE_DIR}               # For <subjective-c/...> includes
        ${CMAKE_CURRENT_SOURCE_DIR}       # For "subjective-c.h" in tests
        ${CMAKE_CURRENT_SOURCE_DIR}/src   # For subjective-c sources
        ${CMAKE_CURRENT_SOURCE_DIR}/src/subjective-c  # For "common.h", "subjective-c.h"
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}                # For build_info.h
)
# Unity Tests - Always in Debug mode for comprehensive testing
# Force Debug build regardless of CMAKE_BUILD_TYPE using generator expressions
target_compile_definitions(subjective-c-tests PRIVATE
    DEBUG
    UNITY_TESTS
    MEMORY_PROFILING_ENABLED=${TINYCLJ_MEMORY_PROFILING_ENABLED_VALUE}
    META_ENABLED=${TINYCLJ_META_ENABLED_VALUE}
    UNITY_INCLUDE_EXEC_TIME
    ZOMBIE_ENABLED
)
# Force Debug compiler flags for all build types
target_compile_options(subjective-c-tests PRIVATE
    $<$<CONFIG:Debug>:-g;-O0;-DDEBUG;-fno-omit-frame-pointer>
    $<$<CONFIG:Release>:-g;-O0;-DDEBUG;-fno-omit-frame-pointer>
    $<$<CONFIG:RelWithDebInfo>:-g;-O0;-DDEBUG;-fno-omit-frame-pointer>
    $<$<CONFIG:MinSizeRel>:-g;-O0;-DDEBUG;-fno-omit-frame-pointer>
)
# Preserve test registration functions by disabling dead code elimination
if(APPLE)
    # Remove dead_strip from global flags if present
    string(REPLACE "-Wl,-dead_strip" "" TMP_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
    string(REPLACE "-Wl,-S" "" TMP_FLAGS "${TMP_FLAGS}")
    if(TMP_FLAGS)
        target_link_options(subjective-c-tests PRIVATE ${TMP_FLAGS})
    endif()
else()
    # Linux/other: disable gc-sections to preserve all functions
    target_link_options(subjective-c-tests PRIVATE -Wl,--no-gc-sections)
endif()
target_link_libraries(subjective-c-tests PRIVATE subjective-c)
