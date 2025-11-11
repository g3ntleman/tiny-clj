#ifndef TINY_CLJ_VECTOR_H
#define TINY_CLJ_VECTOR_H

#include "object.h"
#include "seq.h"  // For SeqIterator

// CljPersistentVector is an opaque pointer - structure definition is in vector.c
typedef struct CljPersistentVector CljPersistentVector;

// Type alias for convenience

// Type-safe casting
static inline CljPersistentVector* as_vector(ID obj) {
    if (!obj || (TAG(obj) != CLJ_VECTOR && TAG(obj) != CLJ_WEAK_VECTOR && TAG(obj) != CLJ_TRANSIENT_VECTOR)) {
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

// === CljValue API ===
/** Empty vector singleton (rc=0, do not retain/release). */
extern CljPersistentVector* empty_vector_singleton;
/** Return empty vector singleton (rc=0, do not retain/release). */
ID empty_vector(void);
/** Create a vector with given capacity; capacity<=0 returns empty-vector singleton. */
CljPersistentVector* make_vector(unsigned int capacity, bool is_mutable);
/** Return a new vector with item appended; original vector remains unchanged.
 * Uses Copy-on-Write: RC=1 → in-place mutation, RC>1 → COW.
 */
CljPersistentVector* vector_conj(CljPersistentVector* vec, ID item);
/** Update element at index with COW: RC=1 → in-place mutation, RC>1 → COW. */
CljPersistentVector* vector_assoc(CljPersistentVector* vec, int index, ID value);
/** Grow vector capacity in-place (for RC=1 or transient vectors).
 * @param v Vector to grow
 * @note Throws exception on OOM
 */
void vector_grow_capacity(CljPersistentVector *v);
/** Get vector count. Returns 0 if vec is NULL. */
int vector_count(CljPersistentVector *vec);
/** Get element at index. Returns retained element or NULL if index out of bounds or nil. */
ID vector_nth(CljPersistentVector *vec, int index);
/** Set element at index. Returns new vector with updated element (COW if needed). */
CljPersistentVector* vector_set_nth(CljPersistentVector* vec, int index, ID value);
/** Initialize seq iterator for vector (internal use by seq.c). */
bool vector_init_seq_iterator(SeqIterator *iter, CljPersistentVector *vec);
/** Get element at index without RETAIN (internal use for seq iterator). */
ID vector_get_element_no_retain(CljPersistentVector *vec, int index);

// === Transient API (Phase 2) ===
/** Convert persistent vector to transient. */
ID transient(ID vec);
/** Append to transient vector (guaranteed in-place). */
ID clj_conj(ID tvec, ID item);
/** Convert transient vector back to persistent. */
ID persistent(ID tvec);

#endif

