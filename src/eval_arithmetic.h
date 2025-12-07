#ifndef EVAL_ARITHMETIC_H
#define EVAL_ARITHMETIC_H

#include "eval.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ARITH_ADD,
    ARITH_SUB,
    ARITH_MUL,
    ARITH_DIV
} ArithOp;

CljObject* eval_arithmetic_generic_with_context(CljList *list,
                                                CljMap *env,
                                                ArithOp op,
                                                EvalState *st,
                                                const EvalContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // EVAL_ARITHMETIC_H

