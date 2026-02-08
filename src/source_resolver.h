#ifndef TINY_CLJ_SOURCE_RESOLVER_H
#define TINY_CLJ_SOURCE_RESOLVER_H

#include "object.h"

/**
 * @brief Resolve a path to bytes using KV store first, then embedded sources.
 * @param path Path string (must be non-empty)
 * @return CljByteArray (AUTORELEASE'd) or NULL if not found
 */
ID resolve_path_to_bytes(const char *path);

#endif // TINY_CLJ_SOURCE_RESOLVER_H
