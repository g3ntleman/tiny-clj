// tdb_alloc.h - Allocation helpers for tiny-db (supports optional PSRAM on ESP32).
//
// Rationale: keep host and ESP32 code paths identical. On ESP32 with ESP-IDF,
// we can automatically prefer PSRAM for large caches when present.
#pragma once

#include <stddef.h>

typedef enum tdb_alloc_kind {
    TDB_ALLOC_KIND_GENERIC = 0,
    TDB_ALLOC_KIND_CACHE = 1,
    TDB_ALLOC_KIND_SCRATCH = 2,
} tdb_alloc_kind_t;

void* tdb_alloc(size_t n, tdb_alloc_kind_t kind);
void tdb_free(void* p);

