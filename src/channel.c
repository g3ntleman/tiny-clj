#include "channel.h"
#include <subjective-c/map.h>
#include <subjective-c/value.h>
#include "symbol.h"
#include <subjective-c/exception.h>  // For throw_oom

/** Create a channel (promise-like) as a transient map.
 * @return New transient map channel with RC=1 (caller must release)
 */
CljMap* make_result_channel(void) {
    // Create a transient map directly (like map_copy_with_additions does)
    // Allocate transient map with embedded data array
    int capacity = 4;  // Enough for :value and :closed
    size_t struct_size = sizeof(CljMap);
    size_t data_size = (size_t)capacity * 2 * sizeof(CljObject*);
    CljMap *tmap = (CljMap*)malloc(struct_size + data_size);
    if (!tmap) {
        throw_oom();
        return NULL;
    }
    
    // Initialize as transient map
    tmap->base.type = CLJ_MAP_TRANSIENT;
    tmap->base.rc = 1;
    tmap->count = 0;
    tmap->capacity = capacity;
    
    // Initialize data array
    for (int i = 0; i < capacity * 2; i++) {
        tmap->data[i] = NULL;
    }
    
    // Initialize with :value = nil and :closed = false
    CljObject *kw_value = (CljObject*)intern_symbol_global(":value");
    CljObject *kw_closed = (CljObject*)intern_symbol_global(":closed");
    
    map_conj(tmap, kw_value, NULL);  // :value = nil
    map_conj(tmap, kw_closed, clj_false);  // :closed = false
    
    return tmap;
}

/** Put a value into the channel (mutates in-place using map_conj).
 * @param chan Channel (transient map)
 * @param value Value to put (can be NULL/nil or immediate)
 */
void result_channel_put(CljMap *chan, ID value) {
    CLJ_ASSERT(chan != NULL);
    
    // Assertion: Only transient maps (and persistent maps with RC=1 in COW cases) can be mutated
    CljObject *obj = (CljObject*)chan;
    CLJ_ASSERT(obj != NULL);
    CLJ_ASSERT(obj->type == CLJ_MAP_TRANSIENT || obj->type == CLJ_MAP);
    
    // In COW cases, persistent maps with RC=1 can be mutated, but we use transient maps for channels
    if (obj->type == CLJ_MAP) {
        CLJ_ASSERT(obj->rc == 1);
    }
    
    CljObject *kw_value = (CljObject*)intern_symbol(NULL, ":value");
    CLJ_ASSERT(kw_value != NULL);
    
#if defined(DEBUG)
    void *chan_ptr_before = (void*)chan;
    CljMap *result = map_conj(chan, kw_value, value);
    CLJ_ASSERT(result != NULL);
    CLJ_ASSERT((void*)result == chan_ptr_before);  // Should return same pointer
#else
    map_conj(chan, kw_value, value);
#endif
}

/** Close the channel (mutates in-place using map_conj).
 * @param chan Channel (transient map)
 */
void result_channel_close(CljMap *chan) {
    CLJ_ASSERT(chan != NULL);
    
    // Assertion: Only transient maps (and persistent maps with RC=1 in COW cases) can be mutated
    CljObject *obj = (CljObject*)chan;
    CLJ_ASSERT(obj != NULL);
    CLJ_ASSERT(obj->type == CLJ_MAP_TRANSIENT || obj->type == CLJ_MAP);
    
    // In COW cases, persistent maps with RC=1 can be mutated, but we use transient maps for channels
    if (obj->type == CLJ_MAP) {
        CLJ_ASSERT(obj->rc == 1);
    }
    
    CljObject *kw_closed = (CljObject*)intern_symbol(NULL, ":closed");
    CLJ_ASSERT(kw_closed != NULL);
    
#if defined(DEBUG)
    void *chan_ptr_before = (void*)chan;
    CljMap *result = map_conj(chan, kw_closed, clj_true);
    CLJ_ASSERT(result != NULL);
    CLJ_ASSERT((void*)result == chan_ptr_before);  // Should return same pointer
    
    // Verify channel was actually mutated
    CljValue closed_val = map_get_sentinel(chan, (CljValue)kw_closed, NULL);
    CLJ_ASSERT(closed_val != NULL);
    CLJ_ASSERT(is_special(closed_val));
    CLJ_ASSERT(as_special(closed_val) == SPECIAL_TRUE);
#else
    map_conj(chan, kw_closed, clj_true);
#endif
}


