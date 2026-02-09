#ifndef SUBJECTIVE_C_VECTOR_H
#define SUBJECTIVE_C_VECTOR_H

#include "object.h"
#include "common.h"

// Plan: remove shared `struct CljVector` layout. Keep only:
// - CljPersistentVector: TAG CLJ_VECTOR_PERSISTENT, owns `data[]`.
// - CljTransientVector: wrapper (TAG CLJ_VECTOR_TRANSIENT) with `backing` (always persistent tag).
//   Transient mutation API: vector_push, vector_pop, vector_set_nth_transient, vector_remove_at, vector_insert_at.

typedef struct CljPersistentVector CljPersistentVector;

typedef struct CljTransientVector {
    CljObject base;                 // type == CLJ_VECTOR_TRANSIENT
    CljPersistentVector *backing;   // always TAG CLJ_VECTOR_PERSISTENT (never WEAK)
} CljTransientVector;

static inline bool is_persistent_vector(ID obj) {
    if (!obj) return false;
    CljType tag = TAG(obj);
    return (tag == CLJ_VECTOR_PERSISTENT);
}

static inline bool is_transient_vector(ID obj) {
    return obj && TAG(obj) == CLJ_VECTOR_TRANSIENT;
}

static inline bool is_vector(ID obj) {
    if (!obj) return false;
    CljType tag = TAG(obj);
    return (tag == CLJ_VECTOR_PERSISTENT || tag == CLJ_VECTOR_TRANSIENT);
}

static inline CljPersistentVector* as_persistent_vector(ID obj) {
#ifdef DEBUG
    CLJ_ASSERT(obj == NULL || is_persistent_vector(obj));
#endif
    return (CljPersistentVector*)obj;
}

// Forward declaration for compatibility helper.
/** @brief Convert transient vector to persistent snapshot
 * @param tvec Transient vector to convert
 * @return Persistent vector (borrowed reference, RETAIN if keeping)
 */
CljPersistentVector* vector_persistent(CljTransientVector *tvec);

static inline CljPersistentVector* as_vector(ID obj) {
    if (!obj) return NULL;
    CljType tag = TAG(obj);
    if (tag == CLJ_VECTOR_TRANSIENT) {
        return vector_persistent((CljTransientVector*)obj);
    }
    return as_persistent_vector(obj);
}

static inline CljTransientVector* as_transient_vector(ID obj) {
#ifdef DEBUG
    CLJ_ASSERT(obj == NULL || TAG(obj) == CLJ_VECTOR_TRANSIENT);
#endif
    return (CljTransientVector*)obj;
}


/** @brief Get number of elements in vector
 * @param vec Vector to count
 * @return Number of elements
 */
unsigned int vector_count(CljPersistentVector *vec);

/** @brief Get capacity (allocated size) of vector
 * @param vec Vector to query
 * @return Allocated capacity
 */
unsigned int vector_capacity(CljPersistentVector *vec);


/** @brief Get element at index
 * @param vec Vector to access
 * @param index Index of element (must be < count)
 * @return Element at index
 */
ID vector_nth(CljPersistentVector *vec, unsigned int index);

/** @brief Find index of value in vector
 * @param vec Vector to search
 * @param value Value to find (uses clj_equal)
 * @return Index of first occurrence or -1 if not found
 */
int vector_index_of(CljPersistentVector *vec, ID value);

/** @brief Get direct pointer to internal array
 * @param vec Vector to access
 * @return Pointer to internal array (do not modify)
 */
ID* vector_as_array(CljPersistentVector *vec);

extern CljPersistentVector* vector_empty_singleton;

/** @brief Get empty vector singleton
 * @return Singleton empty vector
 */
CljPersistentVector* empty_vector(void);
/** @brief Size that make_vector would request for a given capacity (for waste tests). */
size_t vector_requested_allocation_size(unsigned int capacity);

/** Create a vector with given capacity.
 * weak stores elements without retaining/releasing them
 * and sets CLJ_FLAG_WEAK_ELEMENTS on the vector object.
 * Returns empty-vector singleton if capacity == 0 and retention is strong.
 */
CljPersistentVector* make_vector(unsigned int capacity, ElementRetention retention);

/** @brief Append item to vector (persistent, returns new vector)
 * @param vec Source vector
 * @param item Item to append
 * @return New vector with item appended (AUTORELEASE'd)
 */
CljPersistentVector* vector_conj(CljPersistentVector* vec, ID item);

/** @brief Append item to vector, returns owned reference
 * @param vec Source vector
 * @param item Item to append
 * @return New vector (owned, no AUTORELEASE)
 */
CljPersistentVector* vector_conj_owned(CljPersistentVector* vec, ID item);

/** @brief Update element at index (persistent, returns new vector)
 * @param vec Source vector
 * @param index Index to update
 * @param value New value
 * @return New vector with updated element (AUTORELEASE'd)
 */
CljPersistentVector* vector_assoc(CljPersistentVector* vec, unsigned int index, ID value);

/** @brief Update element at index (persistent, returns new vector)
 * @param vec Source vector
 * @param index Index to update
 * @param value New value
 * @return New vector with updated element (AUTORELEASE'd)
 */
CljPersistentVector* vector_set_nth(CljPersistentVector* vec, unsigned int index, ID value);

/** @brief Create copy of vector with specified capacity
 * @param vec Source vector
 * @param capacity Capacity for new vector
 * @return New vector copy
 */
CljPersistentVector* make_vector_copy(CljPersistentVector* vec, unsigned capacity);

/** @brief Remove last element (persistent, returns new vector)
 * @param vec Source vector
 * @return New vector without last element (AUTORELEASE'd)
 */
CljPersistentVector* vector_popped(CljPersistentVector* vec);

/** @brief Remove last element, returns owned reference
 * @param vec Source vector
 * @return New vector (owned, no AUTORELEASE)
 */
CljPersistentVector* vector_popped_owned(CljPersistentVector* vec);

/** @brief Insert item at index (persistent, returns new vector)
 * @param vec Source vector
 * @param index Index to insert at
 * @param item Item to insert
 * @return New vector with item inserted (AUTORELEASE'd)
 */
CljPersistentVector* vector_by_inserting_at(CljPersistentVector* vec, unsigned int index, ID item);

/** @brief Remove element at index (persistent, returns new vector)
 * @param vec Source vector
 * @param index Index to remove
 * @return New vector with element removed (AUTORELEASE'd)
 */
CljPersistentVector* vector_by_removing_at(CljPersistentVector* vec, unsigned int index);

/** @brief Clear all elements from vector (in-place)
 * @param vec Vector to clear
 */
void vector_clear(CljPersistentVector *vec);

/** @brief Truncate vector to n elements (in-place)
 * @param vec Vector to truncate
 * @param n New size (must be <= count)
 */
void vector_truncate(CljPersistentVector *vec, unsigned int n);

// Transient wrapper API (wrapper pointer remains stable; `backing` may be replaced/grown).
/** Create a transient wrapper. Returns AUTORELEASE'd transient (rc stays 1). */
CljTransientVector* vector_transient(CljPersistentVector *vec);
/** Convert transient to persistent snapshot.
 * NOTE: The returned backing is borrowed from the transient; it remains valid
 * only until the transient is mutated or released. RETAIN it if you need to
 * keep it beyond further transient operations or pool drains. */
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

/** @brief Remove element at index from transient vector
 * @param tvec Transient vector (must be CLJ_VECTOR_TRANSIENT)
 * @param index Index to remove
 */
void vector_remove_at(CljTransientVector *tvec, unsigned int index);

/** @brief Insert item at index in transient vector
 * @param tvec Transient vector (must be CLJ_VECTOR_TRANSIENT)
 * @param index Index to insert at
 * @param item Item to insert
 */
void vector_insert_at(CljTransientVector *tvec, unsigned int index, ID item);

/** @brief Get count of vector copies made (for profiling)
 * @return Number of copies
 */
size_t vector_make_copy_count(void);

/** @brief Reset vector copy counter
 */
void vector_make_copy_count_reset(void);

/** @brief Append item in-place (updates slot)
 * @param vec_slot Pointer to vector slot
 * @param item Item to append
 */
void vector_conj_inplace(CljPersistentVector **vec_slot, ID item);

/** @brief Update element in-place (updates slot)
 * @param vec_slot Pointer to vector slot
 * @param index Index to update
 * @param value New value
 */
void vector_assoc_inplace(CljPersistentVector **vec_slot, unsigned int index, ID value);

/** @brief Insert item in-place (updates slot)
 * @param vec_slot Pointer to vector slot
 * @param index Index to insert at
 * @param item Item to insert
 */
void vector_insert_at_inplace(CljPersistentVector **vec_slot, unsigned int index, ID item);

/** @brief Remove element in-place (updates slot)
 * @param vec_slot Pointer to vector slot
 * @param index Index to remove
 */
void vector_remove_at_inplace(CljPersistentVector **vec_slot, unsigned int index);

/** @brief Pop last element in-place (updates slot)
 * @param vec_slot Pointer to vector slot
 */
void vector_pop_inplace(CljPersistentVector **vec_slot);

#define VECTOR_FOR_EACH(vector, elem_var) \
    for (int _i = 0, _cnt = vector_count(vector); (vector) && _i < _cnt; ++_i) \
        for (ID *_data_ptr = vector_as_array(vector); _data_ptr; _data_ptr = NULL) \
            for (ID elem_var = _data_ptr[_i]; _data_ptr; _data_ptr = NULL)

#endif // SUBJECTIVE_C_VECTOR_H
