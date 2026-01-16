// ft_alloc.h - Allocation helpers for flash-tree (supports optional PSRAM on ESP32).
//
// Rationale: keep host and ESP32 code paths identical. On ESP32 with ESP-IDF,
// we can automatically prefer PSRAM for large caches when present.
#pragma once

#include <stddef.h>

typedef enum ft_alloc_kind {
    FT_ALLOC_KIND_GENERIC = 0,
    FT_ALLOC_KIND_CACHE = 1,
    FT_ALLOC_KIND_SCRATCH = 2,
} ft_alloc_kind_t;

void* ft_alloc(size_t n, ft_alloc_kind_t kind);
void ft_free(void* p);

