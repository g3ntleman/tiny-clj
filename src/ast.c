/**
 * @file ast.c
 * @brief AST node construction and callsite cache management.
 */

#include "ast.h"
#include "memory.h"
#include "list.h"
#include "subjective-c/debug_trace.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef DEBUG
static const DebugTraceConfig ast_trace_cfg = {
    .env_var_name = "TINYCLJ_TRACE_AST_ALLOC",
    .env_var_bt_name = "TINYCLJ_TRACE_AST_ALLOC_BT",
    .prefix = "[ast-alloc]",
    .max_traces = 200
};
#endif

/**
 * @brief Construct AST cons cell.
 * @param first First element (retained)
 * @param rest Rest of list (retained)
 * @return New AST node with rc=1, caller must release
 */
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
    static int trace_count = 0;
    debug_trace_allocation(&ast_trace_cfg, (void*)node, (void*)node->first, (void*)node->rest, &trace_count);
#endif

    return node;
}

/**
 * @brief Create lexical slot reference for closures.
 * @param symbol Symbol for debugging (not retained)
 * @param depth Depth in call frame stack (0=current frame)
 * @param slot Slot index within frame
 * @return Slot reference with rc=1, caller must release
 */
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

/**
 * @brief Create AST-backed list node.
 * @param first First element (retained)
 * @param rest Rest list (retained)
 * @return AST list with rc=1, caller must release
 */
CljList* make_ast_list(ID first, CljList *rest) {
    return (CljList*)make_ast_node(first, (ID)rest);
}

/**
 * @brief Cast object to AST node if valid.
 * @param obj Object to cast
 * @return AST node or NULL
 */
CljASTNode* as_ast_node(ID obj) {
    if (!obj) return NULL;
    if (TAG(obj) == CLJ_AST_NODE) {
        return (CljASTNode*)obj;
    }
    return NULL;
}

/**
 * @brief Check if object is an AST node.
 * @param obj Object to check
 * @return true if AST node, false otherwise
 */
bool is_ast_node(ID obj) {
    return obj && TAG(obj) == CLJ_AST_NODE;
}

/**
 * @brief Set callsite cache for AST node.
 * @param node AST node to update
 * @param cache Cache entry (may be NULL to clear)
 */
void ast_node_set_callsite_cache(CljASTNode *node, ID cache) {
    if (!node) return;
    ASSIGN(node->callsite_cache, cache);
}

/**
 * @brief Get callsite cache from AST node.
 * @param node AST node
 * @return Cache entry or NULL
 */
ID ast_node_get_callsite_cache(const CljASTNode *node) {
    if (!node) return NULL;
    return node->callsite_cache;
}

/**
 * @brief Allocate callsite cache entry.
 * @param symbol Symbol being cached (not retained)
 * @param resolved Resolved value (retained via ASSIGN)
 * @param epoch Namespace epoch for invalidation
 * @return Cache entry with rc=1, caller must release
 */
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

/**
 * @brief Cast object to callsite cache if valid.
 * @param obj Object to cast
 * @return Callsite cache or NULL
 */
CljCallsiteCache* as_callsite_cache(ID obj) {
    if (!obj) return NULL;
    if (TAG(obj) == CLJ_CALLSITE_CACHE) {
        return (CljCallsiteCache*)obj;
    }
    return NULL;
}

/**
 * @brief Check if callsite cache is valid.
 * @param cache Cache to validate
 * @param symbol Expected symbol
 * @param epoch Expected epoch
 * @return true if cache matches and has resolved value
 */
bool callsite_cache_is_valid(const CljCallsiteCache *cache, CljSymbol *symbol, uint64_t epoch) {
    return cache && cache->symbol == symbol && cache->epoch == epoch && cache->resolved != NULL;
}

/**
 * @brief Get cached resolution from AST node.
 * @param node AST node with cache
 * @param symbol Symbol to lookup
 * @param epoch Current namespace epoch
 * @return Cached resolved value or NULL if not cached/invalid
 */
ID ast_node_get_cached_resolution(const CljASTNode *node, CljSymbol *symbol, uint64_t epoch) {
    if (!node || !symbol) return NULL;
    CljCallsiteCache *cache = as_callsite_cache(node->callsite_cache);
    if (!cache) return NULL;
    if (!callsite_cache_is_valid(cache, symbol, epoch)) {
        return NULL;
    }
    return cache->resolved;
}

/**
 * @brief Update callsite cache for AST node.
 * @param node AST node to update
 * @param symbol Symbol being resolved
 * @param resolved Resolved value (retained)
 * @param epoch Current namespace epoch
 */
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

/**
 * @brief Clear callsite cache from AST node.
 * @param node AST node to clear
 */
void ast_node_clear_callsite_cache(CljASTNode *node) {
    if (!node) return;
    ast_node_set_callsite_cache(node, NULL);
}
