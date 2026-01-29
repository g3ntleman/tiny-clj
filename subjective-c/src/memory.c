/*
 * Memory Management Implementation for Tiny-CLJ
 * 
 * Centralized memory management with reference counting and autorelease pools.
 * Provides retain/release semantics similar to Objective-C ARC.
 */

#include "memory.h"
#include "callbacks.h"  // clj_to_string (injected by app)
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
#include <stdint.h>

// Optional stack trace support (execinfo/backtrace), gated in object.h.
// On ESP-IDF/newlib this is not available.
#if defined(SUBJECTIVE_C_HAVE_EXECINFO) && SUBJECTIVE_C_HAVE_EXECINFO
#include <execinfo.h>
#endif

// AUTORELEASE POOL: CLJ_VECTOR_TRANSIENT_WEAK (implemented as CljPersistentVector). _restore=vector_count at block start;
// drain_to_depth(mark) RELEASEs [mark,count) and truncates.

#define POOL_INITIAL_CAPACITY 1024

static THREAD_LOCAL CljPersistentVector *g_pool = NULL;
static THREAD_LOCAL bool g_in_drain = false;

#ifdef DEBUG
static THREAD_LOCAL uint32_t g_pool_peak_count = 0;
#endif

#if defined(DEBUG) && defined(ZOMBIE_ENABLED)
#define RCHIST_SIZE 128
typedef struct { CljObject *obj; char op; int rc_after; void *caller; } RcHistEntry;
static THREAD_LOCAL RcHistEntry g_rchist[RCHIST_SIZE];
static THREAD_LOCAL unsigned int g_rchist_head = 0;

static void rchist_push(CljObject *v, char op, int rc_after) {
    g_rchist[g_rchist_head] = (RcHistEntry){ v, op, rc_after, __builtin_return_address(0) };
    g_rchist_head = (g_rchist_head + 1) % RCHIST_SIZE;
}

static void rchist_dump_for_object(CljObject *v) {
    const char *filter_env = getenv("RCHIST_FILTER_ADDR");
    if (filter_env && (uintptr_t)v != (uintptr_t)strtoull(filter_env, NULL, 0))
        return;
    fprintf(stderr, "=== double-release debug: retain/release history for obj=%p (type=%s), newest first (read backwards) ===\n",
            (void*)v, clj_type_name(v->type));
    fprintf(stderr, "  R=retain L=release A=autorelease. Filter by address: RCHIST_FILTER_ADDR=0x%lx\n", (unsigned long)(uintptr_t)v);
    for (unsigned int i = 0; i < RCHIST_SIZE; i++) {
        unsigned int idx = (g_rchist_head + RCHIST_SIZE - 1 - i) % RCHIST_SIZE;
        RcHistEntry *e = &g_rchist[idx];
        if (e->obj != v) continue;
#if defined(SUBJECTIVE_C_HAVE_EXECINFO) && SUBJECTIVE_C_HAVE_EXECINFO
        if (e->caller) {
            void *arr[1] = { e->caller };
            char **syms = backtrace_symbols(arr, 1);
            if (syms && syms[0]) {
                fprintf(stderr, "  %c rc=%d  %s\n", e->op, e->rc_after, syms[0]);
                free(syms);
            } else {
                fprintf(stderr, "  %c rc=%d caller=%p\n", e->op, e->rc_after, e->caller);
                if (syms) free(syms);
            }
        } else
            fprintf(stderr, "  %c rc=%d caller=NULL\n", e->op, e->rc_after);
#else
        fprintf(stderr, "  %c rc=%d caller=%p\n", e->op, e->rc_after, e->caller);
#endif
    }
    fflush(stderr);
}
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

#ifndef ZOMBIE_ENABLED
static void release_object_deep(CljObject *v);
#endif
static void release_object_default(CljObject *v);
static void init_release_dispatch(void);
static SubjectiveCReleaseFn g_release_dispatch[CLJ_TYPE_COUNT];
static bool g_release_dispatch_initialized = false;

void autorelease_pool_init(void) {
    if (g_pool) return;
    g_pool = make_vector(POOL_INITIAL_CAPACITY, true);
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

/** Fills buf with clj_to_string(v), trunc to size-1. Uses injected to_string. No RELEASE: app to_string returns AUTORELEASE'd. */
static void zombie_desc(CljObject *v, char *buf, size_t size) {
    if (!buf || !size) return;
    buf[0] = '\0';
    CljString *s = clj_to_string((ID)v);
    if (!s) return;
    const char *d = clj_string_data(s);
    if (d) {
        size_t n = strlen(d);
        if (n >= size) n = size - 1;
        memcpy(buf, d, n);
        buf[n] = '\0';
    }
}

void retain(CljObject *v) {
    if (is_singleton(v)) return;
#ifdef DEBUG
    if (v->rc <= 0) {
        char message[512], z[384];
        zombie_desc(v, z, sizeof(z));
        (void)mini_snprintf(message, sizeof(message),
            "RETAIN: rc must be > 0 (got rc=%d). Zombie or corrupted refcount. Object %p (type=%s).%s%s",
            (int)v->rc, v, clj_type_name(v->type), z[0] ? " " : "", z);
        CLJException *ex = make_exception(EXCEPTION_ZOMBIE_ACCESS, message, __FILE__, __LINE__, 0);
        if (ex) {
            ex->object = (uintptr_t)v;
            throw_exception_object(AUTORELEASE(ex));  /* In pool; END_TRY does not release. */
        }
        return;
    }
#endif
    MEMORY_PROFILER_TRACK_RETAIN(v);
    v->rc++;
#if defined(DEBUG) && defined(ZOMBIE_ENABLED)
    rchist_push(v, 'R', v->rc);
#endif
}

void release(CljObject *v) {
    if (is_singleton(v)) return;
    if (g_debug_output_active)
        LOGF(stdout, "🔍 release: Object %p, type=%d (%s), rc=%d -> ",
             v, (int)v->type, clj_type_name(v->type), (int)v->rc);
    if (v->rc == 0) {
#if defined(DEBUG) && defined(ZOMBIE_ENABLED)
        rchist_dump_for_object(v);
#endif
        char z[384];
        if (!g_in_drain) zombie_desc(v, z, sizeof(z));
        else z[0] = '\0';
        throw_exception_formatted("UseAfterFreeError", __FILE__, __LINE__, 0,
            "Double-free detected! Object %p (type=%s) was already freed (rc=0). "
            "This indicates the object was released more times than retained, "
            "likely due to duplicate AUTORELEASE or incorrect memory management. %s",
            v, clj_type_name(v->type), z);
        return;
    }

#if defined(DEBUG) && defined(ZOMBIE_ENABLED)
    /* rc must be > pool_count before decrement; else double-release when pool drains. */
    if (TRACKS_RETAINS(v)) {
        uint32_t pool_count = autorelease_count(v);
        int rc_before = v->rc;
        if (pool_count > 0 && rc_before <= (int)pool_count) {
#if defined(DEBUG) && defined(ZOMBIE_ENABLED)
            rchist_dump_for_object(v);
#endif
            fprintf(stderr, "release: Object %p (type=%s) rc=%d <= pool_count=%u. %s\n",
                    (void*)v, clj_type_name(v->type), rc_before, pool_count,
                    rc_before == (int)pool_count ? "Missing RETAIN before RELEASE!" : "Will cause double-release!");
            fflush(stderr);
            CLJ_ASSERT(rc_before > (int)pool_count && "rc must be > pool_count before RELEASE (missing RETAIN or double RELEASE)");
        }
    }
#endif
#if defined(DEBUG) && defined(ZOMBIE_ENABLED)
    rchist_push(v, 'L', v->rc - 1);
#endif
    v->rc--;
    MEMORY_PROFILER_TRACK_RELEASE(v);
    if (v->rc == 0) {
        if (g_debug_output_active) LOGF(stdout, "🔍 release: Object %p will be freed (rc=0)\n", v);
#ifdef ZOMBIE_ENABLED
        if (autorelease_count(v) != 0) {
            char msg[256];
            /* No zombie_desc: clj_to_string can autorelease; we are inside drain (g_in_drain). */
            (void)mini_snprintf(msg, sizeof(msg),
                "Object %p (type=%s) still in autorelease pool; will double-release.",
                (void*)v, clj_type_name(v->type));
            fputs("AutoreleasePoolError: ", stderr);
            fputs(msg, stderr);
            fputs("\n", stderr);
            fflush(stderr);
            (void)throw_exception_formatted("AutoreleasePoolError", __FILE__, __LINE__, 0, "%s", msg);
        }
        /* Mark as zombie: do not DEALLOC; avoids crashes when debugging at cost of RAM */
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
    if (g_in_drain)
        throw_exception_formatted("AutoreleasePoolError", __FILE__, __LINE__, 0,
            "autorelease called during drain");
        return (CljObject*)NULL;
#if defined(DEBUG) && defined(ZOMBIE_ENABLED)
    // Check that object has enough references to survive all autorelease pool entries
    // After adding to pool, we'll have (pool_count + 1) entries, so we need rc > pool_count
    // If rc <= pool_count, the object will be freed when pool drains, causing double-release
    // This can happen due to:
    // 1. Double AUTORELEASE (same object added twice without RETAIN)
    // 2. Missing RETAIN (object should be RETAIN'd before AUTORELEASE if it's already in pool)
    // Only check if both DEBUG and ZOMBIE_ENABLED are defined (autorelease_count requires DEBUG, check requires ZOMBIE)
    if (TRACKS_RETAINS(v)) {
        uint32_t pool_count = autorelease_count(v);
        int rc = v->rc;
        if (rc <= (int)pool_count) {
            rchist_dump_for_object(v);
            if (rc == (int)pool_count) {
                fprintf(stderr, "autorelease: Object %p (type=%s) has rc=%d but already %u times in pool. Missing RETAIN before AUTORELEASE!\n",
                        (void*)v, clj_type_name(v->type), rc, pool_count);
            } else {
                fprintf(stderr, "autorelease: Object %p (type=%s) has rc=%d but already %u times in pool. Will cause double-release!\n",
                        (void*)v, clj_type_name(v->type), rc, pool_count);
            }
            fflush(stderr);
            CLJ_ASSERT(rc > (int)pool_count && "Object has insufficient references: rc must be > current pool_count before adding to pool (missing RETAIN or double AUTORELEASE)");
        }
    }
#endif
    ASSIGN(g_pool, vector_conj_owned(g_pool, v));
#if defined(DEBUG) && defined(ZOMBIE_ENABLED)
    rchist_push(v, 'A', v->rc);
#endif
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
// Never use in Production code.
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

bool is_autorelease_pool_draining(void) { return g_in_drain; }

void autorelease_pool_drain_to_depth(uint32_t mark) {
    if (!g_pool) return;
    unsigned int c = vector_count(g_pool);
    if (g_debug_output_active && c > mark) LOGF(stdout, "🔍 autorelease_pool_drain: [%u..%u)\n", (unsigned)mark, (unsigned)c);
    g_in_drain = true;
    TRY {
        for (unsigned int i = c; i > mark; ) {
            i--;
            ID e = vector_nth(g_pool, i);
            CLJ_ASSERT(e && "pool entry must not be NULL");
            vector_truncate(g_pool, i);
            RELEASE(e);
        }
    }
    CATCH(ex) {
        g_in_drain = false;
        throw_exception_object(ex);
    }
    END_TRY
    g_in_drain = false;
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

#ifndef ZOMBIE_ENABLED
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
#endif

static void release_object_default(CljObject *v) {
    switch (v->type) {
        case CLJ_STRING:
            break;
        // CLJ_SYMBOL: Release handler registered by tiny-clj via subjective_c_register_release_fn()
            
        case CLJ_VECTOR_PERSISTENT:
            {
                // Direct cast - we already know it's a Vector from the switch case
                // Using as_vector() would call TAG() which fails when rc=0 (zombie mode)
                CljPersistentVector *vec = (CljPersistentVector*)v;
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
                if (tvec && tvec->backing) {
                    RELEASE(tvec->backing);
                    tvec->backing = NULL;
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
            // Named fn: env_stack holds (name_sym -> self). RELEASE(env_stack) would double-free.
            // Skip and accept the retain-cycle leak.
            if (func->env_stack && !is_pointer_on_stack(func->env_stack) && !func->name_sym)
                RELEASE(func->env_stack);
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
#ifdef DEBUG
        case CLJ_EXCEPTION: {
            CLJException *ex = (CLJException*)v;
            if (ex->stacktrace) RELEASE(ex->stacktrace);
            break;
        }
#endif
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
