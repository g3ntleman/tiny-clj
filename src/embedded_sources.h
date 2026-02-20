#ifndef TINY_CLJ_EMBEDDED_SOURCES_H
#define TINY_CLJ_EMBEDDED_SOURCES_H

#include <stdbool.h>
#include <stdint.h>

/** Ensure embedded source map is ready (no-op if no embedded sources in this build). */
void embedded_source_map_init(void);

/** Lookup embedded source bytes by virtual path (e.g. /libs/clojure/core.clj). */
bool embedded_source_lookup(const char *path, const uint8_t **out_data, int *out_len);

#endif // TINY_CLJ_EMBEDDED_SOURCES_H
