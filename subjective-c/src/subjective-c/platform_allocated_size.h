/**
 * platform_allocated_size – best-effort allocated size for a heap pointer.
 * Used by memory_profiler. Implementation in platform_allocated_size.c.
 */

#ifndef SUBJECTIVE_C_PLATFORM_ALLOCATED_SIZE_H
#define SUBJECTIVE_C_PLATFORM_ALLOCATED_SIZE_H

#include <stddef.h>

/** Returns byte size of allocation for ptr, or 0 if unavailable. */
size_t platform_allocated_size(const void *ptr);

#endif
