/*
 * Memory Management Implementation for Tiny-CLJ
 * 
 * Centralized memory management with reference counting and autorelease pools.
 * Provides retain/release semantics similar to Objective-C ARC.
 */

#include "memory.h"
#include "common.h"  // CLJ_ASSERT
#include "runtime.h"
#include "object.h"
#include "vector.h"
#include "value.h"  // For IS_IMMEDIATE macro used in memory.h
#include "memory_profiler.h"
#include "types.h"
#include "exception.h"
#include "map.h"
#include "list.h"
#include "ast.h"
#include "atom.h"
#include "function.h"  // For CljFunction
#include "namespace.h"  // For CljNamespace
#include "hashmap.h"  // For CljHashMap
#include "thread_local.h"
#include "mini_format.h"
#include <string.h>
#include <stdlib.h>

// Optional stack trace support (execinfo/backtrace), gated in object.h.
// On ESP-IDF/newlib this is not available.
#if defined(SUBJECTIVE_C_HAVE_EXECINFO) && SUBJECTIVE_C_HAVE_EXECINFO
#include <execinfo.h>
#endif

// AUTORELEASE POOL: CLJ_VECTOR_TRANSIENT_WEAK. _restore=vector_count at block start;
// drain_to_depth(mark) RELEASEs [mark,count) and truncates.

#define POOL_INITIAL_CAPACITY 1024

static THREAD_LOCAL CljVector *g_pool = NULL;

#ifdef DEBUG
static THREAD_LOCAL uint32_t g_pool_peak_count = 0;
#endif

extern bool g_memory_verbose_mode;
static bool g_debug_output_enabled = false;
static bool g_debug_output_active = false;

static inline void update_debug_output_active(void) {
    g_debug_output_active = g_memory_profiling_enabled && g_memory_verbose_mode && g_debug_output_enabled;
}

static SubjectiveCZombieLogFn g_zombie_log_fn = NULL;
void subjective_c_set_zombie_log_fn(SubjectiveCZombieLogFn fn) { g_zombie_log_fn = fn; }

void memory_set_debug_output_enabled(bool enabled) {
    g_debug_output_enabled = enabled;
    update_debug_output_active();
}

bool memory_get_debug_output_enabled(void) {
    return g_debug_output_enabled;
}

void* alloc(size_t type_size, size_t count, CljType obj_type) {
    void *result = malloc(type_size * count);
    if (!result) throw_oom();
    if (type_size >= sizeof(CljObject)) {
        CljObject *obj = (CljObject*)result;
        obj->type = obj_type;
        obj->flags = 0;
        obj->rc = 1;
#if MEMORY_PROFILING_ENABLED
        memory_profiler_track_object_creation_sized(obj, type_size * count);
#else
        MEMORY_PROFILER_TRACK_OBJECT_CREATION(obj);
#endif
    }
    
    return result;
}

static void release_object_deep(CljObject *v);
static void release_object_default(CljObject *v);
static void init_release_dispatch(void);
static SubjectiveCReleaseFn g_release_dispatch[CLJ_TYPE_COUNT];
static bool g_release_dispatch_initialized = false;

void autorelease_pool_init(void) {
    if (g_pool) return;
    g_pool = make_vector_weak(POOL_INITIAL_CAPACITY);
#ifdef DEBUG
    g_pool_peak_count = 0;
#endif
}

static void init_release_dispatch(void) {
    if (g_release_dispatch_initialized) return;
    for (int i = 0; i < CLJ_TYPE_COUNT; i++) {
        g_release_dispatch[i] = release_object_default;
    }
    g_release_dispatch_initialized = true;
}

void retain(CljObject *v) {
    if (!v || (uintptr_t)v < 0x1000) return;
#ifdef DEBUG
    if (v->rc == 0) {
        // Zombie detected: throw exception with stacktrace and zombie object
        // Don't try to print object representation (may fail if object is corrupted)
        char message[512];
        (void)mini_snprintf(message, sizeof(message),
            "Attempted to retain zombie object %p (type=%s). "
            "This object was already freed but marked as zombie for debugging.",
            v, clj_type_name(v->type));
        CLJException *ex = make_exception(EXCEPTION_ZOMBIE_ACCESS, message, __FILE__, __LINE__, 0);
        if (ex) {
            ex->object = (uintptr_t)v;  // Address-only: store without retaining
            throw_exception_object(AUTORELEASE(ex));
        }
        return;
    }
#endif
    if (TRACKS_RETAINS(v)) {
        MEMORY_PROFILER_TRACK_RETAIN(v);
        v->rc++;
    }
}

void release(CljObject *v) {
    if (!v || (uintptr_t)v < 0x1000 || is_singleton(v)) return;
    if (g_debug_output_active)
        LOGF(stdout, "🔍 release: Object %p, type=%d (%s), rc=%d -> ",
             v, (int)v->type, clj_type_name(v->type), (int)v->rc);
    if (v->rc == 0) {
        LOGF(stderr, "DOUBLE-FREE: object=%p type=%s (rc=0)\n", v, clj_type_name(v->type));
#ifdef ZOMBIE_ENABLED
        if (g_zombie_log_fn) g_zombie_log_fn(v, true);
#endif
        #if defined(SUBJECTIVE_C_HAVE_EXECINFO) && SUBJECTIVE_C_HAVE_EXECINFO
        void *trace[32];
        int trace_count = backtrace(trace, (int)(sizeof(trace) / sizeof(trace[0])));
        backtrace_symbols_fd(trace, trace_count, fileno(stderr));
        #endif
        fflush(stderr);
        throw_exception_formatted("UseAfterFreeError", __FILE__, __LINE__, 0,
            "Double-free detected! Object %p (type=%s) was already freed (rc=0). "
            "This indicates the object was released more times than retained, "
            "likely due to duplicate AUTORELEASE or incorrect memory management.",
            v, clj_type_name(v->type));
        return;
    }

    v->rc--;
    MEMORY_PROFILER_TRACK_RELEASE(v);
    if (v->rc == 0) {
        if (g_debug_output_active) LOGF(stdout, "🔍 release: Object %p will be freed (rc=0)\n", v);
        CLJ_ASSERT((autorelease_count(v) == 0) && "Object v still in autorelease pool; will double-release");
#ifdef ZOMBIE_ENABLED
        if (g_zombie_log_fn) g_zombie_log_fn(v, false);
        if (g_debug_output_active) LOGF(stdout, "🔍 release: Object %p marked as zombie (rc=0, not DEALLOCed)\n", v);
#else
        release_object_deep(v);
        DEALLOC(v);
        if (g_debug_output_active) LOGF(stdout, "🔍 release: Object %p freed\n", v);
#endif
    }
}

void autorelease_pool_ensure_active(void) {
    if (!g_pool) autorelease_pool_init();
}

uint32_t autorelease_pool_mark(void) {
    autorelease_pool_ensure_active();
    return (uint32_t)vector_count(g_pool);
}

CljObject *autorelease(CljObject *v) {
    if (!v) return NULL;
    CLJ_ASSERT(g_pool && "autorelease_pool_init() not called");
    ASSIGN(g_pool, vector_conj_owned(g_pool, v));
#ifdef DEBUG
    if (vector_count(g_pool) > g_pool_peak_count)
        g_pool_peak_count = vector_count(g_pool);
#endif
    MEMORY_PROFILER_TRACK_AUTORELEASE(v);
    return v;
}

uint32_t autorelease_pool_peak_count(void) {
#ifdef DEBUG
    return g_pool_peak_count;
#else
    return 0;
#endif
}

void autorelease_pool_peak_reset(void) {
#ifdef DEBUG
    if (!g_pool) { g_pool_peak_count = 0; return; }
    g_pool_peak_count = vector_count(g_pool);
#else
    (void)0;
#endif
}

#ifdef DEBUG
uint32_t autorelease_count(CljObject *obj) {
    if (!obj || !g_pool) return 0;
    uint32_t n = 0;
    unsigned int c = vector_count(g_pool);
    ID *arr = vector_as_array(g_pool);
    for (unsigned int i = 0; i < c; i++)
        if (arr[i] == obj) n++;
    return n;
}
#endif

bool is_autorelease_pool_active(void) { return g_pool != NULL; }

uint32_t autorelease_pool_depth(void) { return 0u; }

void autorelease_pool_drain_to_depth(uint32_t mark) {
    if (!g_pool) return;
    unsigned int c = vector_count(g_pool);
    if (g_debug_output_active && c > mark) LOGF(stdout, "🔍 autorelease_pool_drain: [%u..%u)\n", (unsigned)mark, (unsigned)c);
    for (unsigned int i = c; i > mark; ) {
        i--;
        ID e = vector_nth(g_pool, i);
        CLJ_ASSERT(e && "pool entry must not be NULL");
        vector_truncate(g_pool, i);
        RELEASE(e);
    }
}

void autorelease_pool_free(void) {
    autorelease_pool_drain_to_depth(0);
    if (g_pool) { RELEASE(g_pool); g_pool = NULL; }
}

int retain_count(ID obj) {
    if (!obj || IS_IMMEDIATE(obj)) return 0;
    CljObject *o = (CljObject*)obj;
    return (o->rc == SINGLETON_RC) ? 0 : o->rc;
}


static void release_object_deep(CljObject *v) {
    if (!v || !TRACKS_RETAINS(v)) return;
    init_release_dispatch();
    SubjectiveCReleaseFn fn = (v->type >= 0 && v->type < CLJ_TYPE_COUNT)
        ? g_release_dispatch[v->type]
        : NULL;
    if (fn) {
        fn(v);
    }
}

static void release_object_default(CljObject *v) {
    switch (v->type) {
        case CLJ_STRING:
            break;
            
        // CLJ_SYMBOL: Release handler registered by tiny-clj via subjective_c_register_release_fn()
            
        case CLJ_VECTOR_PERSISTENT:
            {
                // Direct cast - we already know it's a Vector from the switch case
                // Using as_vector() would call TAG() which fails when rc=0 (zombie mode)
                CljVector *vec = (CljVector*)v;
                if (vec) {
                    // Release all vector elements
                    VECTOR_FOR_EACH(vec, elem) {
                        RELEASE(elem);
                    }
                    // Note: data array is automatically freed
                }
            }
            break;

        case CLJ_VECTOR_TRANSIENT:
            {
                CljTransientVector *tvec = (CljTransientVector*)v;
                if (tvec && tvec->backing_store) {
                    RELEASE(tvec->backing_store);
                }
            }
            break;
            
        case CLJ_VECTOR_TRANSIENT_WEAK:
            break;
        case CLJ_MAP: {
            CljMap *map = (CljMap*)v;
            MAP_FOR_EACH(map, key, value) { RELEASE(key); RELEASE(value); }
            break;
        }
            
        case CLJ_HASHMAP:
            {
                CljHashMap *map = (CljHashMap*)v;
                ID hm_key;
                ID hm_val;
                HASHMAP_FOR_EACH(map, hm_key, hm_val) {
                    RELEASE(hm_key);
                    RELEASE(hm_val);
                }
            }
            break;
            
        case CLJ_LIST: {
            CljList *list = (CljList*)v;
            RELEASE(list->first);
            RELEASE(list->rest);
            break;
        }
        case CLJ_AST_NODE: {
            CljASTNode *node = (CljASTNode*)v;
            RELEASE(node->first);
            RELEASE(node->rest);
            RELEASE(node->callsite_cache);
            break;
        }

        case CLJ_CALLSITE_CACHE: {
            CljCallsiteCache *cache = as_callsite_cache(v);
            if (cache) ASSIGN(cache->resolved, NULL);
            break;
        }
        case CLJ_FUNC:
            break;
        case CLJ_CLOSURE: {
            CljFunction *func = (CljFunction*)v;
            RELEASE(func->params);
            RELEASE(func->body);
            if (func->env_stack && !is_pointer_on_stack(func->env_stack)) RELEASE(func->env_stack);
            RELEASE(func->ns);
            break;
        }
            
        case CLJ_BYTE_ARRAY:
#ifndef ZOMBIE_ENABLED
            { CljByteArray *ba = as_byte_array(v);
              if (ba) {
                  if ((ba->base.flags & CLJ_FLAG_BYTE_ARRAY_EXTERNAL) != 0) {
                      CljByteArrayExternal *ext = (CljByteArrayExternal*)ba;
                      if (ext->external_free_fn) ext->external_free_fn(ext->external_ctx);
                  } else if (ba->data) CLJ_FREE(ba->data);
              } }
#else
            (void)v;
#endif
            break;
        case CLJ_ATOM:
            RELEASE(((CljAtom*)v)->value);
            break;
        case CLJ_SEQ:
            break;
        case CLJ_NAMESPACE: {
            CljNamespace *ns = (CljNamespace*)v;
            RELEASE(ns->mappings);
            RELEASE(ns->aliases);
#ifndef ZOMBIE_ENABLED
            if (ns->filename) CLJ_FREE((void*)ns->filename);
#endif
            break;
        }
        default:
            break;
    }
}

bool is_pointer_in_data_segment(const void *ptr) {
    if (!ptr) return false;
    uintptr_t a = (uintptr_t)ptr;
#if UINTPTR_MAX == UINT64_MAX
    return a < 0x100000000ULL;
#else
    return a < 0x08000000UL;
#endif
}

bool is_pointer_on_stack(const void *ptr) {
    if (!ptr) return false;
    volatile char m;
    uintptr_t sp = (uintptr_t)&m, pp = (uintptr_t)ptr;
    return pp >= sp && pp < sp + (8UL * 1024 * 1024);
}

void subjective_c_register_release_fn(CljType type, SubjectiveCReleaseFn fn) {
    if (type < 0 || type >= CLJ_TYPE_COUNT) return;
    init_release_dispatch();
    g_release_dispatch[type] = fn ? fn : release_object_default;
}

void throw_oom(void) {
    extern CLJException *clj_oom_exception;
    strncpy(clj_oom_exception->message, "Out of memory", sizeof(clj_oom_exception->message) - 1);
    clj_oom_exception->message[sizeof(clj_oom_exception->message) - 1] = '\0';
    strncpy(clj_oom_exception->file, __FILE__, sizeof(clj_oom_exception->file) - 1);
    clj_oom_exception->file[sizeof(clj_oom_exception->file) - 1] = '\0';
    clj_oom_exception->line = __LINE__;
    clj_oom_exception->col = 0;
    throw_exception_object(clj_oom_exception);
    abort();
}
