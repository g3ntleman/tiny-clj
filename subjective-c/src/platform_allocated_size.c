/*
 * platform_allocated_size – best-effort allocated size for a heap pointer.
 * Used by memory_profiler. Declaration in platform_allocated_size.h.
 */

#include "platform_allocated_size.h"

#if defined(__APPLE__)
#include <malloc/malloc.h>
#endif

#if defined(ESP32_BUILD) && defined(__has_include) && __has_include(<esp_heap_caps.h>)
#include <esp_heap_caps.h>
#endif

size_t platform_allocated_size(const void *ptr) {
    if (!ptr) return 0;
#if defined(ESP32_BUILD) && defined(__has_include) && __has_include(<esp_heap_caps.h>)
    return heap_caps_get_allocated_size((void*)ptr);
#elif defined(__APPLE__)
    return malloc_size(ptr);
#else
    return 0;
#endif
}
