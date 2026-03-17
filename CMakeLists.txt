cmake_minimum_required(VERSION 3.10)
project(tiny-clj)
include(CheckLanguage)
check_language(OBJC)
if(CMAKE_OBJC_COMPILER)
    enable_language(OBJC)
endif()

option(TINYCLJ_GAME_DEMO "Build optional MiniFB game demo" OFF)
option(TINYCLJ_MINIFB_METAL "Use MiniFB Metal backend on macOS" OFF)
option(TINYCLJ_WITH_TINY_FX "Build optional tiny-fx sound/gfx feature set" ON)

# Generate compile_commands.json for IDE (Go to Definition, IntelliSense)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Feature flags (compile-time): all are positive *_ENABLED macros (0/1)
option(META_ENABLED "Enable metadata support" OFF)
option(MEMORY_PROFILING_ENABLED "Enable memory profiling hooks" OFF)
option(ZOMBIE_ENABLED "Enable zombie mode (for memory-crash debugging)" ON)

if(META_ENABLED)
    set(META_ENABLED_VALUE 1)
else()
    set(META_ENABLED_VALUE 0)
endif()

if(MEMORY_PROFILING_ENABLED)
    set(MEMORY_PROFILING_ENABLED_VALUE 1)
else()
    set(MEMORY_PROFILING_ENABLED_VALUE 0)
endif()

if(ZOMBIE_ENABLED)
    set(ZOMBIE_ENABLED_VALUE 1)
else()
    set(ZOMBIE_ENABLED_VALUE 0)
endif()

# Include common paths configuration
include(cmake_include_paths.cmake)
include(cmake/embedded_clojure_sources.cmake)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g -O0 -DDEBUG")
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

# Ultra-compact build for embedded deployment
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
# NOTE: Avoid adding the whole 'external/' tree to include paths, so that
# template/reference subprojects can't be
# accidentally picked up by includes.
include_directories(${CMAKE_SOURCE_DIR} include src ${CMAKE_SOURCE_DIR}/subjective-c/src/subjective-c)
include_directories(${CMAKE_SOURCE_DIR}/external/tiny-db/inc ${CMAKE_SOURCE_DIR}/external/tiny-db/src)

# Subjective-C subproject (static library + early tests)
#
# IMPORTANT:
# - Build subjective-c in its own binary directory (not inside the subjective-c source dir)
# - Keep all subjective-c artifacts grouped under the top-level build tree
set(TINYCLJ_SUBJECTIVE_C_SOURCE_DIR "${CMAKE_SOURCE_DIR}/subjective-c")
set(TINYCLJ_SUBJECTIVE_C_BINARY_DIR "${CMAKE_BINARY_DIR}/subjective-c-build")
set(TINYCLJ_SUBJECTIVE_C_PUBLIC_INCLUDE_DIR "${TINYCLJ_SUBJECTIVE_C_SOURCE_DIR}/src")
# Subjective-c public headers are under: subjective-c/src/subjective-c/*.h
set(TINYCLJ_SUBJECTIVE_C_HEADERS_DIR "${TINYCLJ_SUBJECTIVE_C_PUBLIC_INCLUDE_DIR}/subjective-c")

# Compatibility anchor for tiny-clj unit tests: src/tests/tests_common.h uses
# relative includes like "../exception.h". Keep includes unchanged and provide
# an include-dir where ".." resolves into the subjective-c header directory.
set(TINYCLJ_SUBJECTIVE_C_TESTS_COMMON_ANCHOR_DIR "${TINYCLJ_SUBJECTIVE_C_HEADERS_DIR}/include_anchor")

# Zombie mode is compile-time and must be consistent across all targets.
# Default is OFF because zombie mode can lead to very high memory usage (OOM) in long test runs.
if(ZOMBIE_ENABLED)
    add_compile_definitions(ZOMBIE_ENABLED)
endif()

add_subdirectory(subjective-c "${TINYCLJ_SUBJECTIVE_C_BINARY_DIR}")

# Ensure core feature flags are set when building subjective-c so structs/layout
# and conditional code stay consistent. Use PRIVATE so consumers (e.g. tiny-clj-repl
# with META_ENABLED=0) can override without macro redefinition warnings.
target_compile_definitions(subjective-c PRIVATE
    MEMORY_PROFILING_ENABLED=${MEMORY_PROFILING_ENABLED_VALUE}
    META_ENABLED=${META_ENABLED_VALUE}
    $<$<BOOL:${ZOMBIE_ENABLED_VALUE}>:ZOMBIE_ENABLED>
)

# Tiny-DB subproject (C-only KV storage stack)
add_subdirectory(external/tiny-db)

# Ensure Subjective-C artifacts appear in its own build directory.
set_target_properties(subjective-c PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${TINYCLJ_SUBJECTIVE_C_BINARY_DIR}/lib"
)
set_target_properties(subjective-c-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${TINYCLJ_SUBJECTIVE_C_BINARY_DIR}/bin"
)

# Set output directory for all executables to "build"
if(NOT DEFINED CMAKE_RUNTIME_OUTPUT_DIRECTORY OR CMAKE_RUNTIME_OUTPUT_DIRECTORY STREQUAL "")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build")
endif()

# Build time as Unix epoch seconds (UTC) for reproducible #inst in runtime stats.
# IMPORTANT: Only set on targets that actually use it (repl.c, builtins.c) to
# avoid full rebuilds on every configure (the value changes each second).
string(TIMESTAMP BUILD_EPOCH_SECONDS "%s" UTC)

# Generate build_info.h from template
configure_file(
    "${CMAKE_SOURCE_DIR}/build_info.h.in"
    "${CMAKE_BINARY_DIR}/build_info.h"
    @ONLY
)

# Make generated headers (notably build_info.h) win over source-root headers.
# This prevents stale checked-in headers from shadowing configure_file outputs.
include_directories(BEFORE ${CMAKE_BINARY_DIR})

tinyclj_setup_embedded_clojure_generation(
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}"
    TINYCLJ_GENERATED_EMBEDDED_CLOJURE_OUTPUTS
    TINYCLJ_GENERATED_EMBEDDED_CLOJURE_DIR
    TINYCLJ_FLASH_FS_CLOJURE_MANIFEST
)
add_custom_target(tinyclj_embedded_clojure_sources DEPENDS ${TINYCLJ_GENERATED_EMBEDDED_CLOJURE_OUTPUTS})

# Core sources stay in the always-on runtime.
set(TINYCLJ_CORE_SOURCES
    src/clojure_core.c
    src/object.c
    src/builtins.c
    src/builtins_gpio.c
    src/builtins_tiny_fx_gfx.c
    src/builtins_net.c
    src/builtins_mdns.c
    src/mdns_codec.c
    src/mdns_resolver.c
    src/builtins_tiny_db.c
    src/builtins_strings.c
    src/builtins_regex.c
    src/runtime.c
    src/namespace.c
    src/symbol.c
    src/symbol_token.c
    src/parser.c
    src/ast_canon.c
    src/ast_compile.c
    src/reader.c
    src/eval.c
    src/function.c
    src/optimize.c
    src/record.c
    src/environment.c
    src/equality.c
    src/hash.c
    src/event_loop.c
    src/renderer_lifecycle.c
    src/rendered_state_snapshot.c
    src/render_backend.c
    src/viewer_collision.c
    src/channel.c
    src/lockfree_spsc_queue.c
    src/list.c
    src/ast.c
    src/atom.c
    src/file_utils.c
    src/source_resolver.c
    src/embedded_sources.c
    src/debug.c
    src/seq.c
    src/error_messages.c
    src/numeric_utils.c
    src/validation.c
    src/args_utils.c
    src/meta.c
    src/eval_arithmetic.c
    src/eval_comparison.c
    src/eval_sequence.c
    src/eval_special_forms.c
    src/eval_compiled.c
    src/to_string.c
    src/macro.c
    src/startup_pipeline.c
    src/fs_layer.c
    src/gpio.c
    src/gpio_host.c
    src/repl_history_backend.c
)

# tiny-fx is an optional feature pack: sound + vector graphics.
set(TINYCLJ_FX_COMMON_SOURCES
    src/sound_tick_scheduler.c
    src/sound_engine.c
    src/builtins_sound.c
    src/gfx.c
    src/tiny_fx_gfx.c
    src/vector_scene_graph.c
    src/scene.c
)

set(TINYCLJ_FX_HOST_SOURCES
    src/sound_backend_host.c
)

set(TINYCLJ_FX_ESP32_SOURCES
    src/sound_backend_esp32.c
)

set(TINYCLJ_HOST_TINY_FX_SOURCES
    ${TINYCLJ_FX_COMMON_SOURCES}
    ${TINYCLJ_FX_HOST_SOURCES}
)

set(TINYCLJ_ESP32_TINY_FX_SOURCES
    ${TINYCLJ_FX_COMMON_SOURCES}
    ${TINYCLJ_FX_ESP32_SOURCES}
)

set(SOURCES ${TINYCLJ_CORE_SOURCES})

function(tinyclj_link_apple_base target_name)
    if(APPLE)
        target_link_libraries(${target_name} PRIVATE "-framework CoreFoundation")
        target_link_directories(${target_name} PRIVATE "${CMAKE_OSX_SYSROOT}/usr/lib/system")
        target_link_libraries(${target_name} PRIVATE system_dnssd)
    endif()
endfunction()

function(tinyclj_target_enable_tiny_fx target_name platform_name)
    if(TINYCLJ_WITH_TINY_FX)
        if("${platform_name}" STREQUAL "HOST")
            target_sources(${target_name} PRIVATE ${TINYCLJ_HOST_TINY_FX_SOURCES})
            if(APPLE)
                target_link_libraries(${target_name} PRIVATE "-framework CoreAudio" "-framework AudioUnit")
            endif()
        elseif("${platform_name}" STREQUAL "ESP32")
            target_sources(${target_name} PRIVATE ${TINYCLJ_ESP32_TINY_FX_SOURCES})
        else()
            message(FATAL_ERROR "Unknown tiny-fx platform: ${platform_name}")
        endif()
        target_compile_definitions(${target_name} PRIVATE TINYCLJ_WITH_TINY_FX=1)
    else()
        target_compile_definitions(${target_name} PRIVATE TINYCLJ_WITH_TINY_FX=0)
    endif()
endfunction()

function(tinyclj_target_use_generated_embedded_clojure target_name)
    add_dependencies(${target_name} tinyclj_embedded_clojure_sources)
    target_include_directories(${target_name} PRIVATE "${TINYCLJ_GENERATED_EMBEDDED_CLOJURE_DIR}")
endfunction()

if(TINYCLJ_WITH_TINY_FX)
    add_library(tiny-fx EXCLUDE_FROM_ALL STATIC ${TINYCLJ_HOST_TINY_FX_SOURCES})
    target_include_directories(tiny-fx PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        ${TINYCLJ_SUBJECTIVE_C_HEADERS_DIR}
    )
    target_link_libraries(tiny-fx PUBLIC subjective-c tiny-db)
    if(APPLE)
        target_link_libraries(tiny-fx PUBLIC "-framework CoreAudio" "-framework AudioUnit")
    endif()
endif()

# Main executable (not built by default; use REPL)
add_executable(tiny-clj EXCLUDE_FROM_ALL 
    src/main.c
    ${SOURCES}
    src/platform_macos.c
)
target_link_libraries(tiny-clj PRIVATE subjective-c)
target_link_libraries(tiny-clj PRIVATE tiny-db)
tinyclj_link_apple_base(tiny-clj)
tinyclj_target_enable_tiny_fx(tiny-clj HOST)
target_include_directories(tiny-clj PRIVATE
    ${TINYCLJ_SUBJECTIVE_C_HEADERS_DIR} # allow "value.h", "common.h", etc.
)
tinyclj_target_use_generated_embedded_clojure(tiny-clj)
# Main Build - Meta only in Debug builds
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_definitions(tiny-clj PRIVATE)
else()
    target_compile_definitions(tiny-clj PRIVATE META_ENABLED=${META_ENABLED_VALUE})
endif()

# ESP32-optimized embedded interpreter (no REPL, executes startup-code.clj)
# Not built by default on host platforms (requires ESP32 toolchain / libs).
add_executable(tiny-clj-esp32 EXCLUDE_FROM_ALL
    src/main_esp32.c
    src/platform_esp32_embedded.c
    src/event_loop.c
    src/renderer_lifecycle.c
    src/rendered_state_snapshot.c
    src/render_backend.c
    src/channel.c
    src/clojure_core.c
    src/object.c
    src/builtins.c
    src/builtins_tiny_db.c
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
    src/record.c
    src/environment.c
    src/equality.c
    src/list.c
    src/ast.c
    src/atom.c
    src/debug.c
    src/seq.c
    src/error_messages.c
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
    src/startup_pipeline.c
    src/file_utils.c
    src/source_resolver.c
    src/embedded_sources.c
    src/fs_layer.c
)
# ESP32-only sources (must not be part of host targets)
target_sources(tiny-clj-esp32 PRIVATE
    src/gpio_esp32.c
)
tinyclj_target_enable_tiny_fx(tiny-clj-esp32 ESP32)
target_link_libraries(tiny-clj-esp32 PRIVATE subjective-c)
target_link_libraries(tiny-clj-esp32 PRIVATE tiny-db)
target_include_directories(tiny-clj-esp32 PRIVATE
    ${TINYCLJ_SUBJECTIVE_C_HEADERS_DIR} # allow "value.h", "common.h", etc.
)
tinyclj_target_use_generated_embedded_clojure(tiny-clj-esp32)


# ESP32 Ultra-Minimal Build (only core features) - DISABLED
# This target has linker issues on macOS and is not needed for development

# ESP32 Production Build - No Meta, Optimized
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_definitions(tiny-clj-esp32 PRIVATE ESP32_BUILD REPL_ENABLED=0 MEMORY_PROFILING_ENABLED=0 LINE_EDITING_ENABLED=0 DEBUG_SYMBOLS_ENABLED=0 ERROR_MESSAGES_ENABLED=0 STRING_FORMATTING_ENABLED=0)
elseif(CMAKE_BUILD_TYPE STREQUAL "Embedded")
    target_compile_definitions(tiny-clj-esp32 PRIVATE ESP32_BUILD REPL_ENABLED=0 MEMORY_PROFILING_ENABLED=0 LINE_EDITING_ENABLED=0 DEBUG_SYMBOLS_ENABLED=0 ERROR_MESSAGES_ENABLED=0 STRING_FORMATTING_ENABLED=0 META_ENABLED=0 COMPLEX_PARSING_ENABLED=0)
else()
    target_compile_definitions(tiny-clj-esp32 PRIVATE META_ENABLED=1 ESP32_BUILD REPL_ENABLED=0 MEMORY_PROFILING_ENABLED=0 LINE_EDITING_ENABLED=0)
endif()
set_target_properties(tiny-clj-esp32 PROPERTIES
    COMPILE_FLAGS "${CMAKE_C_FLAGS_EMBEDDED}"
    LINK_FLAGS "${CMAKE_EXE_LINKER_FLAGS_EMBEDDED}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)

#
# ESP32 UART REPL build (optional; uses weak UART hooks in platform_esp32_uart.c)
#
add_executable(tiny-clj-esp32-repl EXCLUDE_FROM_ALL
    src/repl.c
    src/line_editor.c
    src/platform_esp32_uart.c
    ${SOURCES}
)
# ESP32-only sources (must not be part of host targets)
target_sources(tiny-clj-esp32-repl PRIVATE
    src/gpio_esp32.c
)
tinyclj_target_enable_tiny_fx(tiny-clj-esp32-repl ESP32)
target_link_libraries(tiny-clj-esp32-repl PRIVATE subjective-c)
target_link_libraries(tiny-clj-esp32-repl PRIVATE tiny-db)
target_include_directories(tiny-clj-esp32-repl PRIVATE
    ${TINYCLJ_SUBJECTIVE_C_HEADERS_DIR}
    ${CMAKE_SOURCE_DIR}/external/esp-idf/components/esp_driver_gpio/include
    ${CMAKE_SOURCE_DIR}/external/esp-idf/components/esp_common/include
)
tinyclj_target_use_generated_embedded_clojure(tiny-clj-esp32-repl)
target_compile_definitions(tiny-clj-esp32-repl PRIVATE
    ESP32_BUILD
    REPL_ENABLED=1
    LINE_EDITING_ENABLED=1
    MEMORY_PROFILING_ENABLED=0
    META_ENABLED=0
)
set_target_properties(tiny-clj-esp32-repl PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)


# ESP32 Ultra-Minimal Build Configuration - DISABLED
# This target has linker issues on macOS and is not needed for development

# ESP32 Cross-Compilation Support
if(CMAKE_TOOLCHAIN_FILE MATCHES "esp32.cmake")
    message(STATUS "🔧 Using ESP32 cross-compilation toolchain")
    message(STATUS "📱 Target: ESP32 (Xtensa LX6)")
    message(STATUS "💾 Flash: 4MB, RAM: 520KB")
    message(STATUS "🎯 Binary Size Target: minimized")
endif()

# REPL executable (with line editing support)
add_executable(tiny-clj-repl
    src/repl.c
    src/platform_macos.c
    src/line_editor.c
    ${SOURCES}
)
target_link_libraries(tiny-clj-repl PRIVATE subjective-c)
target_link_libraries(tiny-clj-repl PRIVATE tiny-db)
tinyclj_link_apple_base(tiny-clj-repl)
tinyclj_target_enable_tiny_fx(tiny-clj-repl HOST)
target_include_directories(tiny-clj-repl PRIVATE
    ${TINYCLJ_SUBJECTIVE_C_HEADERS_DIR} # allow "value.h", "common.h", etc.
)
tinyclj_target_use_generated_embedded_clojure(tiny-clj-repl)
# Output directory is set globally to "build"
# REPL Build
# Debug in Debug mode, Release in Release mode
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_definitions(tiny-clj-repl PRIVATE META_ENABLED=0 MEMORY_PROFILING_ENABLED=${MEMORY_PROFILING_ENABLED_VALUE} LINE_EDITING_ENABLED=1)
    set_target_properties(tiny-clj-repl PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    )
    # Release: use CMAKE_C_FLAGS_RELEASE (set above with -O3 -DNDEBUG)
else()
    target_compile_definitions(tiny-clj-repl PRIVATE META_ENABLED=0 MEMORY_PROFILING_ENABLED=${MEMORY_PROFILING_ENABLED_VALUE} DEBUG LINE_EDITING_ENABLED=1)
    set_target_properties(tiny-clj-repl PROPERTIES
        COMPILE_FLAGS "-g -O0 -DDEBUG -DMEMORY_PROFILING_ENABLED=${MEMORY_PROFILING_ENABLED_VALUE} -fno-omit-frame-pointer"
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
target_link_libraries(tiny-clj-profile PRIVATE tiny-db)
tinyclj_link_apple_base(tiny-clj-profile)
tinyclj_target_enable_tiny_fx(tiny-clj-profile HOST)
target_include_directories(tiny-clj-profile PRIVATE
    ${TINYCLJ_SUBJECTIVE_C_HEADERS_DIR} # allow "value.h", "common.h", etc.
)
tinyclj_target_use_generated_embedded_clojure(tiny-clj-profile)
target_compile_definitions(tiny-clj-profile PRIVATE PROFILING_ENABLED=1 META_ENABLED=${META_ENABLED_VALUE} MEMORY_PROFILING_ENABLED=${MEMORY_PROFILING_ENABLED_VALUE} DEBUG)
set_target_properties(tiny-clj-profile PROPERTIES
    COMPILE_FLAGS "-g -O2 -DDEBUG -DMEMORY_PROFILING_ENABLED=${MEMORY_PROFILING_ENABLED_VALUE} -fno-omit-frame-pointer"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)

# ============================================================================
# CATEGORIZED TEST EXECUTABLES
# ============================================================================

set(TINYCLJ_FX_TEST_SOURCES)
if(TINYCLJ_WITH_TINY_FX)
    list(APPEND TINYCLJ_FX_TEST_SOURCES
        src/tests/test_sound_engine.c
        src/tests/test_vector_scene_graph.c
        src/tests/test_gfx_svg.c
        src/tests/test_gfx_collision_contract.c
    )
endif()

# Unity Test Framework (central runner with separate test files)
add_executable(unit-tests
    ${SOURCES}
    subjective-c/tests/test_runner.c
    src/tests/unity_test_runner.c
    subjective-c/tests/test_registry.c
    src/tests/test_memory_macros.c
    src/tests/test_memory.c
    src/tests/test_assets_edn.c
    src/tests/test_closure_capture_minimal.c
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
    src/tests/test_slot_ref.c
    src/tests/test_compiled_ast.c
    src/tests/test_line_editor_serial.c
    src/tests/test_do.c
    src/tests/test_defn.c
    src/tests/test_native_lookup.c
    src/tests/test_gpio_write.c
    src/tests/test_gpio_input.c
    src/tests/test_gpio_architecture_contract.c
    src/tests/test_event_loop_latency.c
    src/tests/test_clojure_core_loading.c
    src/tests/test_memory_leak_fibonacci.c
    src/tests/test_datetime.c
    src/tests/test_instant_uuid.c
    src/tests/test_edn_file_all_types.c
    src/tests/test_loops.c
    src/tests/test_eval_body_vector.c
    src/tests/test_eval_context.c
    src/tests/test_go_blocks.c
    src/tests/test_fixed_point.c
    src/tests/test_arithmetic.c
    src/tests/test_sequences.c
    src/tests/test_list_resolution.c
    src/tests/test_keyword_evaluation.c
    src/tests/test_core.c
    src/tests/test_builtins_destructure.c
    src/tests/test_macros.c
    src/tests/test_macro_alias_regression.c
    src/tests/test_macro_ns_asset_regression.c
    src/tests/test_macro_expander_debug.c
    src/tests/test_threading_macros.c
    src/tests/test_predicates.c
    src/tests/test_require.c
    src/tests/test_require_perf.c
    src/tests/test_qualified_symbol_resolution.c
    src/tests/test_recursive_resolution.c
    src/tests/test_main_entry.c
    src/tests/test_repl.c
    src/tests/test_runner_patterns.c
    src/tests/test_file_io.c
    src/tests/test_embedded_sources.c
    src/tests/test_rrd_scripts.c
    src/tests/tests_resolver.c
    src/tests/test_atom.c
    src/tests/test_atom_watch.c
    src/tests/test_timer.c
    src/tests/test_meta.c
    src/tests/test_hashmap_highlevel.c
    src/tests/test_map.c
    src/tests/test_records.c
    src/tests/test_cow.c
    src/tests/test_special_forms.c
    src/tests/test_dynamic_binding.c
    src/tests/test_core_functions.c
    src/tests/test_regex.c
    src/tests/test_alias_lowlevel.c
    src/tests/test_string.c
    src/tests/test_vector.c
    src/tests/test_values.c
    src/tests/test_time.c
    src/tests/test_yield_sleep_hooks.c
    src/tests/test_yield_sleep.c
    src/tests/test_runtime_stats.c
    src/tests/test_plan_trackA_scripts.c
    src/tests/test_pprint.c
    src/tests/test_print.c
    src/tests/test_static_keywords.c
    src/tests/test_lockfree_spsc_queue.c
    src/tests/test_byte_array.c
    src/tests/test_byte_array_view.c
    src/tests/test_platform_net.c
    src/tests/test_mdns_codec.c
    src/tests/test_mdns_resolver.c
    src/tests/test_mdns_bindings.c
    src/tests/test_platform_mdns.c
    ${TINYCLJ_FX_TEST_SOURCES}
    # src/tests/test_closure_symbol_resolution.c  # File doesn't exist - commented out
    external/unity/src/unity.c
    # Note: src/repl.c, src/platform_macos.c, src/line_editor.c are NOT in ${SOURCES}
    # but are needed for unit-tests (test_repl.c uses repl functionality)
    src/repl.c
    src/platform_macos.c
    src/line_editor.c
)
target_compile_definitions(unit-tests PRIVATE
    DEBUG
    TINY_CLJ_TEST_RUNNER
    UNITY_USE_COMMAND_LINE_ARGS
    LINE_EDITING_ENABLED=1
    MEMORY_PROFILING_ENABLED=${MEMORY_PROFILING_ENABLED_VALUE}
    META_ENABLED=${META_ENABLED_VALUE}
    UNITY_INCLUDE_EXEC_TIME
)
tinyclj_target_enable_tiny_fx(unit-tests HOST)
target_link_libraries(unit-tests PRIVATE subjective-c)
target_link_libraries(unit-tests PRIVATE tiny-db)
tinyclj_link_apple_base(unit-tests)
target_include_directories(unit-tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/subjective-c/tests
        ${TINYCLJ_SUBJECTIVE_C_PUBLIC_INCLUDE_DIR}
        ${TINYCLJ_SUBJECTIVE_C_HEADERS_DIR}
        ${TINYCLJ_SUBJECTIVE_C_TESTS_COMMON_ANCHOR_DIR}
        ${CMAKE_SOURCE_DIR}
)
tinyclj_target_use_generated_embedded_clojure(unit-tests)

# Ensure subjective-c library is compiled with same flags as unit-tests
# This is critical: exception.c must use DEBUG to match CLJException struct size,
# and zombie_detection test needs retain() to check rc<=0 (DEBUG) and ZOMBIE_ENABLED.
# Without this, a Release build would compile subjective-c with NDEBUG and zombie_detection would fail.
# Align subjective-c compile defs with top-level feature flags
target_compile_definitions(subjective-c PRIVATE 
    DEBUG
    UNITY_TESTS
    MEMORY_PROFILING_ENABLED=${MEMORY_PROFILING_ENABLED_VALUE}
    META_ENABLED=${META_ENABLED_VALUE}
    UNITY_INCLUDE_EXEC_TIME
    $<$<BOOL:${ZOMBIE_ENABLED_VALUE}>:ZOMBIE_ENABLED>
)

# Unity Tests - Always in Debug mode for comprehensive testing
# Force Debug build regardless of CMAKE_BUILD_TYPE
# Remove any dependency on CMAKE_BUILD_TYPE for unit-tests
# IMPORTANT: Disable dead code elimination to preserve test registration functions
# that are called via constructor attributes
apply_common_settings(unit-tests)

set_target_properties(unit-tests PROPERTIES OUTPUT_NAME "unit-tests")

# Use release-ish flags for faster runs unless caller overrides.
target_compile_options(unit-tests PRIVATE
    $<$<CONFIG:Debug>:-O0>
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:RelWithDebInfo>:-O2>
    $<$<CONFIG:MinSizeRel>:-Os>
)
# Option to disable ASAN for profiling
option(ENABLE_ASAN "Enable AddressSanitizer for unit-tests" OFF)
if(ENABLE_ASAN)
    if(APPLE)
        # macOS: disable dead_strip to preserve all functions (including test registrations)
        # Use target_link_options to properly set linker flags
        set_target_properties(unit-tests PROPERTIES
            COMPILE_FLAGS "-g -O0 -DDEBUG -DUNITY_TESTS -fno-omit-frame-pointer -fsanitize=address"
            COMPILE_DEFINITIONS "DEBUG;UNITY_TESTS;MEMORY_PROFILING_ENABLED=${MEMORY_PROFILING_ENABLED_VALUE};META_ENABLED=${META_ENABLED_VALUE};UNITY_INCLUDE_EXEC_TIME;LINE_EDITING_ENABLED=1"
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
            COMPILE_DEFINITIONS "DEBUG;UNITY_TESTS;MEMORY_PROFILING_ENABLED=${MEMORY_PROFILING_ENABLED_VALUE};META_ENABLED=${META_ENABLED_VALUE};UNITY_INCLUDE_EXEC_TIME;LINE_EDITING_ENABLED=1"
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
            COMPILE_DEFINITIONS "DEBUG;UNITY_TESTS;MEMORY_PROFILING_ENABLED=${MEMORY_PROFILING_ENABLED_VALUE};META_ENABLED=${META_ENABLED_VALUE};UNITY_INCLUDE_EXEC_TIME;LINE_EDITING_ENABLED=1"
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
            COMPILE_DEFINITIONS "DEBUG;UNITY_TESTS;MEMORY_PROFILING_ENABLED=${MEMORY_PROFILING_ENABLED_VALUE};META_ENABLED=${META_ENABLED_VALUE};UNITY_INCLUDE_EXEC_TIME;LINE_EDITING_ENABLED=1"
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
# Release builds: META_ENABLED deaktiviert (kleinere Binaries, bessere Performance)
# Debug builds: META_ENABLED aktiviert (für Development/Debugging)

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

add_test(NAME repl-main-option-boundary COMMAND tiny-clj-repl -m test.main-entry-missing --x)
set_tests_properties(repl-main-option-boundary PROPERTIES
    WILL_FAIL TRUE
    FAIL_REGULAR_EXPRESSION "Unknown argument"
    LABELS "repl")

add_test(NAME repl-main-conflict-with-eval
    COMMAND bash -c "\"$<TARGET_FILE:tiny-clj-repl>\" -e \"(+ 1 2)\" -m test.main-entry-missing 2>&1 | grep -F -- \"-m/--main cannot be combined\" >/dev/null")
set_tests_properties(repl-main-conflict-with-eval PROPERTIES
    LABELS "repl")

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

# Subjective-C Unity suite (C tests)
add_test(NAME unit-tests COMMAND $<TARGET_FILE:unit-tests>)
set_tests_properties(unit-tests PROPERTIES LABELS "unit")

add_test(
    NAME generate-embedded-clojure-source
    COMMAND python3 "${CMAKE_SOURCE_DIR}/scripts/test_generate_embedded_clojure_source.py"
)
set_tests_properties(generate-embedded-clojure-source PROPERTIES LABELS "unit;scripts")

if(TINYCLJ_GAME_DEMO)
    if(NOT TINYCLJ_WITH_TINY_FX)
        message(FATAL_ERROR "game-demo requires TINYCLJ_WITH_TINY_FX=ON")
    endif()
    include(FetchContent)
    # Select MiniFB macOS backend explicitly to keep runs reproducible.
    if(APPLE)
        unset(USE_METAL_API CACHE)
        if(TINYCLJ_MINIFB_METAL)
            set(MINIFB_USE_METAL_API ON CACHE BOOL "" FORCE)
        else()
            set(MINIFB_USE_METAL_API OFF CACHE BOOL "" FORCE)
        endif()
    endif()
    FetchContent_Declare(
        minifb
        GIT_REPOSITORY https://github.com/emoon/minifb.git
        GIT_TAG master
    )
    FetchContent_MakeAvailable(minifb)

    if(NOT TARGET minifb)
        message(FATAL_ERROR "MiniFB target 'minifb' was not created by dependency build.")
    endif()

    add_executable(game-demo
        src/game_demo_minifb.c
        src/platform_macos.c
        ${SOURCES}
    )
    tinyclj_target_enable_tiny_fx(game-demo HOST)
    tinyclj_target_use_generated_embedded_clojure(game-demo)
    if(APPLE)
        target_sources(game-demo PRIVATE src/game_demo_macos_menu.m)
    endif()
    target_include_directories(game-demo PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    target_compile_definitions(game-demo PRIVATE TINYCLJ_WITH_MINIFB=1)
    target_link_libraries(game-demo PRIVATE minifb m subjective-c tiny-db)
    tinyclj_link_apple_base(game-demo)
endif()

# Seq Benchmark (REMOVED - File no longer exists)
# Use run-tests for sequence testing instead
target_sources(unit-tests PRIVATE src/tests/test_leak_sound_demos.c)
