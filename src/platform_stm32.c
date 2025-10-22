#include "common.h"
#include "platform.h"
#include <stdio.h>

void platform_init() {
}

void platform_print(const char *message) {
    if (!message) return;
    fputs(message, stdout);
    fputc('\n', stdout);
}

const char *platform_name() {
    return "STM32";
}

int platform_set_stdin_nonblocking(int enable) {
    (void)enable;
    return 0; // Stub for STM32; implement with UART if needed
}

int platform_readline_nb(char *buf, int max) {
    (void)buf; (void)max;
    return 0; // Stub: no input by default
}

// Line editor platform functions (stub implementations for STM32)
int platform_get_char(void) {
    return -1; // No input available on STM32 by default
}

void platform_put_char(char c) {
    putchar(c);
}

void platform_put_string(const char *s) {
    if (s) fputs(s, stdout);
}

// Line editor cleanup function for STM32
void cleanup_line_editor(void) {
    // Nothing to do for STM32 - line editor is disabled
}

// Raw mode support (stub for STM32)
void platform_set_raw_mode(int enable) {
    (void)enable;
    // Nothing to do for STM32
}
