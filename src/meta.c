/*
 * Metadata Management
 * 
 * Functions for managing metadata on Clojure objects.
 * Only compiled when META_ENABLED is enabled.
 */

#if defined(META_ENABLED) && META_ENABLED

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
#include "list.h"
// clj_equal is available via map.h -> equality.h

void meta_registry_init() {
    if (!g_runtime.meta_registry) {
        g_runtime.meta_registry = make_hashmap(32);
    }
}

void meta_registry_cleanup() {
    RELEASE(g_runtime.meta_registry);
    g_runtime.meta_registry = NULL;
}

void meta_set(ID v, ID meta) {
    if (!v) return;
    
    CLJ_ASSERT(!IS_IMMEDIATE(v) && "meta_set: immediates cannot have metadata");
    
    meta_registry_init();
    if (!g_runtime.meta_registry) return;
    
    hashmap_assoc_inplace(&g_runtime.meta_registry, v, meta);
    
#if defined(DEBUG)
    ID retrieved_meta = meta_get(v);
    if (meta != NULL) {
        CLJ_ASSERT(retrieved_meta != NULL && "meta_set: metadata should be retrievable");
        CLJ_ASSERT(retrieved_meta == meta && "meta_set: retrieved metadata should match");
    }
#endif
}

ID meta_get(ID v) {
    if (!v || !g_runtime.meta_registry) return NULL;
    ID result = hashmap_get(g_runtime.meta_registry, v);
    return (result == NOT_FOUND) ? NULL : result;
}

void meta_clear(ID v) {
    if (!v || !g_runtime.meta_registry) return;
    
    hashmap_remove_inplace(&g_runtime.meta_registry, v);
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
CljMap* make_location_meta(void *reader_ptr, void *st_ptr) {
    Reader *reader = (Reader*)reader_ptr;
    EvalState *st = (EvalState*)st_ptr;
    if (!reader) return NULL;
    
    // Create map with capacity for 4 entries (:line, :column, :file, :ns)
    CljMap *location_map = make_map(4);
    if (!location_map) return NULL;
    
    // Get line and column from reader
    int line = reader_line(reader);
    int column = reader_column(reader);
    
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
        RELEASE(location_map);
        return NULL;
    }
    
    // Add :line (Clojure-compatible)
    // Use *_inplace to avoid map_assoc()'s unconditional AUTORELEASE.
    if (SYM_KW_LINE) {
        map_assoc_inplace(&location_map, SYM_KW_LINE, fixnum(line));
    }
    
    // Add :column (Clojure-compatible)
    if (kw_column) {
        map_assoc_inplace(&location_map, kw_column, fixnum(column));
    }
    
    // Add :file (Clojure-compatible) - file information not available from EvalState
    // File information would need to come from Reader or other source if needed
    
    // Add :ns (Clojure-compatible, if available)
    if (SYM_KW_NS && ns_name) {
        map_assoc_inplace(&location_map, SYM_KW_NS, ns_name);
    }
    
    return location_map;
}

/**
 * @brief Merge location metadata into existing metadata map
 * @param existing_meta Existing metadata map (can be NULL)
 * @param location_meta Location metadata map (from make_location_meta)
 * @return Merged metadata map or location_meta if existing_meta is NULL
 * 
 * DRY: Wiederverwendbare Funktion zum Zusammenführen von Meta-Maps
 */
CljMap* meta_merge(CljMap *existing_meta, CljMap *location_meta) {
    if (!location_meta) return existing_meta;
    if (!existing_meta) return location_meta;

    CljMap *missing_entries = NULL;
    int has_missing = 0;
    
    MAP_FOR_EACH(location_meta, key, value) {
        if (!key) continue;
        ID existing_value = map_get(existing_meta, key);
        if (existing_value == NOT_FOUND) {
            if (!missing_entries) {
                missing_entries = map_empty();
            }
            map_assoc_inplace(&missing_entries, key, value);
            has_missing = 1;
        }
    }
    
    if (!has_missing) {
        RELEASE(missing_entries);
        return RETAIN(existing_meta);
    }
    
    CljMap *result = as_map(RETAIN(existing_meta));
    // Apply missing entries directly to avoid map_merge(), which uses map_assoc()
    // and can create autorelease churn.
    MAP_FOR_EACH(missing_entries, key, value) {
        if (!key) continue;
        map_assoc_inplace(&result, key, value);
    }
    RELEASE(missing_entries);
    
    return result;
}

// Merge metadata maps with second map taking precedence (overwrites conflicting keys)
// Used when form metadata should override existing metadata (e.g., from register_builtins)
CljMap* meta_merge_with_precedence(CljMap *existing_meta, CljMap *form_meta) {
    if (!form_meta) return existing_meta;
    if (!existing_meta) return form_meta;

    CljMap *result = as_map(RETAIN(existing_meta));
    
    // Add/overwrite all entries from form_meta (form takes precedence)
    MAP_FOR_EACH(form_meta, key, value) {
        if (!key) continue;
        map_assoc_inplace(&result, key, value);
    }
    
    // Also add keys from existing_meta that don't exist in form_meta
    MAP_FOR_EACH(existing_meta, key, value) {
        if (!key) continue;
        ID form_value = map_get(as_map(form_meta), key);
        if (form_value == NOT_FOUND) {
            map_assoc_inplace(&result, key, value);
        }
    }
    
    return result;
}

#endif // META_ENABLED

