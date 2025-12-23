#include "ast.h"
#include "memory.h"
#include "list.h"

CljASTNode* make_ast_node(ID first, CljObject *rest) {
    CljASTNode *node = ALLOC(CljASTNode, 1);
    if (!node) {
        throw_oom();
        return NULL;
    }

    node->base.type = CLJ_AST_NODE;
    node->base.rc = 1;
    node->first = RETAIN(first);
    node->rest = RETAIN(rest);
    node->metadata = NULL;
    node->callsite_cache = NULL;

    return node;
}

CljList* make_ast_list(ID first, CljList *rest) {
    return (CljList*)make_ast_node(first, (CljObject*)rest);
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

void ast_node_set_metadata(CljASTNode *node, CljObject *meta) {
    if (!node) return;
    ASSIGN(node->metadata, meta);
}

CljObject* ast_node_get_metadata(const CljASTNode *node) {
    if (!node) return NULL;
    return node->metadata;
}

void ast_node_set_callsite_cache(CljASTNode *node, CljObject *cache) {
    if (!node) return;
    ASSIGN(node->callsite_cache, cache);
}

CljObject* ast_node_get_callsite_cache(const CljASTNode *node) {
    if (!node) return NULL;
    return node->callsite_cache;
}

CljCallsiteCache* make_callsite_cache(CljSymbol *symbol, ID resolved, uint64_t epoch) {
    CljCallsiteCache *cache = ALLOC(CljCallsiteCache, 1);
    if (!cache) {
        throw_oom();
        return NULL;
    }

    cache->base.type = CLJ_CALLSITE_CACHE;
    cache->base.rc = 1;
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
        ast_node_set_callsite_cache(node, (CljObject*)make_callsite_cache(symbol, resolved, epoch));
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

