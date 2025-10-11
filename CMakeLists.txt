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
  # macOS: favor speed with dead code elimination
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
    src/parser.c
    src/reader.c
    src/function_call.c
    src/list_operations.c
    src/vector.c
    src/map.c
    src/string.c
    src/seq.c
    src/memory_profiler.c
    src/memory_hooks.c
)

# Main executable (not built by default; use REPL)
add_executable(tiny-clj EXCLUDE_FROM_ALL ${SOURCES})

# STM32-optimized main (no test code)
add_executable(tiny-clj-stm32-main
    src/main_stm32.c
    src/platform_stm32.c
    ${SOURCES}
)
target_compile_definitions(tiny-clj-stm32-main PRIVATE ENABLE_META STM32_BUILD)
set_target_properties(tiny-clj-stm32-main PROPERTIES
    COMPILE_FLAGS "-Os -DNDEBUG -ffunction-sections -fdata-sections"
    LINK_FLAGS "-Wl,-dead_strip"
)

# REPL executable (with line editing support)
add_executable(tiny-clj-repl
    src/repl.c
    src/platform_macos.c
    src/line_editor.c
    ${SOURCES}
)
target_compile_definitions(tiny-clj-repl PRIVATE ENABLE_META DEBUG ENABLE_LINE_EDITING)

# STM32-optimized REPL (no test code, minimal size)
add_executable(tiny-clj-stm32
    src/repl.c
    src/platform_stm32.c
    ${SOURCES}
)
target_compile_definitions(tiny-clj-stm32 PRIVATE ENABLE_META STM32_BUILD)
set_target_properties(tiny-clj-stm32 PROPERTIES
    COMPILE_FLAGS "-Os -DNDEBUG -ffunction-sections -fdata-sections"
    LINK_FLAGS "-Wl,-dead_strip"
)

# ============================================================================
# CATEGORIZED TEST EXECUTABLES
# ============================================================================

# Unified Test Runner (all MinUnit tests in one executable - STM32 compatible)
add_executable(run-tests
    src/tests/run_tests_main.c
    src/tests/test_unit.c
    src/tests/test_parser.c
    src/tests/test-namespace.c
    src/tests/test_seq.c
    src/tests/test_for_loops.c
    src/tests/test_eval_string_api.c
    src/tests/test_memory.c
    src/tests/test-exception-handling.c
    src/tests/test-utils.c
    src/tests/test_function_types.c
    src/tests/test_nil_arithmetic.c
    src/tests/test_line_editor.c
    src/tests/test_platform_mock.c
    src/tests/test_platform_abstraction.c
    src/tests/test_repl_line_editing.c
    src/line_editor.c
    src/platform_macos.c
    src/builtins.c
    src/object.c
    src/types.c
    src/vector.c
    src/map.c
    src/string.c
    src/clj_symbols.c
    src/namespace.c
    src/parser.c
    src/reader.c
    src/function_call.c
    src/list_operations.c
    src/seq.c
    src/clojure_core.c
    src/exception.c
    src/memory_profiler.c
    src/memory_hooks.c
    src/runtime.c
)
target_compile_definitions(run-tests PRIVATE ENABLE_META UNIFIED_TEST_RUNNER ENABLE_MEMORY_PROFILING DEBUG ENABLE_LINE_EDITING)
# Regular tests WITH memory profiling for early leak detection

# ============================================================================
# REDUNDANT TARGETS REMOVED - Now in run-tests unified executable
# ============================================================================
# The following targets were removed (all now accessible via run-tests):
#   - test-unit            → run-tests --test unit
#   - test-parser          → run-tests --test parser
#   - test-seq             → run-tests --test seq
#   - test-for-loops       → run-tests --test for_loops
#   - test-eval-string-api → run-tests --test eval_string_api
# ============================================================================

# Exception Handling Tests (TRY/CATCH nested exception handling)
add_executable(test-exception-handling EXCLUDE_FROM_ALL
    src/tests/test-exception-handling.c
    src/tests/minunit.h
    src/object.c
    src/types.c
    src/vector.c
    src/map.c
    src/string.c
    src/clj_symbols.c
    src/namespace.c
    src/parser.c
    src/reader.c
    src/function_call.c
    src/list_operations.c
    src/seq.c
    src/exception.c
    src/runtime.c
)
target_compile_definitions(test-exception-handling PRIVATE ENABLE_META)
# Now using MinUnit instead of Unity

# Integration Tests (component interaction tests)
# Unity-based targets removed - all tests now use MinUnit
# Use test-performance for benchmarks, run-tests for all other tests

# Performance & Benchmark Tests (consolidated)
add_executable(test-performance EXCLUDE_FROM_ALL
    src/tests/test_performance.c
    src/object.c
    src/types.c
    src/vector.c
    src/map.c
    src/string.c
    src/clj_symbols.c
    src/namespace.c
    src/parser.c
    src/reader.c
    src/function_call.c
    src/list_operations.c
    src/seq.c
    src/clojure_core.c
    src/exception.c
    src/memory_profiler.c
    src/memory_hooks.c)
# Performance tests WITHOUT memory profiling for clean benchmarks
target_compile_definitions(test-performance PRIVATE ENABLE_META)
target_compile_options(test-performance PRIVATE -O2)

# Test executable for seq debugging
# EXCLUDE_FROM_ALL: These debug tests access internal SeqIterator details
# Build manually: make test-seq-debug test-performance test-memory-profiling
add_executable(test-seq-debug EXCLUDE_FROM_ALL
    src/tests/test_seq_debug.c
    src/tests/minunit.h
    src/object.c
    src/object.h
    src/types.c
    src/vector.c
    src/vector.h
    src/list_operations.c
    src/list_operations.h
    src/function_call.c
    src/function_call.h
    src/namespace.c
    src/namespace.h
    src/clj_symbols.c
    src/clj_symbols.h
    src/memory_profiler.c
    src/memory_profiler.h
    src/memory_hooks.c
    src/memory_hooks.h
    src/seq.c
    src/seq.h
    src/map.c
    src/map.h
    src/string.c
    src/clj_string.h
    src/parser.c
    src/parser.h
    src/reader.c
    src/reader.h
    src/clojure_core.c
    src/exception.c
    src/runtime.c)
target_compile_definitions(test-seq-debug PRIVATE DEBUG=1)

# Memory Profiling Tests (Debug only)
add_executable(test-memory-profiling EXCLUDE_FROM_ALL
    src/tests/test_memory_profiling.c
    src/object.c
    src/types.c
    src/vector.c
    src/map.c
    src/string.c
    src/clj_error.c
    src/clj_symbols.c
    src/namespace.c
    src/parser.c
    src/reader.c
    src/function_call.c
    src/list_operations.c
    src/seq.c
    src/memory_profiler.c
    src/memory_hooks.c)



# ============================================================================
# COMPILE DEFINITIONS
# ============================================================================

# Meta-Funktionalität aktivieren
target_compile_definitions(tiny-clj PRIVATE ENABLE_META)

# Meta-Funktionalität für Test-Executables aktivieren
target_compile_definitions(tiny-clj PRIVATE ENABLE_META)

# ============================================================================
# CUSTOM TARGETS
# ============================================================================

# Test targets for easy execution
add_custom_target(run-unit-tests
    COMMAND test-unit
    DEPENDS test-unit
    COMMENT "Running unit tests"
)

# Custom targets for legacy test-integration and test-benchmark removed
# Use: ./run-tests for all tests, ./test-performance for benchmarks

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

install(TARGETS tiny-clj run-tests
    RUNTIME DESTINATION bin
)

# =========================================================================
# CTest Integration
# =========================================================================
include(CTest)
enable_testing()

# Note: All tests now in run-tests unified executable
# To run: ./run-tests --suite core  (runs both unit and parser)

# REPL tests (CLI-level)
add_test(NAME repl-eval-add COMMAND tiny-clj-repl -e "(+ 1 2)")
set_tests_properties(repl-eval-add PROPERTIES PASS_REGULAR_EXPRESSION "3\n" LABELS "repl")

add_test(NAME repl-println-vector COMMAND tiny-clj-repl -e "(println [1 2 3])")
set_tests_properties(repl-println-vector PROPERTIES PASS_REGULAR_EXPRESSION "nil" LABELS "repl")

add_test(NAME repl-ns-eval COMMAND tiny-clj-repl -n user -e "(+ 2 2)")
set_tests_properties(repl-ns-eval PROPERTIES PASS_REGULAR_EXPRESSION "4\n" LABELS "repl")

add_test(NAME repl-error-divzero COMMAND tiny-clj-repl -e "(/ 1 0)")
set_tests_properties(repl-error-divzero PROPERTIES WILL_FAIL TRUE LABELS "repl")

# Seq Benchmark (Optimization Test)
add_executable(test-seq-benchmark EXCLUDE_FROM_ALL
    src/tests/test_seq_fast_benchmark.c
    src/seq.c
    src/object.c
    src/types.c
    src/vector.c
    src/list_operations.c
    src/string.c
    src/map.c
    src/clj_symbols.c
    src/namespace.c
    src/parser.c
    src/reader.c
    src/function_call.c
    src/exception.c
    src/memory_profiler.c
    src/memory_hooks.c)
target_compile_definitions(test-seq-benchmark PRIVATE ENABLE_META DEBUG)
