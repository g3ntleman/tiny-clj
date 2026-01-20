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
  "../../../src/environment.c"
  "../../../src/equality.c"
  "../../../src/list.c"
  "../../../src/ast.c"
  "../../../src/atom.c"
  "../../../src/debug.c"
  "../../../src/seq.c"
  "../../../src/error_messages.c"
  "../../../src/memory_profiler.c"
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
  "../../../src/fs_layer.c"
  "../../../src/line_editor.c"
  "../../../src/mdns_codec.c"
  "../../../src/mdns_resolver.c"
)

# Select a single platform implementation at configure time. Valid values:
#  - "uart"      -> include src/platform_esp32_uart.c (REPL UART)
#  - "embedded"  -> include src/platform_esp32_embedded.c (no UART hooks)
if(NOT DEFINED TINYCLJ_PLATFORM)
  set(TINYCLJ_PLATFORM "uart" CACHE STRING "tiny-clj platform: uart|embedded")
endif()

if(TINYCLJ_PLATFORM STREQUAL "uart")
  list(APPEND TINYCLJ_SRCS "../../../src/platform_esp32_uart.c")
elseif(TINYCLJ_PLATFORM STREQUAL "embedded")
  list(APPEND TINYCLJ_SRCS "../../../src/platform_esp32_embedded.c")
else()
  message(FATAL_ERROR "Unknown TINYCLJ_PLATFORM: ${TINYCLJ_PLATFORM} (expected 'uart' or 'embedded')")
endif()

idf_component_register(
  SRCS
    ${TINYCLJ_SRCS}
  INCLUDE_DIRS
    "../../../src"
    "../../../"
  REQUIRES
    subjective-c
    tiny-db
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC
  ESP32_BUILD=1
  REPL_ENABLED=1
  LINE_EDITING_ENABLED=1
  MEMORY_PROFILING_ENABLED=0
  META_ENABLED=0
)

