#ifndef SUBJECTIVE_C_VECTOR_H
#define SUBJECTIVE_C_VECTOR_H

#include "object.h"
#include "common.h"

// Plan: remove shared `struct CljVector` layout. Keep only:
// - CljPersistentVector: TAG CLJ_VECTOR_PERSISTENT, owns `data[]`.
// - CljTransientVector: wrapper (TAG CLJ_VECTOR_TRANSIENT) with `backing` (always persistent tag).
//   Transient mutation API: vector_push, vector_pop, vector_set_nth_transient, vector_remove_at, vector_insert_at.

// Maximum number of elements a persistent vector can hold.
// On ESP32 targets the count/capacity fields are 16-bit, so the hard limit
// is UINT16_MAX.  The same limit is enforced on all platforms so that code
// built and tested on the host accurately reflects ESP32 constraints.
#define CLJ_VECTOR_CAPACITY_MAX 65535u

#if defined(ESP_PLATFORM) || defined(ESP32_BUILD)
// ESP32: shrink count/capacity to 16-bit to reduce header size.
typedef struct CljPersistentVector {
    CljObject base;
    uint16_t count;
    uint16_t capacity;
    ID data[];  // flexible array (capacity entries)
} CljPersistentVector;
#else
// Host (macOS / Linux): keep wide fields for ABI compatibility.
typedef struct CljPersistentVector {
    CljObject base;
    unsigned int count;
    int capacity;
    ID data[];  // flexible array (capacity entries)
} CljPersistentVector;
#endif

/**
 * CljTransientVector — mutable wrapper over a CljPersistentVector backing.
 *
 * Ringbuffer semantics
 * --------------------
 * `head` is the physical index of the first live logical element inside
 * `backing->data`.  All logical indices map to physical storage via:
 *
 *   physical = (head + logical_index) % backing->capacity
 *
 * Operation complexity:
 *   vector_push(tvec, item)        O(1) — writes at (head+count) % cap
 *   vector_pop(tvec)               O(1) — releases tail element
 *   vector_remove_at(tvec, 0)      O(1) — increments head, decrements count
 *   vector_remove_at(tvec, N)      O(n) — wrap-aware element shifting
 *   vector_insert_at(tvec, N, v)   O(n) — wrap-aware element shifting
 *   vector_set_nth_transient       O(1) — maps logical index through head
 *
 * Growth invariant: whenever the backing must grow, elements are copied in
 * logical order into the new allocation and `head` is reset to 0.  This keeps
 * the physical layout contiguous after every resize.
 *
 * vector_persistent(tvec) behavior:
 *   head == 0  →  returns the backing directly
 *   head != 0  →  returns a normalised vector in logical order
 */
#if defined(ESP_PLATFORM) || defined(ESP32_BUILD)
typedef struct CljTransientVector {
    CljObject base;                 // type == CLJ_VECTOR_TRANSIENT
    uint16_t  head;                 // ringbuffer start offset (0 initially)
    CljPersistentVector *backing;   // always TAG CLJ_VECTOR_PERSISTENT (never WEAK)
} CljTransientVector;
#else
typedef struct CljTransientVector {
    CljObject base;                 // type == CLJ_VECTOR_TRANSIENT
    unsigned int head;              // ringbuffer start offset (0 initially)
    CljPersistentVector *backing;   // always TAG CLJ_VECTOR_PERSISTENT (never WEAK)
} CljTransientVector;
#endif

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

/** @brief Return a persistent view of a transient vector.
 *
 * When `head == 0` the backing is returned directly. When `head != 0` a
 * normalised vector in logical order is returned.
 *
 * @param tvec Transient vector to snapshot
 * @return Persistent vector snapshot
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
static INLINE __attribute__((unused)) unsigned int vector_count(CljPersistentVector *vec) {
    return vec ? vec->count : 0u;
}

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

/** @brief Create persistent vector from contiguous item storage
 * @param items Pointer to contiguous items (can be NULL only when count is 0)
 * @param count Number of items to copy
 * @return New persistent vector or empty-vector singleton when count is 0
 */
CljPersistentVector* make_vector_from_stack(ID *items, unsigned int count);

/** @brief Append item to vector (persistent, returns new vector)
 * @param vec Source vector
 * @param item Item to append
 * @return New vector with item appended
 */
CljPersistentVector* vector_conj(CljPersistentVector* vec, ID item);

/** @brief Append item to vector for internal callers that need the raw result
 * @param vec Source vector
 * @param item Item to append
 * @return Updated vector
 */
CljPersistentVector* vector_conj_owned(CljPersistentVector* vec, ID item);

/** @brief Update element at index (persistent, returns new vector)
 * @param vec Source vector
 * @param index Index to update
 * @param value New value
 * @return New vector with updated element
 */
CljPersistentVector* vector_assoc(CljPersistentVector* vec, unsigned int index, ID value);

/** @brief Update element at index (persistent, returns new vector)
 * @param vec Source vector
 * @param index Index to update
 * @param value New value
 * @return New vector with updated element
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
 * @return New vector without last element
 */
CljPersistentVector* vector_popped(CljPersistentVector* vec);

/** @brief Remove last element for internal callers that need the raw result
 * @param vec Source vector
 * @return Updated vector
 */
CljPersistentVector* vector_popped_owned(CljPersistentVector* vec);

/** @brief Insert item at index (persistent, returns new vector)
 * @param vec Source vector
 * @param index Index to insert at
 * @param item Item to insert
 * @return New vector with item inserted
 */
CljPersistentVector* vector_by_inserting_at(CljPersistentVector* vec, unsigned int index, ID item);

/** @brief Remove element at index (persistent, returns new vector)
 * @param vec Source vector
 * @param index Index to remove
 * @return New vector with element removed
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
/** @brief Create a transient wrapper around a persistent vector.
 * @param vec Source persistent vector to wrap
 * @return New transient vector wrapper
 */
CljTransientVector* make_vector_transient(CljPersistentVector *vec);
// (internal) backing replacement helper is intentionally not part of the public API.

/** @brief Return the first logical element of a transient vector.
 * @param tvec Transient vector (must be CLJ_VECTOR_TRANSIENT)
 * @return First logical element or NULL when the transient vector is empty
 */
static inline ID vector_front_transient(CljTransientVector *tvec) {
    if (!tvec || !tvec->backing || tvec->backing->count == 0) {
        return NULL;
    }
    unsigned int cap = (unsigned int)tvec->backing->capacity;
    if (cap == 0u) {
        return NULL;
    }
    return tvec->backing->data[tvec->head % cap];
}

/** @brief Clear a transient vector and reset its ringbuffer head.
 * @param tvec Transient vector (must be CLJ_VECTOR_TRANSIENT)
 */
void vector_clear_transient(CljTransientVector *tvec);

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

/** @brief Truncate transient vector to new count
 * @param tvec Transient vector (must be CLJ_VECTOR_TRANSIENT)
 * @param new_count New count after truncation (must be <= current count)
 */
void vector_truncate_transient(CljTransientVector *tvec, unsigned int new_count);

#define VECTOR_FOR_EACH(vector, elem_var) \
    for (int _i = 0, _cnt = vector_count(vector); (vector) && _i < _cnt; ++_i) \
        for (ID *_data_ptr = vector_as_array(vector); _data_ptr; _data_ptr = NULL) \
            for (ID elem_var = _data_ptr[_i]; _data_ptr; _data_ptr = NULL)

#endif // SUBJECTIVE_C_VECTOR_H
