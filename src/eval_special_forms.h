#ifndef EVAL_SPECIAL_FORMS_H
#define EVAL_SPECIAL_FORMS_H

#include "eval.h"

#ifdef __cplusplus
extern "C" {
#endif

// DRY helper: get env or fallback to namespace mappings
__attribute__((unused))
static INLINE CljPersistentMap *eval_env_or_ns_mappings(CljPersistentMap *env, EvalState *st) {
    return env ? env : (st && st->current_ns ? st->current_ns->mappings : NULL);
}

ID eval_handle_recur(CljPersistentVector *args, const EvalContext *ctx);

// Special Form evaluation functions (exported for symbol initialization)
ID eval_special_if(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_when(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_while(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_cond(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_case(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_do(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_and(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_or(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_fn(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_let(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_var(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_quote(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_recur(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_loop(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_throw(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_go(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_time(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);
#ifdef DEBUG
ID eval_special_heap(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);
#endif
ID eval_special_dotimes(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_try(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_binding(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);
ID eval_special_quasiquote(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);
void eval_special_forms_reset_caches(void);
ID eval_special_defmacro(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // EVAL_SPECIAL_FORMS_H
