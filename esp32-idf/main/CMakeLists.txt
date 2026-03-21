## Choose which runtime product the firmware should expose.
if(NOT DEFINED TINYCLJ_PRODUCT)
  set(TINYCLJ_PRODUCT "tiny-clj" CACHE STRING "tiny-clj product: tiny-clj|tiny-fx")
endif()
set_property(CACHE TINYCLJ_PRODUCT PROPERTY STRINGS tiny-clj tiny-fx)

if(TINYCLJ_PRODUCT STREQUAL "tiny-clj")
  set(TINYCLJ_WITH_TINY_FX OFF CACHE BOOL "Build tiny-fx native feature set" FORCE)
elseif(TINYCLJ_PRODUCT STREQUAL "tiny-fx")
  set(TINYCLJ_WITH_TINY_FX ON CACHE BOOL "Build tiny-fx native feature set" FORCE)
else()
  message(FATAL_ERROR "Unknown TINYCLJ_PRODUCT='${TINYCLJ_PRODUCT}'. Expected tiny-clj or tiny-fx.")
endif()

## Shared runtime contract: always include the embedded core platform plus
## the UART REPL helpers.
set(PLATFORM_SRCS "${CMAKE_CURRENT_LIST_DIR}/../../src/platform_esp32_embedded.c")
list(APPEND PLATFORM_SRCS "${CMAKE_CURRENT_LIST_DIR}/../../src/platform_esp32_uart.c")

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

target_compile_definitions(${COMPONENT_LIB} PRIVATE REPL_ENABLED=1)
