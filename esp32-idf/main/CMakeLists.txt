## TINYCLJ_PRODUCT / TINY_FX_ENABLED: set in esp32-idf/cmake/tinyclj_idf_compile_flags.cmake (before project()).

idf_component_register(
  SRCS
    "tinyclj_idf_main.c"
    "tinyclj_idf_run.c"
    "tinyclj_idf_display.c"
    "tinyclj_idf_renderer.c"
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
    esp_driver_gpio
    esp_driver_spi
    esp_lcd
    esp_driver_uart
    spi_flash
    subjective-c
    tinyclj
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE REPL_ENABLED=1)
