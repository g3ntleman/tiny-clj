#ifndef EVAL_COMPARISON_H
#define EVAL_COMPARISON_H

#include "eval.h"

#ifdef __cplusplus
extern "C" {
#endif

ID eval_comparison_dispatch(CljList *list,
                             CljMap *env,
                             EvalState *st,
                             const EvalContext *ctx,
                             ID op);

#ifdef __cplusplus
}
#endif

#endif // EVAL_COMPARISON_H





