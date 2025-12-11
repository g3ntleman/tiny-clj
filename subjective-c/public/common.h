#ifndef SUBJECTIVE_C_COMMON_H
#define SUBJECTIVE_C_COMMON_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif
void exception_print_native_backtrace(void);
#ifdef __cplusplus
}
#endif

// Custom assert with stack trace
#ifdef DEBUG
    #ifdef ESP32_BUILD
    #define CLJ_ASSERT(expr) do { \
        if (!(expr)) { \
            fprintf(stderr, "\nASSERTION FAILED: %s at %s:%d\n", #expr, __FILE__, __LINE__); \
            abort(); \
        } \
    } while(0)
    #else
    #define CLJ_ASSERT(expr) do { \
        if (!(expr)) { \
            fprintf(stderr, "\nASSERTION FAILED: %s at %s:%d\n", #expr, __FILE__, __LINE__); \
            fprintf(stderr, "Stack Trace:\n"); \
            exception_print_native_backtrace(); \
            fprintf(stderr, "\n"); \
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

#endif // SUBJECTIVE_C_COMMON_H
