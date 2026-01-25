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

if(TINYCLJ_PLATFORM STREQUAL "uart")
  set(_repl_def 1)
else()
  set(_repl_def 0)
endif()

target_compile_definitions(${COMPONENT_LIB} PUBLIC
  ESP32_BUILD=1
  REPL_ENABLED=${_repl_def}
  LINE_EDITING_ENABLED=1
  MEMORY_PROFILING_ENABLED=0
  META_ENABLED=0
)

