#ifndef TINY_CLJ_META_H
#define TINY_CLJ_META_H

#include "object.h"
#include "value.h"

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
CljObject* make_location_meta(void *reader, void *st);

// Helper function to merge metadata maps
CljObject* meta_merge(CljObject *existing_meta, CljObject *location_meta);

// Merge metadata maps with second map taking precedence (overwrites conflicting keys)
CljObject* meta_merge_with_precedence(CljObject *existing_meta, CljObject *form_meta);
#else
// Stubs when meta is disabled
#define meta_set(v, meta) ((void)0)
#define meta_get(v) ((ID)NULL)
#define meta_clear(v) ((void)0)
#define meta_registry_init() ((void)0)
#define meta_registry_cleanup() ((void)0)
#define make_location_meta(reader, st) ((CljObject*)NULL)
#define meta_merge(existing, location) ((existing) ? (existing) : (location))
#define meta_merge_with_precedence(existing, form) ((form) ? (form) : (existing))
#endif

#endif
