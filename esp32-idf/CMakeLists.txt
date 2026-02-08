cmake_minimum_required(VERSION 3.16)

# ESP-IDF top-level project file.
#
# This project exists to measure *real* ESP32 RAM baselines and to exercise
# networking/mDNS under the actual ESP-IDF runtime (WiFi + lwIP + netif).
#
# It intentionally keeps tiny-clj compiled-in but avoids allocating RAM until
# features are used (lazy init).

# Redirect FetchContent dependencies to external/ directory (instead of _deps/)
# This must be set BEFORE including ESP-IDF's project.cmake
get_filename_component(REPO_ROOT "${CMAKE_SOURCE_DIR}/../.." ABSOLUTE)
set(FETCHCONTENT_BASE_DIR "${REPO_ROOT}/external" CACHE PATH "Base directory for FetchContent dependencies")

# Keep this ESP-IDF project minimal (REPL + stats only).
# ESP-IDF supports excluding components by name via EXCLUDE_COMPONENTS.
#
# IMPORTANT:
# - ESP-IDF parses EXCLUDE_COMPONENTS as a *space-separated string* (spaces2list()).
# - Do NOT make this a CMake list with semicolons, otherwise matching breaks.
# - Keep Bluetooth components enabled by NOT listing bt/esp_hid/etc. here.
# - Keep diagnostics for first deploy: app_trace/esp_gdbstub/espcoredump/perfmon are NOT excluded.
set(EXCLUDE_COMPONENTS
    "spiffs fatfs wear_levelling esp_http_server esp_http_client esp_https_server esp_https_ota tcp_transport esp-tls http_parser protobuf-c cmock unity esp_lcd esp_driver_cam sdmmc esp_driver_sdmmc esp_driver_sdspi esp_driver_sdio esp_driver_i2c esp_driver_spi esp_driver_i2s esp_driver_rmt esp_driver_ledc esp_driver_mcpwm esp_driver_pcnt esp_driver_dac esp_driver_sdm mqtt json console esp_vfs_console linenoise"
    CACHE STRING
    "ESP-IDF components to exclude for tinyclj baseline build")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tinyclj_esp32_idf)

