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
#include "symbol.h"  // Must be included before namespace.h for CljSymbol definition
#include "namespace.h"
#include "value.h"
#include "common.h"  // For CLJ_ASSERT
#include "function.h" // For CljCFunc
// clj_equal is available via map.h -> equality.h

void meta_registry_init() {
    // Only initialize once – keep existing registry to preserve all metadata entries
    if (!g_runtime.meta_registry) {
        g_runtime.meta_registry = (CljObject*)make_map(32); // Initial capacity for metadata entries
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
    CljMap *registry = (CljMap*)g_runtime.meta_registry;
    CljMap *new_registry = map_assoc(registry, (ID)v, (ID)meta);
    
    // If map_assoc returned a new map (Copy-on-Write), update registry
    // When RC=1, map_assoc mutates in-place and returns the same map
    // When RC>1 or capacity full, map_assoc creates a new map
    if (new_registry != registry) {
        // New map was created (Copy-on-Write), update registry
        // Use ASSIGN to properly handle reference counting
        ASSIGN(g_runtime.meta_registry, (CljObject*)new_registry);
    }
    
    // Assertion: Verify that the metadata can be retrieved after setting
    // This ensures that meta_set and meta_get work correctly together
    ID retrieved_meta = meta_get(v);
    if (meta != NULL) {
        CLJ_ASSERT(retrieved_meta != NULL && "meta_set: metadata should be retrievable after setting");
        CLJ_ASSERT(retrieved_meta == (ID)meta && "meta_set: retrieved metadata should match the set metadata");
    }
    // Note: If meta is NULL, retrieved_meta may also be NULL, which is acceptable
}

ID meta_get(CljObject *v) {
    if (!v || !g_runtime.meta_registry) return NULL;
    
    CljMap *registry = (CljMap*)g_runtime.meta_registry;
    
    // First try pointer comparison (fast path)
    // CRITICAL: Use sentinel to distinguish "key not found" from "value is NULL"
    // In Clojure, nil is a valid metadata value, so we need to distinguish these cases
    static CljObject not_found_sentinel = { .type = CLJ_NIL, .rc = SINGLETON_RC };
    ID result = (ID)map_get((CljValue)registry, (CljValue)v, (ID)&not_found_sentinel);
    if (result != (ID)&not_found_sentinel) {
        // Key found (value can be NULL, which is valid - means metadata is explicitly nil)
        return result;  // Can be NULL if metadata was explicitly set to nil
    }
    
    if (!registry) return NULL;
    
    // For lists, we need to handle the case where symbols might have different namespaces
    // but are structurally equivalent (e.g., unqualified symbols in different contexts)
    if (TAG(v) == CLJ_LIST) {
        MAP_FOR_EACH(registry, stored_key, value) {
            if (stored_key == (CljObject*)v) {
                // Already checked above, but check again for safety
                return (ID)value;
            }
            // For lists, try structural equality with namespace-agnostic symbol comparison
            if (stored_key && TAG(stored_key) == CLJ_LIST) {
                if (clj_equal(stored_key, (CljObject*)v)) {
                    return (ID)value;
                }
            }
        }
    } else {
        // For non-lists, use standard structural equality
        MAP_FOR_EACH(registry, stored_key, value) {
            if (stored_key == (CljObject*)v) {
                // Already checked above, but check again for safety
                return (ID)value;
            }
            // Try structural equality for non-interned objects (like lists)
            if (stored_key && clj_equal(stored_key, (CljObject*)v)) {
                return (ID)value;
            }
        }
    }
    
    return NULL;
}

void meta_clear(CljObject *v) {
    if (!v || !g_runtime.meta_registry) return;
    
    // Use map_remove which always returns a new map (COW disabled)
    CljMap *new_registry = map_remove(g_runtime.meta_registry, (ID)v);
    if (new_registry != g_runtime.meta_registry) {
        // New map was created, update registry
        // Use ASSIGN to properly handle reference counting
        ASSIGN(g_runtime.meta_registry, new_registry);
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
    CljSymbol *ns_name = current_ns ? current_ns->name : NULL;
    
    // Ensure special symbols are initialized
    if (!SYM_KW_LINE || !SYM_KW_FILE || !SYM_KW_NS) {
        init_special_symbols();
    }
    
    // Get or create :column keyword
    CljSymbol *kw_column = intern_symbol_global(":column");
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
    if (!existing_meta || TAG(existing_meta) != CLJ_MAP || !location_meta || TAG(location_meta) != CLJ_MAP) {
        return existing_meta; // Return existing if types don't match
    }
    
    CljMap *existing_map = as_map(existing_meta);
    CljMap *location_map = as_map(location_meta);
    if (!existing_map || !location_map) return existing_meta;
    
    // Start with existing map
    CljObject *result = existing_meta;
    RETAIN(result);
    
    // Add location metadata entries only if they don't exist in existing map
    MAP_FOR_EACH(location_map, key, value) {
        if (!key) continue;
        
        // Check if key already exists in existing map
        ID existing_value = map_get((CljMap*)existing_meta, (ID)key, NULL);
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

// Merge metadata maps with second map taking precedence (overwrites conflicting keys)
// Used when form metadata should override existing metadata (e.g., from register_builtins)
CljObject* meta_merge_with_precedence(CljObject *existing_meta, CljObject *form_meta) {
    if (!form_meta) return existing_meta;
    if (!existing_meta) return form_meta;
    
    // Check if both are maps
    if (TAG(existing_meta) != CLJ_MAP || TAG(form_meta) != CLJ_MAP) {
        return form_meta; // Return form_meta if types don't match (form takes precedence)
    }
    
    CljMap *existing_map = as_map(existing_meta);
    CljMap *form_map = as_map(form_meta);
    if (!existing_map || !form_map) return form_meta;
    
    // Start with existing map (copy it)
    CljMap *result = existing_map;
    RETAIN(result);
    
    // Add/overwrite all entries from form_meta (form takes precedence)
    MAP_FOR_EACH(form_map, key, value) {
        if (!key) continue;
        // map_assoc will overwrite existing keys
        CljMap *new_result = map_assoc(result, (ID)key, (ID)value);
        if (new_result != result) {
            RELEASE(result);
            result = new_result;
        }
    }
    
    // Also add keys from existing_meta that don't exist in form_meta
    MAP_FOR_EACH(existing_map, key, value) {
        if (!key) continue;
        // Check if key exists in form_meta
        ID form_value = map_get(form_map, (ID)key, NULL);
        if (!form_value) {
            // Key doesn't exist in form_meta, add it from existing_meta
            CljMap *new_result = map_assoc(result, (ID)key, (ID)value);
            if (new_result != result) {
                RELEASE(result);
                result = new_result;
            }
        }
        // If key exists in form_meta, it was already added above (form takes precedence)
    }
    
    return (CljObject*)result;
}

#endif // ENABLE_META

