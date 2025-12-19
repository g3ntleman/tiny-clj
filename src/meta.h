#ifndef TINY_CLJ_META_H
#define TINY_CLJ_META_H

#include "object.h"
#include "value.h"
#include "map.h"

// Meta registry for metadata (only when ENABLE_META is defined)
#ifdef ENABLE_META
// Forward declarations (to avoid circular dependencies)
struct Reader;
struct EvalState;

extern CljObject *meta_registry;

// Meta access functions
void meta_set(CljObject *v, CljObject *meta);
ID meta_get(CljObject *v);
void meta_clear(CljObject *v);
void meta_registry_init();
void meta_registry_cleanup();

// Helper function for source code location metadata
// Note: Uses void* to avoid circular dependencies in header
CljMap* make_location_meta(void *reader, void *st);

// Helper function to merge metadata maps
CljMap* meta_merge(CljMap *existing_meta, CljMap *location_meta);

// Merge metadata maps with second map taking precedence (overwrites conflicting keys)
CljMap* meta_merge_with_precedence(CljMap *existing_meta, CljMap *form_meta);
#else
// Stubs when meta is disabled
#define meta_set(v, meta) ((void)0)
#define meta_get(v) (NULL)
#define meta_clear(v) ((void)0)
#define meta_registry_init() ((void)0)
#define meta_registry_cleanup() ((void)0)
#define make_location_meta(reader, st) ((CljMap*)NULL)
#define meta_merge(existing, location) ((CljMap*)((existing) ? (existing) : (location)))
#define meta_merge_with_precedence(existing, form) ((CljMap*)((form) ? (form) : (existing)))
#endif

#endif
