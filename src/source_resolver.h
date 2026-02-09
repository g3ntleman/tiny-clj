#ifndef TINY_CLJ_SOURCE_RESOLVER_H
#define TINY_CLJ_SOURCE_RESOLVER_H

#include "object.h"

/** Resolve a path to byte array content (KV store or embedded map). Returns NULL if not found. */
ID resolve_path_to_bytes(const char *path);

#endif // TINY_CLJ_SOURCE_RESOLVER_H
