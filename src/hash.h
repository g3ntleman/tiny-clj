#ifndef TINY_CLJ_HASH_H
#define TINY_CLJ_HASH_H

#include "object.h"
#include <stdint.h>

// Complete hash implementation for all Clojure types
uint32_t clj_hash_full(ID value);

#endif // TINY_CLJ_HASH_H

