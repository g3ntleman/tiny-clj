#include "hashset.h"
#include "callbacks.h"  // For clj_hash() and clj_equal()
#include "object.h"
#include "value.h"
#include "memory.h"
#include "exception.h"
#include "common.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

// Sentinel singletons (SINGLETON_RC prevents RETAIN/RELEASE from modifying them)
CljObject g_hashset_empty_sentinel = { .type = CLJ_NIL, .rc = SINGLETON_RC };
CljObject g_hashset_tombstone_sentinel = { .type = CLJ_NIL, .rc = SINGLETON_RC };

// Next power of 2 (for mask: capacity - 1)
static unsigned int next_power_of_2(unsigned int n) {
    if (n < 8) return 8;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return n + 1;
}

/** Create hash set with capacity rounded up to next power of two. */
CljHashSet* make_hashset(unsigned int capacity) {
    unsigned int cap = next_power_of_2(capacity);

    size_t struct_size = sizeof(CljHashSet);
    size_t data_size = (size_t)cap * sizeof(CljObject*);
    size_t total_size =
#if defined(ESP32_BUILD)
        round_up_to_fam_granularity(struct_size + data_size);
#else
        struct_size + data_size;
#endif

    CljHashSet *set = (CljHashSet*)alloc(total_size, 1, CLJ_HASHSET);
    if (!set) {
        throw_oom();
    }

    set->base.type = CLJ_HASHSET;
    set->count = 0;
    set->capacity = cap;
    set->tombstones = 0;

    // Initialize embedded array to HASHSET_EMPTY (NULL is a valid key: nil)
    for (unsigned int i = 0; i < cap; i++) {
        set->data[i] = HASHSET_EMPTY;
    }

    return set;
}

// Linear Probing: Find slot (for contains/add/remove)
// Uses clj_hash() and clj_equal()
// Note: HASHSET_EMPTY marks empty slots, NULL is a valid key (nil)
static unsigned int find_slot(CljHashSet *set, ID key) {
    unsigned int mask = set->capacity - 1;
    unsigned int start_idx = clj_hash(key) & mask;
    unsigned int idx = start_idx;

    do {
        ID stored_key = set->data[idx];

        if (stored_key == HASHSET_EMPTY) {
            return idx;  // Empty slot found
        }

        if (stored_key != HASHSET_TOMBSTONE) {
            if (clj_equal(stored_key, key)) return idx;  // Found
        }

        idx = (idx + 1) & mask;
    } while (idx != start_idx);

    // Set is full (all slots occupied or tombstones)
    return start_idx;
}

ID hashset_get_sentinel(CljHashSet *set, ID key, ID not_found) {
    if (!set) return not_found;
    unsigned int idx = find_slot(set, key);
    ID stored_key = set->data[idx];
    if (stored_key != HASHSET_EMPTY && stored_key != HASHSET_TOMBSTONE) {
        return stored_key;
    }
    return not_found;
}

int hashset_contains(CljHashSet *set, ID key) {
    if (!set) return 0;
    unsigned int idx = find_slot(set, key);
    ID stored_key = set->data[idx];
    return (stored_key != HASHSET_EMPTY && stored_key != HASHSET_TOMBSTONE) ? 1 : 0;
}

unsigned int hashset_count(CljHashSet *set) {
    if (!set) return 0;
    return set->count;
}

// Insert key at given index
// Caller must handle tombstone accounting before calling
static void hashset_insert_at(CljHashSet *set, unsigned int idx, ID key) {
    CLJ_ASSERT(set->data[idx] != HASHSET_TOMBSTONE);
    ASSIGN(set->data[idx], key);
    set->count++;
}

// Direct insert without rehashing check (used for rehashing/copying)
static void hashset_put_direct(CljHashSet *set, ID key) {
    unsigned int idx = find_slot(set, key);
    CLJ_ASSERT(set->data[idx] == HASHSET_EMPTY);
    hashset_insert_at(set, idx, key);
}

// Rehashing: Copy all entries into a new set with larger capacity
static CljHashSet* hashset_rehash(CljHashSet *set, unsigned int new_capacity) {
    CljHashSet *new_set = make_hashset(new_capacity);

    ID key;
    HASHSET_FOR_EACH(set, key) {
        hashset_put_direct(new_set, key);
    }

    return new_set;
}

// Check if rehashing is needed (Load factor > 0.75)
static bool needs_rehash(CljHashSet *set) {
    if (!set) return false;
    size_t total_used = (size_t)set->count + set->tombstones;
    return total_used * 4 > (size_t)set->capacity * 3;
}

// Copy hashset (clean copy without tombstones)
static CljHashSet* hashset_copy(CljHashSet *set) {
    CljHashSet *copy = make_hashset(set->count);
    copy->count = 0;
    copy->tombstones = 0;

    ID key;
    HASHSET_FOR_EACH(set, key) {
        hashset_put_direct(copy, key);
    }
    return copy;
}

// COW implementation with Linear Probing and tombstone reuse
CljHashSet* hashset_add(CljHashSet *set, ID key) {
    if (!set) return set;

    if (needs_rehash(set)) {
        set = hashset_rehash(set, set->capacity * 2);
    }

    if (set->base.rc > 1) {
        CljHashSet *copy = hashset_copy(set);
        set = copy;
    }

    unsigned int mask = set->capacity - 1;
    unsigned int idx = clj_hash(key) & mask;
    unsigned int start_idx = idx;

    do {
        ID stored_key = set->data[idx];

        if (stored_key == HASHSET_EMPTY) {
            break;  // Empty slot found - insert here
        }

        if (stored_key != HASHSET_TOMBSTONE && clj_equal(stored_key, key)) {
            return set;  // Already present
        }

        idx = (idx + 1) & mask;
    } while (idx != start_idx);

    hashset_insert_at(set, idx, key);

    return set;
}

CljHashSet* hashset_remove(CljHashSet *set, ID key) {
    if (!set) return set;

    unsigned int idx = find_slot(set, key);
    ID stored = set->data[idx];
    if (stored == HASHSET_EMPTY || stored == HASHSET_TOMBSTONE) {
        return set;  // Not found
    }

    if (set->base.rc > 1) {
        CljHashSet *copy = make_hashset(set->count > 1 ? set->count - 1 : 0);
        for (unsigned int i = 0; i < set->capacity; i++) {
            ID stored_i = set->data[i];
            if (stored_i != HASHSET_EMPTY && stored_i != HASHSET_TOMBSTONE && i != idx) {
                hashset_put_direct(copy, stored_i);
            }
        }
        return copy;
    }

    RELEASE(set->data[idx]);
    set->data[idx] = HASHSET_TOMBSTONE;
    set->count--;
    set->tombstones++;

    return set;
}

void hashset_add_inplace(CljHashSet **set_slot, ID key) {
    if (!set_slot || !*set_slot) return;
    CljHashSet *current = *set_slot;
    CljHashSet *updated = hashset_add(current, key);
    if (updated && updated != current) {
        RELEASE(current);
        *set_slot = updated;
    }
}

void hashset_remove_inplace(CljHashSet **set_slot, ID key) {
    if (!set_slot || !*set_slot) return;
    CljHashSet *current = *set_slot;
    CljHashSet *updated = hashset_remove(current, key);
    if (updated && updated != current) {
        RELEASE(current);
        *set_slot = updated;
    }
}

// Memory management registration (no-op - destructor is in memory.c release_object_default)
void hashset_register_release_fn(void) {
    // HashSet destructor is already implemented in memory.c release_object_default()
}
