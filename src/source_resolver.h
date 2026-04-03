#ifndef TINY_CLJ_SOURCE_RESOLVER_H
#define TINY_CLJ_SOURCE_RESOLVER_H

#include "object.h"

/** Resolve a path to byte array content (KV store or embedded source table). Returns NULL if not found. */
ID resolve_path_to_bytes(const char *path);

/** Seed flash-backed resolver entries (ESP32) from embedded seed payloads when missing. */
void source_resolver_seed_flash_sources(void);

/** Override macOS app-bundle resource lookup root for testing or embedding. Pass NULL to clear. */
void source_resolver_set_bundle_resource_root(const char *root_path);

/** Clear any explicit app-bundle resource lookup root override. */
void source_resolver_clear_bundle_resource_root(void);

#endif // TINY_CLJ_SOURCE_RESOLVER_H
