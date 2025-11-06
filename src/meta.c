/*
 * Metadata Management
 * 
 * Functions for managing metadata on Clojure objects.
 * Only compiled when ENABLE_META is defined.
 */

#ifdef ENABLE_META

#include <stdlib.h>
#include "meta.h"
#include "object.h"
#include "runtime.h"
#include "map.h"
#include "memory.h"
#include "kv_macros.h"

void meta_registry_init() {
    {
        g_runtime.meta_registry = (void*)make_map(32); // Initial capacity for metadata entries
    }
}

void meta_registry_cleanup() {
    if (g_runtime.meta_registry) {
        RELEASE((CljObject*)g_runtime.meta_registry);
    }
    g_runtime.meta_registry = NULL;
}

void meta_set(CljObject *v, CljObject *meta) {
    if (!v) return;
    
    meta_registry_init();
    if (!g_runtime.meta_registry) return;
    
    // Use the pointer as key (simple implementation)
    // A real implementation would use a hash of the pointer
    (void)map_assoc_cow((CljValue)g_runtime.meta_registry, v, meta);
}

ID meta_get(CljObject *v) {
    if (!v || !g_runtime.meta_registry) return NULL;
    
    return (ID)map_get((CljValue)g_runtime.meta_registry, (CljValue)v);
}

void meta_clear(CljObject *v) {
    if (!v || !g_runtime.meta_registry) return;
    
    // Find the entry and remove it using KV macros
    CljMap *map = (CljMap*)g_runtime.meta_registry;
    int index = KV_FIND_INDEX(map->data, map->count, v);
    if (index >= 0) {
        // Entry found; remove it
        CljObject *old_value = KV_VALUE(map->data, index);
        RELEASE(old_value);
        
        // Shift following elements to the left
        for (int j = index; j < map->count - 1; j++) {
            KV_ASSIGN_PAIR(map->data, j,
                       KV_KEY(map->data, j + 1),
                       KV_VALUE(map->data, j + 1));
        }
        
        map->count--;
    }
}

#endif // ENABLE_META

