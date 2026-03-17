#ifndef TINY_CLJ_SYMBOL_CACHE_H
#define TINY_CLJ_SYMBOL_CACHE_H

#include "symbol.h"
#include <stddef.h>
#include <stdbool.h>

typedef enum {
    SYMBOL_CACHE_SCOPE_GLOBAL = 0,
    SYMBOL_CACHE_SCOPE_DEFAULT_NS = 1,
} SymbolCacheScope;

typedef struct {
    CljSymbol **slot;
    const char *cname;
    SymbolCacheScope scope;
} SymbolCacheEntry;

static inline bool symbol_cache_init(const SymbolCacheEntry *entries,
                                     size_t count,
                                     CljSymbol *default_ns) {
    if (!entries) {
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        CljSymbol **slot = entries[i].slot;
        if (!slot || *slot) {
            continue;
        }

        CljSymbol *sym = NULL;
        if (entries[i].scope == SYMBOL_CACHE_SCOPE_DEFAULT_NS) {
            sym = default_ns ? intern_symbol(default_ns, entries[i].cname) : NULL;
        } else {
            sym = intern_symbol_global(entries[i].cname);
        }

        if (!sym) {
            return false;
        }
        *slot = sym;
    }
    return true;
}

static inline bool symbol_cache_init_global(const SymbolCacheEntry *entries, size_t count) {
    return symbol_cache_init(entries, count, NULL);
}

#endif
