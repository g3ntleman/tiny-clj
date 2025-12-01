#ifndef TINY_CLJ_COMMON_H
#define TINY_CLJ_COMMON_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifndef ESP32_BUILD
#include <execinfo.h>
#include <unistd.h>
#endif

// Custom assert with stack trace
// In Release builds (when DEBUG is not defined), CLJ_ASSERT becomes a NOP
#ifdef DEBUG
    #ifdef ESP32_BUILD
    #define CLJ_ASSERT(expr) do { \
        if (!(expr)) { \
            fprintf(stderr, "\n🚨 ASSERTION FAILED: %s at %s:%d\n", #expr, __FILE__, __LINE__); \
            abort(); \
        } \
    } while(0)
    #else
    #define CLJ_ASSERT(expr) do { \
        if (!(expr)) { \
            fprintf(stderr, "\n🚨 ASSERTION FAILED: %s at %s:%d\n", #expr, __FILE__, __LINE__); \
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
#else
    // Release build: CLJ_ASSERT is a NOP to avoid overhead in hot path
    #define CLJ_ASSERT(expr) ((void)0)
#endif

// Helper macro to silence unused variable warnings intentionally
#ifndef CLJ_UNUSED
#define CLJ_UNUSED(x) ((void)(x))
#endif

// Debug-only assert with stack trace
#ifdef DEBUG
    #define CLJ_DEBUG_ASSERT(expr) CLJ_ASSERT(expr)
#else
    #define CLJ_DEBUG_ASSERT(expr) ((void)0)
#endif

// Utility macros (no multiple evaluation)
// Note: MAX/MIN are not part of C standard, so we define them ourselves
// Some systems provide them in <sys/param.h>, but for portability we define our own
// Using statement expressions (GCC/Clang extension) to avoid multiple evaluation
// Falls back to simple macro if statement expressions not supported
#ifndef MAX
#if defined(__GNUC__) || defined(__clang__)
#define MAX(a, b) ({ \
    __typeof__(a) _a = (a); \
    __typeof__(b) _b = (b); \
    _a > _b ? _a : _b; \
})
#else
// Fallback: simple macro (may evaluate parameters multiple times)
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#endif
#ifndef MIN
#if defined(__GNUC__) || defined(__clang__)
#define MIN(a, b) ({ \
    __typeof__(a) _a = (a); \
    __typeof__(b) _b = (b); \
    _a < _b ? _a : _b; \
})
#else
// Fallback: simple macro (may evaluate parameters multiple times)
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#endif

// EQUALS macro: compares two values using clj_equal() with single evaluation
// Ensures arguments are evaluated only once to avoid side effects
// Note: Requires object.h to be included for clj_equal() declaration
#if defined(__GNUC__) || defined(__clang__)
#define EQUALS(a, b) ({ \
    __typeof__(a) _a = (a); \
    __typeof__(b) _b = (b); \
    clj_equal(_a, _b); \
})
#else
// Fallback: simple macro (may evaluate parameters multiple times)
#define EQUALS(a, b) clj_equal((a), (b))
#endif

#endif // TINY_CLJ_COMMON_H
