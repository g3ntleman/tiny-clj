cmake_minimum_required(VERSION 3.10)
project(tiny-clj)

# Include common paths configuration
include(cmake_include_paths.cmake)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g -O0 -DDEBUG -DENABLE_MEMORY_PROFILING -DENABLE_META -DZOMBIE_ENABLED")
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG}")
# Unity's setjmp/longjmp should be compatible with our exception system
# Both use separate jmp_buf stacks, so they should work together
# Enable stdout flushing in Unity to ensure error messages are visible
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DUNITY_USE_FLUSH_STDOUT")
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
# Target: size-focused embedded build settings (ESP32)
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
  message(STATUS "LTO (Link-Time Optimization) enabled for Release builds")
else()
  message(STATUS "LTO not supported by toolchain (ipo_supported=false)")
  # Fallback: Try explicit LTO flags
  if(APPLE)
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -flto")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -flto")
    message(STATUS "Using explicit -flto flags for LTO")
  endif()
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR} include src external)

# Subjective-C subproject (static library + early tests)
add_subdirectory(subjective-c)

# Set output directory for all executables to "build"
if(NOT DEFINED CMAKE_RUNTIME_OUTPUT_DIRECTORY OR CMAKE_RUNTIME_OUTPUT_DIRECTORY STREQUAL "")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build")
endif()

# Generate build_info.h from template
configure_file(
    "${CMAKE_SOURCE_DIR}/build_info.h.in"
    "${CMAKE_BINARY_DIR}/build_info.h"
    @ONLY
)

# Make build_info.h available to all targets
include_directories(${CMAKE_BINARY_DIR})

# Source files (without main.c - main is target-specific)
list(APPEND SOURCES
    src/clojure_core.c
    src/object.c
    src/builtins.c
    src/builtins_strings.c
    src/builtins_regex.c
    src/runtime.c
    src/namespace.c
    src/symbol.c
    src/symbol_token.c
    src/parser.c
    src/ast_canon.c
    src/reader.c
    src/eval.c
    src/function.c
    src/optimize.c
    src/environment.c
    src/equality.c
    src/hash.c
    src/event_loop.c
    src/channel.c
    src/list.c
    src/ast.c
    src/atom.c
    src/file_utils.c
    src/debug.c
    src/seq.c
    src/error_messages.c
    src/memory_profiler.c
    src/numeric_utils.c
    src/validation.c
    src/args_utils.c
    src/meta.c
    src/eval_arithmetic.c
    src/eval_comparison.c
    src/eval_sequence.c
    src/eval_special_forms.c
    src/to_string.c
    src/macro.c
    src/regex.c
    external/tiny_regex.c
)

# Main executable (not built by default; use REPL)
add_executable(tiny-clj EXCLUDE_FROM_ALL 
    src/main.c
    ${SOURCES}
    src/platform_macos.c
)
target_link_libraries(tiny-clj PRIVATE subjective-c)
# Main Build - Meta only in Debug builds
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_definitions(tiny-clj PRIVATE)
else()
    target_compile_definitions(tiny-clj PRIVATE ENABLE_META)
endif()

# ESP32-optimized embedded interpreter (no REPL, executes startup-code.clj)
add_executable(tiny-clj-esp32
    src/main_esp32.c
    src/platform_esp32_embedded.c
    src/event_loop.c
    src/channel.c
    src/clojure_core.c
    src/object.c
    src/builtins.c
    src/builtins_strings.c
    src/builtins_regex.c
    src/runtime.c
    src/namespace.c
    src/symbol.c
    src/symbol_token.c
    src/parser.c
    src/ast_canon.c
    src/reader.c
    src/eval.c
    src/function.c
    src/optimize.c
    src/environment.c
    src/equality.c
    src/list.c
    src/ast.c
    src/atom.c
    src/debug.c
    src/seq.c
    src/error_messages.c
    src/memory_profiler.c
    src/numeric_utils.c
    src/validation.c
    src/args_utils.c
    src/meta.c
    src/eval_arithmetic.c
    src/eval_comparison.c
    src/eval_sequence.c
    src/eval_special_forms.c
    src/to_string.c
    src/macro.c
    src/regex.c
    external/tiny_regex.c
)
target_link_libraries(tiny-clj-esp32 PRIVATE subjective-c)


# ESP32 Ultra-Minimal Build (only core features for <150KB) - DISABLED
# This target has linker issues on macOS and is not needed for development

# ESP32 Production Build - No Meta, Optimized
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_definitions(tiny-clj-esp32 PRIVATE ESP32_BUILD DISABLE_MEMORY_PROFILING ENABLE_LINE_EDITING=0 DISABLE_DEBUG_SYMBOLS DISABLE_ERROR_MESSAGES DISABLE_STRING_FORMATTING)
elseif(CMAKE_BUILD_TYPE STREQUAL "Embedded")
    target_compile_definitions(tiny-clj-esp32 PRIVATE ESP32_BUILD DISABLE_MEMORY_PROFILING ENABLE_LINE_EDITING=0 DISABLE_DEBUG_SYMBOLS DISABLE_ERROR_MESSAGES DISABLE_STRING_FORMATTING DISABLE_META DISABLE_COMPLEX_PARSING)
else()
    target_compile_definitions(tiny-clj-esp32 PRIVATE ENABLE_META ESP32_BUILD DISABLE_MEMORY_PROFILING ENABLE_LINE_EDITING=0)
endif()
set_target_properties(tiny-clj-esp32 PROPERTIES
    COMPILE_FLAGS "${CMAKE_C_FLAGS_EMBEDDED}"
    LINK_FLAGS "${CMAKE_EXE_LINKER_FLAGS_EMBEDDED}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
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
target_link_libraries(tiny-clj-repl PRIVATE subjective-c)
# Output directory is set globally to "build"
# REPL Build - Always enable ENABLE_META for macOS REPL
# Debug in Debug mode, Release in Release mode
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_definitions(tiny-clj-repl PRIVATE ENABLE_META ENABLE_LINE_EDITING)
    set_target_properties(tiny-clj-repl PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    )
    # Release: use CMAKE_C_FLAGS_RELEASE (set above with -O3 -DNDEBUG)
else()
    target_compile_definitions(tiny-clj-repl PRIVATE ENABLE_META DEBUG ENABLE_LINE_EDITING ENABLE_MEMORY_PROFILING)
    set_target_properties(tiny-clj-repl PROPERTIES
        COMPILE_FLAGS "-g -O0 -DDEBUG -DENABLE_MEMORY_PROFILING -fno-omit-frame-pointer"
        LINK_FLAGS ""
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    )
endif()

# Profiling Build - Optimized but with visible functions for profiling
add_executable(tiny-clj-profile
    src/repl.c
    src/platform_macos.c
    src/line_editor.c
    ${SOURCES}
)
target_link_libraries(tiny-clj-profile PRIVATE subjective-c)
target_compile_definitions(tiny-clj-profile PRIVATE ENABLE_PROFILING ENABLE_META ENABLE_MEMORY_PROFILING DEBUG)
set_target_properties(tiny-clj-profile PROPERTIES
    COMPILE_FLAGS "-g -O2 -DDEBUG -DENABLE_MEMORY_PROFILING -fno-omit-frame-pointer"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)

# ============================================================================
# CATEGORIZED TEST EXECUTABLES
# ============================================================================

# Unity Test Framework (central runner with separate test files)
add_executable(unit-tests
    ${SOURCES}
    subjective-c/tests/test_runner.c
    src/tests/unity_test_runner.c
    subjective-c/tests/test_registry.c
    src/tests/test_memory_macros.c
    src/tests/test_parser.c
    src/tests/test_utf8_emoji.c
    src/tests/test_exception.c
    src/tests/test_basics.c
    src/tests/test_list.c
    src/tests/test_namespace.c
    src/tests/test_symbol_clojure_compat.c
    src/tests/test_core_initialization.c
    src/tests/test_equal.c
    src/tests/test_seq.c
    src/tests/test_recur.c
    src/tests/test_let.c
    src/tests/test_let_performance.c
    src/tests/test_call_frame.c
    src/tests/test_do.c
    src/tests/test_defn.c
    src/tests/test_native_lookup.c
    src/tests/test_clojure_core_loading.c
    src/tests/test_memory_leak_fibonacci.c
    src/tests/test_datetime.c
    src/tests/test_instant_uuid.c
    src/tests/test_loops.c
    src/tests/test_go_blocks.c
    src/tests/test_fixed_point.c
    src/tests/test_arithmetic.c
    src/tests/test_sequences.c
    src/tests/test_list_resolution.c
    src/tests/test_keyword_evaluation.c
    src/tests/test_core.c
    src/tests/test_builtins_destructure.c
    src/tests/test_macros.c
    src/tests/test_macro_expander_debug.c
    src/tests/test_threading_macros.c
    src/tests/test_predicates.c
    src/tests/test_require.c
    src/tests/test_require_perf.c
    src/tests/test_qualified_symbol_resolution.c
    src/tests/test_repl.c
    src/tests/test_file_io.c
    src/tests/test_atom.c
    src/tests/test_timer.c
    src/tests/test_meta.c
    src/tests/test_hashmap_highlevel.c
    src/tests/test_map.c
    src/tests/test_cow.c
    src/tests/test_special_forms.c
    src/tests/test_core_functions.c
    src/tests/test_regex.c
    external/unity/src/unity.c
    # Note: src/repl.c, src/platform_macos.c, src/line_editor.c are NOT in ${SOURCES}
    # but are needed for unit-tests (test_repl.c uses repl functionality)
    src/repl.c
    src/platform_macos.c
    src/line_editor.c
)
target_compile_definitions(unit-tests PRIVATE TINY_CLJ_TEST_RUNNER)
target_link_libraries(unit-tests PRIVATE subjective-c)
target_include_directories(unit-tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/subjective-c/tests
        ${CMAKE_SOURCE_DIR}/subjective-c
        ${CMAKE_SOURCE_DIR}
)

# Ensure subjective-c library is compiled with same flags as unit-tests
# This is critical: exception.c must use DEBUG to match CLJException struct size
# Note: DEBUG is set in subjective-c/CMakeLists.txt based on CMAKE_BUILD_TYPE
# For unit-tests, we need additional flags (UNITY_TESTS, etc.)
# Since unit-tests is always built in Debug mode, DEBUG will be set via CMAKE_BUILD_TYPE
target_compile_definitions(subjective-c PRIVATE 
    UNITY_TESTS 
    ENABLE_MEMORY_PROFILING 
    ENABLE_META 
    UNITY_INCLUDE_EXEC_TIME 
    ZOMBIE_ENABLED
)

# Unity Tests - Always in Debug mode for comprehensive testing
# Force Debug build regardless of CMAKE_BUILD_TYPE
# Remove any dependency on CMAKE_BUILD_TYPE for unit-tests
# IMPORTANT: Disable dead code elimination to preserve test registration functions
# that are called via constructor attributes
apply_common_settings(unit-tests)
# Option to disable ASAN for profiling
option(ENABLE_ASAN "Enable AddressSanitizer for unit-tests" OFF)
if(ENABLE_ASAN)
    if(APPLE)
        # macOS: disable dead_strip to preserve all functions (including test registrations)
        # Use target_link_options to properly set linker flags
        set_target_properties(unit-tests PROPERTIES
            COMPILE_FLAGS "-g -O0 -DDEBUG -DUNITY_TESTS -fno-omit-frame-pointer -fsanitize=address"
            COMPILE_DEFINITIONS "DEBUG;UNITY_TESTS;ENABLE_MEMORY_PROFILING;ENABLE_META;UNITY_INCLUDE_EXEC_TIME;ZOMBIE_ENABLED"
            # Force Debug configuration regardless of CMAKE_BUILD_TYPE
            COMPILE_OPTIONS "$<$<CONFIG:Debug>:-g;-O0;-DDEBUG>"
            COMPILE_OPTIONS "$<$<CONFIG:Release>:-g;-O0;-DDEBUG>"
            COMPILE_OPTIONS "$<$<CONFIG:RelWithDebInfo>:-g;-O0;-DDEBUG>"
            COMPILE_OPTIONS "$<$<CONFIG:MinSizeRel>:-g;-O0;-DDEBUG>"
        )
        # Override global linker flags to prevent dead code elimination
        # This ensures test registration functions (called via constructors) are preserved
        # Use target_link_options to properly set linker flags without dead_strip
        target_link_options(unit-tests PRIVATE
            -fsanitize=address
        )
        # Remove dead_strip from global flags if present
        string(REPLACE "-Wl,-dead_strip" "" TMP_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
        string(REPLACE "-Wl,-S" "" TMP_FLAGS "${TMP_FLAGS}")
        if(TMP_FLAGS)
            target_link_options(unit-tests PRIVATE ${TMP_FLAGS})
        endif()
    else()
        # Linux/other: disable gc-sections to preserve all functions
        set_target_properties(unit-tests PROPERTIES
            COMPILE_FLAGS "-g -O0 -DDEBUG -DUNITY_TESTS -fno-omit-frame-pointer -fsanitize=address"
            COMPILE_DEFINITIONS "DEBUG;UNITY_TESTS;ENABLE_MEMORY_PROFILING;ENABLE_META;UNITY_INCLUDE_EXEC_TIME;ZOMBIE_ENABLED"
            # Force Debug configuration regardless of CMAKE_BUILD_TYPE
            COMPILE_OPTIONS "$<$<CONFIG:Debug>:-g;-O0;-DDEBUG>"
            COMPILE_OPTIONS "$<$<CONFIG:Release>:-g;-O0;-DDEBUG>"
            COMPILE_OPTIONS "$<$<CONFIG:RelWithDebInfo>:-g;-O0;-DDEBUG>"
            COMPILE_OPTIONS "$<$<CONFIG:MinSizeRel>:-g;-O0;-DDEBUG>"
        )
        target_link_options(unit-tests PRIVATE
            -fsanitize=address
            -Wl,--no-gc-sections
        )
    endif()
else()
    if(APPLE)
        # macOS: disable dead_strip to preserve all functions (including test registrations)
        set_target_properties(unit-tests PROPERTIES
            COMPILE_FLAGS "-g -O0 -DDEBUG -DUNITY_TESTS -fno-omit-frame-pointer"
            COMPILE_DEFINITIONS "DEBUG;UNITY_TESTS;ENABLE_MEMORY_PROFILING;ENABLE_META;UNITY_INCLUDE_EXEC_TIME;ZOMBIE_ENABLED"
            # Force Debug configuration regardless of CMAKE_BUILD_TYPE
            COMPILE_OPTIONS "$<$<CONFIG:Debug>:-g;-O0;-DDEBUG>"
            COMPILE_OPTIONS "$<$<CONFIG:Release>:-g;-O0;-DDEBUG>"
            COMPILE_OPTIONS "$<$<CONFIG:RelWithDebInfo>:-g;-O0;-DDEBUG>"
            COMPILE_OPTIONS "$<$<CONFIG:MinSizeRel>:-g;-O0;-DDEBUG>"
        )
        # Override global linker flags to prevent dead code elimination
        # This ensures test registration functions (called via constructors) are preserved
        # Use target_link_options to properly set linker flags without dead_strip
        # Remove dead_strip from global flags if present
        string(REPLACE "-Wl,-dead_strip" "" TMP_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
        string(REPLACE "-Wl,-S" "" TMP_FLAGS "${TMP_FLAGS}")
        if(TMP_FLAGS)
            target_link_options(unit-tests PRIVATE ${TMP_FLAGS})
        endif()
    else()
        # Linux/other: disable gc-sections to preserve all functions
        set_target_properties(unit-tests PROPERTIES
            COMPILE_FLAGS "-g -O0 -DDEBUG -DUNITY_TESTS -fno-omit-frame-pointer"
            COMPILE_DEFINITIONS "DEBUG;UNITY_TESTS;ENABLE_MEMORY_PROFILING;ENABLE_META;UNITY_INCLUDE_EXEC_TIME;ZOMBIE_ENABLED"
            # Force Debug configuration regardless of CMAKE_BUILD_TYPE
            COMPILE_OPTIONS "$<$<CONFIG:Debug>:-g;-O0;-DDEBUG>"
            COMPILE_OPTIONS "$<$<CONFIG:Release>:-g;-O0;-DDEBUG>"
            COMPILE_OPTIONS "$<$<CONFIG:RelWithDebInfo>:-g;-O0;-DDEBUG>"
            COMPILE_OPTIONS "$<$<CONFIG:MinSizeRel>:-g;-O0;-DDEBUG>"
        )
        target_link_options(unit-tests PRIVATE
            -Wl,--no-gc-sections
        )
    endif()
endif()

# Normalize test file paths in Unity-Ausgabe:
# Ersetze das Projekt-Wurzelverzeichnis in __FILE__ durch einen relativen Pfad,
# damit Test-Dateinamen ohne absolute Prefixe erscheinen.
target_compile_options(unit-tests PRIVATE "-fmacro-prefix-map=${CMAKE_SOURCE_DIR}/=" "-DTINY_CLJ_TEST_RUNNER")

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
    src/atom.c
    src/symbol.c
    src/namespace.c
    src/parser.c
    src/ast_canon.c
    src/reader.c
    src/eval.c
    src/function.c
    src/environment.c
    src/equality.c
    src/list.c
    src/seq.c
    src/memory_profiler.c
    src/runtime.c
    src/clojure_core.c
    src/line_editor.c
    src/meta.c
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
    COMMAND ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/unit-tests
    DEPENDS unit-tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running unit tests"
)

# Benchmark targets (integrated from Makefile.benchmark)
add_custom_target(benchmark
    COMMAND ${CMAKE_COMMAND} -E echo "=== All Tests Passed! Running Performance Benchmarks with Analysis ==="
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/benchmark_with_analysis.sh
    DEPENDS unit-tests
    COMMENT "Running all tests first, then benchmarks with change analysis"
)

add_custom_target(benchmark-analyze
    COMMAND ${CMAKE_COMMAND} -E echo "=== Running Enhanced Benchmark Analysis ==="
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/benchmark_with_analysis.sh --update-baseline
    DEPENDS unit-tests
    COMMENT "Running enhanced benchmark analysis"
)

add_custom_target(benchmark-baseline
    COMMAND ${CMAKE_COMMAND} -E echo "=== Creating Performance Baseline ==="
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/benchmark_results.csv ${CMAKE_SOURCE_DIR}/benchmark_baseline.csv
    COMMAND ${CMAKE_COMMAND} -E echo "✓ Baseline created from current benchmark results"
    COMMAND ${CMAKE_COMMAND} -E echo "Run 'make benchmark-compare' to compare future runs"
    DEPENDS benchmark
    COMMENT "Creating performance baseline"
)

add_custom_target(benchmark-compare
    COMMAND ${CMAKE_COMMAND} -E echo "=== Running Performance Change Analysis ==="
    COMMAND make test-benchmark-simple > /dev/null 2>&1
    COMMAND ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/test-benchmark-simple > ${CMAKE_SOURCE_DIR}/benchmark_output.txt
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/benchmark_with_analysis.sh
    DEPENDS unit-tests
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
# Use RUNTIME_OUTPUT_DIRECTORY to get the path where tiny-clj-repl is built
# This avoids hard-coded paths by using CMake variables
# Note: All executables are now in ${CMAKE_SOURCE_DIR}/build
get_target_property(REPL_OUTPUT_DIR tiny-clj-repl RUNTIME_OUTPUT_DIRECTORY)
if(NOT REPL_OUTPUT_DIR)
    set(REPL_OUTPUT_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
endif()
add_test(NAME repl-history-multiple 
    COMMAND bash -c "printf '(+ 1 2)\n(* 3 4)\n(str \"test\")\n' | timeout 5s \"${REPL_OUTPUT_DIR}/tiny-clj-repl\""
)
set_tests_properties(repl-history-multiple PROPERTIES 
    PASS_REGULAR_EXPRESSION "3.*12.*\"test\""
    LABELS "repl;history"
)

# Seq Benchmark (REMOVED - File no longer exists)
# Use run-tests for sequence testing instead
