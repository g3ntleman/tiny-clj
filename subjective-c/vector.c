#include "vector.h"
#include "memory.h"
#include "types.h"  // For SINGLETON_RC
#include "seq.h"  // For SeqIterator
#include "common.h"  // For CLJ_ASSERT
#include "exception.h"  // For throw_exception_formatted
#include "validation.h"  // For throw_index_out_of_bounds
#include <stdlib.h>
#include <stdbool.h>
#include <execinfo.h>  // For backtrace
#include <unistd.h>    // For write

// CljVector struct definition (opaque pointer - only visible in vector.c)
// data is a flexible array member at the end of the struct
struct CljVector {
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
static CljVector *clj_empty_vector_singleton = (CljVector*)&clj_empty_vector_singleton_data;
// Export as external symbol (similar to string_empty_singleton pattern)
CljVector* vector_empty_singleton = (CljVector*)&clj_empty_vector_singleton_data;

/** Return empty vector singleton (rc=0, do not retain/release). */
CljVector* empty_vector(void) {
    return clj_empty_vector_singleton;
}

/** Get vector count. Returns 0 if vec is NULL. */
unsigned int vector_count(CljVector *vec) {
    if (!vec) return 0;
    return vec->count;
}

/** Get element at index. Returns element or NULL if index out of bounds or nil.
 * Element lifetime is tied to the vector - caller must not release.
 * @param vec Vector to access
 * @param index Index (0-based)
 * @return Element or NULL (nil) - lifetime tied to vector
 */
 ID vector_nth(CljVector *vec, unsigned int index) {
    if (vec) {
        if (index < vec->count) {
            return vec->data[index];
        }
        return throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
            "vector_nth: index %u is out of bounds for vector with %u elements", index, vec->count);
    }
    return throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                "vector_nth: vector is NULL");
}

/** Find index of element using clj_equal for comparison.
 * @param vec Vector to search
 * @param value Value to find
 * @return Index of element (0-based) or INDEX_NOT_FOUND if not found
 */
int vector_index_of(CljVector *vec, ID value) {
    if (!vec) return INDEX_NOT_FOUND;
    
    VECTOR_FOR_EACH(vec, elem) {
        if (clj_equal(elem, value)) {
            return _i;
        }
    }
    
    return INDEX_NOT_FOUND;
}

/** Initialize seq iterator for vector (internal use by seq.c).
 * Sets up iterator state without exposing internal data pointer.
 * @param iter Iterator to initialize
 * @param vec Vector to iterate over
 * @return true if successful, false if vector is empty
 */
bool vector_init_seq_iterator(SeqIterator *iter, CljVector *vec) {
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


/** Get raw data array pointer (no copying, direct access).
 * @param vec Vector to access
 * @return Pointer to data array (asserts vec != NULL)
 */
ID* vector_as_array(CljVector *vec) {
    CLJ_ASSERT(vec != NULL && "vector_as_array called with NULL");
    return vec->data;  // Flexible array member - points to end of struct
}

/** Increment count for transient vectors (internal use only).
 * @param vec Transient vector to increment count for
 */
void vector_increment_count(CljVector *vec) {
    CLJ_ASSERT(vec != NULL);
#if defined(DEBUG)
    int tag = TAG(vec);
    CLJ_ASSERT(tag == CLJ_VECTOR_TRANSIENT || tag == CLJ_VECTOR_TRANSIENT_WEAK);
#endif
    vec->count++;
}

void vector_clear(CljVector *vec) {
    CLJ_ASSERT(vec != NULL);
    
    if (vec->base.type != CLJ_VECTOR_TRANSIENT_WEAK) {
        VECTOR_FOR_EACH(vec, elem) {
        RELEASE(elem);
        }
    }
    
    vec->count = 0;
}

/** Set element at index. Only works for transient vectors.
 * @param vec Vector to update (must be transient)
 * @param index Index (0-based)
 * @param value New value (will be retained)
 * @return Updated vector, or NULL on error
 * @throws EXCEPTION_ILLEGAL_ARGUMENT if vec is a persistent vector
 */
CljVector* vector_set_nth(CljVector* vec, unsigned int index, ID value) {
    if (!vec) {
        return NULL;
    }
    
    CljVector *v = as_vector(vec);
    if (!v) {
        return NULL;
    }
    
    // Only transient vectors allowed - persistent vectors are immutable
    if (v->base.type != CLJ_VECTOR_TRANSIENT && v->base.type != CLJ_VECTOR_TRANSIENT_WEAK) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                "vector_set_nth: cannot modify persistent vector. Use transient vector instead.");
        return NULL;
    }
    
    if (index < v->count) {
        return vector_assoc(vec, index, value);
    }
    
    return NULL;
}

/** Copy vector with specified capacity.
 * @param vec Vector to copy
 * @param capacity Capacity for new vector. If too low, elements are cut off in the copy returned.
 * @return New retained vector with copied elements, throws on error. type is also copied.
 */
// Counter for make_vector_copy calls
static size_t g_make_vector_copy_count = 0;

CljVector* make_vector_copy(CljVector* vec, unsigned capacity) {
    g_make_vector_copy_count++;
    
    if (!vec) return NULL;
    CljVector *v = as_vector(vec);
    unsigned count = MIN(capacity, v->count); // trunkate as requested
    
    
    // Create new vector with specified capacity using make_vector
    // make_vector handles singleton case automatically (capacity==0 && type==CLJ_VECTOR)
    // For transient vectors, use CLJ_VECTOR and convert to transient afterwards
    CljType base_type = (v->base.type == CLJ_VECTOR_TRANSIENT) ? CLJ_VECTOR : v->base.type;
    CljVector *vec_copy = make_vector(capacity, base_type);
    
    // If original was transient, convert copy to transient
    if (v->base.type == CLJ_VECTOR_TRANSIENT) {
        vec_copy->base.type = CLJ_VECTOR_TRANSIENT;
    }
    
    // Set count to match original vector (make_vector sets count=0)
    vec_copy->count = count;
    
    // Copy all elements (including nil/NULL - nil is a valid value in Clojure)
    // For CLJ_VECTOR_TRANSIENT_WEAK, don't RETAIN (weak reference)
    // But when copying, we need to retain for normal vectors to maintain reference counting
    // Copy all elements (count was already set to MIN(capacity, v->count) above)
    for (unsigned i = 0; i < count; ++i) {
        ID elem = v->data[i];
        vec_copy->data[i] = (v->base.type == CLJ_VECTOR_TRANSIENT_WEAK) ? elem : (elem ? RETAIN(elem) : NULL);
    }
    
    return vec_copy;
}

/** Remove last element from vector (in-place if RC=1, COW if RC>1).
 * @param vec Vector to pop from
 * @return Same vector (in-place) if RC=1, new vector if RC>1, or NULL on error
 */
CljVector* vector_pop(CljVector* vec) {
    if (vec) {
        CljVector *v = as_vector(vec);
        if (v && v->count > 0) {
            // Happy path: RC=1, mutate in-place (O(1))
            if (v->base.rc == 1) {
                // For CLJ_VECTOR_TRANSIENT_WEAK, don't RELEASE (weak reference)
                if (v->base.type != CLJ_VECTOR_TRANSIENT_WEAK) {
                    // Release last element (element lifetime is tied to vector)
                    ID last_elem = vector_nth(v, v->count - 1);
                    if (last_elem && !is_immediate(last_elem)) {
                        RELEASE((CljObject*)last_elem);
                    }
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
            CljVector *new_vec = make_vector_copy(v, original_count - 1);
            v->count = original_count;  // Restore original count
            
            return new_vec;
        }
        return vec;  // Empty vector, return as-is
    }
    return NULL;
}

/** Insert element at index in vector (in-place if RC=1, COW if RC>1).
 * @param vec Vector to insert into
 * @param index Index where to insert (0-based, must be <= count)
 * @param item Item to insert
 * @return Same vector (in-place) if RC=1, new vector if RC>1, or NULL on error
 */
CljVector* vector_insert_at(CljVector* vec, unsigned int index, ID item) {
    if (!vec) return NULL;
    CljVector *v = as_vector(vec);
    if (!v || index > v->count) return vec;  // Invalid index, return as-is
    
    bool is_weak = (v->base.type == CLJ_VECTOR_TRANSIENT_WEAK);
    bool is_transient = (TAG(vec) == CLJ_VECTOR_TRANSIENT);
    
    // Check if we need to grow capacity
    bool needs_growth = (v->count >= (unsigned int)v->capacity);
    
    // OPTIMIZATION: If RC=1 or transient vector, mutate in-place (O(n) due to shifting)
    if (v->base.rc == 1 || is_transient) {
        // Grow capacity if needed
        if (needs_growth) {
            int new_capacity = v->capacity * 2;
            if (new_capacity < 4) new_capacity = 4;
            CljVector *new_vec = make_vector_copy(v, new_capacity);
            if (!new_vec) return vec;  // Return original on OOM
            // Ensure new vector is still transient if original was transient
            if (is_transient) {
                new_vec->base.type = CLJ_VECTOR_TRANSIENT;
            }
            RELEASE((CljObject*)v);
            v = new_vec;
            vec = new_vec;
        }
        
        // Shift elements right to make room
        for (unsigned int i = v->count; i > index; i--) {
            v->data[i] = v->data[i - 1];
        }
        
        // Insert new element (RETAIN for non-weak vectors)
        v->data[index] = is_weak ? item : (item ? RETAIN(item) : NULL);
        v->count++;
        
        return vec;  // Return same vector (in-place mutation) or new vector if capacity grew
    }
    
    // RC>1: Copy-on-Write (O(n))
    // Calculate new capacity if needed
    int new_capacity = v->capacity;
    if (needs_growth) {
        new_capacity = v->capacity * 2;
        if (new_capacity < 4) new_capacity = 4;
    }
    
    CljVector *new_vec = make_vector(new_capacity, v->base.type);
    if (!new_vec) return vec;  // Return original vector on OOM
    
    new_vec->count = v->count + 1;
    
    // Copy elements before index
    for (unsigned int i = 0; i < index; i++) {
        new_vec->data[i] = is_weak ? v->data[i] : (v->data[i] ? RETAIN(v->data[i]) : NULL);
    }
    
    // Insert new element
    new_vec->data[index] = is_weak ? item : (item ? RETAIN(item) : NULL);
    
    // Copy elements after index
    for (unsigned int i = index; i < v->count; i++) {
        new_vec->data[i + 1] = is_weak ? v->data[i] : (v->data[i] ? RETAIN(v->data[i]) : NULL);
    }
    
    return new_vec;
}

/** Remove element at index from vector (in-place if RC=1, COW if RC>1).
 * @param vec Vector to remove from
 * @param index Index of element to remove (0-based)
 * @return Same vector (in-place) if RC=1, new vector if RC>1, or NULL on error
 */
CljVector* vector_remove_at(CljVector* vec, unsigned int index) {
    if (!vec) return NULL;
    CljVector *v = as_vector(vec);
    if (!v || index >= v->count) return vec;  // Invalid index, return as-is
    
    // OPTIMIZATION: If RC=1 or transient vector, mutate in-place (O(n) due to shifting)
    int tag = TAG(vec);
    bool is_transient = (tag == CLJ_VECTOR_TRANSIENT || tag == CLJ_VECTOR_TRANSIENT_WEAK);
    if (v->base.rc == 1 || is_transient) {
        // Release element at index (only for non-weak vectors)
        if (v->data[index] && tag != CLJ_VECTOR_TRANSIENT_WEAK) {
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
    CljVector *new_vec = make_vector(v->capacity, v->base.type);
    if (!new_vec) return vec;  // Return original vector on OOM
    
    new_vec->count = new_count;
    
    // Copy all elements except the one at index
    for (unsigned int i = 0, j = 0; i < v->count; i++) {
        if (i != index) {
            new_vec->data[j++] = RETAIN(v->data[i]);
        }
    }
    
    return new_vec;
}

// Creates a CljVector*.
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
CljVector* make_vector(unsigned int capacity, CljType type) {
    if (capacity == 0 && type == CLJ_VECTOR) {
        return clj_empty_vector_singleton;
    }
    // Allocate struct + flexible array member in one block
    g_make_vector_count++;
    size_t struct_size = sizeof(CljVector);
    size_t data_size = (size_t)capacity * sizeof(ID);
    size_t total_size = struct_size + data_size;
    
    // Use malloc instead of calloc - we set all fields manually and data[] is filled by vector_conj
    CljVector *vec = (CljVector*)alloc(total_size, 1, type);

    vec->base.type = type;
    vec->base.rc = 1;
    vec->count = 0;
    vec->capacity = capacity;
    // data[] is flexible array member - already allocated at end of struct
    // No need to initialize data[] - elements are set by vector_conj

    return vec;
}

/** Return a new vector with item appended; original vector remains unchanged.
 * Behavior depends on vector type:
 * - CLJ_VECTOR: Uses Copy-on-Write (RC=1 → in-place mutation, RC>1 → COW)
 * - CLJ_VECTOR_TRANSIENT: Always mutates in-place (transient behavior)
 * - CLJ_VECTOR_TRANSIENT_WEAK: Always mutates in-place (transient behavior, weak references)
 */
CljVector* vector_conj(CljVector* vec, ID item) {
    if (!vec) return NULL;

    // Note: item can be NULL (nil) - it's a valid value in Clojure collections

    CljVector *old_vec = as_vector(vec);
    if (!old_vec)
        return NULL;

    CljType vec_type = old_vec->base.type;
    bool is_transient = (vec_type == CLJ_VECTOR_TRANSIENT);
    bool is_weak = (vec_type == CLJ_VECTOR_TRANSIENT_WEAK);

    // For transient vectors (CLJ_VECTOR_TRANSIENT and CLJ_VECTOR_TRANSIENT_WEAK), always mutate in-place
    if (is_transient || is_weak) {
        CljVector *v = old_vec;
        
        // Ensure capacity is sufficient - use make_vector_copy for safety
        if (v->count >= (unsigned int)v->capacity || v->capacity == 0) {
            int newcap = MAX(v->capacity * 2, 4);
            CljVector *new_v = make_vector_copy(v, newcap);
            // Preserve vector type
            new_v->base.type = vec_type;
            RELEASE((CljObject*)v);
            v = new_v;  // Use new vector for adding item
            vec = new_v;  // Update return value
        }
        
        // For CLJ_VECTOR_TRANSIENT_WEAK, don't RETAIN (weak reference)
        // For CLJ_VECTOR_TRANSIENT, RETAIN the item
        v->data[v->count++] = is_weak ? item : (item ? RETAIN(item) : NULL);
        
        return vec; // In-place mutation (or new vector if capacity grew)
    }

    // For CLJ_VECTOR: Use COW semantics
    // HOT-PATH: RC=1 && capacity OK → direct in-place mutation (no branches)
    // Most common case: single owner, enough capacity
    if (old_vec->base.rc == 1 && old_vec->count < (unsigned int)old_vec->capacity) {
        old_vec->data[old_vec->count++] = item ? RETAIN(item) : NULL;
        return vec;  // Return same vector (in-place mutation)
    }

    // Early returns for uncommon cases
    if (old_vec->base.rc == 0) {
        // Empty vector singleton: create new vector
        CljVector* new_vec = make_vector(4, CLJ_VECTOR);
        if (!new_vec)
            return vec;
        new_vec->data[0] = item ? RETAIN(item) : NULL;
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
    CljVector* new_vec = make_vector_copy(old_vec, new_capacity);
    if (!new_vec)
        return vec;
    
    // Append new item (NULL/nil is a valid value)
    new_vec->data[new_vec->count++] = item ? RETAIN(item) : NULL;
    
    return new_vec;  // Return new vector (COW)
}

/** Update element at index with COW: RC=1 → in-place, RC>1 → COW. */
CljVector* vector_assoc(CljVector* vec, unsigned int index, ID value) {
    if (!vec) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                "vector_assoc: vector is NULL");
    }
    if (!value) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                "vector_assoc: value is NULL");
    }
    
    CLJ_ASSERT(vec->base.type == CLJ_VECTOR || vec->base.type == CLJ_VECTOR_TRANSIENT_WEAK || vec->base.type == CLJ_VECTOR_TRANSIENT);

    CljVector *old_vec = as_vector(vec);
    
    bool is_transient = (old_vec->base.type == CLJ_VECTOR_TRANSIENT);
    
    // For CLJ_VECTOR_TRANSIENT_WEAK, allow index == count (append)
    // For CLJ_VECTOR_TRANSIENT, allow index == count (append)
    if (!is_transient && old_vec->base.type != CLJ_VECTOR_TRANSIENT_WEAK && index >= old_vec->count) {
        throw_index_out_of_bounds("vector_assoc", index, old_vec->count, "vector");
    }
    if (old_vec->base.type == CLJ_VECTOR_TRANSIENT_WEAK && index > old_vec->count) {
        throw_index_out_of_bounds("vector_assoc", index, old_vec->count, "weak vector");
    }
    if (is_transient && index > old_vec->count) {
        throw_index_out_of_bounds("vector_assoc", index, old_vec->count, "transient vector");
    }

    // Empty vector singleton (RC=0): Not applicable (index >= count)
    if (old_vec->base.rc == 0) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                "vector_assoc: cannot modify empty vector singleton");
    }

    // OPTIMIZATION: If RC=1 or transient, mutate in-place
    if (old_vec->base.rc == 1 || is_transient) {
        // For CLJ_VECTOR_TRANSIENT_WEAK with index == count (append), we need to grow capacity first
        if (old_vec->base.type == CLJ_VECTOR_TRANSIENT_WEAK && index == old_vec->count) {
            // Append to weak vector - need to ensure capacity
            // After incrementing count, index must be < capacity
            // So we need capacity > index, i.e., capacity >= index + 1
            if (old_vec->capacity <= (int)index) {
                // Grow capacity using make_vector_copy
                // Ensure new capacity is at least index + 1, minimum 4
                int newcap = MAX(MAX(old_vec->capacity * 2, (int)index + 1), 4);
                CljVector *new_vec = make_vector_copy(old_vec, newcap);
                RELEASE((CljObject*)old_vec);
                old_vec = new_vec;
                vec = new_vec;  // Update vec to point to new vector
            }
            old_vec->count++;  // Increment count for append
        }
        
        // Ensure index is within bounds before accessing data[index]
        CLJ_ASSERT(index < (unsigned int)old_vec->capacity);
        
        // For CLJ_VECTOR_TRANSIENT_WEAK, don't RETAIN (weak reference)
        // For CLJ_VECTOR_TRANSIENT, always RETAIN
        if (index < old_vec->count && old_vec->data[index] && !is_transient && old_vec->base.type != CLJ_VECTOR_TRANSIENT_WEAK) {
            RELEASE(old_vec->data[index]);
        }
        old_vec->data[index] = (old_vec->base.type == CLJ_VECTOR_TRANSIENT_WEAK) ? value : RETAIN(value);
        if (is_transient && index == old_vec->count) {
            old_vec->count++;
        }
        return vec;  // Return same vector (in-place mutation) or new vector if capacity grew
    }

    // RC>1: Copy-on-Write - use make_vector_copy for efficiency
    // For CLJ_VECTOR_TRANSIENT_WEAK with index == count (append), ensure capacity is sufficient
    int new_capacity = old_vec->capacity;
    if (old_vec->base.type == CLJ_VECTOR_TRANSIENT_WEAK && index == old_vec->count) {
        // Append to weak vector - ensure capacity is at least index + 1, minimum 4
        new_capacity = MAX(MAX(old_vec->capacity * 2, (int)index + 1), 4);
    }
    CljVector* new_vec = make_vector_copy(old_vec, new_capacity);

    // For CLJ_VECTOR_TRANSIENT_WEAK append, increment count
    if (old_vec->base.type == CLJ_VECTOR_TRANSIENT_WEAK && index == old_vec->count) {
        new_vec->count++;
    }

    // Ensure index is within bounds before accessing data[index]
    CLJ_ASSERT(index < (unsigned int)new_vec->capacity);

    // Update element at index (replace the copied element)
    if (index < old_vec->count && new_vec->data[index] && old_vec->base.type != CLJ_VECTOR_TRANSIENT_WEAK) {
        RELEASE(new_vec->data[index]);
    }
    // For CLJ_VECTOR_TRANSIENT_WEAK, don't RETAIN (weak reference)
    new_vec->data[index] = old_vec->base.type == CLJ_VECTOR_TRANSIENT_WEAK ? value : RETAIN(value);

    return new_vec;  // Return new vector (COW)
}

// === Transient API (Phase 2) ===


/** Convert persistent vector to transient. */
CljVector* vector_transient(CljVector *vec) {    
    
    if (!vec) return NULL;
    if (vec->base.type == CLJ_VECTOR_TRANSIENT) {
        return vec;
    }
    
    // Handle empty vector singleton - create new transient vector with initial capacity
    if (vec->count == 0 && vec->capacity == 0) {
        // Create transient vector with initial capacity (not 0, so we can grow it)
        CljVector *tvec = make_vector(4, CLJ_VECTOR); // might throw oom
        if (!tvec) return NULL;
        tvec->base.type = CLJ_VECTOR_TRANSIENT;
        return tvec;
    }
    
    // Use make_vector_copy to create a copy with flexible array member
    CljVector *tvec = make_vector_copy(vec, vec->capacity);    
    // Change type to transient
    tvec->base.type = CLJ_VECTOR_TRANSIENT;
    
    return tvec;
}


/** Append to transient vector (guaranteed in-place). */
CljVector* clj_conj(CljVector *tvec, ID item) {
    return vector_conj(tvec, item);
}


/** Convert transient vector back to persistent. */
ID vector_persistent(CljVector *tvec) {
    
    if (!tvec || tvec->base.type == CLJ_VECTOR) {
        return (ID)tvec;
    }

    // Clojure-Semantik: Erstelle NEUE persistent collection
    // Use make_vector_copy to copy all elements efficiently
    CljVector* new_vec = make_vector_copy(tvec, tvec->capacity);
    if (!new_vec) return NULL;
    
    // Set type to CLJ_VECTOR (persistent, not transient)
    new_vec->base.type = CLJ_VECTOR;
    
    return (ID)new_vec;
}
