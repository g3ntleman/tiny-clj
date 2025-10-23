#ifndef TINY_CLJ_COMMON_H
#define TINY_CLJ_COMMON_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
#ifndef ESP32_BUILD
#include <execinfo.h>
#include <unistd.h>
#endif

// Custom assert with stack trace
#ifdef ESP32_BUILD
#define CLJ_ASSERT(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "\n🚨 ASSERTION FAILED: %s\n", #expr); \
        fprintf(stderr, "📍 File: %s, Line: %d\n", __FILE__, __LINE__); \
        abort(); \
    } \
} while(0)
#else
#define CLJ_ASSERT(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "\n🚨 ASSERTION FAILED: %s\n", #expr); \
        fprintf(stderr, "📍 File: %s, Line: %d\n", __FILE__, __LINE__); \
        fprintf(stderr, "📚 Stack Trace:\n"); \
        void *array[20]; \
        int size = backtrace(array, 20); \
        char **strings = backtrace_symbols(array, size); \
        for (int i = 0; i < size; i++) { \
            fprintf(stderr, "  %d: %s\n", i, strings[i]); \
        } \
        free(strings); \
        fprintf(stderr, "\n"); \
        abort(); \
    } \
} while(0)
#endif

// Debug-only assert with stack trace
#ifdef DEBUG
    #define CLJ_DEBUG_ASSERT(expr) CLJ_ASSERT(expr)
#else
    #define CLJ_DEBUG_ASSERT(expr) ((void)0)
#endif

#endif // TINY_CLJ_COMMON_H
