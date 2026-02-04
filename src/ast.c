#include "ast.h"
#include "memory.h"
#include "list.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#if defined(__GNUC__) && !defined(ESP32_BUILD) && !defined(ESP_PLATFORM)
#include <execinfo.h>
#endif

#ifdef DEBUG
static bool trace_ast_alloc_enabled(void) {
    static int cached = -1;
    if (cached == -1) {
        const char *env = getenv("TINYCLJ_TRACE_AST_ALLOC");
        cached = (env && env[0] && strcmp(env, "0") != 0) ? 1 : 0;
    }
    return cached == 1;
}

#if defined(__GNUC__) && !defined(ESP32_BUILD) && !defined(ESP_PLATFORM)
static bool trace_ast_backtrace_enabled(void) {
    static int cached = -1;
    if (cached == -1) {
        const char *env = getenv("TINYCLJ_TRACE_AST_ALLOC_BT");
        cached = (env && env[0] && strcmp(env, "0") != 0) ? 1 : 0;
    }
    return cached == 1;
}
#endif
#endif

/** Construct AST cons cell (rc=1, caller releases). */
CljASTNode* make_ast_node(ID first, ID rest) {
    CljASTNode *node = ALLOC(CljASTNode, 1);
    if (!node) {
        throw_oom();
    }

    node->base.type = CLJ_AST_NODE;
    node->first = RETAIN(first);
    node->rest = RETAIN(rest);
    node->callsite_cache = NULL;

#ifdef DEBUG
    if (trace_ast_alloc_enabled()) {
        static int trace_count = 0;
        if (trace_count < 200) {
            fprintf(stderr, "[ast-alloc] %p first=%p rest=%p\n",
                    (void*)node, (void*)node->first, (void*)node->rest);
#if defined(__GNUC__) && !defined(ESP32_BUILD) && !defined(ESP_PLATFORM)
            if (trace_ast_backtrace_enabled()) {
                void *bt[16];
                int n = backtrace(bt, 16);
                char **symbols = backtrace_symbols(bt, n);
                if (symbols) {
                    for (int i = 0; i < n; i++) {
                        fprintf(stderr, "  %s\n", symbols[i]);
                    }
                    free(symbols);
                }
            }
#endif
            trace_count++;
        }
    }
#endif

    return node;
}

/** Create slot reference for closures; rc=1, caller releases. */
CljSlotRef* make_slot_ref(CljSymbol *symbol, uint8_t depth, uint8_t slot) {
    // Can't use ALLOC(CljSlotRef, ...) because TYPE_OF_CljSlotRef isn't defined in subjective-c.
    CljSlotRef *ref = (CljSlotRef*)alloc(sizeof(CljSlotRef), 1, CLJ_SLOT_REF);
    if (!ref) throw_oom();

    // alloc() sets type, but keep this explicit for robustness.
    ref->base.type = CLJ_SLOT_REF;
    ref->symbol = symbol;
    ref->depth = depth;
    ref->slot = slot;
    return ref;
}

CljList* make_ast_list(ID first, CljList *rest) {
    return (CljList*)make_ast_node(first, (ID)rest);
}

CljASTNode* as_ast_node(ID obj) {
    if (!obj) return NULL;
    if (TAG(obj) == CLJ_AST_NODE) {
        return (CljASTNode*)obj;
    }
    return NULL;
}

bool is_ast_node(ID obj) {
    return obj && TAG(obj) == CLJ_AST_NODE;
}

void ast_node_set_callsite_cache(CljASTNode *node, ID cache) {
    if (!node) return;
    ASSIGN(node->callsite_cache, cache);
}

ID ast_node_get_callsite_cache(const CljASTNode *node) {
    if (!node) return NULL;
    return node->callsite_cache;
}

/** Allocate callsite cache entry; rc=1, caller releases. */
CljCallsiteCache* make_callsite_cache(CljSymbol *symbol, ID resolved, uint64_t epoch) {
    CljCallsiteCache *cache = ALLOC(CljCallsiteCache, 1);
    if (!cache) {
        throw_oom();
    }

    cache->base.type = CLJ_CALLSITE_CACHE;
    cache->symbol = symbol;
    cache->resolved = NULL;
    cache->epoch = epoch;
    ASSIGN(cache->resolved, resolved);
    return cache;
}

CljCallsiteCache* as_callsite_cache(ID obj) {
    if (!obj) return NULL;
    if (TAG(obj) == CLJ_CALLSITE_CACHE) {
        return (CljCallsiteCache*)obj;
    }
    return NULL;
}

bool callsite_cache_is_valid(const CljCallsiteCache *cache, CljSymbol *symbol, uint64_t epoch) {
    return cache && cache->symbol == symbol && cache->epoch == epoch && cache->resolved != NULL;
}

ID ast_node_get_cached_resolution(const CljASTNode *node, CljSymbol *symbol, uint64_t epoch) {
    if (!node || !symbol) return NULL;
    CljCallsiteCache *cache = as_callsite_cache(node->callsite_cache);
    if (!cache) return NULL;
    if (!callsite_cache_is_valid(cache, symbol, epoch)) {
        return NULL;
    }
    return cache->resolved;
}

void ast_node_update_callsite_cache(CljASTNode *node, CljSymbol *symbol, ID resolved, uint64_t epoch) {
    if (!node || !symbol || !resolved) return;
    CljCallsiteCache *cache = as_callsite_cache(node->callsite_cache);
    if (!cache) {
        ast_node_set_callsite_cache(node, (ID)make_callsite_cache(symbol, resolved, epoch));
        return;
    }
    cache->symbol = symbol;
    cache->epoch = epoch;
    ASSIGN(cache->resolved, resolved);
}

void ast_node_clear_callsite_cache(CljASTNode *node) {
    if (!node) return;
    ast_node_set_callsite_cache(node, NULL);
}
