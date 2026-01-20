## Choose which platform implementation the application provides.
# Default is `uart` (REPL over UART). Set -DTINYCLJ_PLATFORM=embedded to
# build a non-REPL embedded image.
if(NOT DEFINED TINYCLJ_PLATFORM)
  set(TINYCLJ_PLATFORM "uart" CACHE STRING "tiny-clj platform: uart|embedded")
endif()

if(TINYCLJ_PLATFORM STREQUAL "uart")
  set(PLATFORM_SRC "${CMAKE_SOURCE_DIR}/../../src/platform_esp32_uart.c")
  add_compile_definitions(REPL_ENABLED=1)
else()
  set(PLATFORM_SRC "${CMAKE_SOURCE_DIR}/../../src/platform_esp32_embedded.c")
  add_compile_definitions(REPL_ENABLED=0)
endif()

idf_component_register(
  SRCS
    "tinyclj_idf_main.c"
    "tinyclj_idf_run.c"
    ${PLATFORM_SRC}
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
    tinyclj
)

