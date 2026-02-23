#ifndef TINY_CLJ_META_H
#define TINY_CLJ_META_H

#include "object.h"
#include "value.h"
#include "map.h"

// Meta registry for metadata (only when META_ENABLED is enabled)
#if defined(META_ENABLED) && META_ENABLED
// Forward declarations (to avoid circular dependencies)
struct Reader;
struct EvalState;

extern CljObject *meta_registry;

// Meta access functions
void meta_set(ID v, ID meta);
ID meta_get(ID v);
void meta_clear(ID v);
void meta_registry_init(void);
void meta_registry_cleanup(void);

// Helper function for source code location metadata
// Note: Uses void* to avoid circular dependencies in header
CljPersistentMap* make_location_meta(void *reader, void *st);

// Helper function to merge metadata maps (MEMORY_POLICY: usable/pool-safe return)
CljPersistentMap* meta_merge(CljPersistentMap *existing_meta, CljPersistentMap *location_meta);

// Merge metadata maps with second map taking precedence (MEMORY_POLICY: usable/pool-safe return)
CljPersistentMap* meta_merge_with_precedence(CljPersistentMap *existing_meta, CljPersistentMap *form_meta);
#else
// Stubs when meta is disabled
#define meta_set(v, meta) ((void)0)
#define meta_get(v) (NULL)
#define meta_clear(v) ((void)0)
#define meta_registry_init() ((void)0)
#define meta_registry_cleanup() ((void)0)
#define make_location_meta(reader, st) ((CljPersistentMap*)NULL)
#define meta_merge(existing, location) ((CljPersistentMap*)((existing) ? (existing) : (location)))
#define meta_merge_with_precedence(existing, form) ((CljPersistentMap*)((form) ? (form) : (existing)))
#endif

#endif
