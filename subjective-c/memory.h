#ifndef SUBJECTIVE_C_MEMORY_H
#define SUBJECTIVE_C_MEMORY_H

#include "object.h"
#include "memory_profiler.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>

typedef void (*SubjectiveCReleaseFn)(CljObject *obj);
void subjective_c_register_release_fn(CljType type, SubjectiveCReleaseFn fn);

// Zombie mode is controlled by ZOMBIE_ENABLED macro at compile time
// No runtime API needed

void retain(CljObject *v);
void release(CljObject *v);
void enable_memory_debug_output(void);
void disable_memory_debug_output(void);
CljObject *autorelease(CljObject *v);
bool is_pointer_in_data_segment(const void *ptr);
bool is_pointer_on_stack(const void *ptr);
void throw_oom(void) __attribute__((noreturn));

// Autorelease pool API (checkpoint-based implementation)
void autorelease_pool_init(void);     // Call once at startup
void autorelease_pool_push(void);      // Push new checkpoint
void autorelease_pool_pop(void);       // Pop current checkpoint
void autorelease_pool_cleanup_after_exception(void);
void autorelease_pool_cleanup_all(void);
void autorelease_pool_destroy(void);
bool is_autorelease_pool_active(void);

#ifdef DEBUG
/** @brief Get the reference count of an object (debug only)
 * 
 * @param obj Object to check
 * @return Reference count of the object, or 0 for immediate values/NULL
 * 
 * Debug-only function to inspect the reference count of an object.
 * Immediate values (fixnums, chars, etc.) always return 0.
 */
int retain_count(ID obj);
/** @brief Check if an object is in the autorelease pool (O(n) search)
 * 
 * @param obj Object to check
 * @return true if object is in the current autorelease pool, false otherwise
 * 
 * Debug-only function that searches through the autorelease pool items array
 * to determine if the given object is currently autoreleased.
 * This is O(n) where n is the number of objects in the pool.
 */
bool is_autoreleased(CljObject *obj);
#endif // DEBUG

#define ALLOC(type, count) ((type*) alloc(sizeof(type), (count), TYPE_OF(type)))
#define ALLOC_SIMPLE(obj_type) ((CljObject*) alloc(sizeof(CljObject), 1, obj_type))

#ifdef DEBUG
    #ifdef ZOMBIE_ENABLED
    #define DEALLOC(obj) do { \
        typeof(obj) _tmp = (obj); \
        if (_tmp && (void*)_tmp != (void*)0x1 && !IS_IMMEDIATE(_tmp)) { \
            CljObject *_obj = (CljObject*)_tmp; \
            if (!is_singleton(_obj)) { \
                memory_profiler_track_object_destruction(_obj); \
                _obj->rc = ZOMBIE_RC; \
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

    #define RETAIN(obj) ({ \
        ID _id = (obj); \
        if (!IS_IMMEDIATE(_id)) { \
            retain((CljObject*)_id); \
        } \
        _id; \
    })

    #define RELEASE(obj) ({ \
        ID _id = (obj); \
        if (!IS_IMMEDIATE(_id)) { \
            release((CljObject*)_id); \
        } \
        _id; \
    })

    #define AUTORELEASE(obj) ({ \
        ID _id = (obj); \
        if (!IS_IMMEDIATE(_id)) { \
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

    #define RETAIN(obj) ({ \
        ID _id = (obj); \
        if (!IS_IMMEDIATE(_id)) { \
            retain((CljObject*)_id); \
        } \
        _id; \
    })

    #define RELEASE(obj) ({ \
        ID _id = (obj); \
        if (!IS_IMMEDIATE(_id)) { \
            release((CljObject*)_id); \
        } \
        _id; \
    })

    #define AUTORELEASE(obj) ({ \
        ID _id = (obj); \
        if (!IS_IMMEDIATE(_id)) { \
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

#if defined(ENABLE_MEMORY_PROFILING)
    static inline void logf_impl(FILE *stream, const char *fmt, ...) __attribute__((format(printf,2,3)));
    static inline void logf_impl(FILE *stream, const char *fmt, ...) {
        va_list ap; va_start(ap, fmt); vfprintf(stream, fmt, ap); va_end(ap);
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

// ============================================================================
// AUTORELEASE POOL MACROS - Always available in all builds
// ============================================================================

// WITH_AUTORELEASE_POOL is essential and must be available in all builds
#define WITH_AUTORELEASE_POOL(code) do { \
    autorelease_pool_push(); \
    TRY { \
        code; \
        autorelease_pool_pop(); \
    } CATCH(ex) { \
        autorelease_pool_pop(); \
        THROW(ex); \
    } END_TRY \
} while(0)

#define AUTORELEASE_POOL_BEGIN() autorelease_pool_push()
#define AUTORELEASE_POOL_END() autorelease_pool_pop()

#endif // SUBJECTIVE_C_MEMORY_H
