cmake_minimum_required(VERSION 3.10)
project(tiny-clj)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g -O0 -DDEBUG")
# Disable Unity's setjmp/longjmp to avoid conflict with our exception system
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DUNITY_EXCLUDE_SETJMP")
# Production/Release: platform-specific optimization policy
if(APPLE)
  # macOS: favor size for benchmarks, speed for general release
  set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -DNDEBUG -ffunction-sections -fdata-sections -fvisibility=hidden")
  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -Wl,-dead_strip -Wl,-S")
else()
  # Embedded/other: favor size with aggressive stripping
  set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -Os -DNDEBUG -ffunction-sections -fdata-sections -fvisibility=hidden")
  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -Wl,--gc-sections -Wl,--strip-all -Wl,-S")
endif()

# Benchmark-specific optimization (size-optimized)
set(CMAKE_C_FLAGS_MINSIZEREL "${CMAKE_C_FLAGS_MINSIZEREL} -Os -DNDEBUG -ffunction-sections -fdata-sections -fvisibility=hidden")
if(APPLE)
  set(CMAKE_EXE_LINKER_FLAGS_MINSIZEREL "${CMAKE_EXE_LINKER_FLAGS_MINSIZEREL} -Wl,-dead_strip -Wl,-S")
else()
  set(CMAKE_EXE_LINKER_FLAGS_MINSIZEREL "${CMAKE_EXE_LINKER_FLAGS_MINSIZEREL} -Wl,--gc-sections -Wl,--strip-all -Wl,-S")
endif()

# Enable LTO if the toolchain supports it (Release and MinSizeRel)
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_msg)
if(ipo_supported)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL ON)
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
    src/symbol.c
    src/exception.c
    src/parser.c
    src/reader.c
    src/function_call.c
    src/list_operations.c
    src/vector.c
    src/map.c
    src/string.c
    src/seq.c
    src/memory.c
    src/error_messages.c
    src/memory_profiler.c
    src/value.c
)

# Main executable (not built by default; use REPL)
add_executable(tiny-clj EXCLUDE_FROM_ALL ${SOURCES})

# STM32-optimized sources (no memory profiling for embedded)
set(STM32_SOURCES
    src/clojure_core.c
    src/types.c
    src/object.c
    src/builtins.c
    src/runtime.c
    src/namespace.c
    src/symbol.c
    src/exception.c
    src/parser.c
    src/reader.c
    src/function_call.c
    src/list_operations.c
    src/vector.c
    src/map.c
    src/string.c
    src/seq.c
    src/memory.c
    src/error_messages.c
    src/value.c
)

# STM32-optimized embedded interpreter (no REPL, executes startup-code.clj)
add_executable(tiny-clj-stm32
    src/main_stm32.c
    src/platform_stm32_embedded.c
    src/clojure_core.c
    src/types.c
    src/object.c
    src/builtins.c
    src/runtime.c
    src/namespace.c
    src/symbol.c
    src/exception.c
    src/parser.c
    src/reader.c
    src/function_call.c
    src/list_operations.c
    src/vector.c
    src/map.c
    src/string.c
    src/seq.c
    src/memory.c
    src/error_messages.c
    src/memory_profiler.c
    src/value.c
)
target_compile_definitions(tiny-clj-stm32 PRIVATE ENABLE_META STM32_BUILD DISABLE_MEMORY_PROFILING ENABLE_LINE_EDITING=0)
set_target_properties(tiny-clj-stm32 PROPERTIES
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

# STM32-optimized REPL (no test code, includes memory profiling for REPL)
add_executable(tiny-clj-stm32-repl
    src/repl.c
    src/platform_stm32.c
    src/line_editor.c
    src/memory_profiler.c
    ${STM32_SOURCES}
)
target_compile_definitions(tiny-clj-stm32-repl PRIVATE ENABLE_META STM32_BUILD DISABLE_MEMORY_PROFILING ENABLE_LINE_EDITING=1)
set_target_properties(tiny-clj-stm32-repl PROPERTIES
    COMPILE_FLAGS "-Os -DNDEBUG -ffunction-sections -fdata-sections"
    LINK_FLAGS "-Wl,-dead_strip"
)

# ============================================================================
# CATEGORIZED TEST EXECUTABLES
# ============================================================================

# Unity Test Framework (central runner with separate test files)
add_executable(unity-tests
    src/tests/unity_test_runner.c
    src/tests/memory_tests.c
    src/tests/parser_tests.c
    src/tests/exception_tests.c
    src/tests/unit_tests.c
    src/tests/namespace_tests.c
    src/tests/test_namespace.c
    src/tests/test_equal.c
    src/tests/seq_tests.c
    src/tests/for_loop_tests.c
    src/tests/test_recur.c
    src/tests/fixed_point_tests.c
    external/unity/src/unity.c
    src/builtins.c
    src/object.c
    src/types.c
    src/vector.c
    src/map.c
    src/string.c
    src/symbol.c
    src/namespace.c
    src/parser.c
    src/reader.c
    src/function_call.c
    src/list_operations.c
    src/seq.c
    src/clojure_core.c
    src/exception.c
    src/memory_profiler.c
    src/memory.c
    src/runtime.c
    src/error_messages.c
    src/value.c
)

add_executable(debug_parser
    debug_parser.c
    src/function_call.c
    src/object.c
    src/namespace.c
    src/memory.c
    src/symbol.c
    src/runtime.c
    src/parser.c
    src/reader.c
    src/types.c
    src/string.c
    src/map.c
    src/vector.c
    src/list_operations.c
    src/seq.c
    src/builtins.c
    src/clojure_core.c
    src/exception.c
    src/memory_profiler.c
    src/error_messages.c
    src/value.c
)
target_compile_definitions(unity-tests PRIVATE ENABLE_META ENABLE_MEMORY_PROFILING)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(unity-tests PRIVATE DEBUG)
endif()
target_include_directories(unity-tests PRIVATE external/unity/src)

# Utility tests removed - no main function available
# target_compile_options(run-tests PRIVATE -fsanitize=address -g)
# target_link_options(run-tests PRIVATE -fsanitize=address)
# Regular tests WITH memory profiling for early leak detection

# ============================================================================
# REDUNDANT TARGETS REMOVED - Now in run-tests unified executable
# ============================================================================
# UNIFIED TEST SUITES (v2.0)
# ============================================================================
# Consolidated test structure with 4 main test suites:
# - test-core: Unit tests, parser, functions, API
# - test-memory: Memory profiling, AUTORELEASE, leak detection  
# - test-integration: Exception handling, REPL, namespaces
# - test-performance: Benchmarks, speed tests, efficiency
# ============================================================================

# Common source files for all test suites
set(COMMON_TEST_SOURCES
    src/object.c
    src/types.c
    src/vector.c
    src/map.c
    src/string.c
    src/symbol.c
    src/namespace.c
    src/parser.c
    src/reader.c
    src/function_call.c
    src/list_operations.c
    src/seq.c
    src/exception.c
    src/memory.c
    src/memory_profiler.c
    src/runtime.c
    src/clojure_core.c
    src/line_editor.c
)

# ============================================================================
# UNIFIED TEST SUITES (DEPRECATED - Use run-tests instead)
# ============================================================================
# These test suites are deprecated. Use the unified run-tests executable instead.
# ============================================================================

# ============================================================================
# LEGACY TEST TARGETS (DEPRECATED - Use unified test suites above)
# ============================================================================
# These targets are kept for backward compatibility but are deprecated.
# Use the new unified test suites: test-core, test-memory, test-integration, test-performance
# ============================================================================

# ============================================================================
# LEGACY TEST TARGETS (REMOVED - Files no longer exist)
# ============================================================================
# These targets have been removed as the files no longer exist.
# Use the unified run-tests executable instead.
# ============================================================================



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

install(TARGETS tiny-clj
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

# History test (test that REPL can handle multiple commands)
add_test(NAME repl-history-multiple COMMAND bash -c "echo -e '(+ 1 2)\n(* 3 4)\n(str \"test\")' | timeout 5s ./tiny-clj-repl")
set_tests_properties(repl-history-multiple PROPERTIES PASS_REGULAR_EXPRESSION "3.*12.*test" LABELS "repl;history")

# Seq Benchmark (REMOVED - File no longer exists)
# Use run-tests for sequence testing instead
