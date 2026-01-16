// tdb_alloc.c - Allocation helpers for tiny-db.
//
// On host: uses malloc/free.
// On ESP32 with ESP-IDF available: prefers PSRAM for cache/scratch allocations.

#include "tdb_alloc.h"

#include <stdlib.h>

/*
 * ESP-IDF integration:
 * If the ESP-IDF headers are available, we can try allocating from PSRAM.
 * This keeps the codebase standalone: on host builds these headers are not found.
 */
#if defined(__has_include)
#if __has_include(<esp_heap_caps.h>)
#define TDB_HAVE_ESP_IDF 1
#include <esp_heap_caps.h>
#else
#define TDB_HAVE_ESP_IDF 0
#endif
#else
#define TDB_HAVE_ESP_IDF 0
#endif

void* tdb_alloc(size_t n, tdb_alloc_kind_t kind) {
    (void)kind;
    if (n == 0)
        return NULL;

#if TDB_HAVE_ESP_IDF
    /*
     * Prefer PSRAM for large caches if available; fall back to default heap.
     * If PSRAM is not present/enabled, heap_caps_malloc(MALLOC_CAP_SPIRAM) returns NULL.
     */
    if (kind == TDB_ALLOC_KIND_CACHE || kind == TDB_ALLOC_KIND_SCRATCH) {
        void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
        if (p)
            return p;
    }
    return heap_caps_malloc(n, MALLOC_CAP_DEFAULT);
#else
    return malloc(n);
#endif
}

void tdb_free(void* p) {
    free(p);
}

