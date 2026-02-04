#ifndef SUBJECTIVE_C_COMMON_H
#define SUBJECTIVE_C_COMMON_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "mini_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    STRONG = 0,
    WEAK = 1
} ElementRetention;

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
void exception_print_native_backtrace(void);
#ifdef __cplusplus
}
#endif

// Custom assert with stack trace
#ifdef DEBUG
    #if defined(ESP32_BUILD)
        #define CLJ_ASSERT(expr) do { \
            if (!(expr)) { \
                char _buf[256]; \
                (void)mini_snprintf(_buf, sizeof(_buf), "\nASSERTION FAILED: %s at %s:%d\n", #expr, __FILE__, __LINE__); \
                fputs(_buf, stderr); \
                abort(); \
            } \
        } while(0)
    #else
        #define CLJ_ASSERT(expr) do { \
            if (!(expr)) { \
                char _buf[256]; \
                (void)mini_snprintf(_buf, sizeof(_buf), "\nASSERTION FAILED: %s at %s:%d\n", #expr, __FILE__, __LINE__); \
                fputs(_buf, stderr); \
                fputs("Stack Trace:\n", stderr); \
                exception_print_native_backtrace(); \
                fputs("\n", stderr); \
                abort(); \
            } \
        } while(0)
    #endif
#else
    #define CLJ_ASSERT(expr) ((void)0)
#endif

#ifndef CLJ_UNUSED
#define CLJ_UNUSED(x) ((void)(x))
#endif

#ifdef DEBUG
    #define CLJ_DEBUG_ASSERT(expr) CLJ_ASSERT(expr)
#else
    #define CLJ_DEBUG_ASSERT(expr) ((void)0)
#endif

#ifndef MAX
#if defined(__GNUC__) || defined(__clang__)
#define MAX(a, b) ({ \
    __typeof__(a) _a = (a); \
    __typeof__(b) _b = (b); \
    _a > _b ? _a : _b; \
})
#else
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
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#endif

/**
 * INLINE - Conditional inlining macro for profiling support
 *
 * Use this macro instead of 'inline' for functions that should be:
 * - Inlined in Release builds for maximum performance
 * - NOT inlined in Profiling builds so they appear in profiler output (e.g. sample)
 *
 * Usage:
 *   static INLINE int hot_path_function(int x) { ... }
 *
 * Build modes:
 *   - Release:   INLINE expands to 'inline'     -> function may be inlined
 *   - Profiling: INLINE expands to 'noinline'   -> function appears in profiler
 *
 * Enable profiling mode by defining PROFILING_ENABLED=1:
 *   cmake -DCMAKE_C_FLAGS="-DPROFILING_ENABLED=1" ..
 */
#if defined(PROFILING_ENABLED) && PROFILING_ENABLED
#define INLINE __attribute__((noinline))
#else
#define INLINE inline
#endif

#endif // SUBJECTIVE_C_COMMON_H
