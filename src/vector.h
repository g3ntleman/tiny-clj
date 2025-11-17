#ifndef TINY_CLJ_VECTOR_H
#define TINY_CLJ_VECTOR_H

#include "object.h"
#include "seq.h"  // For SeqIterator

// CljVector is an opaque pointer - structure definition is in vector.c
typedef struct CljVector CljVector;

// Type alias for convenience

// Type-safe casting
static inline CljVector* as_vector(ID obj) {
    if (obj) {
        int tag = TAG(obj);
        if (tag == CLJ_VECTOR || tag == CLJ_VECTOR_WEAK || tag == CLJ_VECTOR_TRANSIENT) {
            return (CljVector*)obj;
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
extern CljVector* vector_empty_singleton;
/** Return empty vector singleton (rc=0, do not retain/release). */
CljVector* empty_vector(void);
/** Create a vector with given capacity; capacity<=0 returns empty-vector singleton.
 * @param capacity Initial capacity (0 returns empty singleton for CLJ_VECTOR)
 * @param type Vector type (CLJ_VECTOR or CLJ_VECTOR_WEAK)
 */
CljVector* make_vector(unsigned int capacity, CljType type);
/** Return a new vector with item appended; original vector remains unchanged.
 * Uses Copy-on-Write: RC=1 → in-place mutation, RC>1 → COW.
 * Supports both CLJ_VECTOR and CLJ_VECTOR_WEAK (weak references don't RETAIN).
 */
CljVector* vector_conj(CljVector* vec, ID item);
/** Append to transient vector (guaranteed in-place, may return new vector if capacity grows).
 * @param tvec Transient vector to append to
 * @param item Item to append
 * @return Updated transient vector (may be new vector if capacity grew)
 */
CljVector* vector_conj_bang(CljVector* tvec, ID item);
/** Update element at index with COW: RC=1 → in-place mutation, RC>1 → COW. */
CljVector* vector_assoc(CljVector* vec, unsigned int index, ID value);
/** Grow vector capacity in-place (for RC=1 or transient vectors).
 * @param v Vector to grow
 * @note Throws exception on OOM
 */
/** Get vector count. Returns 0 if vec is NULL. */
unsigned int vector_count(CljVector *vec);
/** Get element at index. Returns retained element or NULL if index out of bounds or nil. */
ID vector_nth(CljVector *vec, unsigned int index);
/** Set element at index. Returns new vector with updated element (COW if needed). */
CljVector* vector_set_nth(CljVector* vec, unsigned int index, ID value);
/** Copy vector with specified capacity. */
CljVector* make_vector_copy(CljVector* vec, unsigned capacity);
/** Remove last element from vector (in-place if RC=1, COW if RC>1). */
CljVector* vector_pop(CljVector* vec);
/** Insert element at index in vector (in-place if RC=1, COW if RC>1).
 * @param vec Vector to insert into
 * @param index Index where to insert (0-based, must be <= count)
 * @param item Item to insert
 * @return Same vector (in-place) if RC=1, new vector if RC>1, or NULL on error
 */
CljVector* vector_insert_at(CljVector* vec, unsigned int index, ID item);
/** Remove element at index from vector (in-place if RC=1, COW if RC>1).
 * @param vec Vector to remove from
 * @param index Index of element to remove (0-based)
 * @return Same vector (in-place) if RC=1, new vector if RC>1, or NULL on error
 */
CljVector* vector_remove_at(CljVector* vec, unsigned int index);
/** Initialize seq iterator for vector (internal use by seq.c). */
bool vector_init_seq_iterator(SeqIterator *iter, CljVector *vec);
/** Get raw data array pointer (no copying, direct access). */
ID* vector_as_array(CljVector *vec);
/** Increment count for transient vectors (internal use only). */
void vector_increment_count(CljVector *vec);
/** Set count to zero for transient vectors (internal use only). */
void vector_reset_count(CljVector *vec);
/** Clear vector by setting count to zero (only for CLJ_VECTOR_WEAK or CLJ_VECTOR_TRANSIENT). */
void vector_clear(CljVector *vec);

// === Transient API (Phase 2) ===
/** Convert persistent vector to transient. */
CljVector* vector_transient(CljVector *vec);
/** Append to transient vector (guaranteed in-place). */
CljVector* clj_conj(CljVector *tvec, ID item);
/** Convert transient vector back to persistent. */
ID vector_persistent(CljVector *tvec);

/* Verwendung: VECTOR_FOR_EACH(vec, elem) { ... } */
#define VECTOR_FOR_EACH(vector, elem_var) \
    for (int _i = 0, _cnt = vector_count(vector); (vector) && _i < _cnt; ++_i) \
        for (ID *_data_ptr = vector_as_array(vector); _data_ptr; _data_ptr = NULL) \
            for (ID elem_var = _data_ptr[_i]; _data_ptr; _data_ptr = NULL)

#endif
