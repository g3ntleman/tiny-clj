#ifndef SUBJECTIVE_C_MEMORY_H
#define SUBJECTIVE_C_MEMORY_H

#include "object.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h> // malloc/free/realloc/calloc
#include <string.h> // strlen/memcpy

// memory_profiler.h lives in subjective-c. Include when profiling enabled.
#if defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
#include "memory_profiler.h"
// Functions are declared in memory_profiler.h, no need for no-op macros
#else
// Default no-op definitions for memory profiler functions when profiling is disabled
#define memory_profiler_track_raw_alloc(p, n, file, line) ((void)0)
#define memory_profiler_track_raw_free(ptr, file, line) ((void)0)
#define memory_profiler_track_raw_realloc(old_ptr, new_ptr, n, file, line) ((void)0)
#endif

typedef void (*SubjectiveCReleaseFn)(CljObject *obj);
void subjective_c_register_release_fn(CljType type, SubjectiveCReleaseFn fn);

/** When ZOMBIE_ENABLED: optional callback (object, is_double_free) to log pr_str for inspection. */
typedef void (*SubjectiveCZombieLogFn)(CljObject *v, bool is_double_free);
void subjective_c_set_zombie_log_fn(SubjectiveCZombieLogFn fn);

// Zombie mode is controlled by ZOMBIE_ENABLED macro at compile time
// No runtime API needed

void retain(CljObject *v);
void release(CljObject *v);
void memory_set_debug_output_enabled(bool enabled);
bool memory_get_debug_output_enabled(void);
CljObject *autorelease(CljObject *v);
bool is_pointer_in_data_segment(const void *ptr);
bool is_pointer_on_stack(const void *ptr);
void throw_oom(void) __attribute__((noreturn));

// -----------------------------------------------------------------------------
// Raw heap allocation helpers (trackable by memory profiler)
// -----------------------------------------------------------------------------
//
// Rule: Production builds must not contain any profiling instrumentation.
// Therefore, file/line tracking and raw block bookkeeping are only compiled when
// MEMORY_PROFILING_ENABLED=1. Otherwise, these macros are direct malloc/free.
//


#if MEMORY_PROFILING_ENABLED

static inline void* clj_malloc_impl(size_t n, const char *file, int line) {
    (void)file; (void)line;
    void *p = malloc(n);
    if (!p && n != 0) {
        throw_oom(); // never returns
    }
    memory_profiler_track_raw_alloc(p, n, file, line);
    return p;
}

static inline void* clj_calloc_impl(size_t nmemb, size_t size, const char *file, int line) {
    (void)file; (void)line;
    // Best-effort overflow guard.
    if (nmemb != 0 && size > ((size_t)-1) / nmemb) {
        throw_oom(); // never returns
    }
    size_t n = nmemb * size;
    void *p = calloc(nmemb, size);
    if (!p && n != 0) {
        throw_oom(); // never returns
    }
    memory_profiler_track_raw_alloc(p, n, file, line);
    return p;
}

static inline void* clj_realloc_impl(void *old_ptr, size_t n, const char *file, int line) {
    (void)file; (void)line;
    void *new_ptr = realloc(old_ptr, n);
    if (!new_ptr && n != 0) {
        throw_oom(); // never returns; old_ptr remains valid per realloc contract
    }
    // Only track if realloc succeeded or if n==0 (free semantics).
    if (new_ptr || n == 0) {
        memory_profiler_track_raw_realloc(old_ptr, new_ptr, n, file, line);
    }
    return new_ptr;
}

static inline void clj_free_impl(void *ptr, const char *file, int line) {
    (void)file; (void)line;
    memory_profiler_track_raw_free(ptr, file, line);
    free(ptr);
}

#define CLJ_MALLOC(n) clj_malloc_impl((n), __FILE__, __LINE__)
#define CLJ_CALLOC(nmemb, size) clj_calloc_impl((nmemb), (size), __FILE__, __LINE__)
#define CLJ_REALLOC(ptr, n) clj_realloc_impl((ptr), (n), __FILE__, __LINE__)
#define CLJ_FREE(ptr) clj_free_impl((ptr), __FILE__, __LINE__)

#else

// No-op macros for profiler tracking when profiling is disabled
#define memory_profiler_track_raw_alloc(p, n, file, line) ((void)0)
#define memory_profiler_track_raw_free(ptr, file, line) ((void)0)
#define memory_profiler_track_raw_realloc(old_ptr, new_ptr, n, file, line) ((void)0)

#define CLJ_MALLOC(n) malloc((n))
#define CLJ_CALLOC(nmemb, size) calloc((nmemb), (size))
#define CLJ_REALLOC(ptr, n) realloc((ptr), (n))
#define CLJ_FREE(ptr) free((ptr))

#endif // MEMORY_PROFILING_ENABLED

// -----------------------------------------------------------------------------
// Trackable string allocation helpers
// -----------------------------------------------------------------------------
// Use these instead of strdup/free so raw allocations show up in the profiler.
static inline char *clj_strdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *out = (char*)CLJ_MALLOC(n);
    memcpy(out, s, n);
    return out;
}

/** @brief Get the reference count of an object
 *
 * @param obj Object to check
 * @return Reference count of the object, or 0 for immediate values/NULL
 *
 * Primarily intended for debugging/diagnostics.
 */
int retain_count(ID obj);

void autorelease_pool_init(void);
void autorelease_pool_ensure_active(void);
void autorelease_pool_free(void);
bool is_autorelease_pool_active(void);

uint32_t autorelease_pool_mark(void);
uint32_t autorelease_pool_depth(void);
void autorelease_pool_drain_to_depth(uint32_t mark);

#ifdef DEBUG
/** Number of times obj appears in the current autorelease pool (for release() policy: prefer leak over double-free).
 *  DEBUG only; must not be used in Release builds. */
uint32_t autorelease_count(CljObject *obj);
uint32_t autorelease_pool_peak_count(void);
void autorelease_pool_peak_reset(void);
#endif

#define ALLOC(type, count) ((type*) alloc(sizeof(type), (count), TYPE_OF(type)))
#define ALLOC_SIMPLE(obj_type) (ID) alloc(sizeof(CljObject), 1, obj_type)
#define ALLOC_BYTES(obj_type, bytes) ((void*) alloc((bytes), 1, (obj_type)))

#ifdef DEBUG
    #ifdef ZOMBIE_ENABLED
    #define DEALLOC(obj) do { \
        typeof(obj) _tmp = (obj); \
        if (_tmp && (void*)_tmp != (void*)0x1 && !IS_IMMEDIATE(_tmp)) { \
            CljObject *_obj = (CljObject*)_tmp; \
            if (!is_singleton(_obj)) { \
                MEMORY_PROFILER_TRACK_OBJECT_ZOMBIFY(_obj); \
                /* Don't free - keep object at rc=0 for inspection */ \
                /* rc is already 0 from release() */ \
            } \
        } \
    } while(0)
    #else
    #define DEALLOC(obj) do { \
        typeof(obj) _tmp = (obj); \
        if (_tmp && (void*)_tmp != (void*)0x1 && !IS_IMMEDIATE(_tmp)) { \
            CljObject *_obj = (CljObject*)_tmp; \
            if (!is_singleton(_obj)) { \
                memory_profiler_track_object_destruction(_obj); \
                free(_obj); \
            } \
        } \
    } while(0)
    #endif

    // NOTE: These macros are NULL-safe (nil is represented as NULL). Avoiding
    // retain/release/autorelease calls for NULL reduces overhead and code size,
    // which matters for embedded builds.
    #define RETAIN(obj) ({ \
        ID _id = (obj); \
        if (_id && !IS_IMMEDIATE(_id)) { \
            retain((CljObject*)_id); \
        } \
        _id; \
    })

    #define RELEASE(obj) ({ \
        ID _id = (obj); \
        if (_id && !IS_IMMEDIATE(_id)) { \
            release((CljObject*)_id); \
        } \
        _id; \
    })

    #define AUTORELEASE(obj) ({ \
        ID _id = (obj); \
        /* Skip immediates, NULL, and singletons (SINGLETON_RC) */ \
        if (_id && !IS_IMMEDIATE(_id) && !is_singleton((CljObject*)_id)) { \
            autorelease((CljObject*)_id); \
        } \
        _id; \
    })

    #define REFERENCE_COUNT(obj) retain_count(obj)  // Only available in DEBUG builds
    #define WITH_MEMORY_PROFILING(code) do { \
        MEMORY_TEST_START(__FUNCTION__); \
        code; \
        MEMORY_TEST_END(__FUNCTION__); \
    } while(0)
    #define WITH_MEMORY_TEST(code) WITH_MEMORY_PROFILING(code)
    #define WITH_TIME_PROFILING(code) WITH_MEMORY_PROFILING(code)
#else
    #define DEALLOC(obj) do { \
        ID _obj = (obj); \
        if (_obj && !IS_IMMEDIATE(_obj)) { \
            free((void*)_obj); \
        } \
    } while(0)

    // NULL-safe (nil == NULL); avoid calls for NULL for embedded friendliness.
    #define RETAIN(obj) ({ \
        ID _id = (obj); \
        if (_id && !IS_IMMEDIATE(_id)) { \
            retain((CljObject*)_id); \
        } \
        _id; \
    })

    #define RELEASE(obj) ({ \
        ID _id = (obj); \
        if (_id && !IS_IMMEDIATE(_id)) { \
            release((CljObject*)_id); \
        } \
        _id; \
    })

    #define AUTORELEASE(obj) ({ \
        ID _id = (obj); \
        /* Skip immediates, NULL, and singletons (SINGLETON_RC) */ \
        if (_id && !IS_IMMEDIATE(_id) && !is_singleton((CljObject*)_id)) { \
            autorelease((CljObject*)_id); \
        } \
        _id; \
    })

    #define WITH_AUTORELEASE_POOL_EVAL(code) do { code } while(0)
    #define WITH_MEMORY_PROFILING(code) do { code } while(0)
    #define WITH_MEMORY_TEST(code) WITH_MEMORY_PROFILING(code)
    #define WITH_TIME_PROFILING(code) WITH_MEMORY_PROFILING(code)
    #define REFERENCE_COUNT(obj) (0)  // Not available in release builds
#endif

#if MEMORY_PROFILING_ENABLED
    #include "mini_format.h"
    // Use mini_format everywhere (host + embedded) to keep formatter complexity low.
    static inline void logf_impl(FILE *stream, const char *fmt, ...) {
        char buf[256];
        va_list ap;
        va_start(ap, fmt);
        (void)mini_vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        fputs(buf, stream ? stream : stdout);
    }
    #define LOGF(stream, fmt, ...) do { logf_impl((stream), (fmt), ##__VA_ARGS__); } while(0)
#else
    #define LOGF(stream, fmt, ...) do { } while(0)
#endif

#define ASSIGN(var, new_obj) do { \
    ID _new_val = (new_obj); \
    ID _old_val = (var); \
    if (_new_val != _old_val) { \
        RETAIN(_new_val); \
        RELEASE(_old_val); \
        (var) = _new_val; \
    } \
} while(0)

void* alloc(size_t type_size, size_t count, CljType obj_type);

#define WITH_AUTORELEASE_POOL(code) do { \
    uint32_t _restore = autorelease_pool_mark(); \
    code; \
    autorelease_pool_drain_to_depth(_restore); \
} while(0)



#endif // SUBJECTIVE_C_MEMORY_H
