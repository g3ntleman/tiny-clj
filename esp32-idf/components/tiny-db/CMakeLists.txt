# tiny-db (as an ESP-IDF component)
#
# For now we build the core sources only (no tests). The actual flash blockdev
# integration is handled by tiny-clj's fs-layer or the embedding app.

# NOTE: ESP-IDF evaluates component CMakeLists in "script mode" during
# dependency discovery. In that mode, CONFIGURE_DEPENDS is not allowed.
file(GLOB TINY_DB_SRCS
  "../../../external/tiny-db/src/*.c"
)

idf_component_register(
  SRCS
    ${TINY_DB_SRCS}
  INCLUDE_DIRS
    "../../../external/tiny-db/inc"
    "../../../external/tiny-db/src"
)

# tiny-db includes a BSD-derived B-Tree which expects __DBINTERFACE_PRIVATE to be
# defined in the compilation unit (this enables internal macros such as M_32_SWAP).
target_compile_definitions(${COMPONENT_LIB} PRIVATE __DBINTERFACE_PRIVATE)

# tiny-db includes BSD-derived code. On Xtensa GCC, some paths trigger
# -Wmaybe-uninitialized false positives which become fatal under ESP-IDF's -Werror.
# Keep warnings visible, but don't fail the build on these specific ones.
target_compile_options(${COMPONENT_LIB} PRIVATE
  -Wno-error=maybe-uninitialized
)

