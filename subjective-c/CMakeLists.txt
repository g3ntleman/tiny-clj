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

# Inherit DEBUG flags from parent project
# This ensures exception.c is compiled with DEBUG when unit-tests are built
# Check if we're building unit-tests (which needs DEBUG flags)
# We'll set this from the parent CMakeLists.txt after unit-tests is defined

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
)
# Unity Tests - Always in Debug mode for comprehensive testing
# Force Debug build regardless of CMAKE_BUILD_TYPE
if(APPLE)
    set_target_properties(subjective-c-tests PROPERTIES
        COMPILE_FLAGS "-g -O0 -DDEBUG -DUNITY_TESTS -fno-omit-frame-pointer"
        COMPILE_DEFINITIONS "DEBUG;UNITY_TESTS;ENABLE_MEMORY_PROFILING;ENABLE_META;UNITY_INCLUDE_EXEC_TIME;ZOMBIE_ENABLED"
        # Force Debug configuration regardless of CMAKE_BUILD_TYPE
        COMPILE_OPTIONS "$<$<CONFIG:Debug>:-g;-O0;-DDEBUG>"
        COMPILE_OPTIONS "$<$<CONFIG:Release>:-g;-O0;-DDEBUG>"
        COMPILE_OPTIONS "$<$<CONFIG:RelWithDebInfo>:-g;-O0;-DDEBUG>"
        COMPILE_OPTIONS "$<$<CONFIG:MinSizeRel>:-g;-O0;-DDEBUG>"
    )
    # Remove dead_strip from global flags if present to preserve test registration functions
    string(REPLACE "-Wl,-dead_strip" "" TMP_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
    string(REPLACE "-Wl,-S" "" TMP_FLAGS "${TMP_FLAGS}")
    if(TMP_FLAGS)
        target_link_options(subjective-c-tests PRIVATE ${TMP_FLAGS})
    endif()
else()
    # Linux/other: disable gc-sections to preserve all functions
    set_target_properties(subjective-c-tests PROPERTIES
        COMPILE_FLAGS "-g -O0 -DDEBUG -DUNITY_TESTS -fno-omit-frame-pointer"
        COMPILE_DEFINITIONS "DEBUG;UNITY_TESTS;ENABLE_MEMORY_PROFILING;ENABLE_META;UNITY_INCLUDE_EXEC_TIME;ZOMBIE_ENABLED"
        # Force Debug configuration regardless of CMAKE_BUILD_TYPE
        COMPILE_OPTIONS "$<$<CONFIG:Debug>:-g;-O0;-DDEBUG>"
        COMPILE_OPTIONS "$<$<CONFIG:Release>:-g;-O0;-DDEBUG>"
        COMPILE_OPTIONS "$<$<CONFIG:RelWithDebInfo>:-g;-O0;-DDEBUG>"
        COMPILE_OPTIONS "$<$<CONFIG:MinSizeRel>:-g;-O0;-DDEBUG>"
    )
    target_link_options(subjective-c-tests PRIVATE
        -Wl,--no-gc-sections
    )
endif()
target_link_libraries(subjective-c-tests PRIVATE subjective-c)
