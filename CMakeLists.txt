cmake_minimum_required(VERSION 3.10)
project(tiny-clj)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g -O0")
# Production/Release: platform-specific optimization policy
if(APPLE)
  # macOS: favor speed
  set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -DNDEBUG -ffunction-sections -fdata-sections")
  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -Wl,-dead_strip")
else()
  # Embedded/other: favor size
  set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -Os -DNDEBUG -ffunction-sections -fdata-sections")
  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -Wl,--gc-sections")
endif()

# Enable LTO if the toolchain supports it (Release only)
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_msg)
if(ipo_supported)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

# Include directories
include_directories(include src external)

# Source files
list(APPEND SOURCES
    src/clojure_core.c
    src/types.c
    src/CljObject.c
    src/builtins.c
    src/runtime.c
    src/namespace.c
    src/clj_symbols.c
    src/exception.c
    src/clj_parser.c
    src/function_call.c
    src/list_operations.c
    src/vector.c
    src/map.c
    src/string.c
)

# Main executable (not built by default; use REPL)
add_executable(tiny-clj EXCLUDE_FROM_ALL ${SOURCES})

# REPL executable
add_executable(tiny-clj-repl
    src/repl.c
    src/platform_macos.c
    ${SOURCES}
)
target_compile_definitions(tiny-clj-repl PRIVATE ENABLE_META)

# ============================================================================
# CATEGORIZED TEST EXECUTABLES
# ============================================================================

# Unit Tests (isolated, fast tests) - use runner with all unit test files
add_executable(test-unit
    src/tests/test_unit_main.c
    src/tests/test_unit.c
    src/tests/test_assertions.c
    src/tests/test_global_singletons.c
    src/tests/test_alloc_macros.c
    src/unity.c
    src/CljObject.c
    src/types.c
    src/vector.c
    src/map.c
    src/string.c
    src/clj_error.c
    src/clj_symbols.c
    src/namespace.c
    src/clj_parser.c
    src/function_call.c
    src/list_operations.c
)

# Integration Tests (component interaction tests)
add_executable(test-integration
    src/tests/test_integration_main.c
    src/unity.c
    src/CljObject.c
    src/types.c
    src/vector.c
    src/map.c
    src/string.c
    src/clj_error.c
    src/clj_symbols.c
    src/namespace.c
    src/clj_parser.c
    src/function_call.c
    src/clojure_core.c
    src/list_operations.c)

# Benchmark Tests (performance and size tests)
add_executable(test-benchmark
    src/tests/test_benchmark_main.c
    src/unity.c
    src/CljObject.c
    src/types.c
    src/vector.c
    src/map.c
    src/string.c
    src/exception.c
    src/clj_symbols.c
    src/namespace.c
    src/clj_parser.c
    src/function_call.c
    src/clojure_core.c
    src/benchmark.c
    src/list_operations.c)

# Parser Tests
add_executable(test-parser
    src/tests/test_parser.c
    src/unity.c
    src/CljObject.c
    src/types.c
    src/vector.c
    src/map.c
    src/string.c
    src/clj_error.c
    src/clj_symbols.c
    src/namespace.c
    src/clj_parser.c
    src/function_call.c
    src/list_operations.c)

# Test All (wrapper for all test categories)
add_executable(test-all src/tests/test_all_main.c)

# ============================================================================
# LEGACY TEST EXECUTABLES (for backward compatibility)
# ============================================================================

# Array-System-Demo
add_executable(demo-array-system EXCLUDE_FROM_ALL src/tests/demo_array_system.c src/CljObject.c src/types.c src/exception.c src/clj_symbols.c src/namespace.c)

# KV-Makros-Test
add_executable(test-kv-macros EXCLUDE_FROM_ALL src/tests/test_kv_macros.c src/CljObject.c src/types.c src/exception.c src/clj_symbols.c src/namespace.c)

# KV-Makros-Demo
add_executable(kv-macros-demo EXCLUDE_FROM_ALL src/tests/kv_macros_demo.c src/CljObject.c src/types.c src/exception.c src/clj_symbols.c src/namespace.c)


# ============================================================================
# COMPILE DEFINITIONS
# ============================================================================

# Meta-Funktionalität aktivieren
target_compile_definitions(tiny-clj PRIVATE ENABLE_META)

# Meta-Funktionalität für alle Test-Executables aktivieren
target_compile_definitions(test-unit PRIVATE ENABLE_META)
target_compile_definitions(test-unit PRIVATE UNIT_TEST_RUNNER)
target_compile_definitions(test-integration PRIVATE ENABLE_META)
target_compile_definitions(test-benchmark PRIVATE ENABLE_META)

# Embed tests into main build to allow direct invocation
target_compile_definitions(tiny-clj PRIVATE ENABLE_META)
target_compile_definitions(tiny-clj PRIVATE TINY_CLJ_EMBED_TESTS)
target_sources(tiny-clj PRIVATE 
    src/tests/test_unit_main.c 
    src/tests/test_integration_main.c 
    src/tests/test_parser.c 
    src/tests/test_unit.c
    src/tests/test_assertions.c
    src/tests/test_global_singletons.c
    src/tests/test_alloc_macros.c
    src/unity.c
    src/clj_error.c)

# ============================================================================
# CUSTOM TARGETS
# ============================================================================

# Test targets for easy execution
add_custom_target(run-unit-tests
    COMMAND test-unit
    DEPENDS test-unit
    COMMENT "Running unit tests"
)

add_custom_target(run-integration-tests
    COMMAND test-integration
    DEPENDS test-integration
    COMMENT "Running integration tests"
)

add_custom_target(run-benchmark-tests
    COMMAND test-benchmark
    DEPENDS test-benchmark
    COMMENT "Running benchmark tests"
)

add_custom_target(run-all-tests
    COMMAND test-all
    DEPENDS test-all
    COMMENT "Running all tests"
)

# Quick test target (unit + integration)
add_custom_target(run-quick-tests
    COMMAND test-all --quick
    DEPENDS test-all
    COMMENT "Running quick tests (unit + integration)"
)

# ============================================================================
# INSTALLATION
# ============================================================================

install(TARGETS tiny-clj test-unit test-integration test-benchmark test-all
    RUNTIME DESTINATION bin
)

# =========================================================================
# CTest Integration
# =========================================================================
include(CTest)
enable_testing()

add_test(NAME unit COMMAND test-unit --all)
set_tests_properties(unit PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} LABELS "unit")

add_test(NAME integration COMMAND test-integration --all)
set_tests_properties(integration PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} LABELS "integration")

add_test(NAME parser COMMAND test-parser)
set_tests_properties(parser PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} LABELS "parser")

# REPL tests (CLI-level)
add_test(NAME repl-eval-add COMMAND tiny-clj-repl -e "(+ 1 2)")
set_tests_properties(repl-eval-add PROPERTIES PASS_REGULAR_EXPRESSION "3\n" LABELS "repl")

add_test(NAME repl-println-vector COMMAND tiny-clj-repl -e "(println [1 2 3])")
set_tests_properties(repl-println-vector PROPERTIES PASS_REGULAR_EXPRESSION "nil" LABELS "repl")

add_test(NAME repl-ns-eval COMMAND tiny-clj-repl -n user -e "(+ 2 2)")
set_tests_properties(repl-ns-eval PROPERTIES PASS_REGULAR_EXPRESSION "4\n" LABELS "repl")

add_test(NAME repl-error-divzero COMMAND tiny-clj-repl -e "(/ 1 0)")
set_tests_properties(repl-error-divzero PROPERTIES WILL_FAIL TRUE LABELS "repl")
