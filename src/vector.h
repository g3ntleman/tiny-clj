#ifndef TINY_CLJ_VECTOR_H
#define TINY_CLJ_VECTOR_H

#include "object.h"

// CljPersistentVector struct definition
typedef struct {
    CljObject base;
    int count;
    int capacity;
    // mutable_flag removed: COW (RC-based) handles mutability automatically
    CljObject **data;
} CljPersistentVector;

// Type alias for convenience
typedef CljPersistentVector* CljVector;

// Type-safe casting
static inline CljPersistentVector* as_vector(ID obj) {
    if (obj) {
        int tag = TAG(obj);
        if (tag == CLJ_VECTOR || tag == CLJ_WEAK_VECTOR || tag == CLJ_TRANSIENT_VECTOR) {
            return (CljPersistentVector*)obj;
        }
    }
#ifdef DEBUG
    fprintf(stderr, "Assertion failed: Expected Vector, got %s at %s:%d\n", 
            obj ? "invalid type" : "NULL", __FILE__, __LINE__);
#endif
    abort();
}

// === Legacy API removed - use CljValue API instead ===

// === CljValue API ===
/** Empty vector singleton (rc=0, do not retain/release). */
extern CljPersistentVector* empty_vector_singleton;
/** Return empty vector singleton (rc=0, do not retain/release). */
CljPersistentVector* empty_vector(void);
/** Create a vector with given capacity; capacity<=0 returns empty-vector singleton. */
CljVector make_vector(unsigned int capacity, bool is_mutable);
/** Return a new vector with item appended; original vector remains unchanged.
 * Uses Copy-on-Write: RC=1 → in-place mutation, RC>1 → COW.
 */
CljVector vector_conj(CljVector vec, ID item);
/** Update element at index with COW: RC=1 → in-place mutation, RC>1 → COW. */
CljVector vector_assoc(CljVector vec, int index, ID value);
/** Grow vector capacity in-place (for RC=1 or transient vectors).
 * @param v Vector to grow
 * @note Throws exception on OOM
 */
void vector_grow_capacity(CljPersistentVector *v);

// === Transient API (Phase 2) ===
/** Convert persistent vector to transient. */
ID transient(ID vec);
/** Append to transient vector (guaranteed in-place). */
ID clj_conj(ID tvec, ID item);
/** Convert transient vector back to persistent. */
ID persistent(ID tvec);

#endif

