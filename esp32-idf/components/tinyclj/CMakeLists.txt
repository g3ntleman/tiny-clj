# tiny-clj (as an ESP-IDF component)
#
# This compiles the interpreter core plus the ESP32 UART platform layer.
# The embedding app provides the weak hooks (UART/time/sleep/stats) and starts
# the interpreter/REPL.

get_filename_component(TINYCLJ_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(TINYCLJ_SRC_DIR "${TINYCLJ_REPO_ROOT}/src")
include("${TINYCLJ_REPO_ROOT}/cmake/embedded_clojure_sources.cmake")

if(NOT DEFINED TINYCLJ_WITH_TINY_FX)
  set(TINYCLJ_WITH_TINY_FX ON)
endif()

set(TINYCLJ_CORE_SRCS
  "${TINYCLJ_SRC_DIR}/hash.c"
  "${TINYCLJ_SRC_DIR}/event_loop.c"
  "${TINYCLJ_SRC_DIR}/channel.c"
  "${TINYCLJ_SRC_DIR}/clojure_core.c"
  "${TINYCLJ_SRC_DIR}/object.c"
  "${TINYCLJ_SRC_DIR}/builtins.c"
  "${TINYCLJ_SRC_DIR}/builtins_gpio.c"
  "${TINYCLJ_SRC_DIR}/builtins_tiny_fx_gfx.c"
  "${TINYCLJ_SRC_DIR}/builtins_net.c"
  "${TINYCLJ_SRC_DIR}/builtins_mdns.c"
  "${TINYCLJ_SRC_DIR}/builtins_tiny_db.c"
  "${TINYCLJ_SRC_DIR}/builtins_strings.c"
  "${TINYCLJ_SRC_DIR}/builtins_regex.c"
  "${TINYCLJ_SRC_DIR}/runtime.c"
  "${TINYCLJ_SRC_DIR}/namespace.c"
  "${TINYCLJ_SRC_DIR}/symbol.c"
  "${TINYCLJ_SRC_DIR}/symbol_token.c"
  "${TINYCLJ_SRC_DIR}/parser.c"
  "${TINYCLJ_SRC_DIR}/ast_canon.c"
  "${TINYCLJ_SRC_DIR}/reader.c"
  "${TINYCLJ_SRC_DIR}/eval.c"
  "${TINYCLJ_SRC_DIR}/function.c"
  "${TINYCLJ_SRC_DIR}/optimize.c"
  "${TINYCLJ_SRC_DIR}/record.c"
  "${TINYCLJ_SRC_DIR}/environment.c"
  "${TINYCLJ_SRC_DIR}/equality.c"
  "${TINYCLJ_SRC_DIR}/list.c"
  "${TINYCLJ_SRC_DIR}/ast.c"
  "${TINYCLJ_SRC_DIR}/atom.c"
  "${TINYCLJ_SRC_DIR}/debug.c"
  "${TINYCLJ_SRC_DIR}/seq.c"
  "${TINYCLJ_SRC_DIR}/error_messages.c"
  "${TINYCLJ_SRC_DIR}/numeric_utils.c"
  "${TINYCLJ_SRC_DIR}/validation.c"
  "${TINYCLJ_SRC_DIR}/args_utils.c"
  "${TINYCLJ_SRC_DIR}/meta.c"
  "${TINYCLJ_SRC_DIR}/eval_arithmetic.c"
  "${TINYCLJ_SRC_DIR}/eval_comparison.c"
  "${TINYCLJ_SRC_DIR}/eval_sequence.c"
  "${TINYCLJ_SRC_DIR}/eval_special_forms.c"
  "${TINYCLJ_SRC_DIR}/to_string.c"
  "${TINYCLJ_SRC_DIR}/macro.c"
  "${TINYCLJ_SRC_DIR}/file_utils.c"
  "${TINYCLJ_SRC_DIR}/source_resolver.c"
  "${TINYCLJ_SRC_DIR}/embedded_sources.c"
  "${TINYCLJ_SRC_DIR}/fs_layer.c"
  "${TINYCLJ_SRC_DIR}/lockfree_spsc_queue.c"
  "${TINYCLJ_SRC_DIR}/gpio.c"
  "${TINYCLJ_SRC_DIR}/gpio_esp32.c"
  "${TINYCLJ_SRC_DIR}/line_editor.c"
  "${TINYCLJ_SRC_DIR}/repl.c"
  "${TINYCLJ_SRC_DIR}/repl_history_backend.c"
  "${TINYCLJ_SRC_DIR}/mdns_codec.c"
  "${TINYCLJ_SRC_DIR}/mdns_resolver.c"
)

set(TINYCLJ_FX_SRCS
  "${TINYCLJ_SRC_DIR}/sound_tick_scheduler.c"
  "${TINYCLJ_SRC_DIR}/gfx.c"
  "${TINYCLJ_SRC_DIR}/scene.c"
  "${TINYCLJ_SRC_DIR}/tiny_fx_gfx.c"
  "${TINYCLJ_SRC_DIR}/vector_scene_graph.c"
  "${TINYCLJ_SRC_DIR}/renderer_lifecycle.c"
  "${TINYCLJ_SRC_DIR}/rendered_state_snapshot.c"
  "${TINYCLJ_SRC_DIR}/render_backend.c"
  "${TINYCLJ_SRC_DIR}/sound_engine.c"
  "${TINYCLJ_SRC_DIR}/builtins_sound.c"
  "${TINYCLJ_SRC_DIR}/sound_backend_esp32.c"
)

set(TINYCLJ_SRCS ${TINYCLJ_CORE_SRCS})
if(TINYCLJ_WITH_TINY_FX)
  list(APPEND TINYCLJ_SRCS ${TINYCLJ_FX_SRCS})
endif()

# Compute paths for embedded Clojure generation (always needed for include dirs).
tinyclj_setup_embedded_clojure_generation(
  "${TINYCLJ_REPO_ROOT}"
  "${CMAKE_CURRENT_BINARY_DIR}"
  TINYCLJ_GENERATED_EMBEDDED_CLOJURE_OUTPUTS
  TINYCLJ_GENERATED_EMBEDDED_CLOJURE_DIR
  TINYCLJ_FLASH_FS_CLOJURE_MANIFEST
)

# ESP-IDF runs this file twice: first in a scriptable requirements pass where
# add_custom_target, add_dependencies, configure_file etc. are forbidden.
# idf_component_register must always run (it declares the component).
idf_component_register(
  SRCS
    ${TINYCLJ_SRCS}
  INCLUDE_DIRS
    "${TINYCLJ_GENERATED_EMBEDDED_CLOJURE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${TINYCLJ_SRC_DIR}"
    "${TINYCLJ_REPO_ROOT}"
  REQUIRES
    subjective-c
    tiny-db
    esp_app_format
    driver
    esp_adc
    esp_timer
    esp_partition
)

if(NOT CMAKE_SCRIPT_MODE_FILE)
  string(TIMESTAMP BUILD_EPOCH_SECONDS "%s" UTC)
  configure_file(
    "${TINYCLJ_REPO_ROOT}/build_info.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/build_info.h"
    @ONLY
  )

  add_custom_target(tinyclj_embedded_clojure_sources DEPENDS ${TINYCLJ_GENERATED_EMBEDDED_CLOJURE_OUTPUTS})
  add_dependencies(${COMPONENT_LIB} tinyclj_embedded_clojure_sources)

  target_compile_definitions(${COMPONENT_LIB} PUBLIC
    ESP32_BUILD=1
    TINYCLJ_WITH_TINY_FX=$<BOOL:${TINYCLJ_WITH_TINY_FX}>
    TINYCLJ_RAM_BACKEND_ENABLED=0
    LINE_EDITING_ENABLED=1
    MEMORY_PROFILING_ENABLED=0
    META_ENABLED=0
  )
  target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-clobbered)
endif()
