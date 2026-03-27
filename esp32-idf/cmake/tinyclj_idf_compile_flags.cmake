# Shared ESP-IDF firmware compile definitions for all components (subjective-c, tinyclj, main, …).
# Included from esp32-idf/CMakeLists.txt before project() so idf_component_register picks them up via
# add_compile_definitions (see ESP-IDF component.cmake).

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

# Same values as former tinyclj PUBLIC defs; must be visible to subjective-c (THREAD_LOCAL / ESP32_BUILD).
idf_build_set_property(COMPILE_DEFINITIONS "ESP32_BUILD=1" APPEND)
if(TINYCLJ_WITH_TINY_FX)
  idf_build_set_property(COMPILE_DEFINITIONS "TINYCLJ_WITH_TINY_FX=1" APPEND)
else()
  idf_build_set_property(COMPILE_DEFINITIONS "TINYCLJ_WITH_TINY_FX=0" APPEND)
endif()
idf_build_set_property(COMPILE_DEFINITIONS "TINYCLJ_RAM_BACKEND_ENABLED=0" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "LINE_EDITING_ENABLED=1" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "MEMORY_PROFILING_ENABLED=0" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "META_ENABLED=0" APPEND)
