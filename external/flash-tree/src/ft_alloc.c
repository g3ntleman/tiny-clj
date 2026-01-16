// ft_alloc.c - Allocation helpers for flash-tree.
//
// On host: uses malloc/free.
// On ESP32 with ESP-IDF available: prefers PSRAM for cache/scratch allocations.

#include "ft_alloc.h"

#include <stdlib.h>

/*
 * ESP-IDF integration:
 * If the ESP-IDF headers are available, we can try allocating from PSRAM.
 * This keeps the codebase standalone: on host builds these headers are not found.
 */
#if defined(__has_include)
#if __has_include(<esp_heap_caps.h>)
#define FT_HAVE_ESP_IDF 1
#include <esp_heap_caps.h>
#else
#define FT_HAVE_ESP_IDF 0
#endif
#else
#define FT_HAVE_ESP_IDF 0
#endif

void* ft_alloc(size_t n, ft_alloc_kind_t kind) {
    (void)kind;
    if (n == 0)
        return NULL;

#if FT_HAVE_ESP_IDF
    /*
     * Prefer PSRAM for large caches if available; fall back to default heap.
     * If PSRAM is not present/enabled, heap_caps_malloc(MALLOC_CAP_SPIRAM) returns NULL.
     */
    if (kind == FT_ALLOC_KIND_CACHE || kind == FT_ALLOC_KIND_SCRATCH) {
        void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
        if (p)
            return p;
    }
    return heap_caps_malloc(n, MALLOC_CAP_DEFAULT);
#else
    return malloc(n);
#endif
}

void ft_free(void* p) {
    free(p);
}

