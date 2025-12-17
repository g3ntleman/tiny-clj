#include "eval_dispatch.h"
#include "eval_arithmetic.h"
#include "symbol.h"

// O(1) arithmetic dispatch using direct pointer comparison
ID eval_arithmetic_dispatch_with_context(CljList *list,
                                         CljMap *env,
                                         EvalState *st,
                                         ID op,
                                         const EvalContext *ctx) {
    CljSymbol *op_sym = (CljSymbol*)op;
    if (op_sym == SYM_PLUS)     return eval_arithmetic_generic_with_context(list, env, ARITH_ADD, st, ctx);
    if (op_sym == SYM_MINUS)    return eval_arithmetic_generic_with_context(list, env, ARITH_SUB, st, ctx);
    if (op_sym == SYM_MULTIPLY) return eval_arithmetic_generic_with_context(list, env, ARITH_MUL, st, ctx);
    if (op_sym == SYM_DIVIDE)   return eval_arithmetic_generic_with_context(list, env, ARITH_DIV, st, ctx);
    return NULL;
}

