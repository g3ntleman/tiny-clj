// ESP32 Platform functions for embedded execution (no REPL, no Line Editor)
#include "platform.h"
#include <stdio.h>

void platform_init(void) {
    // ESP32-specific initialization
}

void platform_print(const char *message) {
    if (!message) return;
    fputs(message, stdout);
    fputc('\n', stdout);
}

void platform_put_string(const char *s) {
    if (s) fputs(s, stdout);
}

// No line editor functions needed for embedded execution



