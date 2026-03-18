/**
 * @file memory.c
 * @brief Reference counting and autorelease pool implementation.
 *
 * Provides manual reference counting with RETAIN/RELEASE macros and
 * automatic cleanup via autorelease pools (WITH_AUTORELEASE_POOL).
 */

#include "memory.h"
#include "callbacks.h" // clj_to_string (injected by app)
#include "common.h"    // CLJ_ASSERT
#include "runtime.h"
#include "object.h"
#include "vector.h"
#include "value.h" // For IS_IMMEDIATE macro used in memory.h
#include "memory_profiler.h"
#include "types.h"
#include "exception.h"
#include "map.h"
#include "list.h"
#include "ast.h"
#if defined(__GNUC__) && !defined(ESP32_BUILD) && !defined(ESP_PLATFORM)
#include <execinfo.h>
#endif
#include "atom.h"
#include "function.h"   // For CljFunction
#include "namespace.h"  // For CljNamespace
#include "hashmap.h"    // For CljHashMap
#include "hashset.h"    // For CljHashSet
#include "byte_array.h" // For CljByteArray and external buffer flags
#include "subjective-c/record.h" // For CljPersistentRecord/CljRecordDescriptor
#include "subjective-c/platform_allocated_size.h"
#include "thread_local.h"
#include "mini_format.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
// Optional stack trace support (execinfo/backtrace), gated in object.h.
// On ESP-IDF/newlib this is not available.
#if defined(SUBJECTIVE_C_HAVE_EXECINFO) && SUBJECTIVE_C_HAVE_EXECINFO
#include <execinfo.h>
#endif

// AUTORELEASE POOL: CljPersistentVector with CLJ_FLAG_WEAK_ELEMENTS. _restore=vector_count at block start;
// drain_to_depth(mark) RELEASEs [mark,count) and truncates.

#define POOL_INITIAL_CAPACITY 1024

static THREAD_LOCAL CljPersistentVector *g_pool = NULL;
static THREAD_LOCAL bool g_in_drain = false;

#ifdef DEBUG
static THREAD_LOCAL uint32_t g_pool_peak_count = 0;
#endif

#if defined(DEBUG) && defined(ZOMBIE_ENABLED)
#define RCHIST_SIZE 1024
typedef struct {
  CljObject *obj;
  char op;
  int rc_after;
  void *caller;
} RcHistEntry;
static THREAD_LOCAL RcHistEntry g_rchist[RCHIST_SIZE];
static THREAD_LOCAL unsigned int g_rchist_head = 0;

static void rchist_push(CljObject *v, char op, int rc_after) {
  /* Frame 0 = retain/release/autorelease, frame 1 = actual RELEASE/RETAIN/AUTORELEASE call site */
#if defined(DEBUG)
  const char *closure_only = getenv("RCHIST_CLOSURE_ONLY");
  if (closure_only && closure_only[0] && strcmp(closure_only, "0") != 0) {
    if (!v || v->type != CLJ_CLOSURE) {
      return;
    }
  }
#endif
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wframe-address"
#endif
  void *caller = __builtin_return_address(1);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  g_rchist[g_rchist_head] = (RcHistEntry){v, op, rc_after, caller};
  g_rchist_head = (g_rchist_head + 1) % RCHIST_SIZE;
}

static void rchist_dump_for_object(CljObject *v) {
  const char *filter_env = getenv("RCHIST_FILTER_ADDR");
  if (filter_env && (uintptr_t)v != (uintptr_t)strtoull(filter_env, NULL, 0))
    return;
  fprintf(stderr, "=== rc-history debug: retain/release/autorelease history for obj=%p (type=%s), newest first (read backwards) ===\n",
          (void *)v, clj_type_name(v->type));
  fprintf(stderr, "  R=retain L=release A=autorelease. Filter by address: RCHIST_FILTER_ADDR=0x%lx\n", (unsigned long)(uintptr_t)v);
  for (unsigned int i = 0; i < RCHIST_SIZE; i++) {
    unsigned int idx = (g_rchist_head + RCHIST_SIZE - 1 - i) % RCHIST_SIZE;
    RcHistEntry *e = &g_rchist[idx];
    if (e->obj != v)
      continue;
#if defined(SUBJECTIVE_C_HAVE_EXECINFO) && SUBJECTIVE_C_HAVE_EXECINFO
    if (e->caller) {
      void *arr[1] = {e->caller};
      char **syms = backtrace_symbols(arr, 1);
      if (syms && syms[0]) {
        fprintf(stderr, "  %c rc=%d  %s\n", e->op, e->rc_after, syms[0]);
        CLJ_FREE(syms);
      } else {
        fprintf(stderr, "  %c rc=%d caller=%p\n", e->op, e->rc_after, e->caller);
        if (syms)
          CLJ_FREE(syms);
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
static size_t g_heap_limit_bytes = 0;

/** @brief Set zombie logging callback. */
void subjective_c_set_zombie_log_fn(SubjectiveCZombieLogFn fn) {
  g_zombie_log_fn = fn;
}

/** @brief Enable/disable debug output for memory operations. */
void memory_set_debug_output_enabled(bool enabled) {
  g_debug_output_enabled = enabled;
  update_debug_output_active();
}

/** @brief Check if debug output is enabled. */
bool memory_get_debug_output_enabled(void) {
  return g_debug_output_enabled;
}

void memory_set_heap_limit_bytes(size_t limit) {
  g_heap_limit_bytes = limit;
}

size_t memory_get_heap_limit_bytes(void) {
  return g_heap_limit_bytes;
}

size_t memory_current_usage_bytes(void) {
  return g_memory_stats.current_memory_usage;
}

size_t memory_actual_allocation_size(const void *ptr, size_t requested_size) {
  if (!ptr) {
    return requested_size;
  }
  size_t actual = platform_allocated_size(ptr);
  return actual > 0 ? actual : requested_size;
}

size_t memory_tracked_raw_allocation_size(const void *ptr) {
  return memory_actual_allocation_size(ptr, 0);
}

bool memory_heap_limit_would_exceed(size_t released_size, size_t requested_size) {
  if (g_heap_limit_bytes == 0) {
    return false;
  }
  size_t current = memory_current_usage_bytes();
  size_t projected = current >= released_size ? current - released_size : 0;
  if (requested_size > SIZE_MAX - projected) {
    return true;
  }
  projected += requested_size;
  return projected > g_heap_limit_bytes;
}

/**
 * @brief Allocate memory for Clojure objects.
 * @param type_size Size of object type
 * @param count Number of objects
 * @param obj_type Clojure type tag
 * @return Non-NULL allocated object with rc=1. On OOM, throws and never returns.
 */
void *alloc(size_t type_size, size_t count, CljType obj_type) {
  if (count != 0 && type_size > SIZE_MAX / count) {
    throw_oom();
  }
  size_t requested = type_size * count;
  if (requested != 0 && memory_heap_limit_would_exceed(0, requested)) {
    throw_oom();
  }
  void *result = malloc(requested);
  if (!result) {
    fprintf(stderr,
            "OOM alloc request: bytes=%zu type=%s(%d)\n",
            requested, clj_type_name(obj_type), (int)obj_type);
    throw_oom();
  }
  if (requested != 0) {
    size_t actual = memory_actual_allocation_size(result, requested);
    if (memory_heap_limit_would_exceed(0, actual)) {
      free(result);
      fprintf(stderr,
              "OOM alloc request: bytes=%zu type=%s(%d)\n",
              actual, clj_type_name(obj_type), (int)obj_type);
      throw_oom();
    }
  }
  if (type_size >= sizeof(CljObject)) {
    CljObject *obj = (CljObject *)result;
    obj->type = obj_type;
    obj->flags = 0;
    obj->rc = 1;
    // Always call the function; in DEBUG it tracks heap even without full profiling.
    memory_profiler_track_object_creation_sized(obj, type_size * count);
  }

  return result;
}

static void release_object_deep(CljObject *v);
static void release_object_default(CljObject *v);
static void init_release_dispatch(void);
static SubjectiveCReleaseFn g_release_dispatch[CLJ_TYPE_COUNT];
static bool g_release_dispatch_initialized = false;

/**
 * @brief Initialize global autorelease pool (called once at startup).
 */
void autorelease_pool_init(void) {
  if (g_pool)
    return;
  g_pool = make_vector(POOL_INITIAL_CAPACITY, WEAK);
#ifdef DEBUG
  g_pool_peak_count = 0;
#endif
}

static void init_release_dispatch(void) {
  if (g_release_dispatch_initialized)
    return;
  for (int i = 0; i < CLJ_TYPE_COUNT; i++) {
    g_release_dispatch[i] = release_object_default;
  }
  g_release_dispatch_initialized = true;
}

#if defined(DEBUG) && defined(ZOMBIE_ENABLED)
/**
 * @brief Fill buffer with string representation for zombie debugging.
 * @param v Object to describe
 * @param buf Output buffer
 * @param size Buffer size
 */
static void zombie_description(CljObject *v, char *buf, size_t size) {
  if (!buf || !size)
    return;
  buf[0] = '\0';
  /* Do not call clj_to_string when rc<=0: the to_string path may RETAIN (or AUTORELEASE)
   * objects. v and its children have rc=0; RETAIN on rc=0 throws ZombieAccessException,
   * so we would trigger a second throw while building the error message. */
  if (v->rc <= 0) {
    (void)mini_snprintf(buf, size, "(zombie %p)", (void *)v);
    return;
  }
  CljString *s = clj_to_string((ID)v);
  if (!s)
    return;
  const char *d = clj_string_data(s);
  if (d) {
    size_t n = strlen(d);
    if (n >= size)
      n = size - 1;
    memcpy(buf, d, n);
    buf[n] = '\0';
  }
}
#endif

/**
 * @brief Increment reference count (ignores singletons/immediates).
 * @param v Object to retain (NULL-safe)
 */
void retain(CljObject *v) {
  if (is_singleton(v))
    return;
#ifdef DEBUG
  {
    const char *trace_list = getenv("TINYCLJ_TRACE_LIST_RETAIN");
    const char *trace_ast = getenv("TINYCLJ_TRACE_AST_RETAIN");
    if ((v->type == CLJ_LIST && trace_list && trace_list[0] && strcmp(trace_list, "0") != 0) ||
        ((v->type == CLJ_AST_NODE || v->type == CLJ_AST_CALL) &&
         trace_ast && trace_ast[0] && strcmp(trace_ast, "0") != 0)) {
      static int trace_count = 0;
      if (trace_count < 200) {
        fprintf(stderr, "[retain] %s %p rc=%d\n", clj_type_name(v->type), (void *)v, (int)v->rc);
#if defined(__GNUC__) && !defined(ESP32_BUILD) && !defined(ESP_PLATFORM)
        const char *trace_bt = (v->type == CLJ_LIST)
                                   ? getenv("TINYCLJ_TRACE_LIST_RETAIN_BT")
                                   : getenv("TINYCLJ_TRACE_AST_RETAIN_BT");
        if (trace_bt && trace_bt[0] && strcmp(trace_bt, "0") != 0) {
          void *bt[16];
          int n = backtrace(bt, 16);
          char **symbols = backtrace_symbols(bt, n);
          if (symbols) {
            for (int i = 0; i < n; i++) {
              fprintf(stderr, "  %s\n", symbols[i]);
            }
            CLJ_FREE(symbols);
          }
        }
#endif
        trace_count++;
      }
    }
  }
#endif
#ifdef DEBUG
  if (v->rc <= 0) {
#if defined(ZOMBIE_ENABLED)
    rchist_dump_for_object(v);
#endif
    char message[512];
#if defined(ZOMBIE_ENABLED)
    char z[384];
    zombie_description(v, z, sizeof(z));
    const char *zmsg = z;
#else
    const char *zmsg = "";
#endif
    (void)mini_snprintf(message, sizeof(message),
                        "RETAIN: rc must be > 0 (got rc=%d). Zombie or corrupted refcount. Object %p (type=%s).%s%s",
                        (int)v->rc, v, clj_type_name(v->type), zmsg[0] ? " " : "", zmsg);
    CLJException *ex = make_exception(EXCEPTION_ZOMBIE_ACCESS, message, __FILE__, __LINE__, 0);
    if (ex) {
      ex->object = (uintptr_t)v;
      throw_exception_object(AUTORELEASE(ex)); /* In pool; END_TRY does not release. */
    }
    return;
  }
#endif
  MEMORY_PROFILER_TRACK_RETAIN(v);
  CLJ_ASSERT(v->rc < INT16_MAX);
  v->rc++;
#if defined(DEBUG) && defined(ZOMBIE_ENABLED)
  rchist_push(v, 'R', v->rc);
#endif
}

/**
 * @brief Decrement reference count and free if zero (ignores singletons).
 * @param v Object to release (NULL-safe)
 */
void release(CljObject *v) {
  if (is_singleton(v))
    return;
#ifdef DEBUG
  {
    const char *trace_list = getenv("TINYCLJ_TRACE_LIST_RELEASE");
    const char *trace_ast = getenv("TINYCLJ_TRACE_AST_RELEASE");
    if ((v->type == CLJ_LIST && trace_list && trace_list[0] && strcmp(trace_list, "0") != 0) ||
        ((v->type == CLJ_AST_NODE || v->type == CLJ_AST_CALL) &&
         trace_ast && trace_ast[0] && strcmp(trace_ast, "0") != 0)) {
      fprintf(stderr, "[release] %s %p rc=%d\n",
              clj_type_name(v->type), (void *)v, (int)v->rc);
    }
  }
#endif
  if (g_debug_output_active)
    LOGF(stdout, "🔍 release: Object %p, type=%d (%s), rc=%d -> ",
         v, (int)v->type, clj_type_name(v->type), (int)v->rc);
  if (v->rc == 0) {
#if defined(DEBUG) && defined(ZOMBIE_ENABLED)
    rchist_dump_for_object(v);
#endif
    const char *zmsg = "";
#if defined(DEBUG) && defined(ZOMBIE_ENABLED)
    char z[384];
    if (!g_in_drain)
      zombie_description(v, z, sizeof(z));
    else
      z[0] = '\0';
    zmsg = z;
#endif
    throw_exception_formatted("UseAfterFreeError", __FILE__, __LINE__, 0,
                              "Double-free detected! Object %p (type=%s) was already freed (rc=0). "
                              "This indicates the object was released more times than retained, "
                              "likely due to duplicate AUTORELEASE or incorrect memory management. %s",
                              v, clj_type_name(v->type), zmsg);
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
              (void *)v, clj_type_name(v->type), rc_before, pool_count,
              rc_before == (int)pool_count ? "Missing RETAIN before RELEASE!" : "Will cause double-release!");
      fflush(stderr);
      throw_exception_formatted("AutoreleasePoolError", __FILE__, __LINE__, 0,
                                "release: Object %p (type=%s) rc=%d <= pool_count=%u",
                                (void *)v, clj_type_name(v->type), rc_before, pool_count);
      return;
    }
  }
#endif
#if defined(DEBUG) && defined(ZOMBIE_ENABLED)
  rchist_push(v, 'L', v->rc - 1);
#endif
  v->rc--;
  MEMORY_PROFILER_TRACK_RELEASE(v);
  if (v->rc == 0) {
    if (g_debug_output_active)
      LOGF(stdout, "🔍 release: Object %p will be freed (rc=0)\n", v);
#ifdef DEBUG
    {
      const char *trace_list = getenv("TINYCLJ_TRACE_LIST_FREE");
      const char *trace_ast = getenv("TINYCLJ_TRACE_AST_FREE");
      if ((v->type == CLJ_LIST && trace_list && trace_list[0] && strcmp(trace_list, "0") != 0) ||
          ((v->type == CLJ_AST_NODE || v->type == CLJ_AST_CALL) &&
           trace_ast && trace_ast[0] && strcmp(trace_ast, "0") != 0)) {
        fprintf(stderr, "[free] %s %p\n", clj_type_name(v->type), (void *)v);
      }
    }
#endif
#if defined(DEBUG) && defined(ZOMBIE_ENABLED)
    if (autorelease_count(v) != 0) {
      char msg[256];
      /* No zombie_desc: clj_to_string can autorelease; we are inside drain (g_in_drain). */
      (void)mini_snprintf(msg, sizeof(msg),
                          "Object %p (type=%s) still in autorelease pool; will double-release.",
                          (void *)v, clj_type_name(v->type));
      fputs("AutoreleasePoolError: ", stderr);
      fputs(msg, stderr);
      fputs("\n", stderr);
      fflush(stderr);
      (void)throw_exception_formatted("AutoreleasePoolError", __FILE__, __LINE__, 0, "%s", msg);
    }
    release_object_deep(v);                  /* Release nested refs so rc counts match Clojure semantics */
    /*
     * Zombie mode intentionally keeps the top-level allocation around for debugging.
     * We still report this as a logical deallocation so heap-limit checks and memory
     * stats reflect ownership semantics instead of debug-only zombie retention.
     */
    MEMORY_PROFILER_TRACK_OBJECT_ZOMBIFY(v);
#else
    release_object_deep(v);
    if (g_debug_output_active)
      LOGF(stdout, "🔍 release: Object %p will be freed\n", v);
    DEALLOC(v);
#endif
  }
}

void autorelease_pool_ensure_active(void) {
  if (!g_pool)
    autorelease_pool_init();
}

uint32_t autorelease_pool_mark(void) {
  autorelease_pool_ensure_active();
  return (uint32_t)vector_count(g_pool);
}

CljObject *autorelease(CljObject *v) {
  if (!v)
    return NULL;
  if (v == (CljObject *)g_pool)
    return v; /* never add pool to itself (would double-release in remove/drain) */
  CLJ_ASSERT(g_pool && "autorelease_pool_init() not called");
  if (g_in_drain) {
    throw_exception_formatted("AutoreleasePoolError", __FILE__, __LINE__, 0,
                              "autorelease called during drain");
    return (CljObject *)NULL;
  }
#ifdef DEBUG
  {
    const char *trace_list = getenv("TINYCLJ_TRACE_LIST_AUTORELEASE");
    const char *trace_ast = getenv("TINYCLJ_TRACE_AST_AUTORELEASE");
    if ((v->type == CLJ_LIST && trace_list && trace_list[0] && strcmp(trace_list, "0") != 0) ||
        ((v->type == CLJ_AST_NODE || v->type == CLJ_AST_CALL) &&
         trace_ast && trace_ast[0] && strcmp(trace_ast, "0") != 0)) {
      fprintf(stderr, "[autorelease] %s %p rc=%d\n", clj_type_name(v->type), (void *)v, (int)v->rc);
    }
  }
#endif
#if defined(DEBUG)
  {
    const char *dup_trace = getenv("TINYCLJ_TRACE_AUTORELEASE_DUP");
    if (dup_trace && dup_trace[0] && strcmp(dup_trace, "0") != 0) {
      uint32_t pool_count = autorelease_count(v);
      if (pool_count > 0) {
        fprintf(stderr, "autorelease: Object %p (type=%s) already %" PRIu32 " times in pool\n",
                (void *)v, clj_type_name(v->type), pool_count);
        exception_print_native_backtrace();
        fflush(stderr);
      }
    }
  }
#endif
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
                (void *)v, clj_type_name(v->type), rc, pool_count);
      } else {
        fprintf(stderr, "autorelease: Object %p (type=%s) has rc=%d but already %u times in pool. Will cause double-release!\n",
                (void *)v, clj_type_name(v->type), rc, pool_count);
      }
      fflush(stderr);
      CLJ_ASSERT(rc > (int)pool_count && "Object has insufficient references: rc must be > current pool_count before adding to pool (missing RETAIN or double AUTORELEASE)");
    }
  }
#endif
  // Keep pool RC at 1 across growth. ASSIGN(..., vector_conj_owned(...)) would leak the
  // owned return on COW growth and force every subsequent push down the COW path.
  vector_conj_inplace(&g_pool, v);
#if defined(DEBUG) && defined(ZOMBIE_ENABLED)
  rchist_push(v, 'A', v->rc);
#endif
#ifdef DEBUG
  if (vector_count(g_pool) > g_pool_peak_count)
    g_pool_peak_count = vector_count(g_pool);
#endif
  MEMORY_PROFILER_TRACK_AUTORELEASE(v);
#ifdef DEBUG
  {
    const char *trace_any = getenv("TINYCLJ_TRACE_POOL_DRAIN");
    if (trace_any && trace_any[0] && strcmp(trace_any, "0") != 0) {
      fprintf(stderr, "[pool-add] %s %p count=%u\n",
              clj_type_name(v->type), (void *)v, (unsigned)vector_count(g_pool));
    }
  }
#endif
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
  if (!g_pool) {
    g_pool_peak_count = 0;
    return;
  }
  g_pool_peak_count = vector_count(g_pool);
#else
  (void)0;
#endif
}

#ifdef DEBUG
/* DEBUG only; must not be used in Release builds. */
uint32_t autorelease_count(CljObject *obj) {
  if (!obj || !g_pool)
    return 0;
  uint32_t n = 0;
  unsigned int c = vector_count(g_pool);
  ID *arr = vector_as_array(g_pool);
  for (unsigned int i = 0; i < c; i++)
    if (arr[i] == obj)
      n++;
  return n;
}
#endif

bool is_autorelease_pool_active(void) { return g_pool != NULL; }

uint32_t autorelease_pool_depth(void) {
  if (!g_pool) {
    return 0u;
  }
  return (uint32_t)vector_count(g_pool);
}

bool is_autorelease_pool_draining(void) { return g_in_drain; }

void autorelease_pool_drain_to_depth(uint32_t mark) {
  if (!g_pool)
    return;
  unsigned int c = vector_count(g_pool);
  if (g_debug_output_active && c > mark)
    LOGF(stdout, "🔍 autorelease_pool_drain: [%u..%u)\n", (unsigned)mark, (unsigned)c);
#ifdef DEBUG
  {
    const char *trace_any = getenv("TINYCLJ_TRACE_POOL_DRAIN");
    if (trace_any && trace_any[0] && strcmp(trace_any, "0") != 0) {
      fprintf(stderr, "[pool-drain-start] mark=%u count=%u\n", (unsigned)mark, (unsigned)c);
    }
  }
#endif
  g_in_drain = true;
  ExceptionHandler drain_handler;
  drain_handler.next = global_exception_stack.top;
  drain_handler.exception = NULL;
  global_exception_stack.top = &drain_handler;

  if (setjmp(drain_handler.jump_state) == 0) {
    for (unsigned int i = c; i > mark;) {
      i--;
      ID e = vector_nth(g_pool, i);
      CLJ_ASSERT(e && "pool entry must not be NULL");
      vector_truncate(g_pool, i);
#ifdef DEBUG
      {
        const char *trace_any = getenv("TINYCLJ_TRACE_POOL_DRAIN");
        const char *trace_list = getenv("TINYCLJ_TRACE_LIST_POOL_DRAIN");
        const char *trace_ast = getenv("TINYCLJ_TRACE_AST_POOL_DRAIN");
        if (trace_any && trace_any[0] && strcmp(trace_any, "0") != 0) {
          if (!IS_IMMEDIATE(e)) {
            fprintf(stderr, "[pool-drain] %s %p rc=%d\n",
                    clj_type_name(((CljObject *)e)->type), (void *)e, (int)((CljObject *)e)->rc);
          } else {
            fprintf(stderr, "[pool-drain] immediate %p\n", (void *)e);
          }
        } else if ((!IS_IMMEDIATE(e)) &&
                   ((TAG(e) == CLJ_LIST && trace_list && trace_list[0] && strcmp(trace_list, "0") != 0) ||
                    ((TAG(e) == CLJ_AST_NODE || TAG(e) == CLJ_AST_CALL) &&
                     trace_ast && trace_ast[0] && strcmp(trace_ast, "0") != 0))) {
          fprintf(stderr, "[pool-drain] %s %p rc=%d\n",
                  clj_type_name(((CljObject *)e)->type), (void *)e, (int)((CljObject *)e)->rc);
        }
      }
#endif
      RELEASE(e);
    }
    global_exception_stack.top = drain_handler.next;
    g_in_drain = false;
  } else {
    CLJException *ex = drain_handler.exception;
    global_exception_stack.top = drain_handler.next;
    g_in_drain = false;
    throw_exception_object(ex);
    return;
  }
  g_in_drain = false;

  // Reset pool capacity after large spikes so nested pools do not leave permanent
  // Vector growth visible to heap-growth tests (e.g. many AUTORELEASEs in one eval).
  unsigned int keep = vector_count(g_pool);
  unsigned int target_capacity = POOL_INITIAL_CAPACITY;
  while (target_capacity < keep && target_capacity < (1u << 30)) {
    target_capacity <<= 1;
  }
  if (g_pool && g_pool->capacity > (int)target_capacity) {
    CljPersistentVector *new_pool = make_vector((int)target_capacity, WEAK);
    if (new_pool) {
      if (keep > 0) {
        memcpy(new_pool->data, g_pool->data, keep * sizeof(ID));
        new_pool->count = keep;
      }
      RELEASE(g_pool);
      g_pool = new_pool;
    }
  }
}

void autorelease_pool_free(void) {
  autorelease_pool_drain_to_depth(0);
  if (g_pool) {
    RELEASE(g_pool);
    g_pool = NULL;
  }
}

int retain_count(ID obj) {
  if (!obj || IS_IMMEDIATE(obj))
    return 0;
  CljObject *o = (CljObject *)obj;
  return (o->rc == SINGLETON_RC) ? 0 : o->rc;
}

static void release_object_deep(CljObject *v) {
  if (!v || !TRACKS_RETAINS(v))
    return;
  init_release_dispatch();
  SubjectiveCReleaseFn fn = ((unsigned)v->type < CLJ_TYPE_COUNT)
                                ? g_release_dispatch[v->type]
                                : NULL;
  if (fn) {
    fn(v);
  }
}

static void release_object_default(CljObject *v) {
  switch (v->type) {
  case CLJ_STRING:
    if ((v->flags & CLJ_FLAG_EXTERNAL_DATA) != 0) {
      CljByteArrayView *ext = (CljByteArrayView *)v;
#ifndef ZOMBIE_ENABLED
      if (ext->external_free_fn)
        ext->external_free_fn(ext->external_ctx);
#else
      if ((v->flags & CLJ_FLAG_EXTERNAL_IS_OBJECT) != 0 && ext->external_free_fn)
        ext->external_free_fn(ext->external_ctx);
#endif
    }
    break;
    // CLJ_SYMBOL: Release handler registered by tiny-clj via subjective_c_register_release_fn()

  case CLJ_VECTOR_PERSISTENT: {
    // Direct cast - we already know it's a Vector from the switch case
    // Using as_vector() would call TAG() which fails when rc=0 (zombie mode)
    CljPersistentVector *vec = (CljPersistentVector *)v;
    if (vec) {
      if (!has_weak_elements((const CljObject *)vec)) {
        // Release all vector elements
        VECTOR_FOR_EACH(vec, elem) {
          RELEASE(elem);
        }
      }
      // Note: data array is automatically freed
    }
  } break;

  case CLJ_VECTOR_TRANSIENT: {
    CljTransientVector *tvec = (CljTransientVector *)v;
    if (tvec && tvec->backing) {
      RELEASE(tvec->backing);
      tvec->backing = NULL;
    }
  } break;
  case CLJ_MAP_PERSISTENT: {
    CljPersistentMap *map = (CljPersistentMap *)v;
    if (map && !has_weak_elements((const CljObject *)map)) {
      MAP_FOR_EACH(map, key, value) {
        RELEASE(key);
        RELEASE(value);
      }
    }
    break;
  }
  case CLJ_MAP_TRANSIENT: {
    CljTransientMap *tmap = (CljTransientMap *)v;
    RELEASE(tmap->backing);
    tmap->backing = NULL;
    break;
  }

  case CLJ_HASHMAP: {
    CljHashMap *map = (CljHashMap *)v;
    ID hm_key;
    ID hm_val;
    HASHMAP_FOR_EACH(map, hm_key, hm_val) {
      RELEASE(hm_key);
      RELEASE(hm_val);
    }
  } break;

  case CLJ_HASHSET: {
    CljHashSet *set = (CljHashSet *)v;
    ID hs_key;
    HASHSET_FOR_EACH(set, hs_key) {
      RELEASE(hs_key);
    }
  } break;

  case CLJ_RECORD_DESCRIPTOR: {
    CljRecordDescriptor *desc = (CljRecordDescriptor *)v;
    if (!desc)
      break;
    RELEASE(desc->type_symbol);
    RELEASE(desc->field_keys);
    break;
  }

  case CLJ_RECORD: {
    CljPersistentRecord *record = (CljPersistentRecord *)v;
    if (!record)
      break;
    unsigned int field_count = record_declared_field_count(record);
    for (unsigned int i = 0; i < field_count; i++) {
      RELEASE(record->values[i]);
    }
    RELEASE(record->descriptor);
    break;
  }

  case CLJ_LIST: {
    CljList *list = (CljList *)v;
#ifdef DEBUG
    {
      const char *trace_deep = getenv("TINYCLJ_TRACE_LIST_RELEASE_DEEP");
      if (trace_deep && trace_deep[0] && strcmp(trace_deep, "0") != 0) {
        fprintf(stderr, "[list-release] %p first=%p rest=%p\n",
                (void *)list, (void *)list->first, (void *)list->rest);
        if (list->first) {
          if (IS_IMMEDIATE(list->first)) {
            fprintf(stderr, "  first: immediate %p\n", (void *)list->first);
          } else {
            CljObject *o = (CljObject *)list->first;
            fprintf(stderr, "  first: %s %p rc=%d\n",
                    clj_type_name(o->type), (void *)o, (int)o->rc);
          }
        } else {
          fprintf(stderr, "  first: NULL\n");
        }
        if (list->rest) {
          if (IS_IMMEDIATE(list->rest)) {
            fprintf(stderr, "  rest: immediate %p\n", (void *)list->rest);
          } else {
            CljObject *o = (CljObject *)list->rest;
            fprintf(stderr, "  rest: %s %p rc=%d\n",
                    clj_type_name(o->type), (void *)o, (int)o->rc);
          }
        } else {
          fprintf(stderr, "  rest: NULL\n");
        }
      }
    }
#endif
    RELEASE(list->first);

    // Release list tails iteratively to avoid deep recursive release chains
    // for long persistent lists (e.g. large lazy-for materializations).
    ID tail = list->rest;
    list->rest = NULL;
    while (tail && !IS_IMMEDIATE(tail) && TAG(tail) == CLJ_LIST) {
      CljObject *tail_obj = (CljObject *)tail;
      if (tail_obj->rc != 1) {
        break;
      }

      CljList *tail_list = (CljList *)tail;
      ID next_tail = tail_list->rest;

      // Detach before RELEASE(tail) so the nested destructor does not recurse.
      tail_list->rest = NULL;
      RELEASE(tail_list->first);
      tail_list->first = NULL;
      RELEASE(tail);

      tail = next_tail;
    }
    RELEASE(tail);
    break;
  }
  case CLJ_AST_NODE: {
    CljASTNode *node = (CljASTNode *)v;
#ifdef DEBUG
    {
      const char *trace_deep = getenv("TINYCLJ_TRACE_AST_RELEASE_DEEP");
      if (trace_deep && trace_deep[0] && strcmp(trace_deep, "0") != 0) {
        fprintf(stderr, "[ast-release] %p first=%p rest=%p cache=%p\n",
                (void *)node, (void *)node->first, (void *)node->rest, (void *)node->callsite_cache);
        if (node->first) {
          if (IS_IMMEDIATE(node->first)) {
            fprintf(stderr, "  first: immediate %p\n", (void *)node->first);
          } else {
            CljObject *o = (CljObject *)node->first;
            fprintf(stderr, "  first: %s %p rc=%d\n",
                    clj_type_name(o->type), (void *)o, (int)o->rc);
          }
        } else {
          fprintf(stderr, "  first: NULL\n");
        }
        if (node->rest) {
          if (IS_IMMEDIATE(node->rest)) {
            fprintf(stderr, "  rest: immediate %p\n", (void *)node->rest);
          } else {
            CljObject *o = (CljObject *)node->rest;
            fprintf(stderr, "  rest: %s %p rc=%d\n",
                    clj_type_name(o->type), (void *)o, (int)o->rc);
          }
        } else {
          fprintf(stderr, "  rest: NULL\n");
        }
        if (node->callsite_cache) {
          CljObject *o = (CljObject *)node->callsite_cache;
          fprintf(stderr, "  cache: %s %p rc=%d\n",
                  clj_type_name(o->type), (void *)o, (int)o->rc);
        } else {
          fprintf(stderr, "  cache: NULL\n");
        }
      }
    }
#endif
    RELEASE(node->first);
    RELEASE(node->rest);
    RELEASE(node->callsite_cache);
    break;
  }
  case CLJ_AST_CALL: {
    CljASTCall *call = (CljASTCall *)v;
#ifdef DEBUG
    {
      const char *trace_deep = getenv("TINYCLJ_TRACE_AST_RELEASE_DEEP");
      if (trace_deep && trace_deep[0] && strcmp(trace_deep, "0") != 0) {
        fprintf(stderr, "[ast-call-release] %p op=%p args=%p cache=%p\n",
                (void *)call, (void *)call->op, (void *)call->args, (void *)call->callsite_cache);
        if (call->op) {
          if (IS_IMMEDIATE(call->op)) {
            fprintf(stderr, "  op: immediate %p\n", (void *)call->op);
          } else {
            CljObject *o = (CljObject *)call->op;
            fprintf(stderr, "  op: %s %p rc=%d\n",
                    clj_type_name(o->type), (void *)o, (int)o->rc);
          }
        } else {
          fprintf(stderr, "  op: NULL\n");
        }
        if (call->args) {
          CljObject *o = (CljObject *)call->args;
          fprintf(stderr, "  args: %s %p rc=%d\n",
                  clj_type_name(o->type), (void *)o, (int)o->rc);
        } else {
          fprintf(stderr, "  args: NULL\n");
        }
        if (call->callsite_cache) {
          CljObject *o = (CljObject *)call->callsite_cache;
          fprintf(stderr, "  cache: %s %p rc=%d\n",
                  clj_type_name(o->type), (void *)o, (int)o->rc);
        } else {
          fprintf(stderr, "  cache: NULL\n");
        }
      }
    }
#endif
    RELEASE(call->op);
    RELEASE(call->args);
    RELEASE(call->callsite_cache);
    break;
  }
  case CLJ_CALLSITE_CACHE: {
    CljCallsiteCache *cache = as_callsite_cache(v);
    if (cache)
      ASSIGN(cache->resolved, NULL);
    break;
  }
  case CLJ_FUNC:
    break;
  case CLJ_CLOSURE: {
    CljFunction *func = (CljFunction *)v;
    for (uint8_t i = 0; i < func->param_count; i++) {
      RELEASE(func->params[i]);
    }
    RELEASE(func->body);
    // env_stack is always heap-managed (or NULL) for persistent closures.
    // Local self-recursion no longer relies on env_stack self-cycles.
    if (func->env_stack && !is_pointer_on_stack(func->env_stack))
      RELEASE(func->env_stack);
    RELEASE(func->ns);
    break;
  }

  case CLJ_BYTE_ARRAY:
#ifndef ZOMBIE_ENABLED
  {
    CljByteArray *ba = as_byte_array(v);
    if (ba) {
      if ((ba->base.flags & CLJ_FLAG_EXTERNAL_DATA) != 0) {
        CljByteArrayView *ext = (CljByteArrayView *)ba;
        if (ext->external_free_fn)
          ext->external_free_fn(ext->external_ctx);
      } else if (ba->data)
        CLJ_FREE(ba->data);
    }
  }
#else
    (void)v;
#endif
  break;
  case CLJ_ATOM:
    RELEASE(((CljAtom *)v)->value);
    break;
#ifdef DEBUG
  case CLJ_EXCEPTION: {
    CLJException *ex = (CLJException *)v;
    RELEASE(ex->stacktrace);
    break;
  }
#endif
  case CLJ_SEQ:
    break;
  case CLJ_NAMESPACE: {
    CljNamespace *ns = (CljNamespace *)v;
    RELEASE(ns->mappings);
    RELEASE(ns->private_mappings);
    RELEASE(ns->aliases);
#ifndef ZOMBIE_ENABLED
    if (ns->filename)
      CLJ_FREE((void *)ns->filename);
#endif
    break;
  }
  default:
    break;
  }
}

bool is_pointer_in_data_segment(const void *ptr) {
  if (!ptr)
    return false;
  uintptr_t a = (uintptr_t)ptr;
#if UINTPTR_MAX == UINT64_MAX
  return a < 0x100000000ULL;
#else
  return a < 0x08000000UL;
#endif
}

bool is_pointer_on_stack(const void *ptr) {
  if (!ptr)
    return false;
  volatile char m;
  uintptr_t sp = (uintptr_t)&m, pp = (uintptr_t)ptr;
  return pp >= sp && pp < sp + (8UL * 1024 * 1024);
}

void subjective_c_register_release_fn(CljType type, SubjectiveCReleaseFn fn) {
  if (type < 0 || type >= CLJ_TYPE_COUNT)
    return;
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
  fputs("OOM throw-site backtrace:\n", stderr);
  exception_print_native_backtrace();
  throw_exception_object(clj_oom_exception);
  abort();
}
