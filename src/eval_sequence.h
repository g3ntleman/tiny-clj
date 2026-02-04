#ifndef EVAL_SEQUENCE_H
#define EVAL_SEQUENCE_H

#include "eval.h"
#include "builtins.h"

#ifdef __cplusplus
extern "C" {
#endif

ID eval_and_call_native_with_context(CljList *list,
                                     CljPersistentMap *env,
                                     ID (*native_func)(ID*, unsigned int),
                                     unsigned int max_args,
                                     const EvalContext *ctx);

ID eval_map_lookup(CljList *list,
                   CljPersistentMap *env,
                   EvalState *st,
                   const EvalContext *ctx,
                   ID map);

#ifdef __cplusplus
}
#endif

#endif // EVAL_SEQUENCE_H
