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

// Optional runtime stats hooks (override in ESP32/ESP-IDF integration).
// Return SIZE_MAX if unknown/unavailable.
__attribute__((weak)) size_t tinyclj_esp32_heap_bytes_free(void) { return (size_t)-1; }
__attribute__((weak)) size_t tinyclj_esp32_heap_bytes_total(void) { return (size_t)-1; }
__attribute__((weak)) size_t tinyclj_esp32_flash_bytes_free(void) { return (size_t)-1; }
__attribute__((weak)) size_t tinyclj_esp32_flash_bytes_total(void) { return (size_t)-1; }

/*
 * ESP-IDF Beispiel (in deinem ESP32-Projekt kompilieren, NICHT in tiny-clj):
 *
 * Ziel:
 * - Diese Funktionen überschreiben die weak hooks oben.
 * - Werte sind Bytes.
 * - Flash-Werte sollen "app-usable" sein: Overhead/Reserve bereits abgezogen.
 *
 * Hinweis:
 * - Heap total: hängt von deinen Heap-Caps ab (Default/8bit/32bit/...).
 * - Flash free: muss typischerweise aus deinem Flash-Storage Glue-Code kommen
 *   (z.B. aus eigener Belegungstabelle oder DB-Statistiken), nicht "aus dem Nichts".
 *
 * #include <stddef.h>
 * #include <esp_system.h>
 * #include <esp_heap_caps.h>
 *
 * size_t tinyclj_esp32_heap_bytes_free(void) {
 *   return esp_get_free_heap_size();
 * }
 *
 * size_t tinyclj_esp32_heap_bytes_total(void) {
 *   // Beispiel: "Default heap" Kapazität.
 *   return heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
 * }
 *
 * // Tiny-DB Partition (Beispiel: feste Partition "tiny-db")
 * // Du kannst hier auch eine eigene Label-Konfiguration verwenden.
 * #include <esp_partition.h>
 *
 * static size_t flash_partition_total_bytes_app_usable(void) {
 *   const esp_partition_t *p = esp_partition_find_first(0x40, 0x00, "tiny-db");
 *   if (!p) return (size_t)-1;
 *   // TODO: Ziehe hier deinen festen Overhead/Reserve ab (GC-Reserve, Metadata, Tiny-CLJ Reservierungen).
 *   // return (p->size > overhead) ? (p->size - overhead) : 0;
 *   return p->size;
 * }
 *
 * size_t tinyclj_esp32_flash_bytes_total(void) {
 *   return flash_partition_total_bytes_app_usable();
 * }
 *
 * size_t tinyclj_esp32_flash_bytes_free(void) {
 *   // TODO: Aus deinem Flash-Storage Glue-Code ermitteln.
 *   // Beispiele (je nach Integration):
 *   // - Partition total minus "bytes used" aus eigener Belegungstabelle
 *   // - Tiny-DB interne Statistiken (falls du sie pflegst/abfragst)
 *   // - Eine conservative Schätzung, die GC-Reserve berücksichtigt
 *   return (size_t)-1; // unbekannt => tiny-clj liefert den Key nicht
 * }
 */

// -----------------------------------------------------------------------------
// Platform API
// -----------------------------------------------------------------------------

void platform_init(void) {
    // ESP32-specific init should happen in the embedding app.
}

void platform_print(const char *message) {
    if (!message) return;
    fputs(message, stdout);
    fputc('\n', stdout);
}

const char *platform_name(void) {
    return "ESP32-UART";
}

// -----------------------------------------------------------------------------
// Sleep hook (override in ESP32/ESP-IDF integration)
// -----------------------------------------------------------------------------
__attribute__((weak)) void tinyclj_esp32_sleep_ms(unsigned int ms) { (void)ms; }

void platform_sleep_ms(unsigned int ms) {
    tinyclj_esp32_sleep_ms(ms);
}

// -----------------------------------------------------------------------------
// Optional runtime stats
// -----------------------------------------------------------------------------
size_t platform_heap_bytes_free(void) { return tinyclj_esp32_heap_bytes_free(); }
size_t platform_heap_bytes_total(void) { return tinyclj_esp32_heap_bytes_total(); }
size_t platform_flash_bytes_free(void) { return tinyclj_esp32_flash_bytes_free(); }
size_t platform_flash_bytes_total(void) { return tinyclj_esp32_flash_bytes_total(); }

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

