## Choose which platform implementation the application provides.
# Default is `uart` (REPL over UART). Set -DTINYCLJ_PLATFORM=embedded to
# build a non-REPL embedded image.
if(NOT DEFINED TINYCLJ_PLATFORM)
  set(TINYCLJ_PLATFORM "uart" CACHE STRING "tiny-clj platform: uart|embedded")
endif()

## Always include the embedded core platform implementation; append UART
## helpers when building a REPL image.
set(PLATFORM_SRCS "${CMAKE_CURRENT_LIST_DIR}/../../src/platform_esp32_embedded.c")
if(TINYCLJ_PLATFORM STREQUAL "uart")
  # UART REPL build needs:
  # - core ESP32 platform hooks (init/sleep/time/net) from platform_esp32_embedded.c
  # - UART I/O + line editor hooks from platform_esp32_uart.c
  list(APPEND PLATFORM_SRCS "${CMAKE_CURRENT_LIST_DIR}/../../src/platform_esp32_uart.c")
  set(_repl_def 1)
else()
  set(_repl_def 0)
endif()

idf_component_register(
  SRCS
    "tinyclj_idf_main.c"
    "tinyclj_idf_run.c"
    ${PLATFORM_SRCS}
  INCLUDE_DIRS
    "."
  REQUIRES
    freertos
    esp_system
    esp_timer
    esp_common
    esp_hw_support
    heap
    log
    esp_driver_uart
    spi_flash
    subjective-c
    tinyclj
)

# Propagate REPL_ENABLED to the component for conditional compilation.
target_compile_definitions(${COMPONENT_LIB} PRIVATE REPL_ENABLED=${_repl_def})

