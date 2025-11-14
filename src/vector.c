#include "vector.h"
#include "memory.h"
#include "value.h"  // For IS_IMMEDIATE macro (used indirectly by RETAIN/RELEASE macros from memory.h)
#include "types.h"  // For SINGLETON_RC
#include "seq.h"  // For SeqIterator
#include "common.h"  // For CLJ_ASSERT
#include <stdlib.h>
#include <stdbool.h>
#include <execinfo.h>  // For backtrace
#include <unistd.h>    // For write

// CljPersistentVector struct definition (opaque pointer - only visible in vector.c)
// data is a flexible array member at the end of the struct
struct CljPersistentVector {
    CljObject base;
    unsigned int count;
    int capacity;
    ID data[];  // Flexible array member - elements stored at end of malloc block
};

// Empty-vector singleton: CLJ_VECTOR with rc=SINGLETON_RC, statically initialized
// Note: Flexible array member cannot be initialized, so we use a struct with no data array
static struct {
    CljObject base;
    int count;
    int capacity;
    // No data array for empty vector (capacity = 0)
} clj_empty_vector_singleton_data = {
    .base = { .type = CLJ_VECTOR, .rc = SINGLETON_RC },
    .count = 0,
    .capacity = 0
};
static CljPersistentVector *clj_empty_vector_singleton = (CljPersistentVector*)&clj_empty_vector_singleton_data;
// Export as external symbol (similar to string_empty_singleton pattern)
CljPersistentVector* vector_empty_singleton = (CljPersistentVector*)&clj_empty_vector_singleton_data;

/** Return empty vector singleton (rc=0, do not retain/release). */
CljPersistentVector* empty_vector(void) {
    return clj_empty_vector_singleton;
}

/** Get vector count. Returns 0 if vec is NULL. */
unsigned int vector_count(CljPersistentVector *vec) {
    if (!vec) return 0;
    return vec->count;
}

/** Get element at index. Returns retained element or NULL if index out of bounds or nil.
 * @param vec Vector to access
 * @param index Index (0-based)
 * @return Retained element or NULL (nil)
 */
ID vector_nth(CljPersistentVector *vec, unsigned int index) {
    if (!vec || index >= vec->count) return NULL;
    ID result = vec->data[index];
    // For CLJ_WEAK_VECTOR, don't RETAIN (weak reference)
    if (vec->base.type != CLJ_WEAK_VECTOR) {
        RETAIN(result);
    }
    return result;
}

/** Initialize seq iterator for vector (internal use by seq.c).
 * Sets up iterator state without exposing internal data pointer.
 * @param iter Iterator to initialize
 * @param vec Vector to iterate over
 * @return true if successful, false if vector is empty
 */
bool vector_init_seq_iterator(SeqIterator *iter, CljPersistentVector *vec) {
    if (!iter || !vec) return false;
    
    // Check if empty
    if (vec->count == 0) {
        return false;  // Empty vector
    }
    
    // Store vector in container (already set by seq_iter_init)
    // Set index and count, but NOT data pointer
    iter->state.vec.index = 0;
    iter->state.vec.count = vec->count;
    iter->state.vec.data = NULL;  // Don't expose internal pointer
    iter->seq_type = CLJ_VECTOR;
    
    return true;
}

/** Get element at index without RETAIN (internal use for seq iterator).
 * @param vec Vector to access
 * @param index Index (0-based)
 * @return Element or NULL (nil), NOT retained
 */
ID vector_get_element_no_retain(CljPersistentVector *vec, unsigned int index) {
    if (!vec || index >= vec->count) return NULL;
    ID result = vec->data[index];
    if (!result) return NULL;  // nil is stored as NULL
    return result;  // No RETAIN - caller must handle reference counting
}

/** Get raw data array pointer (no copying, direct access).
 * @param vec Vector to access
 * @return Pointer to data array or NULL if vec is NULL or capacity is 0
 */
ID* vector_as_array(CljPersistentVector *vec) {
    if (!vec || vec->capacity == 0) return NULL;
    return vec->data;  // Flexible array member - points to end of struct
}

/** Increment count for transient vectors (internal use only).
 * @param vec Transient vector to increment count for
 */
void vector_increment_count(CljPersistentVector *vec) {
    if (vec && (TAG(vec) == CLJ_TRANSIENT_VECTOR || TAG(vec) == CLJ_WEAK_VECTOR)) {
        vec->count++;
    }
}

/** Set count to zero for transient vectors (internal use only).
 * @param vec Transient vector to reset count for
 */
void vector_reset_count(CljPersistentVector *vec) {
    if (vec && (TAG(vec) == CLJ_TRANSIENT_VECTOR || TAG(vec) == CLJ_WEAK_VECTOR)) {
        vec->count = 0;
    }
}

void vector_clear(CljPersistentVector *vec) {
    CLJ_ASSERT(vec != NULL);
    CLJ_ASSERT(vec->base.type == CLJ_WEAK_VECTOR || vec->base.type == CLJ_TRANSIENT_VECTOR);
    vec->count = 0;
}

/** Set element at index. Returns new vector with updated element (COW if needed).
 * @param vec Vector to update
 * @param index Index (0-based)
 * @param value New value (will be retained)
 * @return New vector with updated element, or NULL on error
 */
CljPersistentVector* vector_set_nth(CljPersistentVector* vec, unsigned int index, ID value) {
    if (!vec) return NULL;
    CljPersistentVector *v = as_vector(vec);
    if (!v || index >= v->count) return NULL;
    return vector_assoc(vec, index, value);
}

/** Copy vector with specified capacity.
 * @param vec Vector to copy
 * @param capacity Capacity for new vector. If too low, elements are cut off in the copy returned.
 * @return New retained vector with copied elements, throws on error. type is also copied.
 */
// Counter for make_vector_copy calls
static size_t g_make_vector_copy_count = 0;

CljPersistentVector* make_vector_copy(CljPersistentVector* vec, unsigned capacity) {
    g_make_vector_copy_count++;
    
    if (!vec) return NULL;
    CljPersistentVector *v = as_vector(vec);
    unsigned count = MIN(capacity, v->count); // trunkate as requested
    
    
    // Create new vector with specified capacity using make_vector
    // make_vector handles singleton case automatically (capacity==0 && type==CLJ_VECTOR)
    // For transient vectors, use CLJ_VECTOR and convert to transient afterwards
    CljType base_type = (v->base.type == CLJ_TRANSIENT_VECTOR) ? CLJ_VECTOR : v->base.type;
    CljPersistentVector *vec_copy = make_vector(capacity, base_type);
    if (!vec_copy)
        return NULL;
    
    // If original was transient, convert copy to transient
    if (v->base.type == CLJ_TRANSIENT_VECTOR) {
        vec_copy->base.type = CLJ_TRANSIENT_VECTOR;
    }
    
    // Set count to match original vector (make_vector sets count=0)
    vec_copy->count = count;
    
    // Copy all elements (including nil/NULL - nil is a valid value in Clojure)
    // For CLJ_WEAK_VECTOR, don't RETAIN (weak reference)
    // But when copying, we need to retain for normal vectors to maintain reference counting
    // Copy all elements (count was already set to MIN(capacity, v->count) above)
    for (unsigned i = 0; i < count; ++i) {
        ID elem = v->data[i];
        vec_copy->data[i] = (v->base.type == CLJ_WEAK_VECTOR) ? elem : (elem ? RETAIN(elem) : NULL);
    }
    
    return vec_copy;
}

/** Remove last element from vector (in-place if RC=1, COW if RC>1).
 * @param vec Vector to pop from
 * @return Same vector (in-place) if RC=1, new vector if RC>1, or NULL on error
 */
CljPersistentVector* vector_pop(CljPersistentVector* vec) {
    if (!vec) return NULL;
    CljPersistentVector *v = as_vector(vec);
    if (!v || v->count == 0) return vec;  // Empty vector, return as-is
    
    // OPTIMIZATION: If RC=1, mutate in-place (O(1))
    if (v->base.rc == 1) {
        // For CLJ_WEAK_VECTOR, don't RELEASE (weak reference)
        if (v->base.type != CLJ_WEAK_VECTOR) {
            // Release last element
            RELEASE(vector_nth(v, v->count - 1));
        }
        // Set last element to NULL and decrement count
        v->count--;
        return vec;  // Return same vector (in-place mutation)
    }
    
    // RC>1: Copy-on-Write (O(n))
    // Create new vector with count-1 elements using make_vector_copy
    // First, temporarily reduce count to copy only first count-1 elements
    int original_count = v->count;
    v->count = original_count - 1;
    CljPersistentVector *new_vec = make_vector_copy(v, original_count - 1);
    v->count = original_count;  // Restore original count
    
    return new_vec;
}

/** Remove element at index from vector (in-place if RC=1, COW if RC>1).
 * @param vec Vector to remove from
 * @param index Index of element to remove (0-based)
 * @return Same vector (in-place) if RC=1, new vector if RC>1, or NULL on error
 */
CljPersistentVector* vector_remove_at(CljPersistentVector* vec, unsigned int index) {
    if (!vec) return NULL;
    CljPersistentVector *v = as_vector(vec);
    if (!v || index >= v->count) return vec;  // Invalid index, return as-is
    
    // OPTIMIZATION: If RC=1 or transient vector, mutate in-place (O(n) due to shifting)
    bool is_transient = (TAG(vec) == CLJ_TRANSIENT_VECTOR || TAG(vec) == CLJ_WEAK_VECTOR);
    if (v->base.rc == 1 || is_transient) {
        // Release element at index (only for non-weak vectors)
        if (v->data[index] && TAG(vec) != CLJ_WEAK_VECTOR) {
            RELEASE(v->data[index]);
        }
        // Shift elements left
        for (unsigned int i = index + 1; i < v->count; i++) {
            v->data[i - 1] = v->data[i];
        }
        v->count--;
        return vec;  // Return same vector (in-place mutation)
    }
    
    // RC>1: Copy-on-Write (O(n))
    // Create new vector without the element at index
    int new_count = v->count - 1;
    CljPersistentVector *new_vec = make_vector(v->capacity, v->base.type);
    if (!new_vec) return vec;  // Return original vector on OOM
    
    new_vec->count = new_count;
    
    // Copy elements before index
    for (unsigned int i = 0; i < index; i++) {
        new_vec->data[i] = RETAIN(v->data[i]);
    }
    
    // Copy elements after index
    for (unsigned int i = index + 1; i < v->count; i++) {
        new_vec->data[i - 1] = RETAIN(v->data[i]);
    }
    
    return new_vec;
}

// Creates a CljPersistentVector*.
// Notes:
// - When capacity <= 0, returns empty-vector singleton (rc=0, data=NULL); do
// not retain/release.
// - When capacity > 0, returns heap vector (rc=1) with zero-initialized backing
// store.
// Legacy make_vector removed - use make_vector instead

// Legacy make_weak_vector removed - use make_vector instead

// Legacy vector_push_inplace removed - use conj for transient vectors instead

// Legacy vector_conj removed - use vector_conj instead

// Legacy vector_from_items removed - use make_vector instead

// Legacy make_vector_from_stack removed - use make_vector instead

// === Neue CljValue API (Phase 1: Parallel) ===

// Counter for make_vector calls (actual allocations, excluding singleton)
static size_t g_make_vector_count = 0;

/** Create a vector with given capacity; capacity<=0 returns empty-vector singleton. */
CljPersistentVector* make_vector(unsigned int capacity, CljType type) {
    if (capacity == 0 && type == CLJ_VECTOR) {
        return clj_empty_vector_singleton;
    }
    // Allocate struct + flexible array member in one block
    g_make_vector_count++;
    size_t struct_size = sizeof(CljPersistentVector);
    size_t data_size = (size_t)capacity * sizeof(ID);
    size_t total_size = struct_size + data_size;
    
    // Use malloc instead of calloc - we set all fields manually and data[] is filled by vector_conj
    CljPersistentVector *vec = (CljPersistentVector*)alloc(total_size, 1, type);
    if (!vec)
        throw_oom();

    vec->base.type = type;
    vec->base.rc = 1;
    vec->count = 0;
    vec->capacity = capacity;
    // data[] is flexible array member - already allocated at end of struct
    // No need to initialize data[] - elements are set by vector_conj

    return vec;
}

/** Return a new vector with item appended; original vector remains unchanged.
 * Uses Copy-on-Write: RC=1 → in-place mutation, RC>1 → COW.
 * Hot-path (RC=1, capacity OK): No branches, direct in-place mutation.
 * Supports both CLJ_VECTOR and CLJ_WEAK_VECTOR.
 */
CljPersistentVector* vector_conj(CljPersistentVector* vec, ID item) {
    if (!vec) return NULL;
    // vector_conj supports persistent vectors (CLJ_VECTOR) and weak vectors (CLJ_WEAK_VECTOR), not transient vectors
    // Using transient vector with vector_conj is a programming error - use vector_conj_bang instead
    CLJ_ASSERT(TAG(vec) != CLJ_TRANSIENT_VECTOR);

    // Note: item can be NULL (nil) - it's a valid value in Clojure collections

    CljPersistentVector *old_vec = as_vector(vec);
    if (!old_vec)
        return NULL;

    bool is_weak = (old_vec->base.type == CLJ_WEAK_VECTOR);

    // HOT-PATH: RC=1 && capacity OK → direct in-place mutation (no branches)
    // Most common case: single owner, enough capacity
    if (old_vec->base.rc == 1 && old_vec->count < (unsigned int)old_vec->capacity) {
        // For CLJ_WEAK_VECTOR, don't RETAIN (weak reference)
        old_vec->data[old_vec->count++] = is_weak ? item : (item ? RETAIN(item) : NULL);
        return vec;  // Return same vector (in-place mutation)
    }

    // Early returns for uncommon cases
    if (old_vec->base.rc == 0) {
        // Empty vector singleton: create new vector
        CljPersistentVector* new_vec = make_vector(4, is_weak ? CLJ_WEAK_VECTOR : CLJ_VECTOR);
        if (!new_vec)
            return vec;
        new_vec->data[0] = is_weak ? item : (item ? RETAIN(item) : NULL);
        new_vec->count = 1;
        return new_vec;
    }

    // COW path: RC>1 or capacity insufficient (even with RC=1, use COW for safety)
    // Note: Even with RC=1, we can't use realloc because the vector might be stored
    // in a container (Map, List) that has a stale pointer. COW is safer.
    
    // COW path: RC>1
    // Calculate new capacity (same logic as vector_assoc)
    int new_capacity = old_vec->capacity;
    if (old_vec->count >= (unsigned int)old_vec->capacity) {
        new_capacity = old_vec->capacity * 2;
        if (new_capacity < 4) new_capacity = 4;
    }
    
    // Use make_vector_copy to copy existing elements (preserves type)
    CljPersistentVector* new_vec = make_vector_copy(old_vec, new_capacity);
    if (!new_vec)
        return vec;
    
    // Append new item (NULL/nil is a valid value)
    // For CLJ_WEAK_VECTOR, don't RETAIN (weak reference)
    new_vec->data[new_vec->count++] = is_weak ? item : (item ? RETAIN(item) : NULL);
    
    return new_vec;  // Return new vector (COW)
}

/** Update element at index with COW: RC=1 → in-place, RC>1 → COW. */
CljPersistentVector* vector_assoc(CljPersistentVector* vec, unsigned int index, ID value) {
    if (!vec || (vec->base.type != CLJ_VECTOR && vec->base.type != CLJ_WEAK_VECTOR) || !value)
        return NULL;

    CljPersistentVector *old_vec = as_vector(vec);
    if (!old_vec)
        return NULL;
    
    // For CLJ_WEAK_VECTOR, allow index == count (append)
    if (old_vec->base.type != CLJ_WEAK_VECTOR && index >= old_vec->count)
        return NULL;
    if (old_vec->base.type == CLJ_WEAK_VECTOR && index > old_vec->count)
        return NULL;

    // Empty vector singleton (RC=0): Not applicable (index >= count)
    if (old_vec->base.rc == 0) {
        return NULL;
    }

    // OPTIMIZATION: If RC=1, mutate in-place
    if (old_vec->base.rc == 1) {
        // For CLJ_WEAK_VECTOR with index == count (append), we need to grow capacity first
        if (old_vec->base.type == CLJ_WEAK_VECTOR && index == old_vec->count) {
            // Append to weak vector - need to ensure capacity
            // After incrementing count, index must be < capacity
            // So we need capacity > index, i.e., capacity >= index + 1
            if (old_vec->capacity <= (int)index) {
                // Grow capacity using make_vector_copy
                // Ensure new capacity is at least index + 1, minimum 4
                int newcap = MAX(MAX(old_vec->capacity * 2, (int)index + 1), 4);
                CljPersistentVector *new_vec = make_vector_copy(old_vec, newcap);
                if (!new_vec) return vec;  // Return original on OOM
                RELEASE((CljObject*)old_vec);
                old_vec = new_vec;
                vec = new_vec;  // Update vec to point to new vector
            }
            old_vec->count++;  // Increment count for append
        }
        
        // Ensure index is within bounds before accessing data[index]
        if (index >= (unsigned int)old_vec->capacity) {
            return vec;  // Safety check
        }
        
        // For CLJ_WEAK_VECTOR, don't RETAIN (weak reference)
        old_vec->data[index] = old_vec->base.type == CLJ_WEAK_VECTOR ? value : RETAIN(value);
        return vec;  // Return same vector (in-place mutation) or new vector if capacity grew
    }

    // RC>1: Copy-on-Write - use make_vector_copy for efficiency
    // For CLJ_WEAK_VECTOR with index == count (append), ensure capacity is sufficient
    int new_capacity = old_vec->capacity;
    if (old_vec->base.type == CLJ_WEAK_VECTOR && index == old_vec->count) {
        // Append to weak vector - ensure capacity is at least index + 1, minimum 4
        new_capacity = MAX(MAX(old_vec->capacity * 2, (int)index + 1), 4);
    }
    CljPersistentVector* new_vec = make_vector_copy(old_vec, new_capacity);
    if (!new_vec)
        return vec;  // Return original vector on OOM

    // For CLJ_WEAK_VECTOR append, increment count
    if (old_vec->base.type == CLJ_WEAK_VECTOR && index == old_vec->count) {
        new_vec->count++;
    }

    // Ensure index is within bounds before accessing data[index]
    if (index >= (unsigned int)new_vec->capacity) {
        return new_vec;  // Safety check
    }

    // Update element at index (replace the copied element)
    if (index < old_vec->count && new_vec->data[index] && old_vec->base.type != CLJ_WEAK_VECTOR) {
        RELEASE(new_vec->data[index]);
    }
    // For CLJ_WEAK_VECTOR, don't RETAIN (weak reference)
    new_vec->data[index] = old_vec->base.type == CLJ_WEAK_VECTOR ? value : RETAIN(value);

    return new_vec;  // Return new vector (COW)
}

// === Transient API (Phase 2) ===

// Counter for transient calls
static size_t g_transient_count = 0;

/** Convert persistent vector to transient. */
ID transient(ID vec) {
    g_transient_count++;
    
    
    if (!vec) return NULL;
    CljObject *obj = (CljObject*)vec;
    if (obj->type != CLJ_VECTOR) {
        return NULL;
    }
    
    CljPersistentVector *v = as_vector(vec);
    if (!v) return NULL;
    
    // Handle empty vector singleton - create new transient vector with initial capacity
    if (v->count == 0 && v->capacity == 0) {
        // Create transient vector with initial capacity (not 0, so we can grow it)
        CljPersistentVector *tvec = make_vector(4, CLJ_VECTOR);
        if (!tvec) return NULL;
        tvec->base.type = CLJ_TRANSIENT_VECTOR;
        return tvec;
    }
    
    // Use make_vector_copy to create a copy with flexible array member
    CljPersistentVector *tvec = make_vector_copy(v, v->capacity);
    if (!tvec) return NULL;
    
    // Change type to transient
    tvec->base.type = CLJ_TRANSIENT_VECTOR;
    
    return tvec;
}


/** Append to transient vector (guaranteed in-place). */
CljPersistentVector* clj_conj(CljPersistentVector *tvec, ID item) {
    return vector_conj_bang(tvec, item);
}

/** Append to transient vector (guaranteed in-place, may return new vector if capacity grows).
 * @param tvec Transient vector to append to
 * @param item Item to append
 * @return Updated transient vector (may be new vector if capacity grew)
 */
CljPersistentVector* vector_conj_bang(CljPersistentVector *tvec, ID item) {
    // Note: item can be NULL (nil) - it's a valid value in Clojure collections
    CljPersistentVector *v = tvec;
    
    // Garantiert in-place für Transients
    // Ensure capacity is sufficient - use make_vector_copy for safety
    // Note: Even transient vectors can't use realloc safely if they're stored in containers
    if (v->count >= (unsigned int)v->capacity || v->capacity == 0) {
        int newcap = MAX(v->capacity * 2, 4);
        CljPersistentVector *new_v = make_vector_copy(v, newcap);
        if (!new_v) { throw_oom(); return NULL; }
        // Ensure new vector is still transient
        new_v->base.type = CLJ_TRANSIENT_VECTOR;
        RELEASE((CljObject*)v);
        v = new_v;  // Use new vector for adding item
        tvec = new_v;  // Update return value
    }
    
    // NULL (nil) is a valid value - RETAIN handles NULL safely
    v->data[v->count++] = RETAIN(item);
    
    return tvec; // In-place mutation (or new vector if capacity grew)
}

// Counter for persistent calls
static size_t g_persistent_count = 0;

/** Convert transient vector back to persistent. */
ID persistent(ID tvec) {
    g_persistent_count++;
    
    if (!tvec) return NULL;
    CljObject *obj = (CljObject*)tvec;
    if (obj->type != CLJ_TRANSIENT_VECTOR) {
        return NULL;
    }
    
    CljPersistentVector *v = as_vector(tvec);
    if (!v) return NULL;
    
    // Clojure-Semantik: Erstelle NEUE persistent collection
    // Use make_vector_copy to copy all elements efficiently
    CljPersistentVector* new_vec = make_vector_copy(v, v->capacity);
    if (!new_vec) return NULL;
    
    // Set type to CLJ_VECTOR (persistent, not transient)
    new_vec->base.type = CLJ_VECTOR;
    
    return new_vec;
}
