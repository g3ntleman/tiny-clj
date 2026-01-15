#ifndef EVAL_SPECIAL_FORMS_H
#define EVAL_SPECIAL_FORMS_H

#include "eval.h"

#ifdef __cplusplus
extern "C" {
#endif

// DRY helper: get env or fallback to namespace mappings
static INLINE CljMap *eval_env_or_ns_mappings(CljMap *env, EvalState *st) {
    return env ? env : (st && st->current_ns ? st->current_ns->mappings : NULL);
}

ID eval_special_form_dispatch(CljList *list,
                              CljMap *env,
                              EvalState *st,
                              const EvalContext *ctx,
                              CljSymbol *op_sym);

ID eval_handle_recur(CljList *list, const EvalContext *ctx);

// Special Form evaluation functions (exported for symbol initialization)
ID eval_special_if(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_when(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_while(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_cond(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_case(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_do(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_and(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_or(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_fn(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_let(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_var(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_quote(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_recur(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_loop(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_throw(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_go(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_time(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_dotimes(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_try(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_binding(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_quasiquote(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_defmacro(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // EVAL_SPECIAL_FORMS_H



