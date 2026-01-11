#ifndef TINY_CLJ_EVAL_COMPILED_H
#define TINY_CLJ_EVAL_COMPILED_H

#include "compiled_ast.h"

// Compiled evaluators (pretreatment targets). These are safe to call only when
// the caller has decided to use compiled execution for the given node.
ID eval_compiled_if(CljASTNode *node, CljMap *env, EvalState *st, const EvalContext *ctx);
ID eval_compiled_do(CljASTNode *node, CljMap *env, EvalState *st, const EvalContext *ctx);
ID eval_compiled_let(CljASTNode *node, CljMap *env, EvalState *st, const EvalContext *ctx);
ID eval_compiled_fn(CljASTNode *node, CljMap *env, EvalState *st, const EvalContext *ctx);
ID eval_compiled_call(CljASTNode *node, CljMap *env, EvalState *st, const EvalContext *ctx);

#endif
