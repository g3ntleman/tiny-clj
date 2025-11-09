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
#include "reader.h"
#include "namespace.h"
#include "symbol.h"
#include "value.h"

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
    CljMap *new_registry = map_assoc((CljMap*)g_runtime.meta_registry, (ID)v, (ID)meta);
    
    // If map_assoc returned a new map (Copy-on-Write), update registry
    // When RC=1, map_assoc mutates in-place and returns the same map
    // When RC>1 or capacity full, map_assoc creates a new map
    if (new_registry != (ID)g_runtime.meta_registry) {
        // New map was created (Copy-on-Write), update registry
        // Old map is automatically handled by map_assoc (released if RC>1)
        g_runtime.meta_registry = (void*)new_registry;
    }
}

ID meta_get(CljObject *v) {
    if (!v || !g_runtime.meta_registry) return NULL;
    
    return (ID)map_get((CljMap*)g_runtime.meta_registry, (CljValue)v);
}

void meta_clear(CljObject *v) {
    if (!v || !g_runtime.meta_registry) return;
    
    // Use map_remove which always returns a new map (COW disabled)
    CljMap *new_registry = map_remove((CljMap*)g_runtime.meta_registry, (ID)v);
    if (new_registry != (CljMap*)g_runtime.meta_registry) {
        // New map was created, update registry
        RELEASE((CljObject*)g_runtime.meta_registry);
        g_runtime.meta_registry = (void*)new_registry;
    }
    // If key was not found, map_remove returns original map (no change needed)
}

/**
 * @brief Create a map with source code location metadata (:line, :column, :file, :ns)
 * Clojure-compatible metadata keys
 * @param reader Reader instance (for line and column)
 * @param st Evaluation state (for file and namespace)
 * @return Map with location metadata or NULL on error
 * 
 * DRY: Wiederverwendbare Funktion zum Erstellen von Sourcecode-Meta-Map
 */
CljObject* make_location_meta(void *reader_ptr, void *st_ptr) {
    Reader *reader = (Reader*)reader_ptr;
    EvalState *st = (EvalState*)st_ptr;
    if (!reader) return NULL;
    
    // Create map with capacity for 4 entries (:line, :column, :file, :ns)
    CljMap *location_map = make_map(4);
    if (!location_map) return NULL;
    
    // Get line and column from reader
    int line = reader_line(reader);
    int column = reader_column(reader);
    
    // Get file from EvalState (if available)
    const char *file = st ? st->file : NULL;
    
    // Get namespace from EvalState (if available)
    CljNamespace *current_ns = st ? st->current_ns : NULL;
    CljObject *ns_name = current_ns ? current_ns->name : NULL;
    
    // Ensure special symbols are initialized
    if (!SYM_KW_LINE || !SYM_KW_FILE || !SYM_KW_NS) {
        init_special_symbols();
    }
    
    // Get or create :column keyword
    CljObject *kw_column = intern_symbol_global(":column");
    if (!kw_column) {
        RELEASE((CljObject*)location_map);
        return NULL;
    }
    
    // Add :line (Clojure-compatible)
    // CRITICAL: map_assoc may return a new map (COW), so we must use the result
    if (SYM_KW_LINE) {
        ID updated_map = map_assoc((CljValue)location_map, (CljValue)SYM_KW_LINE, fixnum(line));
        ASSIGN(location_map, (CljMap*)updated_map);
    }
    
    // Add :column (Clojure-compatible)
    if (kw_column) {
        ID updated_map = map_assoc((CljValue)location_map, (CljValue)kw_column, fixnum(column));
        ASSIGN(location_map, (CljMap*)updated_map);
    }
    
    // Add :file (Clojure-compatible, if available)
    if (SYM_KW_FILE && file) {
        struct CljString *file_str = make_string(file);
        if (file_str) {
            ID updated_map = map_assoc((CljValue)location_map, (CljValue)SYM_KW_FILE, (CljValue)file_str);
            ASSIGN(location_map, (CljMap*)updated_map);
            RELEASE(file_str); // map_assoc retains it
        }
    }
    
    // Add :ns (Clojure-compatible, if available)
    if (SYM_KW_NS && ns_name) {
        ID updated_map = map_assoc((CljValue)location_map, (CljValue)SYM_KW_NS, (CljValue)ns_name);
        ASSIGN(location_map, (CljMap*)updated_map);
    }
    
    return (CljObject*)location_map;
}

/**
 * @brief Merge location metadata into existing metadata map
 * @param existing_meta Existing metadata map (can be NULL)
 * @param location_meta Location metadata map (from make_location_meta)
 * @return Merged metadata map or location_meta if existing_meta is NULL
 * 
 * DRY: Wiederverwendbare Funktion zum Zusammenführen von Meta-Maps
 * Only adds location metadata if keys don't already exist (doesn't overwrite)
 */
CljObject* meta_merge(CljObject *existing_meta, CljObject *location_meta) {
    if (!location_meta) return existing_meta;
    if (!existing_meta) return location_meta;
    
    // Check if both are maps
    if (!is_type(existing_meta, CLJ_MAP) || !is_type(location_meta, CLJ_MAP)) {
        return existing_meta; // Return existing if types don't match
    }
    
    CljMap *existing_map = as_map(existing_meta);
    CljMap *location_map = as_map(location_meta);
    if (!existing_map || !location_map) return existing_meta;
    
    // Start with existing map
    CljObject *result = existing_meta;
    RETAIN(result);
    
    // Add location metadata entries only if they don't exist in existing map
    for (int i = 0; i < location_map->count; i++) {
        CljObject *key = KV_KEY(location_map->data, i);
        CljObject *value = KV_VALUE(location_map->data, i);
        
        if (!key) continue;
        
        // Check if key already exists in existing map
        ID existing_value = map_get((CljMap*)existing_meta, (ID)key);
        if (!existing_value) {
            // Key doesn't exist, add it using map_assoc
            CljMap *new_result = map_assoc((CljMap*)result, (ID)key, (ID)value);
            if (new_result != (CljMap*)result) {
                // New map was created, update result
                RELEASE(result);
                result = (CljObject*)new_result;
            }
        }
        // If key exists, skip it (don't overwrite existing metadata)
    }
    
    return result;
}

#endif // ENABLE_META

