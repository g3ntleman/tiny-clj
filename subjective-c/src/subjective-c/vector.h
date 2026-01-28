#ifndef SUBJECTIVE_C_VECTOR_H
#define SUBJECTIVE_C_VECTOR_H

#include "object.h"
#include "common.h"

// Plan: remove shared `struct CljVector` layout. Keep only:
// - CljPersistentVector: TAG CLJ_VECTOR_PERSISTENT or CLJ_VECTOR_TRANSIENT_WEAK, owns `data[]`.
// - CljTransientVector: wrapper (TAG CLJ_VECTOR_TRANSIENT) with `backing` (always persistent tag).
//   Transient mutation API: vector_push, vector_pop, clj_conj, and vector_set_nth_transient.

typedef struct CljPersistentVector CljPersistentVector;

typedef struct CljTransientVector {
    CljObject base;                 // type == CLJ_VECTOR_TRANSIENT
    CljPersistentVector *backing;   // always TAG CLJ_VECTOR_PERSISTENT (never WEAK)
} CljTransientVector;

static inline bool is_persistent_vector(ID obj) {
    if (!obj) return false;
    CljType tag = TAG(obj);
    return (tag == CLJ_VECTOR_PERSISTENT || tag == CLJ_VECTOR_TRANSIENT_WEAK);
}

static inline bool is_transient_vector(ID obj) {
    return obj && TAG(obj) == CLJ_VECTOR_TRANSIENT;
}

static inline bool is_vector(ID obj) {
    if (!obj) return false;
    CljType tag = TAG(obj);
    return (tag == CLJ_VECTOR_PERSISTENT || tag == CLJ_VECTOR_TRANSIENT_WEAK || tag == CLJ_VECTOR_TRANSIENT);
}

static inline CljPersistentVector* as_persistent_vector(ID obj) {
#ifdef DEBUG
    CLJ_ASSERT(obj == NULL || is_persistent_vector(obj));
#endif
    return (CljPersistentVector*)obj;
}

static inline CljTransientVector* as_transient_vector(ID obj) {
#ifdef DEBUG
    CLJ_ASSERT(obj == NULL || TAG(obj) == CLJ_VECTOR_TRANSIENT);
#endif
    return (CljTransientVector*)obj;
}

unsigned int vector_count(CljPersistentVector *vec);
unsigned int vector_capacity(CljPersistentVector *vec);
ID vector_nth(CljPersistentVector *vec, unsigned int index);
int vector_index_of(CljPersistentVector *vec, ID value);
ID* vector_as_array(CljPersistentVector *vec);

extern CljPersistentVector* vector_empty_singleton;
CljPersistentVector* empty_vector(void);
/** Create a vector with given capacity.
 * If weakElements is true, vector stores elements without retaining/releasing them.
 * Returns empty-vector singleton if capacity == 0 and weakElements == false.
 */
CljPersistentVector* make_vector(unsigned int capacity, bool weakElements);

CljPersistentVector* vector_conj(CljPersistentVector* vec, ID item);
/** Append; returns owned (no AUTORELEASE). Use ASSIGN(slot, vector_conj_owned(slot, item)) to update. */
CljPersistentVector* vector_conj_owned(CljPersistentVector* vec, ID item);
CljPersistentVector* vector_assoc(CljPersistentVector* vec, unsigned int index, ID value);
CljPersistentVector* vector_set_nth(CljPersistentVector* vec, unsigned int index, ID value);
CljPersistentVector* make_vector_copy(CljPersistentVector* vec, unsigned capacity);
CljPersistentVector* vector_popped(CljPersistentVector* vec);
/** Returns owned (no AUTORELEASE). Use with ASSIGN for slot update. */
CljPersistentVector* vector_popped_owned(CljPersistentVector* vec);
CljPersistentVector* vector_insert_at(CljPersistentVector* vec, unsigned int index, ID item);
CljPersistentVector* vector_remove_at(CljPersistentVector* vec, unsigned int index);
void vector_clear(CljPersistentVector *vec);
/** n<=count; caller releases [n,count) when vec retains. */
void vector_truncate(CljPersistentVector *vec, unsigned int n);

// Transient wrapper API (wrapper pointer remains stable; `backing` may be replaced/grown).
CljTransientVector* vector_transient(CljPersistentVector *vec);
CljPersistentVector* vector_persistent(CljTransientVector *tvec);
// (internal) backing replacement helper is intentionally not part of the public API.

/** Push item to end of transient vector (in-place mutation).
 * Only works on transient vectors. Throws exception if called on persistent vector.
 * @param tvec Transient vector (must be CLJ_VECTOR_TRANSIENT)
 * @param item Item to push (can be NULL/nil)
 */
void vector_push(CljTransientVector *tvec, ID item);

/** Pop last item from transient vector (in-place mutation).
 * Only works on transient vectors. Throws exception if called on persistent vector.
 * @param tvec Transient vector (must be CLJ_VECTOR_TRANSIENT)
 */
void vector_pop(CljTransientVector *tvec);

/** Set element at index in transient vector (in-place via backing).
 * Uses persistent COW semantics under the hood:
 * - Within bounds: updates element at index.
 * - Out of bounds: throws index-out-of-bounds exception.
 */
void vector_set_nth_transient(CljTransientVector *tvec, unsigned int index, ID value);

size_t vector_make_copy_count(void);
void vector_make_copy_count_reset(void);

void vector_conj_inplace(CljPersistentVector **vec_slot, ID item);
void vector_assoc_inplace(CljPersistentVector **vec_slot, unsigned int index, ID value);
void vector_insert_at_inplace(CljPersistentVector **vec_slot, unsigned int index, ID item);
void vector_remove_at_inplace(CljPersistentVector **vec_slot, unsigned int index);
void vector_pop_inplace(CljPersistentVector **vec_slot);

#define VECTOR_FOR_EACH(vector, elem_var) \
    for (int _i = 0, _cnt = vector_count(vector); (vector) && _i < _cnt; ++_i) \
        for (ID *_data_ptr = vector_as_array(vector); _data_ptr; _data_ptr = NULL) \
            for (ID elem_var = _data_ptr[_i]; _data_ptr; _data_ptr = NULL)

#endif // SUBJECTIVE_C_VECTOR_H
