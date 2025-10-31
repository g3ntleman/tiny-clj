cmake_minimum_required(VERSION 3.10)
project(tiny-clj)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Set output directories - all executables go to bin/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}/bin)

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
    src/object.c
    src/builtins.c
    src/runtime.c
    src/namespace.c
    src/clj_symbols.c
    src/exception.c
    src/clj_parser.c
    src/reader.c
    src/function_call.c
    src/list_operations.c
    src/vector.c
    src/map.c
    src/string.c
    src/seq.c
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

# Note: All tests have been migrated to Unity Test Framework
# Use unity-tests executable (if built) instead of run-tests

# Exception Handling Tests (TRY/CATCH nested exception handling)
add_executable(test-exception-handling
    src/tests/test-exception-handling.c
    src/unity.c
    src/object.c
    src/types.c
    src/vector.c
    src/map.c
    src/string.c
    src/clj_symbols.c
    src/namespace.c
    src/clj_parser.c
    src/reader.c
    src/function_call.c
    src/clojure_core.c
    src/list_operations.c
    src/seq.c
    src/exception.c
    src/runtime.c
)
target_compile_definitions(test-exception-handling PRIVATE ENABLE_META)

# Integration Tests (component interaction tests)
add_executable(test-integration
    src/tests/test_integration_main.c
    src/unity.c
    src/object.c
    src/types.c
    src/vector.c
    src/map.c
    src/string.c
    src/clj_error.c
    src/clj_symbols.c
    src/namespace.c
    src/clj_parser.c
    src/reader.c
    src/function_call.c
    src/clojure_core.c
    src/list_operations.c
    src/seq.c)

# Legacy Benchmark Tests (Unity-based, kept for compatibility)
# Note: All tests now use Unity Test Framework
add_executable(test-benchmark EXCLUDE_FROM_ALL
    src/tests/test_integration_main.c
    src/unity.c
    src/object.c
    src/types.c
    src/vector.c
    src/map.c
    src/string.c
    src/exception.c
    src/clj_symbols.c
    src/namespace.c
    src/clj_parser.c
    src/reader.c
    src/function_call.c
    src/clojure_core.c
    src/benchmark.c
    src/seq.c
    src/list_operations.c)
target_compile_definitions(test-benchmark PRIVATE ENABLE_META)

# Note: test-performance, test-seq-debug, and test-memory-profiling removed - MinUnit tests have been migrated to Unity



# ============================================================================
# COMPILE DEFINITIONS
# ============================================================================

# Meta-Funktionalität aktivieren
target_compile_definitions(tiny-clj PRIVATE ENABLE_META)

# Meta-Funktionalität für alle Test-Executables aktivieren
target_compile_definitions(test-integration PRIVATE ENABLE_META)
target_compile_definitions(test-benchmark PRIVATE ENABLE_META)

# Note: tiny-clj binary is EXCLUDE_FROM_ALL (optional build)
target_compile_definitions(tiny-clj PRIVATE ENABLE_META)

# ============================================================================
# CUSTOM TARGETS
# ============================================================================
# Note: Old test targets removed - use test executables directly from bin/

# ============================================================================
# INSTALLATION
# ============================================================================

install(TARGETS tiny-clj tiny-clj-repl test-exception-handling test-integration test-benchmark
    RUNTIME DESTINATION bin
)

# =========================================================================
# CTest Integration
# =========================================================================
include(CTest)
enable_testing()

# Note: All tests have been migrated to Unity Test Framework
# Use unity-tests executable for all tests
add_test(NAME integration COMMAND test-integration --all)
set_tests_properties(integration PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} LABELS "integration")

# REPL tests (CLI-level)
add_test(NAME repl-eval-add COMMAND tiny-clj-repl -e "(+ 1 2)")
set_tests_properties(repl-eval-add PROPERTIES PASS_REGULAR_EXPRESSION "3\n" LABELS "repl")

add_test(NAME repl-println-vector COMMAND tiny-clj-repl -e "(println [1 2 3])")
set_tests_properties(repl-println-vector PROPERTIES PASS_REGULAR_EXPRESSION "nil" LABELS "repl")

add_test(NAME repl-ns-eval COMMAND tiny-clj-repl -n user -e "(+ 2 2)")
set_tests_properties(repl-ns-eval PROPERTIES PASS_REGULAR_EXPRESSION "4\n" LABELS "repl")

add_test(NAME repl-error-divzero COMMAND tiny-clj-repl -e "(/ 1 0)")
set_tests_properties(repl-error-divzero PROPERTIES WILL_FAIL TRUE LABELS "repl")

# Note: test-seq-benchmark removed - test_seq_fast_benchmark.c was a MinUnit test that has been migrated to Unity
