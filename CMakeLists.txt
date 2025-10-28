cmake_minimum_required(VERSION 3.10)
project(tiny-clj)

# Include common paths configuration
include(cmake_include_paths.cmake)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g -O0 -DDEBUG -DENABLE_MEMORY_PROFILING")
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

# Ultra-compact build for <150KB target (embedded deployment)
# Target: ARM Cortex-M (32-bit) for ESP32
set(CMAKE_C_FLAGS_EMBEDDED "${CMAKE_C_FLAGS_EMBEDDED} -Os -DNDEBUG -ffunction-sections -fdata-sections -fvisibility=hidden -flto -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-stack-protector -fomit-frame-pointer -fno-ident -fno-common")
if(APPLE)
  # For macOS testing: use size-optimized build (simulate embedded constraints)
  set(CMAKE_EXE_LINKER_FLAGS_EMBEDDED "${CMAKE_EXE_LINKER_FLAGS_EMBEDDED} -Wl,-dead_strip -Wl,-S -Wl,-x")
else()
  # For embedded: ESP32 toolchain (when using cross-compiler)
  set(CMAKE_C_FLAGS_EMBEDDED "${CMAKE_C_FLAGS_EMBEDDED} -mlongcalls")
  set(CMAKE_EXE_LINKER_FLAGS_EMBEDDED "${CMAKE_EXE_LINKER_FLAGS_EMBEDDED} -Wl,--gc-sections -Wl,--strip-all -Wl,-S -Wl,--strip-debug -Wl,--no-export-dynamic -Wl,--build-id=none -mlongcalls")
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
    src/byte_array.c
    src/reader.c
    src/function_call.c
    src/list.c
    src/vector.c
    src/map.c
    src/strings.c
    src/seq.c
    src/memory.c
    src/error_messages.c
    src/memory_profiler.c
    src/value.c
    src/numeric_utils.c
    src/validation.c
    src/args_utils.c
)

# Main executable (not built by default; use REPL)
add_executable(tiny-clj EXCLUDE_FROM_ALL ${SOURCES})
# Main Build - Meta only in Debug builds
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_definitions(tiny-clj PRIVATE)
else()
    target_compile_definitions(tiny-clj PRIVATE ENABLE_META)
endif()

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
    src/byte_array.c
    src/reader.c
    src/function_call.c
    src/list.c
    src/vector.c
    src/map.c
    src/strings.c
    src/seq.c
    src/memory.c
    src/error_messages.c
    src/value.c
    src/numeric_utils.c
    src/validation.c
    src/args_utils.c
)

# ESP32-optimized embedded interpreter (no REPL, executes startup-code.clj)
add_executable(tiny-clj-esp32
    src/main_esp32.c
    src/platform_esp32_embedded.c
    src/clojure_core.c
    src/types.c
    src/object.c
    src/builtins.c
    src/runtime.c
    src/namespace.c
    src/symbol.c
    src/exception.c
    src/parser.c
    src/byte_array.c
    src/reader.c
    src/function_call.c
    src/list.c
    src/vector.c
    src/map.c
    src/strings.c
    src/seq.c
    src/memory.c
    src/error_messages.c
    src/memory_profiler.c
    src/value.c
    src/numeric_utils.c
    src/validation.c
    src/args_utils.c
)


# ESP32 Ultra-Minimal Build (only core features for <150KB) - DISABLED
# This target has linker issues on macOS and is not needed for development

# ESP32 Production Build - No Meta, Optimized
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_definitions(tiny-clj-esp32 PRIVATE ESP32_BUILD DISABLE_MEMORY_PROFILING ENABLE_LINE_EDITING=0 DISABLE_DEBUG_SYMBOLS DISABLE_ERROR_MESSAGES DISABLE_STRING_FORMATTING)
elseif(CMAKE_BUILD_TYPE STREQUAL "Embedded")
    target_compile_definitions(tiny-clj-esp32 PRIVATE ESP32_BUILD DISABLE_MEMORY_PROFILING ENABLE_LINE_EDITING=0 DISABLE_DEBUG_SYMBOLS DISABLE_ERROR_MESSAGES DISABLE_STRING_FORMATTING DISABLE_META DISABLE_COMPLEX_PARSING DISABLE_VECTOR_OPERATIONS DISABLE_MAP_OPERATIONS)
else()
    target_compile_definitions(tiny-clj-esp32 PRIVATE ENABLE_META ESP32_BUILD DISABLE_MEMORY_PROFILING ENABLE_LINE_EDITING=0)
endif()
set_target_properties(tiny-clj-esp32 PROPERTIES
    COMPILE_FLAGS "${CMAKE_C_FLAGS_EMBEDDED}"
    LINK_FLAGS "${CMAKE_EXE_LINKER_FLAGS_EMBEDDED}"
)


# ESP32 Ultra-Minimal Build Configuration - DISABLED
# This target has linker issues on macOS and is not needed for development

# ESP32 Cross-Compilation Support
if(CMAKE_TOOLCHAIN_FILE MATCHES "esp32.cmake")
    message(STATUS "🔧 Using ESP32 cross-compilation toolchain")
    message(STATUS "📱 Target: ESP32 (Xtensa LX6)")
    message(STATUS "💾 Flash: 4MB, RAM: 520KB")
    message(STATUS "🎯 Binary Size Target: <150KB")
endif()

# REPL executable (with line editing support)
add_executable(tiny-clj-repl
    src/repl.c
    src/platform_macos.c
    src/line_editor.c
    ${SOURCES}
)
# REPL Build - Always in Debug mode for development
target_compile_definitions(tiny-clj-repl PRIVATE ENABLE_META DEBUG ENABLE_LINE_EDITING)
set_target_properties(tiny-clj-repl PROPERTIES
    COMPILE_FLAGS "-g -O0 -DDEBUG -DENABLE_MEMORY_PROFILING"
)

# STM32-optimized REPL (no test code, includes memory profiling for REPL)
add_executable(tiny-clj-esp32-repl
    src/repl.c
    src/platform_stm32.c
    src/line_editor.c
    src/memory_profiler.c
    ${STM32_SOURCES}
)
# STM32 REPL Build - No Meta in Release, Optimized
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_definitions(tiny-clj-esp32-repl PRIVATE ESP32_BUILD DISABLE_MEMORY_PROFILING ENABLE_LINE_EDITING=1)
else()
    target_compile_definitions(tiny-clj-esp32-repl PRIVATE ENABLE_META ESP32_BUILD DISABLE_MEMORY_PROFILING ENABLE_LINE_EDITING=1)
endif()
set_target_properties(tiny-clj-esp32-repl PROPERTIES
    COMPILE_FLAGS "-Os -DNDEBUG -ffunction-sections -fdata-sections"
    LINK_FLAGS "-Wl,-dead_strip"
)

# ============================================================================
# CATEGORIZED TEST EXECUTABLES
# ============================================================================

# Unity Test Framework (central runner with separate test files)
add_executable(unity-tests
    src/tests/unity_test_runner.c
    src/tests/test_registry.c
    src/tests/test_memory.c
    src/tests/test_parser.c
    src/tests/test_exception.c
    src/tests/test_basics.c
    src/tests/test_namespace_legacy.c
    src/tests/test_namespace.c
    src/tests/test_equal.c
    src/tests/test_seq.c
    src/tests/test_values.c
    src/tests/test_recur.c
    src/tests/test_let.c
    src/tests/test_do.c
    src/tests/test_defn.c
    src/tests/test_time.c
    src/tests/test_loops.c
    src/tests/test_fixed_point_legacy.c
    src/tests/test_fixed_point.c
    src/tests/test_sequences.c
    src/tests/test_byte_array.c
    src/tests/test_cow.c
    external/unity/src/unity.c
    src/builtins.c
    src/byte_array.c
    src/object.c
    src/types.c
    src/vector.c
    src/map.c
    src/strings.c
    src/symbol.c
    src/namespace.c
    src/parser.c
    src/reader.c
    src/function_call.c
    src/list.c
    src/seq.c
    src/clojure_core.c
    src/exception.c
    src/memory_profiler.c
    src/memory.c
    src/runtime.c
    src/error_messages.c
    src/value.c
    src/numeric_utils.c
    src/validation.c
    src/args_utils.c
)

# Debug parser target removed - file not found
# Unity Tests - Always in Debug mode for comprehensive testing
apply_common_settings(unity-tests)
set_target_properties(unity-tests PROPERTIES
    COMPILE_FLAGS "-g -O0 -DDEBUG -fsanitize=address -fno-omit-frame-pointer"
    LINK_FLAGS "-fsanitize=address"
)

# Copy-on-Write Assumptions Tests are now integrated into cow_tests.c


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
    src/strings.c
    src/symbol.c
    src/namespace.c
    src/parser.c
    src/reader.c
    src/function_call.c
    src/list.c
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

# Meta-Funktionalität wird jetzt pro-Target basierend auf Build-Type konfiguriert
# Release builds: ENABLE_META deaktiviert (kleinere Binaries, bessere Performance)
# Debug builds: ENABLE_META aktiviert (für Development/Debugging)

# ============================================================================
# CUSTOM TARGETS
# ============================================================================

# Test targets for easy execution
add_custom_target(run-unit-tests
    COMMAND unity-tests
    DEPENDS unity-tests
    COMMENT "Running unit tests"
)

# Benchmark targets (integrated from Makefile.benchmark)
add_custom_target(benchmark
    COMMAND ${CMAKE_COMMAND} -E echo "=== All Tests Passed! Running Performance Benchmarks with Analysis ==="
    COMMAND ./scripts/benchmark_with_analysis.sh
    DEPENDS unity-tests
    COMMENT "Running all tests first, then benchmarks with change analysis"
)

add_custom_target(benchmark-analyze
    COMMAND ${CMAKE_COMMAND} -E echo "=== Running Enhanced Benchmark Analysis ==="
    COMMAND ./scripts/benchmark_with_analysis.sh --update-baseline
    DEPENDS unity-tests
    COMMENT "Running enhanced benchmark analysis"
)

add_custom_target(benchmark-baseline
    COMMAND ${CMAKE_COMMAND} -E echo "=== Creating Performance Baseline ==="
    COMMAND ${CMAKE_COMMAND} -E copy benchmark_results.csv benchmark_baseline.csv
    COMMAND ${CMAKE_COMMAND} -E echo "✓ Baseline created from current benchmark results"
    COMMAND ${CMAKE_COMMAND} -E echo "Run 'make benchmark-compare' to compare future runs"
    DEPENDS benchmark
    COMMENT "Creating performance baseline"
)

add_custom_target(benchmark-compare
    COMMAND ${CMAKE_COMMAND} -E echo "=== Running Performance Change Analysis ==="
    COMMAND make test-benchmark-simple > /dev/null 2>&1
    COMMAND ./test-benchmark-simple > benchmark_output.txt
    COMMAND ./scripts/benchmark_with_analysis.sh
    DEPENDS unity-tests
    COMMENT "Running performance change analysis"
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
