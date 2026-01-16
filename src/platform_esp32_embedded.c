// ESP32 Platform functions for embedded execution (no REPL, no Line Editor)
#include "platform.h"
#include <stdio.h>
#include <stddef.h>

void platform_init(void) {
    // ESP32-specific initialization
}

// -----------------------------------------------------------------------------
// Sleep hook (override in ESP32/ESP-IDF integration)
// -----------------------------------------------------------------------------
__attribute__((weak)) void tinyclj_esp32_sleep_ms(unsigned int ms) { (void)ms; }

void platform_sleep_ms(unsigned int ms) {
    tinyclj_esp32_sleep_ms(ms);
}

void platform_print(const char *message) {
    if (!message) return;
    fputs(message, stdout);
    fputc('\n', stdout);
}

void platform_put_string(void *ctx, const char *s) {
    (void)ctx;
    if (s) fputs(s, stdout);
}

const char *platform_name(void) {
    return "esp32";
}

// No line editor functions needed for embedded execution

// -----------------------------------------------------------------------------
// Optional runtime stats hooks (override in ESP32/ESP-IDF integration).
// Return SIZE_MAX if unknown/unavailable.
// -----------------------------------------------------------------------------
__attribute__((weak)) size_t tinyclj_esp32_heap_bytes_free(void) { return (size_t)-1; }
__attribute__((weak)) size_t tinyclj_esp32_heap_bytes_total(void) { return (size_t)-1; }
__attribute__((weak)) size_t tinyclj_esp32_flash_bytes_free(void) { return (size_t)-1; }
__attribute__((weak)) size_t tinyclj_esp32_flash_bytes_total(void) { return (size_t)-1; }

/*
 * ESP-IDF Beispiel: siehe Kommentarblock in src/platform_esp32_uart.c
 * (die gleichen vier Funktionen kannst du für embedded builds überschreiben).
 */

size_t platform_heap_bytes_free(void) { return tinyclj_esp32_heap_bytes_free(); }
size_t platform_heap_bytes_total(void) { return tinyclj_esp32_heap_bytes_total(); }
size_t platform_flash_bytes_free(void) { return tinyclj_esp32_flash_bytes_free(); }
size_t platform_flash_bytes_total(void) { return tinyclj_esp32_flash_bytes_total(); }



