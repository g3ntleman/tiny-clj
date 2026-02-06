#ifndef TINY_CLJ_AST_H
#define TINY_CLJ_AST_H

#include "object.h"
#include "value.h"
#include "symbol.h"
#include "vector.h"
#include <stdint.h>

struct CljList;

typedef struct CljASTNode {
    CljObject base;       // object header
    ID first;             // head of list (form or value)
    ID rest;              // tail (next ASTNode or nil)
    ID callsite_cache;    // optional CljCallsiteCache for resolution
} CljASTNode;

typedef struct CljASTCall {
    CljObject base;             // object header
    ID op;                      // operator (e.g. fn ref)
    CljPersistentVector *args;  // argument list
    ID callsite_cache;          // optional CljCallsiteCache for resolution
} CljASTCall;

CljASTNode* make_ast_node(ID first, ID rest);
struct CljList* make_ast_list(ID first, struct CljList *rest);
CljASTCall* make_ast_call(ID op, CljPersistentVector *args);

CljASTNode* as_ast_node(ID obj);
bool is_ast_node(ID obj);
CljASTCall* as_ast_call(ID obj);
bool is_ast_call(ID obj);
void ast_node_set_callsite_cache(CljASTNode *node, ID cache);
ID ast_node_get_callsite_cache(const CljASTNode *node);
void ast_call_set_callsite_cache(CljASTCall *call, ID cache);
ID ast_call_get_callsite_cache(const CljASTCall *call);

// Lexical addressing: direct reference to a local variable via (depth, slot).
// - depth=0: current CallFrame
// - depth=1: parent CallFrame, etc.
typedef struct CljSlotRef {
    CljObject base;       // object header
    uint8_t depth;        // lexical depth (0 = current frame)
    uint8_t slot;         // slot index in that frame
#ifdef DEBUG
    CljSymbol *symbol;    // for debugging/errors only (symbols are interned)
#endif
} CljSlotRef;

CljSlotRef* make_slot_ref(CljSymbol *symbol, uint8_t depth, uint8_t slot);
static inline bool is_slot_ref(ID obj) { return obj && TAG(obj) == CLJ_SLOT_REF; }

typedef struct CljCallsiteCache {
    CljObject base;       // object header
    CljSymbol *symbol;    // symbol this cache is for
    ID resolved;          // cached resolution (fn, macro, etc.)
    uint64_t epoch;       // validity epoch (env/namespace version)
} CljCallsiteCache;

CljCallsiteCache* make_callsite_cache(CljSymbol *symbol, ID resolved, uint64_t epoch);
CljCallsiteCache* as_callsite_cache(ID obj);
bool callsite_cache_is_valid(const CljCallsiteCache *cache, CljSymbol *symbol, uint64_t epoch);
ID ast_node_get_cached_resolution(const CljASTNode *node, CljSymbol *symbol, uint64_t epoch);
void ast_node_update_callsite_cache(CljASTNode *node, CljSymbol *symbol, ID resolved, uint64_t epoch);
void ast_node_clear_callsite_cache(CljASTNode *node);
ID ast_call_get_cached_resolution(const CljASTCall *call, CljSymbol *symbol, uint64_t epoch);
void ast_call_update_callsite_cache(CljASTCall *call, CljSymbol *symbol, ID resolved, uint64_t epoch);
void ast_call_clear_callsite_cache(CljASTCall *call);

#endif
