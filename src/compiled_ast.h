#ifndef TINY_CLJ_COMPILED_AST_H
#define TINY_CLJ_COMPILED_AST_H

#include "ast.h"
#include "eval.h"      // EvalContext
#include "map.h"       // CljPersistentMap
#include "namespace.h" // EvalState

typedef enum {
    CLJ_COMPILED_KIND_NONE = 0,
    CLJ_COMPILED_KIND_CALL,
    CLJ_COMPILED_KIND_IF,
    CLJ_COMPILED_KIND_DO,
    CLJ_COMPILED_KIND_LET,
    CLJ_COMPILED_KIND_FN,
} CljCompiledKind;

typedef ID (*CljCompiledEvalFn)(CljASTNode *node, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);

typedef struct {
    CljCompiledKind kind;
    CljCompiledEvalFn eval;
} CljCompiledPayload;

static inline const CljCompiledPayload* compiled_payload_from_ptr(const void *p) {
    return (const CljCompiledPayload*)p;
}

#endif
