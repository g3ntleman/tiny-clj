#ifndef EVAL_SEQUENCE_H
#define EVAL_SEQUENCE_H

#include "eval.h"

#ifdef __cplusplus
extern "C" {
#endif

ID eval_sequence_dispatch_with_context(CljList *list,
                                       CljMap *env,
                                       ID op,
                                       const EvalContext *ctx);

ID eval_loop_dispatch(CljList *list,
                      CljMap *env,
                      ID op,
                      EvalState *st);

ID eval_map_lookup(CljList *list,
                   CljMap *env,
                   EvalState *st,
                   const EvalContext *ctx,
                   ID map);

#ifdef __cplusplus
}
#endif

#endif // EVAL_SEQUENCE_H




