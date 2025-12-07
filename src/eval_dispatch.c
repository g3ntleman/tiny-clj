#include "eval_dispatch.h"
#include "eval_arithmetic.h"
#include "symbol.h"

typedef struct {
    CljSymbol *symbol;
    ArithOp op;
} ArithmeticDispatchEntry;

static ArithmeticDispatchEntry arithmetic_dispatch_table[] = {
    { NULL, ARITH_ADD },
    { NULL, ARITH_SUB },
    { NULL, ARITH_MUL },
    { NULL, ARITH_DIV }
};

static void init_arithmetic_dispatch_table(void) {
    arithmetic_dispatch_table[0].symbol = SYM_PLUS;
    arithmetic_dispatch_table[1].symbol = SYM_MINUS;
    arithmetic_dispatch_table[2].symbol = SYM_MULTIPLY;
    arithmetic_dispatch_table[3].symbol = SYM_DIVIDE;
}

static inline int lookup_arithmetic_op(CljSymbol *op_sym) {
    if (!arithmetic_dispatch_table[0].symbol && SYM_PLUS) {
        init_arithmetic_dispatch_table();
    }
    for (int i = 0; i < 4; i++) {
        if (arithmetic_dispatch_table[i].symbol == op_sym) {
            return arithmetic_dispatch_table[i].op;
        }
    }
    return -1;
}

ID eval_arithmetic_dispatch_with_context(CljList *list,
                                         CljMap *env,
                                         EvalState *st,
                                         ID op,
                                         const EvalContext *ctx) {
    CljSymbol *op_sym = (CljSymbol*)op;
    int arith_index = lookup_arithmetic_op(op_sym);
    if (arith_index >= 0) {
        return eval_arithmetic_generic_with_context(list, env, (ArithOp)arith_index, st, ctx);
    }
    return NULL;
}
