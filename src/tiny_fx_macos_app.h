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
 * The buffer layout matches MiniFB's MFB_KB_KEY_* enum values.
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
 * @return Matching MiniFB key, or MFB_KB_KEY_UNKNOWN.
 */
mfb_key tinyfx_macos_key_from_virtual_key(unsigned short key_code);

#ifndef MFB_KB_KEY_LAST
#define MFB_KB_KEY_LAST KB_KEY_LAST
#define MFB_KB_KEY_UNKNOWN KB_KEY_UNKNOWN
#define MFB_KB_KEY_SPACE KB_KEY_SPACE
#define MFB_KB_KEY_APOSTROPHE KB_KEY_APOSTROPHE
#define MFB_KB_KEY_COMMA KB_KEY_COMMA
#define MFB_KB_KEY_MINUS KB_KEY_MINUS
#define MFB_KB_KEY_PERIOD KB_KEY_PERIOD
#define MFB_KB_KEY_SLASH KB_KEY_SLASH
#define MFB_KB_KEY_0 KB_KEY_0
#define MFB_KB_KEY_1 KB_KEY_1
#define MFB_KB_KEY_2 KB_KEY_2
#define MFB_KB_KEY_3 KB_KEY_3
#define MFB_KB_KEY_4 KB_KEY_4
#define MFB_KB_KEY_5 KB_KEY_5
#define MFB_KB_KEY_6 KB_KEY_6
#define MFB_KB_KEY_7 KB_KEY_7
#define MFB_KB_KEY_8 KB_KEY_8
#define MFB_KB_KEY_9 KB_KEY_9
#define MFB_KB_KEY_SEMICOLON KB_KEY_SEMICOLON
#define MFB_KB_KEY_EQUAL KB_KEY_EQUAL
#define MFB_KB_KEY_A KB_KEY_A
#define MFB_KB_KEY_B KB_KEY_B
#define MFB_KB_KEY_C KB_KEY_C
#define MFB_KB_KEY_D KB_KEY_D
#define MFB_KB_KEY_E KB_KEY_E
#define MFB_KB_KEY_F KB_KEY_F
#define MFB_KB_KEY_G KB_KEY_G
#define MFB_KB_KEY_H KB_KEY_H
#define MFB_KB_KEY_I KB_KEY_I
#define MFB_KB_KEY_J KB_KEY_J
#define MFB_KB_KEY_K KB_KEY_K
#define MFB_KB_KEY_L KB_KEY_L
#define MFB_KB_KEY_M KB_KEY_M
#define MFB_KB_KEY_N KB_KEY_N
#define MFB_KB_KEY_O KB_KEY_O
#define MFB_KB_KEY_P KB_KEY_P
#define MFB_KB_KEY_Q KB_KEY_Q
#define MFB_KB_KEY_R KB_KEY_R
#define MFB_KB_KEY_S KB_KEY_S
#define MFB_KB_KEY_T KB_KEY_T
#define MFB_KB_KEY_U KB_KEY_U
#define MFB_KB_KEY_V KB_KEY_V
#define MFB_KB_KEY_W KB_KEY_W
#define MFB_KB_KEY_X KB_KEY_X
#define MFB_KB_KEY_Y KB_KEY_Y
#define MFB_KB_KEY_Z KB_KEY_Z
#define MFB_KB_KEY_LEFT_BRACKET KB_KEY_LEFT_BRACKET
#define MFB_KB_KEY_BACKSLASH KB_KEY_BACKSLASH
#define MFB_KB_KEY_RIGHT_BRACKET KB_KEY_RIGHT_BRACKET
#define MFB_KB_KEY_GRAVE_ACCENT KB_KEY_GRAVE_ACCENT
#define MFB_KB_KEY_ESCAPE KB_KEY_ESCAPE
#define MFB_KB_KEY_ENTER KB_KEY_ENTER
#define MFB_KB_KEY_TAB KB_KEY_TAB
#define MFB_KB_KEY_BACKSPACE KB_KEY_BACKSPACE
#define MFB_KB_KEY_INSERT KB_KEY_INSERT
#define MFB_KB_KEY_DELETE KB_KEY_DELETE
#define MFB_KB_KEY_RIGHT KB_KEY_RIGHT
#define MFB_KB_KEY_LEFT KB_KEY_LEFT
#define MFB_KB_KEY_DOWN KB_KEY_DOWN
#define MFB_KB_KEY_UP KB_KEY_UP
#define MFB_KB_KEY_LEFT_SHIFT KB_KEY_LEFT_SHIFT
#define MFB_KB_KEY_LEFT_CONTROL KB_KEY_LEFT_CONTROL
#define MFB_KB_KEY_LEFT_ALT KB_KEY_LEFT_ALT
#define MFB_KB_KEY_LEFT_SUPER KB_KEY_LEFT_SUPER
#define MFB_KB_KEY_RIGHT_SHIFT KB_KEY_RIGHT_SHIFT
#define MFB_KB_KEY_RIGHT_CONTROL KB_KEY_RIGHT_CONTROL
#define MFB_KB_KEY_RIGHT_ALT KB_KEY_RIGHT_ALT
#define MFB_KB_KEY_RIGHT_SUPER KB_KEY_RIGHT_SUPER
#define MFB_KB_KEY_MENU KB_KEY_MENU
#define MFB_STATE_OK STATE_OK
#define MFB_STATE_EXIT STATE_EXIT
#define MFB_STATE_INVALID_WINDOW STATE_INVALID_WINDOW
#define MFB_STATE_INVALID_BUFFER STATE_INVALID_BUFFER
#define MFB_STATE_INTERNAL_ERROR STATE_INTERNAL_ERROR
#endif

#endif /* TINY_CLJ_TINY_FX_MACOS_APP_H */
