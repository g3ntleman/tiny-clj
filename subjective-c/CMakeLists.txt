cmake_minimum_required(VERSION 3.10)

project(subjective-c C)

# Set C standard consistently with main project
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# -----------------------------------------------------------------------------
# Subjective-C static library (currently placeholder, real sources follow later)
# -----------------------------------------------------------------------------

set(SUBJECTIVE_C_SOURCES
    subjective_c_init.c
    value.c
    memory.c
    strings.c
    vector.c
    exception.c
    map.c
    byte_array.c
    hashmap.c
    callbacks.c
)

add_library(subjective-c STATIC ${SUBJECTIVE_C_SOURCES})
target_include_directories(subjective-c
    PUBLIC
        ${CMAKE_SOURCE_DIR}               # For <subjective-c/...> includes
        ${CMAKE_SOURCE_DIR}/src
)
target_compile_features(subjective-c PUBLIC c_std_99)

# Inherit build configuration from parent project based on CMAKE_BUILD_TYPE
# This ensures subjective-c is built with the same build type as tiny-clj targets
# Debug builds of tiny-clj should link Debug builds of subjective-c
# Release builds of tiny-clj should link Release builds of subjective-c
# Use generator expressions to support all build types properly
# DEBUG is enabled for Debug builds (default if CMAKE_BUILD_TYPE is not set)
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR NOT CMAKE_BUILD_TYPE)
    target_compile_definitions(subjective-c PUBLIC DEBUG)
    message(STATUS "subjective-c: DEBUG enabled (CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})")
else()
    message(STATUS "subjective-c: DEBUG disabled (CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})")
endif()

# Size-focused embedded builds: avoid pulling in printf-style formatting code.
# Enable this for the embedded configuration and when cross-compiling with the ESP32 toolchain.
if(CMAKE_BUILD_TYPE STREQUAL "Embedded" OR (DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES "esp32\\.cmake"))
    target_compile_definitions(subjective-c PUBLIC DISABLE_STRING_FORMATTING)
    message(STATUS "subjective-c: DISABLE_STRING_FORMATTING enabled")
endif()
# CMake automatically applies CMAKE_C_FLAGS_<CONFIG> based on build type
# No need to manually set COMPILE_FLAGS - CMake handles this automatically

# -----------------------------------------------------------------------------
# Unity-based test executable (runs very early in migration)
# -----------------------------------------------------------------------------

set(SUBJECTIVE_C_TEST_SOURCES
    tests/test_runner.c
    tests/test_registry.c
    tests/test_placeholder.c
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
    ${CMAKE_SOURCE_DIR}/src/types.c
    ${CMAKE_SOURCE_DIR}/src/list.c
)

add_executable(subjective-c-tests ${SUBJECTIVE_C_TEST_SOURCES})
target_include_directories(subjective-c-tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/external/unity/src
        ${CMAKE_SOURCE_DIR}               # For <subjective-c/...> includes
        ${CMAKE_CURRENT_SOURCE_DIR}       # For "subjective-c.h" in tests
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}                # For build_info.h
)
# Unity Tests - Always in Debug mode for comprehensive testing
# Force Debug build regardless of CMAKE_BUILD_TYPE using generator expressions
target_compile_definitions(subjective-c-tests PRIVATE
    DEBUG
    UNITY_TESTS
    ENABLE_MEMORY_PROFILING
    ENABLE_META
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
