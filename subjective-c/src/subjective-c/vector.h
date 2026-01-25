#ifndef SUBJECTIVE_C_VECTOR_H
#define SUBJECTIVE_C_VECTOR_H

#include "object.h"
#include "common.h"

typedef struct CljVector CljVector;

static inline CljVector* as_vector(ID obj) {
#ifdef DEBUG
    CljType tag;
    CLJ_ASSERT(((tag = TAG(obj)), (obj == NULL || tag == CLJ_VECTOR || tag == CLJ_VECTOR_TRANSIENT_WEAK || tag == CLJ_VECTOR_TRANSIENT)));
#endif
    return (CljVector*)obj;
}

static inline bool is_vector(CljObject *obj) {
    if (!obj) return false;
    int tag = (int)TAG(obj);
    return (tag == CLJ_VECTOR || tag == CLJ_VECTOR_TRANSIENT_WEAK || tag == CLJ_VECTOR_TRANSIENT);
}

extern CljVector* vector_empty_singleton;
CljVector* empty_vector(void);
CljVector* make_vector(unsigned int capacity, CljType type);
CljVector* vector_conj(CljVector* vec, ID item);
/** Append; returns owned (no AUTORELEASE). Use ASSIGN(slot, vector_conj_owned(slot, item)) to update. */
CljVector* vector_conj_owned(CljVector* vec, ID item);
CljVector* vector_assoc(CljVector* vec, unsigned int index, ID value);
unsigned int vector_count(CljVector *vec);
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
ID vector_persistent(CljVector *tvec);

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
