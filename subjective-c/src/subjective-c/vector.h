#ifndef SUBJECTIVE_C_VECTOR_H
#define SUBJECTIVE_C_VECTOR_H

#include "object.h"
#include "common.h"

typedef struct CljVector CljVector;
typedef struct CljVector CljPersistentVector;

typedef struct CljTransientVector {
    CljObject base;
    CljPersistentVector *backing_store;
} CljTransientVector;

unsigned int vector_count(CljVector *vec);
int vector_capacity(CljVector *vec);

static inline CljVector* as_vector(ID obj) {
#ifdef DEBUG
    CljType tag = TAG(obj);
    CLJ_ASSERT(obj == NULL || tag == CLJ_VECTOR_PERSISTENT || tag == CLJ_VECTOR_TRANSIENT_WEAK);
#endif
    return (CljVector*)obj;
}

static inline CljTransientVector* as_transient_vector(ID obj) {
#ifdef DEBUG
    CljType tag = TAG(obj);
    CLJ_ASSERT(obj == NULL || tag == CLJ_VECTOR_TRANSIENT);
#endif
    return (CljTransientVector*)obj;
}

static inline bool is_vector(CljObject *obj) {
    if (!obj) return false;
    int tag = (int)TAG(obj);
    return (tag == CLJ_VECTOR_PERSISTENT || tag == CLJ_VECTOR_TRANSIENT);
}

static inline bool is_transient_vector(CljObject *obj) {
    return obj && TAG(obj) == CLJ_VECTOR_TRANSIENT;
}

static inline CljVector* transient_vector_backing_store(CljTransientVector *tvec) {
    CLJ_ASSERT(tvec && tvec->base.type == CLJ_VECTOR_TRANSIENT);
    return (CljVector*)tvec->backing_store;
}

static inline unsigned int transient_vector_count(CljTransientVector *tvec) {
    CLJ_ASSERT(tvec && tvec->base.type == CLJ_VECTOR_TRANSIENT);
    return vector_count(tvec->backing_store);
}

static inline int transient_vector_capacity(CljTransientVector *tvec) {
    CLJ_ASSERT(tvec && tvec->base.type == CLJ_VECTOR_TRANSIENT);
    return vector_capacity(tvec->backing_store);
}

extern CljVector* vector_empty_singleton;
CljVector* empty_vector(void);
/** Create a vector with given capacity and type. Returns empty-vector singleton if capacity == 0 and type == CLJ_VECTOR_PERSISTENT. */
CljVector* make_vector(unsigned int capacity, CljType type);
/** Create a weak transient vector for internal use (e.g., autorelease pool). */
CljVector* make_vector_weak(unsigned int capacity);
CljVector* vector_conj(CljVector* vec, ID item);
/** Append; returns owned (no AUTORELEASE). Use ASSIGN(slot, vector_conj_owned(slot, item)) to update. */
CljVector* vector_conj_owned(CljVector* vec, ID item);
CljVector* vector_assoc(CljVector* vec, unsigned int index, ID value);
ID vector_nth(CljVector *vec, unsigned int index);
int vector_index_of(CljVector *vec, ID value);
CljVector* vector_set_nth(CljVector* vec, unsigned int index, ID value);
CljVector* make_vector_copy(CljVector* vec, unsigned capacity);
CljVector* vector_pop(CljVector* vec);
/** Returns owned (no AUTORELEASE). Use with ASSIGN for slot update. */
CljVector* vector_pop_owned(CljVector* vec);
CljVector* vector_insert_at(CljVector* vec, unsigned int index, ID item);
CljVector* vector_remove_at(CljVector* vec, unsigned int index);
ID* vector_as_array(CljVector *vec);
void vector_increment_count(CljVector *vec);
void vector_clear(CljVector *vec);
/** n<=count; caller releases [n,count) when vec retains. */
void vector_truncate(CljVector *vec, unsigned int n);
CljVector* vector_transient(CljVector *vec);
CljVector* clj_conj(CljVector *tvec, ID item);
CljPersistentVector* vector_persistent(CljTransientVector *tvec);

/** Push item to end of transient vector (in-place mutation).
 * Only works on transient vectors. Throws exception if called on persistent vector.
 * @param tvec Transient vector (must be CLJ_VECTOR_TRANSIENT)
 * @param item Item to push (can be NULL/nil)
 * @return Same transient vector pointer (always in-place mutation)
 */
CljVector* vector_push(CljVector *tvec, ID item);

/** Pop last item from transient vector (in-place mutation).
 * Only works on transient vectors. Throws exception if called on persistent vector.
 * @param tvec Transient vector (must be CLJ_VECTOR_TRANSIENT)
 * @return Same transient vector pointer (always in-place mutation)
 */
CljVector* vector_pop_transient(CljVector *tvec);

size_t vector_make_copy_count(void);
void vector_make_copy_count_reset(void);

void vector_conj_inplace(CljVector **vec_slot, ID item);
void vector_assoc_inplace(CljVector **vec_slot, unsigned int index, ID value);
void vector_insert_at_inplace(CljVector **vec_slot, unsigned int index, ID item);
void vector_remove_at_inplace(CljVector **vec_slot, unsigned int index);
void vector_pop_inplace(CljVector **vec_slot);

#define VECTOR_FOR_EACH(vector, elem_var) \
    for (int _i = 0, _cnt = vector_count(vector); (vector) && _i < _cnt; ++_i) \
        for (ID *_data_ptr = vector_as_array(vector); _data_ptr; _data_ptr = NULL) \
            for (ID elem_var = _data_ptr[_i]; _data_ptr; _data_ptr = NULL)

#endif // SUBJECTIVE_C_VECTOR_H
