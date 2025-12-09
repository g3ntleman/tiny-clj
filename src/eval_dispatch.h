#ifndef EVAL_DISPATCH_H
#define EVAL_DISPATCH_H

#include "eval.h"

#ifdef __cplusplus
extern "C" {
#endif

ID eval_arithmetic_dispatch_with_context(CljList *list,
                                         CljMap *env,
                                         EvalState *st,
                                         ID op,
                                         const EvalContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // EVAL_DISPATCH_H



