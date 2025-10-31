#include "common.h"
#include "platform.h"
#include <stdio.h>

void platform_init() {
}

void platform_print(const char *message) {
    if (message == NULL) {
        return;
    }
    printf("%s\n", message);
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
