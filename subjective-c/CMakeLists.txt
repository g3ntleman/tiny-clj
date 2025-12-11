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
)

add_library(subjective-c STATIC ${SUBJECTIVE_C_SOURCES})
target_include_directories(subjective-c
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/public
        ${CMAKE_SOURCE_DIR}/src
)
target_compile_features(subjective-c PUBLIC c_std_99)

# -----------------------------------------------------------------------------
# Unity-based test executable (runs very early in migration)
# -----------------------------------------------------------------------------

set(SUBJECTIVE_C_TEST_SOURCES
    tests/main.c
    tests/test_registry.c
    tests/test_placeholder.c
    tests/test_strings.c
    tests/test_vectors.c
    tests/test_map.c
    tests/test_memory.c
    tests/test_value.c
    tests/test_byte_array.c
    tests/test_support_stubs.c
    ${CMAKE_SOURCE_DIR}/external/unity/src/unity.c
    ${CMAKE_SOURCE_DIR}/src/memory_profiler.c
    ${CMAKE_SOURCE_DIR}/src/validation.c
    ${CMAKE_SOURCE_DIR}/src/ast.c
    ${CMAKE_SOURCE_DIR}/src/equality.c
    ${CMAKE_SOURCE_DIR}/src/error_messages.c
    ${CMAKE_SOURCE_DIR}/src/object.c
    ${CMAKE_SOURCE_DIR}/src/types.c
)

add_executable(subjective-c-tests ${SUBJECTIVE_C_TEST_SOURCES})
target_include_directories(subjective-c-tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/external/unity/src
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/public
        ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(subjective-c-tests PRIVATE subjective-c)
