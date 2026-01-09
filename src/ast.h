#ifndef TINY_CLJ_AST_H
#define TINY_CLJ_AST_H

#include "object.h"
#include "value.h"
#include "symbol.h"
#include <stdint.h>

struct CljList;

typedef struct CljASTNode {
    CljObject base;
    CljObject *first;
    CljObject *rest;
    CljObject *callsite_cache;
} CljASTNode;

CljASTNode* make_ast_node(ID first, CljObject *rest);
struct CljList* make_ast_list(ID first, struct CljList *rest);

CljASTNode* as_ast_node(ID obj);
bool is_ast_node(ID obj);
void ast_node_set_callsite_cache(CljASTNode *node, CljObject *cache);
CljObject* ast_node_get_callsite_cache(const CljASTNode *node);

typedef struct CljCallsiteCache {
    CljObject base;
    CljSymbol *symbol;
    ID resolved;
    uint64_t epoch;
} CljCallsiteCache;

CljCallsiteCache* make_callsite_cache(CljSymbol *symbol, ID resolved, uint64_t epoch);
CljCallsiteCache* as_callsite_cache(ID obj);
bool callsite_cache_is_valid(const CljCallsiteCache *cache, CljSymbol *symbol, uint64_t epoch);
ID ast_node_get_cached_resolution(const CljASTNode *node, CljSymbol *symbol, uint64_t epoch);
void ast_node_update_callsite_cache(CljASTNode *node, CljSymbol *symbol, ID resolved, uint64_t epoch);
void ast_node_clear_callsite_cache(CljASTNode *node);

#endif
