#ifndef TINY_CLJ_META_H
#define TINY_CLJ_META_H

#include "object.h"
#include "value.h"

// Meta registry for metadata (only when ENABLE_META is defined)
#ifdef ENABLE_META
extern CljObject *meta_registry;

// Meta access functions
void meta_set(CljObject *v, CljObject *meta);
ID meta_get(CljObject *v);
void meta_clear(CljObject *v);
void meta_registry_init();
void meta_registry_cleanup();
#else
// Stubs when meta is disabled
#define meta_set(v, meta) ((void)0)
#define meta_get(v) ((ID)NULL)
#define meta_clear(v) ((void)0)
#define meta_registry_init() ((void)0)
#define meta_registry_cleanup() ((void)0)
#endif

#endif
