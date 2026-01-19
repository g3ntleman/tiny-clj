#include <stdint.h>
#include <stddef.h>

#if defined(ESP_PLATFORM) && defined(__has_include)
#if __has_include(<esp_system.h>) && __has_include(<esp_timer.h>) && __has_include(<esp_heap_caps.h>) && __has_include(<esp_spi_flash.h>) && __has_include(<esp_flash.h>) && __has_include(<driver/uart.h>) && __has_include(<freertos/FreeRTOS.h>) && __has_include(<freertos/task.h>)
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <esp_spi_flash.h>
#include <esp_flash.h>
#include <driver/uart.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#define TINYCLJ_HAVE_ESP_IDF_HEADERS 1
#else
#define TINYCLJ_HAVE_ESP_IDF_HEADERS 0
#endif
#elif defined(ESP_PLATFORM)
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <esp_spi_flash.h>
#include <esp_flash.h>
#include <driver/uart.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#define TINYCLJ_HAVE_ESP_IDF_HEADERS 1
#else
#define TINYCLJ_HAVE_ESP_IDF_HEADERS 0
#endif

#if !TINYCLJ_HAVE_ESP_IDF_HEADERS
// Editor-only stubs for environments that don't have ESP-IDF headers configured.
// This keeps local indexing/linting working; the real implementation is compiled
// when building under ESP-IDF (ESP_PLATFORM).
typedef int esp_err_t;
#ifndef ESP_OK
#define ESP_OK 0
#endif
#ifndef MALLOC_CAP_DEFAULT
#define MALLOC_CAP_DEFAULT 0
#endif
static inline void vTaskDelay(int ticks) { (void)ticks; }
static inline int pdMS_TO_TICKS(unsigned int ms) { return (int)ms; }
static inline int64_t esp_timer_get_time(void) { return 0; }
static inline size_t esp_get_free_heap_size(void) { return (size_t)-1; }
static inline size_t heap_caps_get_total_size(int caps) { (void)caps; return (size_t)-1; }
static inline esp_err_t esp_flash_get_size(void *chip, uint32_t *out_size_bytes) {
    (void)chip;
    if (out_size_bytes) *out_size_bytes = 0;
    return ESP_OK;
}
#endif // !TINYCLJ_HAVE_ESP_IDF_HEADERS

// tiny-clj embedded hooks
void tinyclj_esp32_sleep_ms(unsigned int ms);
uint32_t tinyclj_esp32_current_time_ms(void);
size_t tinyclj_esp32_heap_bytes_free(void);
size_t tinyclj_esp32_heap_bytes_total(void);
size_t tinyclj_esp32_flash_bytes_free(void);
size_t tinyclj_esp32_flash_bytes_total(void);

int tinyclj_esp32_uart_read_byte_nonblocking(void);
void tinyclj_esp32_uart_write_bytes(const uint8_t *data, size_t n);
void tinyclj_esp32_uart_flush(void);

// Entry point implemented in tinyclj_idf_run.c
void tinyclj_idf_start(void);

void tinyclj_esp32_sleep_ms(unsigned int ms) {
    // FreeRTOS delay.
    vTaskDelay(pdMS_TO_TICKS(ms));
}

uint32_t tinyclj_esp32_current_time_ms(void) {
    // IMPORTANT: tiny-clj integers are fixnums (29-bit). We must keep this value bounded.
    //
    // tiny-clj's `current-time-ms` is specified as milliseconds within a 24h window [0..86400000).
    // On ESP32 we derive this from monotonic time since boot modulo 86400000.
    return (uint32_t)((esp_timer_get_time() / 1000) % 86400000);
}

size_t tinyclj_esp32_heap_bytes_free(void) {
    return esp_get_free_heap_size();
}

size_t tinyclj_esp32_heap_bytes_total(void) {
    return heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
}

size_t tinyclj_esp32_flash_bytes_total(void) {
    // Total physical SPI flash size (bytes).
    uint32_t size_bytes = 0;
    if (esp_flash_get_size(NULL, &size_bytes) != ESP_OK || size_bytes == 0) {
        return (size_t)-1;
    }
    return (size_t)size_bytes;
}

size_t tinyclj_esp32_flash_bytes_free(void) {
    // "Free flash bytes" is storage-backend specific (partition + filesystem/DB usage).
    // Until tiny-db / FS is wired to a specific partition here, report "unavailable".
    return (size_t)-1;
}

void app_main(void) {
#if defined(ESP_PLATFORM)
    // UART0 is usually the default console. We install a small driver so the
    // REPL can do non-blocking reads/writes via uart_read_bytes/uart_write_bytes.
    const uart_config_t cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    (void)uart_param_config(UART_NUM_0, &cfg);
    (void)uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0);
#endif

    tinyclj_idf_start();
}

int tinyclj_esp32_uart_read_byte_nonblocking(void) {
#if defined(ESP_PLATFORM)
    uint8_t b = 0;
    int n = uart_read_bytes(UART_NUM_0, &b, 1, 0);
    if (n == 1) return (int)b;
    return -2; // no input
#else
    return -2;
#endif
}

void tinyclj_esp32_uart_write_bytes(const uint8_t *data, size_t n) {
#if defined(ESP_PLATFORM)
    if (!data || n == 0) return;
    (void)uart_write_bytes(UART_NUM_0, (const char*)data, (int)n);
#else
    (void)data; (void)n;
#endif
}

void tinyclj_esp32_uart_flush(void) {
    // No-op on ESP-IDF UART driver; writes are queued internally.
}

