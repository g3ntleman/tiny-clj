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

// CljVector struct definition (opaque pointer - only visible in vector.c)
// data is a flexible array member at the end of the struct
struct CljVector {
    CljObject base;
    unsigned int count;
    int capacity;
    ID data[];  // Flexible array member - elements stored at end of malloc block
};

// Empty-vector singleton: CLJ_VECTOR_PERSISTENT_PERSISTENT with rc=SINGLETON_RC, statically initialized
// Note: Flexible array member cannot be initialized, so we use a struct with no data array
static struct {
    CljObject base;
    int count;
    int capacity;
    // No data array for empty vector (capacity = 0)
} clj_empty_vector_singleton_data = {
    .base = { .type = CLJ_VECTOR_PERSISTENT_PERSISTENT, .rc = SINGLETON_RC },
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
    if (vec->base.type != CLJ_VECTOR_PERSISTENT_TRANSIENT) {
        return vec->count;
    }
    CljTransientVector *tvec = (CljTransientVector*)vec;
    return vector_count((CljVector*)tvec->backing_store);
}

/** Get vector capacity. Returns 0 if vec is NULL. */
int vector_capacity(CljVector *vec) {
    if (!vec) return 0;
    if (vec->base.type != CLJ_VECTOR_PERSISTENT_TRANSIENT) {
        return vec->capacity;
    }
    CljTransientVector *tvec = (CljTransientVector*)vec;
    return vector_capacity((CljVector*)tvec->backing_store);
}

/** Get element at index. Returns element or NULL if index out of bounds or nil.
 * Element lifetime is tied to the vector - caller must not release.
 * @param vec Vector to access
 * @param index Index (0-based)
 * @return Element or NULL (nil) - lifetime tied to vector
 */
ID vector_nth(CljVector *vec, unsigned int index) {
    if (vec) {
        if (vec->base.type == CLJ_VECTOR_PERSISTENT_TRANSIENT) {
            CljTransientVector *tvec = (CljTransientVector*)vec;
            return vector_nth((CljVector*)tvec->backing_store, index);
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

int vector_index_of(CljVector *vec, ID value) {
    if (!vec) return INDEX_NOT_FOUND;
    if (vec->base.type == CLJ_VECTOR_PERSISTENT_TRANSIENT) {
        CljTransientVector *tvec = (CljTransientVector*)vec;
        return vector_index_of((CljVector*)tvec->backing_store, value);
    }
    
    VECTOR_FOR_EACH(vec, elem) {
        if (clj_equal(elem, value)) {
            return _i;
        }
    }
    
    return INDEX_NOT_FOUND;
}

ID* vector_as_array(CljVector *vec) {
    CLJ_ASSERT(vec != NULL && "vector_as_array called with NULL");
    if (vec->base.type == CLJ_VECTOR_PERSISTENT_TRANSIENT) {
        CljTransientVector *tvec = (CljTransientVector*)vec;
        return vector_as_array((CljVector*)tvec->backing_store);
    }
    return vec->data;  // Flexible array member - points to end of struct
}

void vector_increment_count(CljVector *vec) {
    CLJ_ASSERT(vec != NULL);
#if defined(DEBUG)
    int tag = TAG(vec);
    CLJ_ASSERT(tag == CLJ_VECTOR_PERSISTENT_TRANSIENT_WEAK);
#endif
    vec->count++;
}

void vector_clear(CljVector *vec) {
    CLJ_ASSERT(vec != NULL);
<<<<<<< HEAD
    if (vec->base.type == CLJ_VECTOR_PERSISTENT_TRANSIENT) {
        CljTransientVector *tvec = (CljTransientVector*)vec;
        vector_clear((CljVector*)tvec->backing_store);
        return;
    }
    
    if (vec->base.type != CLJ_VECTOR_PERSISTENT_TRANSIENT_WEAK) {
        VECTOR_FOR_EACH(vec, elem) {
        RELEASE(elem);
        }
=======
    if (vec->base.type != CLJ_VECTOR_TRANSIENT_WEAK) {
        VECTOR_FOR_EACH(vec, elem) { RELEASE(elem); }
>>>>>>> af65598be4ce5ad0372af581ec81496acb467be8
    }
    vec->count = 0;
}

void vector_truncate(CljVector *vec, unsigned int n) {
    CLJ_ASSERT(vec && n <= vec->count);
    vec->count = n;
}

CljVector* vector_set_nth(CljVector* vec, unsigned int index, ID value) {
    if (!vec) {
        return NULL;
    }
    
<<<<<<< HEAD
    unsigned int tag = TAG(vec);
    if (tag != CLJ_VECTOR_PERSISTENT_TRANSIENT && tag != CLJ_VECTOR_PERSISTENT_TRANSIENT_WEAK) {
=======
    CljVector *v = as_vector(vec);
    if (!v) {
        return NULL;
    }
    
    if (v->base.type != CLJ_VECTOR_TRANSIENT && v->base.type != CLJ_VECTOR_TRANSIENT_WEAK) {
>>>>>>> af65598be4ce5ad0372af581ec81496acb467be8
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                "vector_set_nth: cannot modify persistent vector. Use transient vector instead.");
        return NULL;
    }
    
    if (index < vector_count(vec)) {
        return vector_assoc(vec, index, value);
    }
    
    return NULL;
}

static size_t g_make_vector_copy_count = 0;

size_t vector_make_copy_count(void) {
    return g_make_vector_copy_count;
}

void vector_make_copy_count_reset(void) {
    g_make_vector_copy_count = 0;
}

CljVector* make_vector_copy(CljVector* vec, unsigned capacity) {
    g_make_vector_copy_count++;
    if (!vec) return NULL;
<<<<<<< HEAD
    CljVector *v = vec;
    if (TAG(vec) == CLJ_VECTOR_PERSISTENT_TRANSIENT) {
        CljTransientVector *tvec = as_transient_vector(vec);
        v = transient_vector_backing_store(tvec);
    } else {
        v = as_vector(vec);
    }
    unsigned count = MIN(capacity, v->count); // trunkate as requested
    
    
    // Create new vector with specified capacity using make_vector
    // make_vector handles singleton case automatically (capacity==0 && type==CLJ_VECTOR_PERSISTENT)
    CljVector *vec_copy = make_vector(capacity, v->base.type);
    
    // Set count to match original vector (make_vector sets count=0)
    vec_copy->count = count;
    
    // Copy all elements (including nil/NULL - nil is a valid value in Clojure)
    // For CLJ_VECTOR_PERSISTENT_TRANSIENT_WEAK, don't RETAIN (weak reference)
    // But when copying, we need to retain for normal vectors to maintain reference counting
    // Copy all elements (count was already set to MIN(capacity, v->count) above)
    for (unsigned i = 0; i < count; ++i) {
        ID elem = v->data[i];
        vec_copy->data[i] = (v->base.type == CLJ_VECTOR_PERSISTENT_TRANSIENT_WEAK) ? elem : (elem ? RETAIN(elem) : NULL);
    }
    
=======
    CljVector *v = as_vector(vec);
    unsigned count = MIN(capacity, v->count);
    CljType base_type = (v->base.type == CLJ_VECTOR_TRANSIENT) ? CLJ_VECTOR : v->base.type;
    CljVector *vec_copy = make_vector(capacity, base_type);
    if (v->base.type == CLJ_VECTOR_TRANSIENT) vec_copy->base.type = CLJ_VECTOR_TRANSIENT;
    vec_copy->count = count;
    for (unsigned i = 0; i < count; i++)
        vec_copy->data[i] = (v->base.type == CLJ_VECTOR_TRANSIENT_WEAK) ? v->data[i] : (v->data[i] ? RETAIN(v->data[i]) : NULL);
>>>>>>> af65598be4ce5ad0372af581ec81496acb467be8
    return vec_copy;
}

static CljVector* vector_pop_core(CljVector* vec, int autorelease_new) {
    if (vec) {
        if (TAG(vec) == CLJ_VECTOR_PERSISTENT_TRANSIENT) {
            CljTransientVector *tvec = as_transient_vector(vec);
            CljVector *new_backing = vector_pop((CljVector*)tvec->backing_store);
            ASSIGN(tvec->backing_store, (CljPersistentVector*)new_backing);
            return vec;
        }
        CljVector *v = as_vector(vec);
        if (v && v->count > 0) {
            if (v->base.rc == 1) {
<<<<<<< HEAD
                // For CLJ_VECTOR_PERSISTENT_TRANSIENT_WEAK, don't RELEASE (weak reference)
                if (v->base.type != CLJ_VECTOR_PERSISTENT_TRANSIENT_WEAK) {
                    // Release last element (element lifetime is tied to vector)
=======
                if (v->base.type != CLJ_VECTOR_TRANSIENT_WEAK) {
>>>>>>> af65598be4ce5ad0372af581ec81496acb467be8
                    ID last_elem = vector_nth(v, v->count - 1);
                    if (last_elem && !is_immediate(last_elem)) RELEASE(last_elem);
                }
                v->count--;
                return vec;
            }
            int original_count = v->count;
            v->count = original_count - 1;
            CljVector *new_vec = make_vector_copy(v, original_count - 1);
            if (autorelease_new) new_vec = AUTORELEASE(new_vec);
            v->count = original_count;
            return new_vec;
        }
        return vec;
    }
    return NULL;
}

<<<<<<< HEAD
CljVector* vector_pop(CljVector* vec) {
    CljVector* result = vector_pop_core(vec);
    if (result && result != vec) {
        return AUTORELEASE(result);
    }
    return result;
}
=======
CljVector* vector_pop(CljVector* vec) { return vector_pop_core(vec, 1); }

CljVector* vector_pop_owned(CljVector* vec) { return vector_pop_core(vec, 0); }
>>>>>>> af65598be4ce5ad0372af581ec81496acb467be8

static CljVector* vector_insert_at_core(CljVector* vec, unsigned int index, ID item) {
    if (!vec) return NULL;
    if (TAG(vec) == CLJ_VECTOR_PERSISTENT_TRANSIENT) {
        CljTransientVector *tvec = as_transient_vector(vec);
        CljVector *new_backing = vector_insert_at((CljVector*)tvec->backing_store, index, item);
        ASSIGN(tvec->backing_store, (CljPersistentVector*)new_backing);
        return vec;
    }
    CljVector *v = as_vector(vec);
<<<<<<< HEAD
    if (!v || index > v->count) return vec;  // Invalid index, return as-is
    
    bool is_weak = (v->base.type == CLJ_VECTOR_PERSISTENT_TRANSIENT_WEAK);
    
    // Check if we need to grow capacity
    bool needs_growth = (v->count >= (unsigned int)v->capacity);
    
    // OPTIMIZATION: If RC=1 or transient weak vector, mutate in-place (O(n) due to shifting)
    if (v->base.rc == 1 || is_weak) {
        // Grow capacity if needed
        if (needs_growth) {
            int new_capacity = v->capacity * 2;
            if (new_capacity < 4) new_capacity = 4;
            CljVector *new_vec = make_vector_copy(v, new_capacity);
            if (!new_vec) return vec;  // Return original on OOM
=======
    if (!v || index > v->count) return vec;
    bool is_weak = (v->base.type == CLJ_VECTOR_TRANSIENT_WEAK);
    bool is_transient = (TAG(vec) == CLJ_VECTOR_TRANSIENT);
    bool needs_growth = (v->count >= (unsigned int)v->capacity);
    if (v->base.rc == 1 || is_transient) {
        if (needs_growth) {
            int nc = v->capacity * 2;
            if (nc < 4) nc = 4;
            CljVector *new_vec = make_vector_copy(v, nc);
            if (!new_vec) return vec;
            if (is_transient) new_vec->base.type = CLJ_VECTOR_TRANSIENT;
>>>>>>> af65598be4ce5ad0372af581ec81496acb467be8
            v = new_vec;
            vec = new_vec;
        }
        for (unsigned int i = v->count; i > index; i--) v->data[i] = v->data[i - 1];
        v->data[index] = is_weak ? item : (item ? RETAIN(item) : NULL);
        v->count++;
        return vec;
    }
    int nc = v->capacity;
    if (needs_growth) { nc = v->capacity * 2; if (nc < 4) nc = 4; }
    CljVector *new_vec = make_vector(nc, v->base.type);
    if (!new_vec) return vec;
    new_vec->count = v->count + 1;
    for (unsigned int i = 0; i < index; i++)
        new_vec->data[i] = is_weak ? v->data[i] : (v->data[i] ? RETAIN(v->data[i]) : NULL);
    new_vec->data[index] = is_weak ? item : (item ? RETAIN(item) : NULL);
    for (unsigned int i = index; i < v->count; i++)
        new_vec->data[i + 1] = is_weak ? v->data[i] : (v->data[i] ? RETAIN(v->data[i]) : NULL);
    return new_vec;
}

CljVector* vector_insert_at(CljVector* vec, unsigned int index, ID item) {
    CljVector* result = vector_insert_at_core(vec, index, item);
    if (result && result != vec) {
        unsigned char from_tag = TAG(vec);
        if (from_tag == CLJ_VECTOR_PERSISTENT_TRANSIENT || from_tag == CLJ_VECTOR_PERSISTENT_TRANSIENT_WEAK) {
            RELEASE(vec);
        }
        return AUTORELEASE(result);
    }
    return result;
}

static CljVector* vector_remove_at_core(CljVector* vec, unsigned int index, int autorelease_new) {
    if (!vec) return NULL;
    if (TAG(vec) == CLJ_VECTOR_PERSISTENT_TRANSIENT) {
        CljTransientVector *tvec = as_transient_vector(vec);
        CljVector *new_backing = vector_remove_at((CljVector*)tvec->backing_store, index);
        ASSIGN(tvec->backing_store, (CljPersistentVector*)new_backing);
        return vec;
    }
    CljVector *v = as_vector(vec);
<<<<<<< HEAD
    if (!v || index >= v->count) return vec;  // Invalid index, return as-is
    
    // OPTIMIZATION: If RC=1 or transient weak vector, mutate in-place (O(n) due to shifting)
    int tag = TAG(vec);
    bool is_weak = (tag == CLJ_VECTOR_PERSISTENT_TRANSIENT_WEAK);
    if (v->base.rc == 1 || is_weak) {
        // Release element at index (only for non-weak vectors)
        if (v->data[index] && tag != CLJ_VECTOR_PERSISTENT_TRANSIENT_WEAK) {
            RELEASE(v->data[index]);
        }
        // Shift elements left
        for (unsigned int i = index + 1; i < v->count; i++) {
            v->data[i - 1] = v->data[i];
        }
=======
    if (!v || index >= v->count) return vec;
    int tag = TAG(vec);
    bool is_transient = (tag == CLJ_VECTOR_TRANSIENT || tag == CLJ_VECTOR_TRANSIENT_WEAK);
    if (v->base.rc == 1 || is_transient) {
        if (v->data[index] && tag != CLJ_VECTOR_TRANSIENT_WEAK) RELEASE(v->data[index]);
        for (unsigned int i = index + 1; i < v->count; i++) v->data[i - 1] = v->data[i];
>>>>>>> af65598be4ce5ad0372af581ec81496acb467be8
        v->count--;
        return vec;
    }
    int new_count = v->count - 1;
    CljVector *new_vec = make_vector(v->capacity, v->base.type);
    if (!new_vec) return vec;
    if (autorelease_new) new_vec = AUTORELEASE(new_vec);
    new_vec->count = new_count;
    for (unsigned int i = 0, j = 0; i < v->count; i++)
        if (i != index) new_vec->data[j++] = RETAIN(v->data[i]);
    return new_vec;
}

<<<<<<< HEAD
/** Remove element at index from vector (in-place if RC=1, COW if RC>1).
 * @param vec Vector to remove from
 * @param index Index of element to remove (0-based)
 * @return Same vector (in-place) if RC=1, new vector if RC>1, or NULL on error
 */
CljVector* vector_remove_at(CljVector* vec, unsigned int index) {
    CljVector* result = vector_remove_at_core(vec, index);
    if (result && result != vec) {
        return AUTORELEASE(result);
    }
    return result;
}
=======
CljVector* vector_remove_at(CljVector* vec, unsigned int index) { return vector_remove_at_core(vec, index, 1); }
>>>>>>> af65598be4ce5ad0372af581ec81496acb467be8

static CljVector* vector_remove_at_owned(CljVector* vec, unsigned int index) { return vector_remove_at_core(vec, index, 0); }

static size_t g_make_vector_count = 0;

CljVector* make_vector(unsigned int capacity, CljType type) {
    if (type == CLJ_VECTOR_PERSISTENT_TRANSIENT) {
        CljVector *backing = make_vector(capacity ? capacity : 4, CLJ_VECTOR_PERSISTENT);
        if (!backing) return NULL;

        CljTransientVector *tvec = (CljTransientVector*)alloc(sizeof(CljTransientVector), 1, CLJ_VECTOR_PERSISTENT_TRANSIENT);
        if (!tvec) {
            RELEASE(backing);
            throw_oom();
        }
        tvec->base.type = CLJ_VECTOR_PERSISTENT_TRANSIENT;
        tvec->base.rc = 1;
    tvec->backing_store = (CljPersistentVector*)backing;
        return (CljVector*)tvec;
    }
    if (capacity == 0 && type == CLJ_VECTOR_PERSISTENT) {
        return clj_empty_vector_singleton;
    }
    g_make_vector_count++;
    size_t total = sizeof(CljVector) + (size_t)capacity * sizeof(ID);
    CljVector *vec = (CljVector*)alloc(total, 1, type);
    vec->base.type = type;
    vec->base.rc = 1;
    vec->count = 0;
    vec->capacity = capacity;
    return vec;
}

<<<<<<< HEAD
/** Core implementation: Return a new vector with item appended; original vector remains unchanged.
 * Returns owned object (rc=1, no AUTORELEASE).
 * Behavior depends on vector type:
 * - CLJ_VECTOR_PERSISTENT: Uses Copy-on-Write (RC=1 → in-place mutation, RC>1 → COW)
 * - CLJ_VECTOR_PERSISTENT_TRANSIENT: Always mutates in-place (transient behavior)
 * - CLJ_VECTOR_PERSISTENT_TRANSIENT_WEAK: Always mutates in-place (transient behavior, weak references)
 */
static CljVector* vector_conj_core(CljVector* vec, ID item) {
    if (!vec) return NULL;

    // Note: item can be NULL (nil) - it's a valid value in Clojure collections

    if (vec->base.type == CLJ_VECTOR_PERSISTENT_TRANSIENT) {
        CljTransientVector *tvec = (CljTransientVector*)vec;
        CljVector *new_backing = vector_conj((CljVector*)tvec->backing_store, item);
        ASSIGN(tvec->backing_store, (CljPersistentVector*)new_backing);
        return vec;  // Always return same transient pointer
    }

=======
CljVector* vector_conj_owned(CljVector* vec, ID item) {
    if (!vec) return NULL;
>>>>>>> af65598be4ce5ad0372af581ec81496acb467be8
    CljVector *old_vec = as_vector(vec);

    CljType vec_type = old_vec->base.type;
    bool is_weak = (vec_type == CLJ_VECTOR_PERSISTENT_TRANSIENT_WEAK);

<<<<<<< HEAD
    // For transient weak vectors, always mutate in-place
    if (is_weak) {
=======
    if (is_transient || is_weak) {
>>>>>>> af65598be4ce5ad0372af581ec81496acb467be8
        CljVector *v = old_vec;
        if (v->count >= (unsigned int)v->capacity || v->capacity == 0) {
            int newcap = MAX(v->capacity * 2, 4);
            CljVector *new_v = make_vector_copy(v, newcap);
            // Preserve vector type
            new_v->base.type = vec_type;
            v = new_v;
            vec = new_v;
        }
<<<<<<< HEAD
        
        // For CLJ_VECTOR_PERSISTENT_TRANSIENT_WEAK, don't RETAIN (weak reference)
        v->data[v->count++] = item;
        
        return vec; // In-place mutation (or new vector if capacity grew)
    }

    // For CLJ_VECTOR_PERSISTENT: Use COW semantics
    // HOT-PATH: RC=1 && capacity OK → direct in-place mutation (no branches)
    // Most common case: single owner, enough capacity
=======
        v->data[v->count++] = is_weak ? item : (item ? RETAIN(item) : NULL);
        return vec;
    }
>>>>>>> af65598be4ce5ad0372af581ec81496acb467be8
    if (old_vec->base.rc == 1 && old_vec->count < (unsigned int)old_vec->capacity) {
        old_vec->data[old_vec->count++] = item ? RETAIN(item) : NULL;
        return vec;
    }
    if (old_vec->base.rc == 0) {
<<<<<<< HEAD
        // Empty vector singleton: create new vector
        CljVector* new_vec = make_vector(4, CLJ_VECTOR_PERSISTENT);
=======
        CljVector* new_vec = make_vector(4, CLJ_VECTOR);
>>>>>>> af65598be4ce5ad0372af581ec81496acb467be8
        if (!new_vec)
            return vec;
        new_vec->data[0] = item ? RETAIN(item) : NULL;
        new_vec->count = 1;
        return new_vec;
    }
    int new_capacity = old_vec->capacity;
    if (old_vec->count >= (unsigned int)old_vec->capacity) {
        new_capacity = old_vec->capacity * 2;
        if (new_capacity < 4) new_capacity = 4;
    }
    CljVector* new_vec = make_vector_copy(old_vec, new_capacity);
    if (!new_vec) return vec;
    new_vec->data[new_vec->count++] = item ? RETAIN(item) : NULL;
    return new_vec;
}

<<<<<<< HEAD
/** Return a new vector with item appended; original vector remains unchanged.
 * Behavior depends on vector type:
 * - CLJ_VECTOR_PERSISTENT: Uses Copy-on-Write (RC=1 → in-place mutation, RC>1 → COW)
 * - CLJ_VECTOR_PERSISTENT_TRANSIENT: Always mutates in-place (transient behavior)
 * - CLJ_VECTOR_PERSISTENT_TRANSIENT_WEAK: Always mutates in-place (transient behavior, weak references)
 */
=======
>>>>>>> af65598be4ce5ad0372af581ec81496acb467be8
CljVector* vector_conj(CljVector* vec, ID item) {
    CljVector* result = vector_conj_owned(vec, item);
    if (result && result != vec) {
        unsigned char from_tag = TAG(vec);
        if (from_tag == CLJ_VECTOR_PERSISTENT_TRANSIENT || from_tag == CLJ_VECTOR_PERSISTENT_TRANSIENT_WEAK) {
            RELEASE(vec);
        }
        return AUTORELEASE(result);
    }
<<<<<<< HEAD
    return result;  // result == vec (in-place), no new object created
=======
    return result;
>>>>>>> af65598be4ce5ad0372af581ec81496acb467be8
}

/** Core implementation: Update element at index with COW: RC=1 → in-place, RC>1 → COW.
 * Returns owned object (rc=1, no AUTORELEASE).
 * Note: value can be NULL (nil) - that's a valid value in Clojure!
 */
static CljVector* vector_assoc_core(CljVector* vec, unsigned int index, ID value) {
    if (!vec) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                "vector_assoc: vector is NULL");
    }
    // Note: value can be NULL (nil) - that's a valid value in Clojure!
    
    CLJ_ASSERT(vec->base.type == CLJ_VECTOR_PERSISTENT || vec->base.type == CLJ_VECTOR_PERSISTENT_TRANSIENT_WEAK || vec->base.type == CLJ_VECTOR_PERSISTENT_TRANSIENT);

    if (vec->base.type == CLJ_VECTOR_PERSISTENT_TRANSIENT) {
        CljTransientVector *tvec = as_transient_vector(vec);
        CljVector *backing = (CljVector*)tvec->backing_store;

        // transient assoc allows index == count (append), but not index > count
        if (index > backing->count) {
            throw_index_out_of_bounds("vector_assoc", index, backing->count, "transient vector");
            return vec;
        }

        // Append is handled via conj to reuse growth logic.
        if (index == backing->count) {
            CljVector *new_backing = vector_conj(backing, value);
            ASSIGN(tvec->backing_store, (CljPersistentVector*)new_backing);
            return vec;
        }

        // Overwrite in-place. IMPORTANT: transient semantics do NOT release old value.
        backing->data[index] = value ? RETAIN(value) : NULL;
        return vec;
    }

    CljVector *old_vec = as_vector(vec);
    
    bool is_weak = (old_vec->base.type == CLJ_VECTOR_PERSISTENT_TRANSIENT_WEAK);
    
    // For CLJ_VECTOR_PERSISTENT_TRANSIENT_WEAK, allow index == count (append)
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
        // For CLJ_VECTOR_PERSISTENT_TRANSIENT_WEAK with index == count (append), we need to grow capacity first
        if (is_weak && index == old_vec->count) {
            // Append to weak vector - need to ensure capacity
            // After incrementing count, index must be < capacity
            // So we need capacity > index, i.e., capacity >= index + 1
            if (old_vec->capacity <= (int)index) {
                // Grow capacity using make_vector_copy
                // Ensure new capacity is at least index + 1, minimum 4
                int newcap = MAX(MAX(old_vec->capacity * 2, (int)index + 1), 4);
                CljVector *new_vec = make_vector_copy(old_vec, newcap);
                old_vec = new_vec;
                vec = new_vec;  // Update vec to point to new vector
            }
            old_vec->count++;  // Increment count for append
        }
        
        // Ensure index is within bounds before accessing data[index]
        CLJ_ASSERT(index < (unsigned int)old_vec->capacity);
        
        // For CLJ_VECTOR_PERSISTENT_TRANSIENT_WEAK, don't RETAIN (weak reference)
        // For CLJ_VECTOR_PERSISTENT_TRANSIENT, always RETAIN
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
    // For CLJ_VECTOR_PERSISTENT_TRANSIENT_WEAK with index == count (append), ensure capacity is sufficient
    int new_capacity = old_vec->capacity;
    if (is_weak && index == old_vec->count) {
        // Append to weak vector - ensure capacity is at least index + 1, minimum 4
        new_capacity = MAX(MAX(old_vec->capacity * 2, (int)index + 1), 4);
    }
    CljVector* new_vec = make_vector_copy(old_vec, new_capacity);

    // For CLJ_VECTOR_PERSISTENT_TRANSIENT_WEAK append, increment count
    if (is_weak && index == old_vec->count) {
        new_vec->count++;
    }

    // Ensure index is within bounds before accessing data[index]
    CLJ_ASSERT(index < (unsigned int)new_vec->capacity);

    // Update element at index (replace the copied element)
    if (index < old_vec->count && new_vec->data[index] && !is_weak) {
        RELEASE(new_vec->data[index]);
    }
    // For CLJ_VECTOR_PERSISTENT_TRANSIENT_WEAK, don't RETAIN (weak reference)
    new_vec->data[index] = is_weak ? value : RETAIN(value);

    return new_vec;  // owned (rc=1)
}

CljVector* vector_assoc(CljVector* vec, unsigned int index, ID value) {
    CljVector* result = vector_assoc_core(vec, index, value);
    if (result && result != vec) {
        unsigned char from_tag = TAG(vec);
        if (from_tag == CLJ_VECTOR_PERSISTENT_TRANSIENT || from_tag == CLJ_VECTOR_PERSISTENT_TRANSIENT_WEAK) {
            RELEASE(vec);
        }
<<<<<<< HEAD
        return AUTORELEASE(result);  // New object created, autorelease it
    }
    return result;  // result == vec (in-place), no new object created
=======
        return AUTORELEASE(result);
    }
    return result;
>>>>>>> af65598be4ce5ad0372af581ec81496acb467be8
}

// === _owned functions (for internal use) ===

static CljVector* vector_assoc_owned(CljVector* vec, unsigned int index, ID value) {
    return vector_assoc_core(vec, index, value);
}

static CljVector* vector_insert_at_owned(CljVector* vec, unsigned int index, ID item) {
    return vector_insert_at_core(vec, index, item);
}

void vector_conj_inplace(CljVector **vec_slot, ID item) {
    if (!vec_slot || !*vec_slot) return;
    CljVector *current = *vec_slot;
    CljVector *updated = vector_conj_owned(current, item);
    if (updated && updated != current) {
        RELEASE(current);
        *vec_slot = updated;
    }
}

void vector_assoc_inplace(CljVector **vec_slot, unsigned int index, ID value) {
    if (!vec_slot || !*vec_slot) return;
    CljVector *current = *vec_slot;
    CljVector *updated = vector_assoc_owned(current, index, value);
    if (updated && updated != current) {
        RELEASE(current);
        *vec_slot = updated;
    }
}

void vector_insert_at_inplace(CljVector **vec_slot, unsigned int index, ID item) {
    if (!vec_slot || !*vec_slot) return;
    CljVector *current = *vec_slot;
    CljVector *updated = vector_insert_at_owned(current, index, item);
    if (updated && updated != current) {
        RELEASE(current);
        *vec_slot = updated;
    }
}

void vector_remove_at_inplace(CljVector **vec_slot, unsigned int index) {
    if (!vec_slot || !*vec_slot) return;
    CljVector *current = *vec_slot;
    CljVector *updated = vector_remove_at_owned(current, index);
    if (updated && updated != current) {
        RELEASE(current);
        *vec_slot = updated;
    }
}

void vector_pop_inplace(CljVector **vec_slot) {
    if (!vec_slot || !*vec_slot) return;
    CljVector *current = *vec_slot;
    CljVector *updated = vector_pop_owned(current);
    if (updated && updated != current) {
        RELEASE(current);
        *vec_slot = updated;
    }
}

CljVector* vector_transient(CljVector *vec) {
    if (!vec) return NULL;
    if (vec->base.type == CLJ_VECTOR_PERSISTENT_TRANSIENT) {
        return vec;
    }
    if (vec->base.type == CLJ_VECTOR_PERSISTENT_TRANSIENT_WEAK) {
        return vec;
    }
    
    // Create a persistent backing store with exclusive ownership (RC=1).
    CljVector *backing = NULL;
    if (vec->count == 0 && vec->capacity == 0) {
        backing = make_vector(4, CLJ_VECTOR_PERSISTENT);
    } else {
        backing = make_vector_copy(vec, vec->capacity);
    }
    if (!backing) return NULL;

    CljTransientVector *tvec = (CljTransientVector*)alloc(sizeof(CljTransientVector), 1, CLJ_VECTOR_PERSISTENT_TRANSIENT);
    if (!tvec) {
        RELEASE(backing);
        throw_oom();
    }
    tvec->base.type = CLJ_VECTOR_PERSISTENT_TRANSIENT;
    tvec->base.rc = 1;
    tvec->backing_store = (CljPersistentVector*)backing;
    return (CljVector*)tvec;
}


/** Append to transient vector (guaranteed in-place). */
CljVector* clj_conj(CljVector *tvec, ID item) {
    return vector_conj(tvec, item);
}


<<<<<<< HEAD
/** Convert transient vector back to persistent. */
CljPersistentVector* vector_persistent(CljTransientVector *tvec) {
    if (!tvec) {
        return NULL;
    }
    CLJ_ASSERT(tvec->base.type == CLJ_VECTOR_PERSISTENT_TRANSIENT);
    // Return backing_store (persistent CLJ_VECTOR_PERSISTENT).
    return tvec->backing_store;
=======
ID vector_persistent(CljVector *tvec) {
    if (!tvec || tvec->base.type == CLJ_VECTOR) return tvec;
    CljVector* new_vec = make_vector_copy(tvec, tvec->capacity);
    if (!new_vec) return NULL;
    new_vec->base.type = CLJ_VECTOR;
    return new_vec;
>>>>>>> af65598be4ce5ad0372af581ec81496acb467be8
}
