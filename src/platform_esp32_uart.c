// ESP32 UART platform functions for REPL + line editor.
//
// This file is macOS-buildable (stubs), and intended to be wired up to ESP-IDF
// by providing the weak hook functions below.

#include "platform.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

// -----------------------------------------------------------------------------
// Weak hooks (override in ESP32/ESP-IDF integration)
// -----------------------------------------------------------------------------

__attribute__((weak)) int tinyclj_esp32_uart_read_byte_nonblocking(void) {
    // Return -2 for "no data" (line_editor convention).
    return -2;
}

__attribute__((weak)) void tinyclj_esp32_uart_write_bytes(const uint8_t *data, size_t n) {
    // Fallback: write to stdout (useful for host builds).
    if (!data || n == 0) return;
    fwrite(data, 1, n, stdout);
    fflush(stdout);
}

__attribute__((weak)) void tinyclj_esp32_uart_flush(void) {
    fflush(stdout);
}

int platform_set_stdin_nonblocking(int enable) {
    (void)enable;
    // Not applicable on ESP32 UART; input is always non-blocking via hook.
    return 0;
}

int platform_readline_nb(char *buf, int max) {
    (void)buf; (void)max;
    // Not used when LINE_EDITING_ENABLED=1.
    return 0;
}

int platform_get_char(void *ctx) {
    (void)ctx;
    return tinyclj_esp32_uart_read_byte_nonblocking();
}

void platform_put_char(void *ctx, char c) {
    (void)ctx;
    tinyclj_esp32_uart_write_bytes((const uint8_t *)&c, 1);
}

void platform_put_string(void *ctx, const char *s) {
    (void)ctx;
    if (!s) return;
    tinyclj_esp32_uart_write_bytes((const uint8_t *)s, strlen(s));
}

void platform_set_raw_mode(int enable) {
    (void)enable;
    // No-op: terminal modes are host-only.
}

