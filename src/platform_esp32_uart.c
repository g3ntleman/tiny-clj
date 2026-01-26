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
// Optional stdout observer hook (used by REPL to decide whether to print a newline
// before the next prompt).
// -----------------------------------------------------------------------------
__attribute__((weak)) void tinyclj_stdout_observe_bytes(const char *data, size_t n) {
    (void)data;
    (void)n;
}

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

// -----------------------------------------------------------------------------
// REPL / line editor platform APIs
// -----------------------------------------------------------------------------

int platform_readline_nb(char *buf, int max) {
    if (!buf || max <= 1) return -1;

    static char linebuf[2048];
    static int len = 0;

    // Drain available bytes into a line buffer until we see a line terminator.
    for (;;) {
        int b = tinyclj_esp32_uart_read_byte_nonblocking();
        if (b == -2) {
            break; // no input available
        }
        if (b == -1) {
            if (len == 0) return -1; // EOF and nothing buffered
            // Flush buffered bytes as final line.
            int outlen = (len < (max - 1)) ? len : (max - 1);
            memcpy(buf, linebuf, (size_t)outlen);
            buf[outlen] = '\0';
            len = 0;
            return outlen;
        }

        if (len + 1 >= (int)sizeof(linebuf)) {
            len = 0;
            return -1;
        }

        char c = (char)b;
        // Treat CR as end-of-line for typical UART monitors.
        if (c == '\r') c = '\n';
        linebuf[len++] = c;
        if (c == '\n') {
            break;
        }
    }

    // Look for a newline; if present, return a line without the terminator.
    for (int i = 0; i < len; i++) {
        if (linebuf[i] == '\n') {
            int outlen = (i < (max - 1)) ? i : (max - 1);
            memcpy(buf, linebuf, (size_t)outlen);
            buf[outlen] = '\0';

            int remaining = len - (i + 1);
            if (remaining > 0) {
                memmove(linebuf, linebuf + i + 1, (size_t)remaining);
            }
            len = remaining;
            return outlen;
        }
    }

    return 0;
}

int platform_get_char(void *ctx) {
    (void)ctx;
    return tinyclj_esp32_uart_read_byte_nonblocking();
}

void platform_put_char(void *ctx, char c) {
    (void)ctx;
    uint8_t b = (uint8_t)c;
    tinyclj_esp32_uart_write_bytes(&b, 1);
    tinyclj_stdout_observe_bytes((const char*)&c, 1);
}

void platform_put_string(void *ctx, const char *s) {
    (void)ctx;
    if (!s) return;
    size_t n = strlen(s);
    if (n == 0) return;
    tinyclj_esp32_uart_write_bytes((const uint8_t*)s, n);
    tinyclj_stdout_observe_bytes(s, n);
}

void platform_set_raw_mode(int enable) {
    (void)enable;
    // On ESP32 UART we don't have a host terminal mode to switch.
}

int platform_set_stdin_nonblocking(int enable) {
    (void)enable;
    return 0;
}

