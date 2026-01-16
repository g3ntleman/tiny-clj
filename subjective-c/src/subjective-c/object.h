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

#include "common.h"
#include <stdio.h>
#if !defined(ESP32_BUILD)
#include <execinfo.h>
#endif
#if defined(__APPLE__)
#include <pthread.h>
#endif

// Forward declaration to avoid circular dependency with exception.h
void *throw_exception_formatted(const char *type, const char *file, int line, int col, const char *format, ...);

#define INDEX_NOT_FOUND (-1)
#define TRACKS_RETAINS(obj) ((obj) && !is_singleton(obj))
#define IS_SINGLETON(obj) is_singleton(obj)

#define TYPE_OF_CljList CLJ_LIST
#define TYPE_OF_CljASTNode CLJ_AST_NODE
#define TYPE_OF_CljCallsiteCache CLJ_CALLSITE_CACHE
#define TYPE_OF_CljSymbol CLJ_SYMBOL
#define TYPE_OF_CljFunction CLJ_CLOSURE
#define TYPE_OF_CljCFunc CLJ_FUNC
#define TYPE_OF_CljVector CLJ_VECTOR
#define TYPE_OF_CljPersistentMap CLJ_MAP
#define TYPE_OF_CljMap CLJ_MAP
#define TYPE_OF_CLJException CLJ_EXCEPTION
#define TYPE_OF_CljSeqIterator CLJ_SEQ
#define TYPE_OF_CljByteArray CLJ_BYTE_ARRAY
#define TYPE_OF_CljInstant CLJ_INSTANT
#define TYPE_OF_CljUUID CLJ_UUID
#define TYPE_OF_CljAtom CLJ_ATOM
#define TYPE_OF_int CLJ_INT
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
#define CLJ_ARITH_OP_SHIFT 4
#define CLJ_ARITH_OP_MASK 0x30
#define CLJ_COMP_OP_SHIFT 6
#define CLJ_COMP_OP_MASK 0xC0

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
    // This will crash if pointer is invalid, but that's better than silent corruption
    CljType type = obj_ptr->type;

#ifdef DEBUG
    // Detect use-after-free: check if object is a zombie (freed but not deallocated)
    // Check zombie status AFTER accessing type (type is at offset 0, rc is later)
    // If zombie mode is enabled, objects are marked as zombies when freed
    // This check will catch use-after-free errors before they cause corruption
#ifdef ZOMBIE_ENABLED
    // In zombie mode, rc=0 means the object is a zombie (freed but not DEALLOCed)
    // Access rc field - if this crashes, it means the pointer was invalid
    // and we would have crashed anyway when accessing type
    if (obj_ptr->rc == 0)
    {
        // This is a use-after-free error! The object was freed but we're still accessing it.
        // Report the error with detailed information
        const char *type_name = "unknown";
        if (type < CLJ_TYPE_COUNT)
        {
            type_name = clj_type_name(type);
        }
        throw_exception_formatted("ZombieAccessError", __FILE__, __LINE__, 0,
                                  "Use-after-free detected: Attempted to access freed object %p (type=%s, rc=0). "
                                  "This object was freed but is still being accessed. Check for dangling pointers or missing RETAIN.",
                                  obj_ptr, type_name);
        // Return a safe default to prevent further crashes
        return CLJ_NIL;
    }
#endif
#endif

    // Return the type we already accessed
    return type;
}

static inline bool is_singleton(CljObject *obj)
{
    if (!obj || (uintptr_t)obj < 0x1000)
    {
        return true;
    }
#ifdef DEBUG
    if (obj->rc == 0)
    {
        return false;
    }
#endif
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
            (void)clj_mini_snprintf(buf, sizeof(buf),
                                    "assert_type failed: invalid pointer %p (expected %d)\n",
                                    (void *)obj, (int)expected_type);
            fputs(buf, stderr);
        }
        void *trace[16];
        int trace_count = backtrace(trace, 16);
        backtrace_symbols_fd(trace, trace_count, fileno(stderr));
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
            (void)clj_mini_snprintf(buf, sizeof(buf),
                                    "assert_type failed: pointer %p appears to be on stack (expected %d)\n",
                                    (void *)obj, (int)expected_type);
            fputs(buf, stderr);
        }
        void *trace[16];
        int trace_count = backtrace(trace, 16);
        backtrace_symbols_fd(trace, trace_count, fileno(stderr));
        return NULL;
    }
#else
    void *current_fp = __builtin_frame_address(0);
    uintptr_t fp_val = (uintptr_t)current_fp;
    if (ptr_val > fp_val && ptr_val < fp_val + (8 * 1024 * 1024))
    {
        {
            char buf[256];
            (void)clj_mini_snprintf(buf, sizeof(buf),
                                    "assert_type failed: pointer %p appears to be on stack (expected %d)\n",
                                    (void *)obj, (int)expected_type);
            fputs(buf, stderr);
        }
        void *trace[16];
        int trace_count = backtrace(trace, 16);
        backtrace_symbols_fd(trace, trace_count, fileno(stderr));
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
        (void)clj_mini_snprintf(buf, sizeof(buf),
                                "assert_type failed: expected %d actual %d\n",
                                (int)expected_type, (int)actual_type);
        fputs(buf, stderr);
    }
    void *trace[16];
    int trace_count = backtrace(trace, 16);
    backtrace_symbols_fd(trace, trace_count, fileno(stderr));
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
