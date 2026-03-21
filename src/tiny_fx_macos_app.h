#ifndef TINY_CLJ_TINY_FX_MACOS_APP_H
#define TINY_CLJ_TINY_FX_MACOS_APP_H

#include <stdbool.h>
#include <stdint.h>
#include "MiniFB_enums.h"

typedef struct TinyFxMacosWindow TinyFxMacosWindow;

/**
 * @brief Open the native macOS tiny-fx host window.
 *
 * @param title Initial window title.
 * @param width Content width in pixels.
 * @param height Content height in pixels.
 * @return Opaque window handle, or NULL on failure.
 */
TinyFxMacosWindow *tinyfx_macos_window_open(const char *title, unsigned width, unsigned height);

/**
 * @brief Close and release a native macOS tiny-fx window.
 *
 * @param window Window handle returned by tinyfx_macos_window_open().
 */
void tinyfx_macos_window_close(TinyFxMacosWindow *window);

/**
 * @brief Present one XRGB8888 framebuffer to the macOS host view.
 *
 * @param window Window handle.
 * @param buffer Source pixel buffer.
 * @param width Buffer width in pixels.
 * @param height Buffer height in pixels.
 * @return MiniFB-compatible update state.
 */
mfb_update_state tinyfx_macos_window_update(TinyFxMacosWindow *window,
                                            const uint32_t *buffer,
                                            unsigned width,
                                            unsigned height);

/**
 * @brief Pump pending AppKit events without presenting a new frame.
 *
 * @param window Window handle.
 * @return false when the window requested close, true otherwise.
 */
bool tinyfx_macos_window_pump_events(TinyFxMacosWindow *window);

/**
 * @brief Wait/poll for the next frame boundary.
 *
 * The current implementation is an event-pump step that keeps the host loop
 * responsive without depending on MiniFB's internal AppKit bootstrap.
 *
 * @param window Window handle.
 * @return false when the window requested close, true otherwise.
 */
bool tinyfx_macos_window_wait_sync(TinyFxMacosWindow *window);

/**
 * @brief Return the current keyboard state buffer.
 *
 * The buffer layout matches MiniFB's KB_KEY_* enum values.
 *
 * @param window Window handle.
 * @return Pointer to key-state buffer, or NULL.
 */
const uint8_t *tinyfx_macos_window_get_key_buffer(TinyFxMacosWindow *window);

/**
 * @brief Show or hide the mouse cursor while the tiny-fx window is active.
 *
 * @param window Window handle.
 * @param show Whether the cursor should be visible.
 */
void tinyfx_macos_window_show_cursor(TinyFxMacosWindow *window, bool show);

/**
 * @brief Apply a content viewport to the native host window.
 *
 * The current Cocoa host renders directly to the full content view, so this is
 * retained as a compatibility no-op.
 *
 * @param window Window handle.
 * @param offset_x Viewport x-offset.
 * @param offset_y Viewport y-offset.
 * @param width Viewport width.
 * @param height Viewport height.
 * @return true when the arguments are valid.
 */
bool tinyfx_macos_window_set_viewport(TinyFxMacosWindow *window,
                                      unsigned offset_x,
                                      unsigned offset_y,
                                      unsigned width,
                                      unsigned height);

/**
 * @brief Translate a macOS virtual key code into the MiniFB key enum.
 *
 * @param key_code macOS virtual key code from NSEvent.
 * @return Matching MiniFB key, or KB_KEY_UNKNOWN.
 */
mfb_key tinyfx_macos_key_from_virtual_key(unsigned short key_code);

#endif /* TINY_CLJ_TINY_FX_MACOS_APP_H */
