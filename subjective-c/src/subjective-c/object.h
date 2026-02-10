/*
 * CljObject Header
 *
 * Core data structure definitions for Tiny-Clj:
 * - CljObject base structure for all Clojure data types
 * - Type checking and casting utilities
 * - Memory management with reference counting
 */

#ifndef SUBJECTIVE_C_OBJECT_H
#define SUBJECTIVE_C_OBJECT_H

/**
 * ID – opaque reference to any Clojure value (heap object or immediate).
 * Typedef for void* so all object pointers and immediates can be passed uniformly.
 *
 * Rule: Do not cast to or from ID. Pass values as ID; use macros (RETAIN, RELEASE,
 * AUTORELEASE) and APIs that accept ID so no cast is needed at the call site.
 * If an API requires a concrete pointer type, change the API to take ID or use
 * an accessor that accepts ID – do not cast.
 */
typedef void *ID;
typedef ID CljValue;

#include "types.h"

// IMPORTANT (cross-compile / embedded):
// `common.h` includes <string.h>, and some libc implementations include <strings.h>.
// Since subjective-c provides its own "strings.h" header, we must ensure core types
// like `CljObject` are defined BEFORE including `common.h`, otherwise a circular
// include can occur during toolchain builds (e.g. Xtensa/newlib).

typedef struct CljObject
{
    uint8_t type;
    uint8_t flags;
    int16_t rc;
} CljObject;

// Forward declaration to avoid implicit declaration when headers include each other
// through libc's <string.h>/<strings.h> on embedded toolchains.
static inline void *assert_type(CljObject *obj, CljType expected_type);

#include "common.h"
#include <stdio.h>

// Ensure CLJ_ASSERT exists even if a different common.h was pulled in
// via the toolchain's <string.h>/<strings.h> include chain.
#ifndef CLJ_ASSERT
#define CLJ_ASSERT(expr) ((void)0)
#endif

// Optional: stack trace support (execinfo/backtrace) for debug diagnostics.
// Not available on ESP-IDF/newlib, so keep it disabled for embedded builds.
#if !defined(ESP32_BUILD) && !defined(ESP_PLATFORM)
#define SUBJECTIVE_C_HAVE_EXECINFO 1
#include <execinfo.h>
#else
#define SUBJECTIVE_C_HAVE_EXECINFO 0
#endif
#if defined(__APPLE__)
#include <pthread.h>
#endif

// Forward declaration to avoid circular dependency with exception.h
void throw_exception_formatted(const char *type, const char *file, int line, int col, const char *format, ...);

#define INDEX_NOT_FOUND (-1)
#define TRACKS_RETAINS(obj) ((obj) && !is_singleton(obj))
#define IS_SINGLETON(obj) is_singleton(obj)

#define TYPE_OF_CljList CLJ_LIST
#define TYPE_OF_CljASTNode CLJ_AST_NODE
#define TYPE_OF_CljASTCall CLJ_AST_CALL
#define TYPE_OF_CljCallsiteCache CLJ_CALLSITE_CACHE
#define TYPE_OF_CljSymbol CLJ_SYMBOL
#define TYPE_OF_CljFunction CLJ_CLOSURE
#define TYPE_OF_CljCFunc CLJ_FUNC
#define TYPE_OF_CljVector CLJ_VECTOR_PERSISTENT
#define TYPE_OF_CljPersistentMap CLJ_MAP_PERSISTENT
#define TYPE_OF_CljHashSet CLJ_HASHSET
#define TYPE_OF_CLJException CLJ_EXCEPTION
#define TYPE_OF_CljSeqIterator CLJ_SEQ
#define TYPE_OF_CljLazySeq CLJ_LAZY_SEQ
#define TYPE_OF_CljByteArray CLJ_BYTE_ARRAY
#define TYPE_OF_CljInstant CLJ_INSTANT
#define TYPE_OF_CljUUID CLJ_UUID
#define TYPE_OF_CljAtom CLJ_ATOM
#define TYPE_OF_int CLJ_FIXNUM
#define TYPE_OF_double CLJ_FLOAT
#define TYPE_OF_char CLJ_STRING
#define TYPE_OF_CljNamespace CLJ_NAMESPACE
#define TYPE_OF(struct_type) TYPE_OF_##struct_type

// Sentinel for distinguishing "not found" or "nil" from NULL pointer
extern CljObject g_not_found_sentinel;
#define NOT_FOUND (&g_not_found_sentinel)

/*
 * CljObject.flags byte layout for symbols:
 *
 *   Bit:  7   6   5   4   3   2   1   0
 *         └───┴───┘───┴───┘       │   │   │
 *         CompOp   ArithOp        │   │   └─ CLJ_FLAG_DYNAMIC (0x01)
 *         (0-3)    (0-3)          │   └───── CLJ_FLAG_ARITHMETIC (0x02)
 *                                 └───────── CLJ_FLAG_COMPARISON (0x04)
 *
 *   ArithOp: ADD=0, SUB=1, MUL=2, DIV=3
 *   CompOp:  LT=0, GT=1, LE=2, GE=3 (= handled separately)
 *
 *   Example flags:  +  → 0x02,  -  → 0x12,  *  → 0x22,  /  → 0x32
 *                   <  → 0x04,  >  → 0x44,  <= → 0x84,  >= → 0xC4
 */
#define CLJ_FLAG_DYNAMIC 0x01 // Earmuffed dynamic var symbol (e.g. *ns*)
#define CLJ_FLAG_ARITHMETIC 0x02
#define CLJ_FLAG_COMPARISON 0x04
#define CLJ_FLAG_NATIVE 0x08 // Native/builtin function (no macro lookup needed)
#define CLJ_FLAG_WEAK_ELEMENTS 0x20 // Vectors that store elements without retaining/releasing.
#define CLJ_ARITH_OP_SHIFT 4
#define CLJ_ARITH_OP_MASK 0x30
#define CLJ_COMP_OP_SHIFT 6
#define CLJ_COMP_OP_MASK 0xC0

static inline bool has_weak_elements(const CljObject *obj) {
    return obj && (obj->flags & CLJ_FLAG_WEAK_ELEMENTS) != 0;
}

static inline CljType TAG(ID obj)
{
    // Early NULL check - must come before any pointer dereference
    // Check both NULL and zero pointer explicitly
    if (!obj || (uintptr_t)obj == 0)
        return CLJ_NIL;

    // Check for immediate values (tagged pointers with LSB set)
    if ((uintptr_t)obj & 0x1)
    {
        return (CljType)((uintptr_t)obj & 0x7);
    }

    CljObject *obj_ptr = (CljObject *)obj;

    // Safety check: validate pointer before accessing type field
    // This prevents crashes when obj points to freed memory or invalid addresses
    // Double-check NULL after cast (defensive programming)
    if (!obj_ptr || (uintptr_t)obj_ptr < 0x1000)
    {
        // Invalid pointer - likely a freed object or corrupted pointer
        return CLJ_NIL;
    }

    // Access type field first (it's at the beginning of the struct, safer)
    CljType type = obj_ptr->type;
    return type;
}

static inline bool is_singleton(CljObject *obj)
{
    if (!obj || (uintptr_t)obj < 0x1000) {
        return true;
    }
    return obj->rc == SINGLETON_RC;
}

bool clj_equal(ID a, ID b);
static inline bool clj_is_truthy(ID v)
{
    if (v)
    {
        if (((uintptr_t)v & 0x7) == CLJ_BOOL)
        {
            uint8_t special = (uint8_t)((uintptr_t)v >> 3);
            if (special == 0)
                return false;
        }
        return true;
    }
    return false;
}

int reference_count(CljObject *obj);

#ifdef DEBUG
static inline void *assert_type(CljObject *obj, CljType expected_type)
{
    // Safety: validate pointer before calling TAG()
    // Use uintptr_t to avoid any pointer dereferencing during validation
    uintptr_t ptr_val = (uintptr_t)obj;

    // Check NULL and invalid pointer range without dereferencing
    if (ptr_val == 0 || ptr_val < 0x1000)
    {
        {
            char buf[256];
            (void)mini_snprintf(buf, sizeof(buf),
                                    "assert_type failed: invalid pointer %p (expected %d)\n",
                                    (void *)obj, (int)expected_type);
            fputs(buf, stderr);
        }
#if SUBJECTIVE_C_HAVE_EXECINFO
        void *trace[16];
        int trace_count = backtrace(trace, 16);
        backtrace_symbols_fd(trace, trace_count, fileno(stderr));
#endif
        return NULL;
    }

    // Check if pointer is on stack (stack pointers should not be used as objects).
    // IMPORTANT: Don't use heuristics based on the current frame address; on macOS
    // the allocator can hand out mmap'd heap addresses near the stack region and
    // a simple "$fp..$fp+8MB" check can produce false positives after many allocations.
    // Use the actual thread stack bounds when available.
#if defined(__APPLE__)
    void *stack_hi = pthread_get_stackaddr_np(pthread_self());
    size_t stack_size = pthread_get_stacksize_np(pthread_self());
    uintptr_t stack_hi_val = (uintptr_t)stack_hi;
    uintptr_t stack_lo_val = stack_hi_val - (uintptr_t)stack_size;
    if (ptr_val >= stack_lo_val && ptr_val <= stack_hi_val)
    {
        {
            char buf[256];
            (void)mini_snprintf(buf, sizeof(buf),
                                    "assert_type failed: pointer %p appears to be on stack (expected %d)\n",
                                    (void *)obj, (int)expected_type);
            fputs(buf, stderr);
        }
#if SUBJECTIVE_C_HAVE_EXECINFO
        void *trace[16];
        int trace_count = backtrace(trace, 16);
        backtrace_symbols_fd(trace, trace_count, fileno(stderr));
#endif
        return NULL;
    }
#elif defined(ESP32_BUILD) || defined(ESP_PLATFORM)
    /* On ESP32, heap and stack share internal SRAM (0x3ffxxxxx). The fp+8MB heuristic
     * would falsely flag valid heap pointers as stack and block loading (e.g. clojure.core).
     * Skip the stack check here; invalid pointers may still be caught at free() by the
     * allocator or by CONFIG_HEAP_POISONING. */
#else
    void *current_fp = __builtin_frame_address(0);
    uintptr_t fp_val = (uintptr_t)current_fp;
    if (ptr_val > fp_val && ptr_val < fp_val + (8 * 1024 * 1024))
    {
        {
            char buf[256];
            (void)mini_snprintf(buf, sizeof(buf),
                                    "assert_type failed: pointer %p appears to be on stack (expected %d)\n",
                                    (void *)obj, (int)expected_type);
            fputs(buf, stderr);
        }
#if SUBJECTIVE_C_HAVE_EXECINFO
        void *trace[16];
        int trace_count = backtrace(trace, 16);
        backtrace_symbols_fd(trace, trace_count, fileno(stderr));
#endif
        return NULL;
    }
#endif

    // Now safe to call TAG() - pointer has been validated
    // TAG() will do additional validation and handle invalid pointers gracefully
    CljType actual_type = TAG(obj);
    if (actual_type == expected_type)
    {
        return obj;
    }
    {
        char buf[128];
        (void)mini_snprintf(buf, sizeof(buf),
                                "assert_type failed: expected %d actual %d\n",
                                (int)expected_type, (int)actual_type);
        fputs(buf, stderr);
    }
#if SUBJECTIVE_C_HAVE_EXECINFO
    void *trace[16];
    int trace_count = backtrace(trace, 16);
    backtrace_symbols_fd(trace, trace_count, fileno(stderr));
#endif
    CLJ_ASSERT(0 && "Type assertion failed");
    return NULL;
}
#else
// Release build: zero-overhead cast
static inline void *assert_type(CljObject *obj, CljType expected_type)
{
    (void)expected_type;
    return obj;
}
#endif

#endif // SUBJECTIVE_C_OBJECT_H
