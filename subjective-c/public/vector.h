#ifndef SUBJECTIVE_C_VECTOR_H
#define SUBJECTIVE_C_VECTOR_H

#include "object.h"
#include "common.h"
#include "seq.h"

typedef struct CljVector CljVector;

// Offsets for inline accessors (computed via offsetof in vector.c)
extern const unsigned int cljvector_count_offset;
extern const unsigned int cljvector_data_offset;

static inline unsigned int vector_count(CljVector *vec) {
    if (!vec) return 0;
    return *(unsigned int*)((uint8_t*)vec + cljvector_count_offset);
}

static inline ID* vector_as_array(CljVector *vec) {
    return (ID*)((uint8_t*)vec + cljvector_data_offset);
}

#ifdef DEBUG
static inline CljVector* as_vector(ID obj) {
    if (obj) {
        CljType tag = TAG(obj);
        if (tag == CLJ_VECTOR || tag == CLJ_VECTOR_TRANSIENT_WEAK || tag == CLJ_VECTOR_TRANSIENT) {
            return (CljVector*)obj;
        }
    }
    CLJ_ASSERT(0 && "Expected Vector type");
    return NULL;
}
#else
// Release: zero-overhead cast
static inline CljVector* as_vector(ID obj) {
    return (CljVector*)obj;
}
#endif

static inline bool is_vector(CljObject *obj) {
    if (!obj) return false;
    CljType tag = TAG(obj);
    return tag == CLJ_VECTOR || tag == CLJ_VECTOR_TRANSIENT_WEAK || tag == CLJ_VECTOR_TRANSIENT;
}

extern CljVector* vector_empty_singleton;
CljVector* empty_vector(void);
CljVector* make_vector(unsigned int capacity, CljType type);
CljVector* vector_conj(CljVector* vec, ID item);
CljVector* vector_assoc(CljVector* vec, unsigned int index, ID value);
ID vector_nth(CljVector *vec, unsigned int index);
int vector_index_of(CljVector *vec, ID value);
CljVector* vector_set_nth(CljVector* vec, unsigned int index, ID value);
CljVector* make_vector_copy(CljVector* vec, unsigned capacity);
CljVector* vector_pop(CljVector* vec);
CljVector* vector_insert_at(CljVector* vec, unsigned int index, ID item);
CljVector* vector_remove_at(CljVector* vec, unsigned int index);
bool vector_init_seq_iterator(SeqIterator *iter, CljVector *vec);
void vector_increment_count(CljVector *vec);
void vector_clear(CljVector *vec);
CljVector* vector_transient(CljVector *vec);
CljVector* clj_conj(CljVector *tvec, ID item);
ID vector_persistent(CljVector *tvec);

// Optimized: vector_as_array called once (cached in _data)
// Note: _i is available for code that needs the index
#define VECTOR_FOR_EACH(vector, elem_var) \
    for (ID *_data = (vector) ? vector_as_array(vector) : NULL; _data; _data = NULL) \
        for (int _i = 0, _cnt = vector_count(vector); _i < _cnt; ++_i) \
            for (ID elem_var = _data[_i], *_once = (ID*)1; _once; _once = NULL)

#endif // SUBJECTIVE_C_VECTOR_H
