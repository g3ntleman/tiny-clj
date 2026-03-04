# tiny-clj (as an ESP-IDF component)
#
# This compiles the interpreter core plus the ESP32 UART platform layer.
# The embedding app provides the weak hooks (UART/time/sleep/stats) and starts
# the interpreter/REPL.

set(TINYCLJ_SRCS
  "../../../src/hash.c"
  "../../../src/event_loop.c"
  "../../../src/channel.c"
  "../../../src/clojure_core.c"
  "../../../src/object.c"
  "../../../src/builtins.c"
  "../../../src/builtins_net.c"
  "../../../src/builtins_mdns.c"
  "../../../src/builtins_tiny_db.c"
  "../../../src/builtins_strings.c"
  "../../../src/builtins_regex.c"
  "../../../src/runtime.c"
  "../../../src/namespace.c"
  "../../../src/symbol.c"
  "../../../src/symbol_token.c"
  "../../../src/parser.c"
  "../../../src/ast_canon.c"
  "../../../src/reader.c"
  "../../../src/eval.c"
  "../../../src/function.c"
  "../../../src/optimize.c"
  "../../../src/record.c"
  "../../../src/environment.c"
  "../../../src/equality.c"
  "../../../src/list.c"
  "../../../src/ast.c"
  "../../../src/atom.c"
  "../../../src/debug.c"
  "../../../src/seq.c"
  "../../../src/error_messages.c"
  "../../../src/numeric_utils.c"
  "../../../src/validation.c"
  "../../../src/args_utils.c"
  "../../../src/meta.c"
  "../../../src/eval_arithmetic.c"
  "../../../src/eval_comparison.c"
  "../../../src/eval_sequence.c"
  "../../../src/eval_special_forms.c"
  "../../../src/to_string.c"
  "../../../src/macro.c"
  "../../../src/file_utils.c"
  "../../../src/source_resolver.c"
  "../../../src/embedded_sources.c"
  "../../../src/fs_layer.c"
  "../../../src/gfx.c"
  "../../../src/lockfree_spsc_queue.c"
  "../../../src/scene.c"
  "../../../src/tiny_gfx.c"
  "../../../src/vector_scene_graph.c"
  "../../../src/gpio_esp32.c"
  "../../../src/audio_engine.c"
  "../../../src/builtins_audio.c"
  "../../../src/audio_backend_esp32.c"
  "../../../src/line_editor.c"
  "../../../src/repl.c"
  "../../../src/repl_history_backend.c"
  "../../../src/mdns_codec.c"
  "../../../src/mdns_resolver.c"
)

# Build time (UTC epoch seconds) for runtime stats :build-time
string(TIMESTAMP BUILD_EPOCH_SECONDS "%s" UTC)

get_filename_component(TINYCLJ_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

# Generate build_info.h for the ESP-IDF component build. The desktop/host build
# generates this header in its own build dir, but IDF compiles the same sources
# in a separate build graph and needs its own generated copy.
configure_file(
  "${TINYCLJ_REPO_ROOT}/build_info.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/build_info.h"
  @ONLY
)

idf_component_register(
  SRCS
    ${TINYCLJ_SRCS}
  INCLUDE_DIRS
    "${CMAKE_CURRENT_BINARY_DIR}"
    "../../../src"
    "../../../"
  REQUIRES
    subjective-c
    tiny-db
    driver
    esp_timer
    esp_partition
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC
  ESP32_BUILD=1
  TINYCLJ_RAM_BACKEND_ENABLED=0
  LINE_EDITING_ENABLED=1
  DEBUG=1
  MEMORY_PROFILING_ENABLED=0
  META_ENABLED=0
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-clobbered)
