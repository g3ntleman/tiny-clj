#include "eval_compiled.h"

#include "eval.h"
#include "eval_special_forms.h"
#include "list.h"

static inline CljList make_pseudo_list(ID head, ID rest) {
    CljList l;
    l.base.type = CLJ_LIST;
    l.base.rc = 1;
    l.first = head;
    l.rest = rest;
    return l;
}

ID eval_compiled_if(CljASTNode *node, CljMap *env, EvalState *st, const EvalContext *ctx) {
    if (!node) return NULL;
    CljList pseudo = make_pseudo_list((ID)SYM_IF, node->rest);
    return eval_special_if(&pseudo, env, st, ctx);
}

ID eval_compiled_do(CljASTNode *node, CljMap *env, EvalState *st, const EvalContext *ctx) {
    if (!node) return NULL;
    CljList pseudo = make_pseudo_list((ID)SYM_DO, node->rest);
    return eval_special_do(&pseudo, env, st, ctx);
}

ID eval_compiled_let(CljASTNode *node, CljMap *env, EvalState *st, const EvalContext *ctx) {
    if (!node) return NULL;
    CljList pseudo = make_pseudo_list((ID)SYM_LET, node->rest);
    return eval_special_let(&pseudo, env, st, ctx);
}

ID eval_compiled_fn(CljASTNode *node, CljMap *env, EvalState *st, const EvalContext *ctx) {
    if (!node) return NULL;
    CljList pseudo = make_pseudo_list((ID)SYM_FN, node->rest);
    return eval_special_fn(&pseudo, env, st, ctx);
}

ID eval_compiled_call(CljASTNode *node, CljMap *env, EvalState *st, const EvalContext *ctx) {
    if (!node) return NULL;
    // Minimal compiled call: delegate to existing evaluator for now.
    // The pretreatment win comes from avoiding repeated special-form dispatch when enabled.
    return eval_list((CljList*)node, env, st, ctx);
}

