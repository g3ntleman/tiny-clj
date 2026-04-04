#ifndef SUBJECTIVE_C_COMMON_H
#define SUBJECTIVE_C_COMMON_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
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

#include <string.h>

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

#ifndef CLJ_JOIN2
#define CLJ_JOIN2(a, b) a##b
#endif

#ifndef CLJ_JOIN
#define CLJ_JOIN(a, b) CLJ_JOIN2(a, b)
#endif

#if defined(__clang__)
#if __has_attribute(cleanup)
#define CLJ_HAS_CLEANUP_ATTRIBUTE 1
#else
#define CLJ_HAS_CLEANUP_ATTRIBUTE 0
#endif
#elif defined(__GNUC__)
#define CLJ_HAS_CLEANUP_ATTRIBUTE 1
#else
#define CLJ_HAS_CLEANUP_ATTRIBUTE 0
#endif

typedef void (*CljScopeReleaseFn)(void);

typedef struct {
    CljScopeReleaseFn release_fn;
    bool armed;
    bool once;
} CljScopeGuard;

static inline void clj_scope_guard_cleanup(CljScopeGuard *guard) {
    if (!guard || !guard->armed || !guard->release_fn) {
        return;
    }
    guard->release_fn();
    guard->armed = false;
}

/*
 * WITH_MUTEX(lock) expects lock_acquire()/lock_release() functions.
 * Example: WITH_MUTEX(event_loop_ingress_lock) { ... }
 */
#if CLJ_HAS_CLEANUP_ATTRIBUTE
#define WITH_MUTEX(lock) \
    for (CljScopeGuard CLJ_JOIN(_clj_scope_guard_, __LINE__) __attribute__((cleanup(clj_scope_guard_cleanup))) = { \
             .release_fn = lock##_release, .armed = false, .once = true }; \
         CLJ_JOIN(_clj_scope_guard_, __LINE__).once && \
             ((lock##_acquire()), CLJ_JOIN(_clj_scope_guard_, __LINE__).armed = true, true); \
         CLJ_JOIN(_clj_scope_guard_, __LINE__).once = false)
#else
#define WITH_MUTEX(lock) \
    for (bool CLJ_JOIN(_clj_mutex_once_, __LINE__) = ((lock##_acquire()), true); \
         CLJ_JOIN(_clj_mutex_once_, __LINE__); \
         (lock##_release()), CLJ_JOIN(_clj_mutex_once_, __LINE__) = false)
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
