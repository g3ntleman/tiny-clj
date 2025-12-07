#ifndef EVAL_SPECIAL_FORMS_H
#define EVAL_SPECIAL_FORMS_H

#include "eval.h"

#ifdef __cplusplus
extern "C" {
#endif

ID eval_special_form_dispatch(CljList *list,
                              CljMap *env,
                              EvalState *st,
                              const EvalContext *ctx,
                              CljSymbol *op_sym);

ID eval_handle_recur(CljList *list, const EvalContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // EVAL_SPECIAL_FORMS_H

