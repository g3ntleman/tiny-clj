#ifndef TINY_CLJ_VECTOR_H
#define TINY_CLJ_VECTOR_H

#include "object.h"

// CljPersistentVector struct definition
typedef struct {
    CljObject base;
    int count;
    int capacity;
    int mutable_flag;
    CljObject **data;
} CljPersistentVector;

// Type-safe casting
static inline CljPersistentVector* as_vector(ID obj) {
    if (!is_type((CljObject*)obj, CLJ_VECTOR) && !is_type((CljObject*)obj, CLJ_WEAK_VECTOR) && !is_type((CljObject*)obj, CLJ_TRANSIENT_VECTOR)) {
#ifdef DEBUG
        const char *actual_type = obj ? "Vector" : "NULL";
        fprintf(stderr, "Assertion failed: Expected Vector, got %s at %s:%d\n", 
                actual_type, __FILE__, __LINE__);
#endif
        abort();
    }
    return (CljPersistentVector*)obj;
}

// === Legacy API removed - use CljValue API instead ===

// === Neue CljValue API (Phase 1: Parallel) ===
/** Create a vector with given capacity; capacity<=0 returns empty-vector singleton. */
CljValue make_vector(unsigned int capacity, bool is_mutable);
/** Return a new vector with item appended; original vector remains unchanged. */
CljValue vector_conj(CljValue vec, CljValue item);

// === Transient API (Phase 2) ===
/** Convert persistent vector to transient. */
CljValue transient(CljValue vec);
/** Append to transient vector (guaranteed in-place). */
CljValue clj_conj(CljValue tvec, CljValue item);
/** Convert transient vector back to persistent. */
CljValue persistent(CljValue tvec);

#endif

