#ifndef SUBJECTIVE_C_MEMORY_H
#define SUBJECTIVE_C_MEMORY_H

#include "object.h"
#include "value.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h> // malloc/free/realloc/calloc
#include <string.h> // strlen/memcpy

// memory_profiler.h lives in subjective-c. Include when profiling is enabled
// (and also in DEBUG for raw tracking).
#if MEMORY_PROFILING_ENABLED
#include "memory_profiler.h"
// Functions are declared in memory_profiler.h, no need for no-op macros
#else
#include "memory_profiler.h"
// Default no-op definitions for memory profiler functions when profiling is disabled.
// In DEBUG we still want raw tracking, so keep real functions there.
#ifndef DEBUG
#define memory_profiler_track_raw_alloc(p, n, file, line) ((void)0)
#define memory_profiler_track_raw_free(ptr, file, line) ((void)0)
#define memory_profiler_track_raw_realloc(old_ptr_addr, new_ptr, n, file, line) ((void)0)
#endif
#endif

typedef void (*SubjectiveCReleaseFn)(CljObject *obj);

/** @brief Register custom release function for a type
 * @param type Type to register release function for
 * @param fn Release function to call when object is deallocated
 */
void subjective_c_register_release_fn(CljType type, SubjectiveCReleaseFn fn);

/** When ZOMBIE_ENABLED: optional callback (object, is_double_free) to log pr_str for inspection. */
typedef void (*SubjectiveCZombieLogFn)(CljObject *v, bool is_double_free);

/** @brief Set zombie log callback for debugging double-free issues
 * @param fn Callback function called when zombie objects are accessed
 */
void subjective_c_set_zombie_log_fn(SubjectiveCZombieLogFn fn);

// Zombie mode is controlled by ZOMBIE_ENABLED macro at compile time
// No runtime API needed

/** @brief Increment reference count of object
 * @param v Object to retain (NULL-safe, skips immediates)
 */
void retain(CljObject *v);

/** @brief Decrement reference count, deallocates when reaching zero
 * @param v Object to release (NULL-safe, skips immediates)
 */
void release(CljObject *v);

/** @brief Enable/disable debug output for memory operations
 * @param enabled True to enable debug output
 */
void memory_set_debug_output_enabled(bool enabled);

/** @brief Get current debug output state
 * @return True if debug output is enabled
 */
bool memory_get_debug_output_enabled(void);

/** @brief Set a process-wide heap usage limit in bytes. Zero disables the limit. */
void memory_set_heap_limit_bytes(size_t limit);

/** @brief Get the active process-wide heap usage limit in bytes. */
size_t memory_get_heap_limit_bytes(void);

/** @brief Return tracked current heap usage in bytes. */
size_t memory_current_usage_bytes(void);

/** @brief Return best-effort allocated size for a pointer, or requested_size when unknown. */
size_t memory_actual_allocation_size(const void *ptr, size_t requested_size);

/** @brief Return tracked/raw best-effort size for an existing raw allocation pointer. */
size_t memory_tracked_raw_allocation_size(const void *ptr);

/** @brief Check whether replacing released_size with requested_size would exceed the heap limit. */
bool memory_heap_limit_would_exceed(size_t released_size, size_t requested_size);

/** @brief Add object to autorelease pool for deferred release
 * @param v Object to autorelease
 * @return The same object (for convenience)
 */
CljObject *autorelease(CljObject *v);

/** @brief Check if pointer is in the data segment
 * @param ptr Pointer to check
 * @return True if pointer is in data segment
 */
bool is_pointer_in_data_segment(const void *ptr);

/** @brief Check if pointer is on the stack
 * @param ptr Pointer to check
 * @return True if pointer is on stack
 */
bool is_pointer_on_stack(const void *ptr);

/** @brief Throw out-of-memory exception on the main thread, or log and return on a background thread.
 *  After a background-thread return, raw allocators (CLJ_MALLOC, etc.) call abort() — the heap cannot be
 *  left in a partially allocated state across arbitrary call sites.
 */
void throw_oom(void);

// -----------------------------------------------------------------------------
// Raw heap allocation helpers (trackable by memory profiler)
// -----------------------------------------------------------------------------
//
// Rule: Production builds must not contain any profiling instrumentation.
// Therefore, file/line tracking and raw block bookkeeping are only compiled when
// MEMORY_PROFILING_ENABLED=1. Otherwise, these macros are direct malloc/free.
//


#if MEMORY_PROFILING_ENABLED || defined(DEBUG)

static inline void* clj_malloc_impl(size_t n, const char *file, int line) {
    (void)file; (void)line;
    if (n != 0 && memory_heap_limit_would_exceed(0, n)) {
        throw_oom();
        abort();
    }
    void *p = malloc(n);
    if (!p && n != 0) {
        throw_oom();
        abort();
    }
    if (p && n != 0) {
        size_t actual = memory_actual_allocation_size(p, n);
        if (memory_heap_limit_would_exceed(0, actual)) {
            free(p);
            throw_oom();
            abort();
        }
    }
    memory_profiler_track_raw_alloc(p, n, file, line);
    return p;
}

static inline void* clj_calloc_impl(size_t nmemb, size_t size, const char *file, int line) {
    (void)file; (void)line;
    // Best-effort overflow guard.
    if (nmemb != 0 && size > ((size_t)-1) / nmemb) {
        throw_oom();
        abort();
    }
    size_t n = nmemb * size;
    if (n != 0 && memory_heap_limit_would_exceed(0, n)) {
        throw_oom();
        abort();
    }
    void *p = calloc(nmemb, size);
    if (!p && n != 0) {
        throw_oom();
        abort();
    }
    if (p && n != 0) {
        size_t actual = memory_actual_allocation_size(p, n);
        if (memory_heap_limit_would_exceed(0, actual)) {
            free(p);
            throw_oom();
            abort();
        }
    }
    memory_profiler_track_raw_alloc(p, n, file, line);
    return p;
}

static inline void* clj_realloc_impl(void *old_ptr, size_t n, const char *file, int line) {
    (void)file; (void)line;
    size_t old_size = old_ptr ? memory_tracked_raw_allocation_size(old_ptr) : 0;
    if (n != 0 && memory_heap_limit_would_exceed(old_size, n)) {
        throw_oom();
        abort();
    }
    uintptr_t old_ptr_addr = (uintptr_t)old_ptr;
    void *new_ptr = realloc(old_ptr, n);
    if (!new_ptr && n != 0) {
        throw_oom();
        abort();
    }
    // Only track if realloc succeeded or if n==0 (free semantics).
    if (new_ptr || n == 0) {
        memory_profiler_track_raw_realloc(old_ptr_addr, new_ptr, n, file, line);
    }
    return new_ptr;
}

static inline void clj_free_impl(void *ptr, const char *file, int line) {
    (void)file; (void)line;
    memory_profiler_track_raw_free(ptr, file, line);
    free(ptr);
}

#else

// No-op macros for profiler tracking when profiling is disabled
#define memory_profiler_track_raw_alloc(p, n, file, line) ((void)0)
#define memory_profiler_track_raw_free(ptr, file, line) ((void)0)
#define memory_profiler_track_raw_realloc(old_ptr_addr, new_ptr, n, file, line) ((void)0)

static inline void* clj_malloc_impl(size_t n, const char *file, int line) {
    (void)file; (void)line;
    void *p = malloc(n);
    if (!p && n != 0) {
        throw_oom();
        abort();
    }
    return p;
}

static inline void* clj_calloc_impl(size_t nmemb, size_t size, const char *file, int line) {
    (void)file; (void)line;
    if (nmemb != 0 && size > ((size_t)-1) / nmemb) {
        throw_oom();
        abort();
    }
    size_t n = nmemb * size;
    void *p = calloc(nmemb, size);
    if (!p && n != 0) {
        throw_oom();
        abort();
    }
    return p;
}

static inline void* clj_realloc_impl(void *old_ptr, size_t n, const char *file, int line) {
    (void)file; (void)line;
    void *new_ptr = realloc(old_ptr, n);
    if (!new_ptr && n != 0) {
        throw_oom();
        abort();
    }
    return new_ptr;
}

static inline void clj_free_impl(void *ptr, const char *file, int line) {
    (void)file; (void)line;
    free(ptr);
}

#endif // MEMORY_PROFILING_ENABLED

static inline void* clj_host_malloc_impl(size_t n) {
    void *p = malloc(n);
    if (!p && n != 0) {
        throw_oom();
        abort();
    }
    return p;
}

static inline void* clj_host_calloc_impl(size_t nmemb, size_t size) {
    if (nmemb != 0 && size > ((size_t)-1) / nmemb) {
        throw_oom();
        abort();
    }
    size_t n = nmemb * size;
    void *p = calloc(nmemb, size);
    if (!p && n != 0) {
        throw_oom();
        abort();
    }
    return p;
}

static inline void* clj_host_realloc_impl(void *old_ptr, size_t n) {
    void *new_ptr = realloc(old_ptr, n);
    if (!new_ptr && n != 0) {
        throw_oom();
        abort();
    }
    return new_ptr;
}

static inline void clj_host_free_impl(void *ptr) {
    free(ptr);
}

// -----------------------------------------------------------------------------
// Canonical allocation macro API
// -----------------------------------------------------------------------------
//
// Use CLJ_BUF_* for raw buffers and C arrays.
// Use CLJ_OBJ_* for CljObject-based allocations (type/flags/rc initialization).
//
#define CLJ_BUF_MALLOC(n) clj_malloc_impl((n), __FILE__, __LINE__)
#define CLJ_BUF_CALLOC(nmemb, size) clj_calloc_impl((nmemb), (size), __FILE__, __LINE__)
#define CLJ_BUF_REALLOC(ptr, n) clj_realloc_impl((ptr), (n), __FILE__, __LINE__)
#define CLJ_BUF_FREE(ptr) clj_free_impl((ptr), __FILE__, __LINE__)

/* Host-emulation buffers stay outside the tiny-clj/Clojure heap limit. */
#define CLJ_HOST_MALLOC(n) clj_host_malloc_impl((n))
#define CLJ_HOST_CALLOC(nmemb, size) clj_host_calloc_impl((nmemb), (size))
#define CLJ_HOST_REALLOC(ptr, n) clj_host_realloc_impl((ptr), (n))
#define CLJ_HOST_FREE(ptr) clj_host_free_impl((ptr))

// Backward-compatible aliases. Prefer CLJ_BUF_* in new code.
#define CLJ_MALLOC(n) CLJ_BUF_MALLOC(n)
#define CLJ_CALLOC(nmemb, size) CLJ_BUF_CALLOC((nmemb), (size))
#define CLJ_REALLOC(ptr, n) CLJ_BUF_REALLOC((ptr), (n))
#define CLJ_FREE(ptr) CLJ_BUF_FREE(ptr)

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

/** @brief Initialize the autorelease pool system
 */
void autorelease_pool_init(void);

/** @brief Ensure autorelease pool is active, creating if needed
 */
void autorelease_pool_ensure_active(void);

/** @brief Free the autorelease pool and all contained objects
 */
void autorelease_pool_free(void);

/** @brief Check if autorelease pool is currently active
 * @return True if pool is active
 */
bool is_autorelease_pool_active(void);

/** @brief Check whether autorelease pool is currently draining.
 * @return True while drain_to_depth() is releasing pool entries.
 */
bool is_autorelease_pool_draining(void);

/** @brief Mark current autorelease pool depth for later restoration
 * @return Current pool depth marker
 */
uint32_t autorelease_pool_mark(void);

/** @brief Get current autorelease pool depth
 * @return Current depth (number of autoreleased objects)
 */
uint32_t autorelease_pool_depth(void);

/** @brief Drain autorelease pool to a previously marked depth
 * @param mark Depth marker from autorelease_pool_mark()
 */
void autorelease_pool_drain_to_depth(uint32_t mark);

#ifdef DEBUG
/** @brief Get number of times obj appears in current autorelease pool
 * @param obj Object to count
 * @return Number of times obj is in autorelease pool (DEBUG only)
 */
uint32_t autorelease_count(CljObject *obj);

/** @brief Get peak autorelease pool count since last reset
 * @return Peak count (DEBUG only)
 */
uint32_t autorelease_pool_peak_count(void);

/** @brief Reset peak autorelease pool counter
 */
void autorelease_pool_peak_reset(void);
#endif

// Object allocation helpers (CljObject-based)
#define CLJ_OBJ_ALLOC(type, count) ((type*) alloc(sizeof(type), (count), TYPE_OF(type)))
#define CLJ_OBJ_NEW(type) CLJ_OBJ_ALLOC(type, 1)
// clj_type is a CljType enum value (e.g. CLJ_MAP_PERSISTENT).
#define CLJ_OBJ_ALLOC_BYTES(clj_type, bytes) alloc((bytes), 1, (clj_type))

// Backward-compatible aliases. Prefer CLJ_OBJ_* in new code.
#define ALLOC(type, count) CLJ_OBJ_ALLOC(type, count)
#define ALLOC_BYTES(obj_type, bytes) ((obj_type*) alloc((bytes), 1, (obj_type)))

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
        CLJ_ASSERT(_tmp && "DEALLOC requires non-NULL object"); \
        CLJ_ASSERT((void*)_tmp != (void*)0x1 && "DEALLOC received sentinel pointer"); \
        CLJ_ASSERT(!IS_IMMEDIATE(_tmp) && "DEALLOC received immediate value"); \
        CljObject *_obj = (CljObject*)_tmp; \
        CLJ_ASSERT(!is_singleton(_obj) && "DEALLOC received singleton object"); \
        memory_profiler_track_object_destruction(_obj); \
        free(_obj); \
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
            const char *_trace = getenv("TINYCLJ_TRACE_AUTORELEASE_CALL"); \
            if (_trace && _trace[0] && strcmp(_trace, "0") != 0) { \
                fprintf(stderr, "[autorelease-call] %p\n", (void*)_id); \
            } \
            autorelease_pool_ensure_active(); \
            autorelease((CljObject*)_id); \
            if (_trace && _trace[0] && strcmp(_trace, "0") != 0) { \
                fprintf(stderr, "[autorelease-called] %p\n", (void*)_id); \
            } \
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
            CljObject *_o = (CljObject*)_obj; \
            if (!is_singleton(_o)) { \
                memory_profiler_track_object_destruction(_o); \
                free((void*)_o); \
            } \
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
            const char *_trace = getenv("TINYCLJ_TRACE_AUTORELEASE_CALL"); \
            if (_trace && _trace[0] && strcmp(_trace, "0") != 0) { \
                fprintf(stderr, "[autorelease-call] %p\n", (void*)_id); \
            } \
            autorelease_pool_ensure_active(); \
            autorelease((CljObject*)_id); \
            if (_trace && _trace[0] && strcmp(_trace, "0") != 0) { \
                fprintf(stderr, "[autorelease-called] %p\n", (void*)_id); \
            } \
        } \
        _id; \
    })

    #define WITH_AUTORELEASE_POOL_EVAL(code) do { code } while(0)
    #define WITH_MEMORY_PROFILING(code) do { code } while(0)
    #define WITH_MEMORY_TEST(code) WITH_MEMORY_PROFILING(code)
    #define WITH_TIME_PROFILING(code) WITH_MEMORY_PROFILING(code)
    #define REFERENCE_COUNT(obj) (0)  // Not available in release builds
#endif

#if MEMORY_PROFILING_ENABLED || defined(DEBUG)
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

/** @brief Allocate memory for object(s) with reference counting.
 * @param type_size Size of each object
 * @param count Number of objects
 * @param obj_type Type tag for the object
 * @return Non-NULL allocated memory. On OOM, throws on the main thread and never returns.
 */
void* alloc(size_t type_size, size_t count, CljType obj_type)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((returns_nonnull))
#endif
;

#define WITH_AUTORELEASE_POOL(code) do { \
    uint32_t _restore = autorelease_pool_mark(); \
    code; \
    autorelease_pool_drain_to_depth(_restore); \
} while(0)



#endif // SUBJECTIVE_C_MEMORY_H
