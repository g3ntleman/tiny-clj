#include "vector.h"
#include "memory.h"
#include "types.h"  // For SINGLETON_RC
#include "common.h"  // For CLJ_ASSERT
#include "exception.h"  // For throw_exception_formatted
#include "validation.h"  // For throw_index_out_of_bounds
#include <stdlib.h>
#include <stdbool.h>
#if !defined(ESP32_BUILD) && !defined(ESP_PLATFORM)
#include <execinfo.h>  // For backtrace
#endif
#include <unistd.h>    // For write

// CljPersistentVector struct definition (opaque pointer - only visible in vector.c)
// data is a flexible array member at the end of the struct
struct CljPersistentVector {
    CljObject base;
    unsigned int count;
    int capacity;
    ID data[];  // Flexible array member - elements stored at end of malloc block
};

// Empty-vector singleton: CLJ_VECTOR_PERSISTENT with rc=SINGLETON_RC, statically initialized
// Note: Flexible array member cannot be initialized, so we use a struct with no data array
static struct {
    CljObject base;
    int count;
    int capacity;
    // No data array for empty vector (capacity = 0)
} clj_empty_vector_singleton_data = {
    .base = { .type = CLJ_VECTOR_PERSISTENT, .rc = SINGLETON_RC },
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
    if (vec->base.type != CLJ_VECTOR_TRANSIENT) {
        return vec->count;
    }
    CljTransientVector *tvec = as_transient_vector(vec);
    return vector_count(tvec->backing_store);
}

/** Get vector capacity. Returns 0 if vec is NULL. */
int vector_capacity(CljPersistentVector *vec) {
    if (!vec) return 0;
    if (vec->base.type != CLJ_VECTOR_TRANSIENT) {
        return vec->capacity;
    }
    CljTransientVector *tvec = as_transient_vector(vec);
    return vector_capacity(tvec->backing_store);
}

/** Get element at index. Returns element or NULL if index out of bounds or nil.
 * Element lifetime is tied to the vector - caller must not release.
 * @param vec Vector to access
 * @param index Index (0-based)
 * @return Element or NULL (nil) - lifetime tied to vector
 */
ID vector_nth(CljPersistentVector *vec, unsigned int index) {
    if (vec) {
        if (vec->base.type == CLJ_VECTOR_TRANSIENT) {
            CljTransientVector *tvec = as_transient_vector(vec);
            return vector_nth(tvec->backing_store, index);
        }
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
int vector_index_of(CljPersistentVector *vec, ID value) {
    if (!vec) return INDEX_NOT_FOUND;
    if (vec->base.type == CLJ_VECTOR_TRANSIENT) {
        CljTransientVector *tvec = as_transient_vector(vec);
        return vector_index_of(tvec->backing_store, value);
    }
    
    VECTOR_FOR_EACH(vec, elem) {
        if (clj_equal(elem, value)) {
            return _i;
        }
    }
    
    return INDEX_NOT_FOUND;
}

/** Get raw data array pointer (no copying, direct access).
 * @param vec Vector to access
 * @return Pointer to data array (asserts vec != NULL)
 */
ID* vector_as_array(CljPersistentVector *vec) {
    CLJ_ASSERT(vec != NULL && "vector_as_array called with NULL");
    if (vec->base.type == CLJ_VECTOR_TRANSIENT) {
        CljTransientVector *tvec = as_transient_vector(vec);
        return vector_as_array(tvec->backing_store);
    }
    return vec->data;  // Flexible array member - points to end of struct
}

/** Increment count for transient vectors (internal use only).
 * @param vec Transient vector to increment count for
 */
void vector_increment_count(CljPersistentVector *vec) {
    CLJ_ASSERT(vec != NULL);
#if defined(DEBUG)
    int tag = TAG(vec);
    CLJ_ASSERT(tag == CLJ_VECTOR_TRANSIENT_WEAK);
#endif
    vec->count++;
}

void vector_clear(CljPersistentVector *vec) {
    CLJ_ASSERT(vec != NULL);
    if (vec->base.type == CLJ_VECTOR_TRANSIENT) {
        CljTransientVector *tvec = as_transient_vector(vec);
        // Clearing a transient should not mutate a shared backing store.
        // Swap to a fresh empty backing store and release the old one.
        CljPersistentVector *new_backing = make_vector(4, CLJ_VECTOR_PERSISTENT);
        if (!new_backing) return;
        ASSIGN(tvec->backing_store, new_backing);
        RELEASE(new_backing); // balance ASSIGN's RETAIN to keep rc==1 in the transient
        return;
    }
    
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
CljPersistentVector* vector_set_nth(CljPersistentVector* vec, unsigned int index, ID value) {
    if (!vec) {
        return NULL;
    }
    
    unsigned int tag = TAG(vec);
    if (tag != CLJ_VECTOR_TRANSIENT && tag != CLJ_VECTOR_TRANSIENT_WEAK) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                "vector_set_nth: cannot modify persistent vector. Use transient vector instead.");
        return NULL;
    }
    
    if (index < vector_count(vec)) {
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

size_t vector_make_copy_count(void) {
    return g_make_vector_copy_count;
}

void vector_make_copy_count_reset(void) {
    g_make_vector_copy_count = 0;
}

CljPersistentVector* make_vector_copy(CljPersistentVector* vec, unsigned capacity) {
    g_make_vector_copy_count++;
    
    if (!vec) return NULL;
    CljPersistentVector *v = vec;
    if (TAG(vec) == CLJ_VECTOR_TRANSIENT) {
        CljTransientVector *tvec = as_transient_vector(vec);
        v = vector_persistent(tvec);
    } else {
        v = as_vector(vec);
    }
    unsigned count = MIN(capacity, v->count); // trunkate as requested
    
    
    // Create new vector with specified capacity using make_vector
    // make_vector handles singleton case automatically (capacity==0 && type==CLJ_VECTOR_PERSISTENT)
    CljPersistentVector *vec_copy = make_vector(capacity, v->base.type);
    
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
/** Core implementation: Remove last element from vector.
 * Returns owned object (rc=1, no AUTORELEASE).
 */
static CljPersistentVector* vector_pop_core(CljPersistentVector* vec) {
    if (vec) {
        if (TAG(vec) == CLJ_VECTOR_TRANSIENT) {
            CljTransientVector *tvec = as_transient_vector(vec);
            CljPersistentVector *backing = tvec->backing_store;
            CljPersistentVector *new_backing = vector_pop(backing);
            ASSIGN(tvec->backing_store, new_backing);
            return vec;
        }
        CljPersistentVector *v = as_vector(vec);
        if (v && v->count > 0) {
            // Happy path: RC=1, mutate in-place (O(1))
            if (v->base.rc == 1) {
                // For CLJ_VECTOR_TRANSIENT_WEAK, don't RELEASE (weak reference)
                if (v->base.type != CLJ_VECTOR_TRANSIENT_WEAK) {
                    // Release last element (element lifetime is tied to vector)
                    ID last_elem = vector_nth(v, v->count - 1);
                    if (last_elem && !is_immediate(last_elem)) {
                        RELEASE(last_elem);
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
            CljPersistentVector *new_vec = make_vector_copy(v, original_count - 1);
            v->count = original_count;  // Restore original count
            
            return new_vec;  // owned (rc=1)
        }
        return vec;  // Empty vector, return as-is
    }
    return NULL;
}

CljPersistentVector* vector_pop(CljPersistentVector* vec) {
    CljPersistentVector* result = vector_pop_core(vec);
    if (result && result != vec) {
        return AUTORELEASE(result);
    }
    return result;
}

/** Core implementation: Insert element at index in vector (in-place if RC=1, COW if RC>1).
 * Returns owned object (rc=1, no AUTORELEASE).
 * @param vec Vector to insert into
 * @param index Index where to insert (0-based, must be <= count)
 * @param item Item to insert
 * @return Same vector (in-place) if RC=1, new vector if RC>1, or NULL on error
 */
static CljPersistentVector* vector_insert_at_core(CljPersistentVector* vec, unsigned int index, ID item) {
    if (!vec) return NULL;
    if (TAG(vec) == CLJ_VECTOR_TRANSIENT) {
        CljTransientVector *tvec = as_transient_vector(vec);
        CljPersistentVector *new_backing = vector_insert_at(tvec->backing_store, index, item);
        ASSIGN(tvec->backing_store, new_backing);
        return vec;
    }
    CljPersistentVector *v = as_vector(vec);
    if (!v || index > v->count) return vec;  // Invalid index, return as-is
    
    bool is_weak = (v->base.type == CLJ_VECTOR_TRANSIENT_WEAK);
    
    // Check if we need to grow capacity
    bool needs_growth = (v->count >= (unsigned int)v->capacity);
    
    // OPTIMIZATION: If RC=1 or transient weak vector, mutate in-place (O(n) due to shifting)
    if (v->base.rc == 1 || is_weak) {
        // Grow capacity if needed
        if (needs_growth) {
            int new_capacity = v->capacity * 2;
            if (new_capacity < 4) new_capacity = 4;
            CljPersistentVector *new_vec = make_vector_copy(v, new_capacity);
            if (!new_vec) return vec;  // Return original on OOM
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
    
    CljPersistentVector *new_vec = make_vector(new_capacity, v->base.type);
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
    
    return new_vec;  // owned (rc=1)
}

/** Insert element at index in vector (in-place if RC=1, COW if RC>1).
 * @param vec Vector to insert into
 * @param index Index where to insert (0-based, must be <= count)
 * @param item Item to insert
 * @return Same vector (in-place) if RC=1, new vector if RC>1, or NULL on error
 */
CljPersistentVector* vector_insert_at(CljPersistentVector* vec, unsigned int index, ID item) {
    CljPersistentVector* result = vector_insert_at_core(vec, index, item);
    if (result && result != vec) {
        unsigned char from_tag = TAG(vec);
        if (from_tag == CLJ_VECTOR_TRANSIENT || from_tag == CLJ_VECTOR_TRANSIENT_WEAK) {
            RELEASE(vec);
        }
        return AUTORELEASE(result);
    }
    return result;
}

/** Core implementation: Remove element at index from vector (in-place if RC=1, COW if RC>1).
 * Returns owned object (rc=1, no AUTORELEASE).
 * @param vec Vector to remove from
 * @param index Index of element to remove (0-based)
 * @return Same vector (in-place) if RC=1, new vector if RC>1, or NULL on error
 */
static CljPersistentVector* vector_remove_at_core(CljPersistentVector* vec, unsigned int index) {
    if (!vec) return NULL;
    if (TAG(vec) == CLJ_VECTOR_TRANSIENT) {
        CljTransientVector *tvec = as_transient_vector(vec);
        CljPersistentVector *new_backing = vector_remove_at(tvec->backing_store, index);
        ASSIGN(tvec->backing_store, new_backing);
        return vec;
    }
    CljPersistentVector *v = as_vector(vec);
    if (!v || index >= v->count) return vec;  // Invalid index, return as-is
    
    // OPTIMIZATION: If RC=1 or transient weak vector, mutate in-place (O(n) due to shifting)
    int tag = TAG(vec);
    bool is_weak = (tag == CLJ_VECTOR_TRANSIENT_WEAK);
    if (v->base.rc == 1 || is_weak) {
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
    CljPersistentVector *new_vec = make_vector(v->capacity, v->base.type);
    if (!new_vec) return vec;  // Return original vector on OOM
    
    new_vec->count = new_count;
    
    // Copy all elements except the one at index
    for (unsigned int i = 0, j = 0; i < v->count; i++) {
        if (i != index) {
            new_vec->data[j++] = RETAIN(v->data[i]);
        }
    }
    
    return new_vec;  // owned (rc=1)
}

/** Remove element at index from vector (in-place if RC=1, COW if RC>1).
 * @param vec Vector to remove from
 * @param index Index of element to remove (0-based)
 * @return Same vector (in-place) if RC=1, new vector if RC>1, or NULL on error
 */
CljPersistentVector* vector_remove_at(CljPersistentVector* vec, unsigned int index) {
    CljPersistentVector* result = vector_remove_at_core(vec, index);
    if (result && result != vec) {
        return AUTORELEASE(result);
    }
    return result;
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
    if (type == CLJ_VECTOR_TRANSIENT) {
        CljPersistentVector *backing = make_vector(capacity ? capacity : 4, CLJ_VECTOR_PERSISTENT);
        if (!backing) return NULL;

        CljTransientVector *tvec = ALLOC(CljTransientVector, 1);
        if (!tvec) {
            RELEASE(backing);
            throw_oom();
        }
        tvec->base.type = CLJ_VECTOR_TRANSIENT;
        tvec->base.rc = 1;
        tvec->backing_store = backing;
        return (CljPersistentVector*)tvec;
    }
    if (capacity == 0 && type == CLJ_VECTOR_PERSISTENT) {
        return clj_empty_vector_singleton;
    }
    // Allocate struct + flexible array member in one block
    g_make_vector_count++;
    size_t struct_size = sizeof(CljPersistentVector);
    size_t data_size = (size_t)capacity * sizeof(ID);
    size_t total_size = struct_size + data_size;
    
    // Use malloc instead of calloc - we set all fields manually and data[] is filled by vector_conj
    CljPersistentVector *vec = alloc(total_size, 1, type);

    vec->base.type = type;
    vec->base.rc = 1;
    vec->count = 0;
    vec->capacity = capacity;
    // data[] is flexible array member - already allocated at end of struct
    // No need to initialize data[] - elements are set by vector_conj

    return vec;
}

/** Core implementation: Return a new vector with item appended; original vector remains unchanged.
 * Returns owned object (rc=1, no AUTORELEASE).
 * Behavior depends on vector type:
 * - CLJ_VECTOR_PERSISTENT: Uses Copy-on-Write (RC=1 → in-place mutation, RC>1 → COW)
 * - CLJ_VECTOR_TRANSIENT: Always mutates in-place (transient behavior)
 * - CLJ_VECTOR_TRANSIENT_WEAK: Always mutates in-place (transient behavior, weak references)
 */
static CljPersistentVector* vector_conj_core(CljPersistentVector* vec, ID item) {
    if (!vec) return NULL;

    // Note: item can be NULL (nil) - it's a valid value in Clojure collections

    if (vec->base.type == CLJ_VECTOR_TRANSIENT) {
        CljTransientVector *tvec = as_transient_vector(vec);
        CljPersistentVector *backing = tvec->backing_store;
        CljPersistentVector *new_backing = vector_conj(backing, item);
        ASSIGN(tvec->backing_store, new_backing);
        return vec;  // Always return same transient pointer
    }

    CljPersistentVector *old_vec = as_vector(vec);

    CljType vec_type = old_vec->base.type;
    bool is_weak = (vec_type == CLJ_VECTOR_TRANSIENT_WEAK);

    // For transient weak vectors, always mutate in-place
    if (is_weak) {
        CljPersistentVector *v = old_vec;
        
        // Ensure capacity is sufficient - use make_vector_copy for safety
        if (v->count >= (unsigned int)v->capacity || v->capacity == 0) {
            int newcap = MAX(v->capacity * 2, 4);
            CljPersistentVector *new_v = make_vector_copy(v, newcap);
            // Preserve vector type
            new_v->base.type = vec_type;
            v = new_v;  // Use new vector for adding item
            vec = new_v;  // Update return value
        }
        
        // For CLJ_VECTOR_TRANSIENT_WEAK, don't RETAIN (weak reference)
        v->data[v->count++] = item;
        
        return vec; // In-place mutation (or new vector if capacity grew)
    }

    // For CLJ_VECTOR_PERSISTENT: Use COW semantics
    // HOT-PATH: RC=1 && capacity OK → direct in-place mutation (no branches)
    // Most common case: single owner, enough capacity
    if (old_vec->base.rc == 1 && old_vec->count < (unsigned int)old_vec->capacity) {
        old_vec->data[old_vec->count++] = item ? RETAIN(item) : NULL;
        return vec;  // Return same vector (in-place mutation)
    }

    // Early returns for uncommon cases
    if (old_vec->base.rc == 0) {
        // Empty vector singleton: create new vector
        CljPersistentVector* new_vec = make_vector(4, CLJ_VECTOR_PERSISTENT);
        if (!new_vec)
            return vec;
        new_vec->data[0] = item ? RETAIN(item) : NULL;
        new_vec->count = 1;
        return new_vec;  // owned (rc=1)
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
    new_vec->data[new_vec->count++] = item ? RETAIN(item) : NULL;
    
    return new_vec;  // owned (rc=1)
}

/** Return a new vector with item appended; original vector remains unchanged.
 * Behavior depends on vector type:
 * - CLJ_VECTOR_PERSISTENT: Uses Copy-on-Write (RC=1 → in-place mutation, RC>1 → COW)
 * - CLJ_VECTOR_TRANSIENT: Always mutates in-place (transient behavior)
 * - CLJ_VECTOR_TRANSIENT_WEAK: Always mutates in-place (transient behavior, weak references)
 */
CljPersistentVector* vector_conj(CljPersistentVector* vec, ID item) {
    CljPersistentVector* result = vector_conj_core(vec, item);
    if (result && result != vec) {
        unsigned char from_tag = TAG(vec);
        if (from_tag == CLJ_VECTOR_TRANSIENT || from_tag == CLJ_VECTOR_TRANSIENT_WEAK) {
            RELEASE(vec);
        }
        return AUTORELEASE(result);
    }
    return result;  // result == vec (in-place), no new object created
}

/** Core implementation: Update element at index with COW: RC=1 → in-place, RC>1 → COW.
 * Returns owned object (rc=1, no AUTORELEASE).
 * Note: value can be NULL (nil) - that's a valid value in Clojure!
 */
static CljPersistentVector* vector_assoc_core(CljPersistentVector* vec, unsigned int index, ID value) {
    if (!vec) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                "vector_assoc: vector is NULL");
    }
    // Note: value can be NULL (nil) - that's a valid value in Clojure!
    
    CLJ_ASSERT(vec->base.type == CLJ_VECTOR_PERSISTENT || vec->base.type == CLJ_VECTOR_TRANSIENT_WEAK || vec->base.type == CLJ_VECTOR_TRANSIENT);

    if (vec->base.type == CLJ_VECTOR_TRANSIENT) {
        CljTransientVector *tvec = as_transient_vector(vec);
        CljPersistentVector *backing = tvec->backing_store;

        // transient assoc allows index == count (append), but not index > count
        if (index > backing->count) {
            throw_index_out_of_bounds("vector_assoc", index, backing->count, "transient vector");
            return vec;
        }

        // Append is handled via conj to reuse growth logic.
        if (index == backing->count) {
            CljPersistentVector *new_backing = vector_conj(backing, value);
            ASSIGN(tvec->backing_store, new_backing);
            return vec;
        }

        // Overwrite in-place requires exclusive backing (avoid mutating shared persistent vectors).
        if (is_singleton((CljObject*)backing) || backing->base.type != CLJ_VECTOR_PERSISTENT || backing->base.rc != 1) {
            CljPersistentVector *new_backing = NULL;
            if (backing->count == 0 && backing->capacity == 0) {
                new_backing = make_vector(4, CLJ_VECTOR_PERSISTENT);
            } else {
                new_backing = make_vector_copy(backing, backing->capacity);
            }
            if (!new_backing) return vec;
            new_backing->base.type = CLJ_VECTOR_PERSISTENT;
            ASSIGN(tvec->backing_store, new_backing);
            RELEASE(new_backing); // keep rc==1 for transient backing
            backing = tvec->backing_store;
        }

        // Overwrite in-place. IMPORTANT: transient semantics do NOT release old value.
        backing->data[index] = value ? RETAIN(value) : NULL;
        return vec;
    }

    CljPersistentVector *old_vec = as_vector(vec);
    
    bool is_weak = (old_vec->base.type == CLJ_VECTOR_TRANSIENT_WEAK);
    
    // For CLJ_VECTOR_TRANSIENT_WEAK, allow index == count (append)
    if (!is_weak && index >= old_vec->count) {
        throw_index_out_of_bounds("vector_assoc", index, old_vec->count, "vector");
    }
    if (is_weak && index > old_vec->count) {
        throw_index_out_of_bounds("vector_assoc", index, old_vec->count, "weak vector");
    }

    // Empty vector singleton (RC=0): Not applicable (index >= count)
    if (old_vec->base.rc == 0) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                "vector_assoc: cannot modify empty vector singleton");
    }

    // OPTIMIZATION: If RC=1 or transient weak, mutate in-place
    if (old_vec->base.rc == 1 || is_weak) {
        // For CLJ_VECTOR_TRANSIENT_WEAK with index == count (append), we need to grow capacity first
        if (is_weak && index == old_vec->count) {
            // Append to weak vector - need to ensure capacity
            // After incrementing count, index must be < capacity
            // So we need capacity > index, i.e., capacity >= index + 1
            if (old_vec->capacity <= (int)index) {
                // Grow capacity using make_vector_copy
                // Ensure new capacity is at least index + 1, minimum 4
                int newcap = MAX(MAX(old_vec->capacity * 2, (int)index + 1), 4);
                CljPersistentVector *new_vec = make_vector_copy(old_vec, newcap);
                old_vec = new_vec;
                vec = new_vec;  // Update vec to point to new vector
            }
            old_vec->count++;  // Increment count for append
        }
        
        // Ensure index is within bounds before accessing data[index]
        CLJ_ASSERT(index < (unsigned int)old_vec->capacity);
        
        // For CLJ_VECTOR_TRANSIENT_WEAK, don't RETAIN (weak reference)
        // For CLJ_VECTOR_TRANSIENT, always RETAIN
        if (index < old_vec->count && old_vec->data[index] && !is_weak) {
            RELEASE(old_vec->data[index]);
        }
        old_vec->data[index] = is_weak ? value : RETAIN(value);
        if (is_weak && index == old_vec->count) {
            old_vec->count++;
        }
        return vec;  // Return same vector (in-place mutation) or new vector if capacity grew
    }

    // RC>1: Copy-on-Write - use make_vector_copy for efficiency
    // For CLJ_VECTOR_TRANSIENT_WEAK with index == count (append), ensure capacity is sufficient
    int new_capacity = old_vec->capacity;
    if (is_weak && index == old_vec->count) {
        // Append to weak vector - ensure capacity is at least index + 1, minimum 4
        new_capacity = MAX(MAX(old_vec->capacity * 2, (int)index + 1), 4);
    }
    CljPersistentVector* new_vec = make_vector_copy(old_vec, new_capacity);

    // For CLJ_VECTOR_TRANSIENT_WEAK append, increment count
    if (is_weak && index == old_vec->count) {
        new_vec->count++;
    }

    // Ensure index is within bounds before accessing data[index]
    CLJ_ASSERT(index < (unsigned int)new_vec->capacity);

    // Update element at index (replace the copied element)
    if (index < old_vec->count && new_vec->data[index] && !is_weak) {
        RELEASE(new_vec->data[index]);
    }
    // For CLJ_VECTOR_TRANSIENT_WEAK, don't RETAIN (weak reference)
    new_vec->data[index] = is_weak ? value : RETAIN(value);

    return new_vec;  // owned (rc=1)
}

/** Update element at index with COW: RC=1 → in-place, RC>1 → COW.
 * Note: value can be NULL (nil) - that's a valid value in Clojure!
 */
CljPersistentVector* vector_assoc(CljPersistentVector* vec, unsigned int index, ID value) {
    CljPersistentVector* result = vector_assoc_core(vec, index, value);
    if (result && result != vec) {
        unsigned char from_tag = TAG(vec);
        if (from_tag == CLJ_VECTOR_TRANSIENT || from_tag == CLJ_VECTOR_TRANSIENT_WEAK) {
            RELEASE(vec);
        }
        return AUTORELEASE(result);  // New object created, autorelease it
    }
    return result;  // result == vec (in-place), no new object created
}

// === _owned functions (for internal use) ===

static CljPersistentVector* vector_conj_owned(CljPersistentVector* vec, ID item) {
    return vector_conj_core(vec, item);
}

static CljPersistentVector* vector_assoc_owned(CljPersistentVector* vec, unsigned int index, ID value) {
    return vector_assoc_core(vec, index, value);
}

static CljPersistentVector* vector_insert_at_owned(CljPersistentVector* vec, unsigned int index, ID item) {
    return vector_insert_at_core(vec, index, item);
}

static CljPersistentVector* vector_remove_at_owned(CljPersistentVector* vec, unsigned int index) {
    return vector_remove_at_core(vec, index);
}

static CljPersistentVector* vector_pop_owned(CljPersistentVector* vec) {
    return vector_pop_core(vec);
}

// === _inplace functions (for long-lived slots) ===

void vector_conj_inplace(CljPersistentVector **vec_slot, ID item) {
    if (!vec_slot || !*vec_slot) return;
    CljPersistentVector *current = *vec_slot;
    CljPersistentVector *updated = vector_conj_owned(current, item);
    if (updated && updated != current) {
        RELEASE(current);
        *vec_slot = updated;
    }
}

void vector_assoc_inplace(CljPersistentVector **vec_slot, unsigned int index, ID value) {
    if (!vec_slot || !*vec_slot) return;
    CljPersistentVector *current = *vec_slot;
    CljPersistentVector *updated = vector_assoc_owned(current, index, value);
    if (updated && updated != current) {
        RELEASE(current);
        *vec_slot = updated;
    }
}

void vector_insert_at_inplace(CljPersistentVector **vec_slot, unsigned int index, ID item) {
    if (!vec_slot || !*vec_slot) return;
    CljPersistentVector *current = *vec_slot;
    CljPersistentVector *updated = vector_insert_at_owned(current, index, item);
    if (updated && updated != current) {
        RELEASE(current);
        *vec_slot = updated;
    }
}

void vector_remove_at_inplace(CljPersistentVector **vec_slot, unsigned int index) {
    if (!vec_slot || !*vec_slot) return;
    CljPersistentVector *current = *vec_slot;
    CljPersistentVector *updated = vector_remove_at_owned(current, index);
    if (updated && updated != current) {
        RELEASE(current);
        *vec_slot = updated;
    }
}

void vector_pop_inplace(CljPersistentVector **vec_slot) {
    if (!vec_slot || !*vec_slot) return;
    CljPersistentVector *current = *vec_slot;
    CljPersistentVector *updated = vector_pop_owned(current);
    if (updated && updated != current) {
        RELEASE(current);
        *vec_slot = updated;
    }
}

// === Transient API (Phase 2) ===


/** Convert persistent vector to transient. */
CljTransientVector* vector_transient(CljPersistentVector *vec) {
    
    if (!vec) return NULL;
    if (vec->base.type == CLJ_VECTOR_TRANSIENT) {
        return as_transient_vector((ID)vec);
    }
    if (vec->base.type == CLJ_VECTOR_TRANSIENT_WEAK) {
        // CLJ_VECTOR_TRANSIENT_WEAK is currently unused; avoid inventing semantics here.
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "vector_transient: cannot convert CLJ_VECTOR_TRANSIENT_WEAK to transient");
        return NULL;
    }
    
    // Wrap the persistent vector as backing store. We RETAIN it here, and the first
    // mutating operation will copy-on-write if the backing is shared (rc>1).
    CljPersistentVector *backing = NULL;
    if (is_singleton((CljObject*)vec) || (vec->count == 0 && vec->capacity == 0)) {
        // Don't ever mutate the empty singleton; allocate a small backing store instead.
        backing = make_vector(4, CLJ_VECTOR_PERSISTENT);
    } else {
        backing = RETAIN(vec);
    }
    if (!backing) return NULL;

    CljTransientVector *tvec = ALLOC(CljTransientVector, 1);
    if (!tvec) {
        RELEASE(backing);
        throw_oom();
    }
    tvec->base.type = CLJ_VECTOR_TRANSIENT;
    tvec->base.rc = 1;
    tvec->backing_store = backing;
    return tvec;
}


/** Append to transient vector (guaranteed in-place). */
void clj_conj(CljTransientVector *tvec, ID item) {
    // Mutates in-place; backing_store may be replaced (growth/COW), but the transient wrapper stays stable.
    (void)vector_conj((CljPersistentVector*)tvec, item);
}


/** Convert transient vector back to persistent. */
CljPersistentVector* vector_persistent(CljTransientVector *tvec) {
    if (!tvec) {
        return NULL;
    }
    CLJ_ASSERT(tvec->base.type == CLJ_VECTOR_TRANSIENT);
    // Return backing_store (persistent CLJ_VECTOR_PERSISTENT).
    return tvec->backing_store;
}
