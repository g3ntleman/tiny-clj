#include "eval_comparison.h"
#include "eval.h"
#include "error_messages.h"
#include "exception.h"
#include "strings.h"

#define RELEASE_TWO_ARGS_SAFE(a, b) do { \
    RELEASE(a); \
    RELEASE(b); \
} while(0)

#define RELEASE_TWO_ARGS(a, b) RELEASE_TWO_ARGS_SAFE(a, b)

typedef enum { COMP_LT, COMP_GT, COMP_LE, COMP_GE, COMP_EQ } ComparisonOp;

static bool extract_numeric_values(CljObject *a, CljObject *b, float *val_a, float *val_b) {
    switch (TAG(a)) {
        case CLJ_INT:
            *val_a = (float)as_fixnum((CljValue)a);
            break;
        case CLJ_FLOAT:
            *val_a = as_fixed((CljValue)a);
            break;
        default:
            return false;
    }

    switch (TAG(b)) {
        case CLJ_INT:
            *val_b = (float)as_fixnum((CljValue)b);
            break;
        case CLJ_FLOAT:
            *val_b = as_fixed((CljValue)b);
            break;
        default:
            return false;
    }

    return true;
}

static bool compare_numeric_values(ID a, ID b, ComparisonOp op) {
    float val_a, val_b;

    if (!extract_numeric_values(a, b, &val_a, &val_b)) {
        return false;
    }

    switch (op) {
        case COMP_LT: return val_a < val_b;
        case COMP_GT: return val_a > val_b;
        case COMP_LE: return val_a <= val_b;
        case COMP_GE: return val_a >= val_b;
        case COMP_EQ: return val_a == val_b;
        default: return false;
    }
}

static CljObject* eval_numeric_comparison(CljList *list,
                                          CljMap *env,
                                          EvalState *st,
                                          const EvalContext *ctx,
                                          ComparisonOp op) {
    CLJ_ASSERT(env != NULL);
    ID a = eval_arg_with_context(list, 1, env, st, ctx);
    ID b = eval_arg_with_context(list, 2, env, st, ctx);
    if (!a || !b) {
        RELEASE(a);
        RELEASE(b);
        return NULL;
    }

    bool result = compare_numeric_values(a, b, op);

    if (!result && op != COMP_EQ) {
        float val_a, val_b;
        if (!extract_numeric_values(a, b, &val_a, &val_b)) {
            RELEASE(a);
            RELEASE(b);
            return throw_exception_formatted(EXCEPTION_TYPE, __FILE__, __LINE__, 0, ERR_EXPECTED_NUMBER);
        }
    }

    RELEASE(a);
    RELEASE(b);
    return result ? clj_true : clj_false;
}

ID eval_comparison_dispatch(CljList *list,
                             CljMap *env,
                             EvalState *st,
                             const EvalContext *ctx,
                             ID op) {
    CljSymbol *op_sym = (CljSymbol*)op;
    if (op_sym == SYM_EQUALS) {
        ID a = eval_arg_with_context(list, 1, env, st, ctx);
        ID b = eval_arg_with_context(list, 2, env, st, ctx);
        if (!a || !b) {
            RELEASE(a);
            RELEASE(b);
            return NULL;
        }

        if (compare_numeric_values(a, b, COMP_EQ)) {
            RELEASE_TWO_ARGS(a, b);
            return clj_true;
        }

        float val_a, val_b;
        if (extract_numeric_values(a, b, &val_a, &val_b)) {
            RELEASE_TWO_ARGS(a, b);
            return clj_false;
        }

        bool equal = clj_equal(a, b);
        RELEASE_TWO_ARGS(a, b);
        return equal ? clj_true : clj_false;
    }

    if (op_sym == SYM_LT) return eval_numeric_comparison(list, env, st, ctx, COMP_LT);
    if (op_sym == SYM_GT) return eval_numeric_comparison(list, env, st, ctx, COMP_GT);
    if (op_sym == SYM_LE) return eval_numeric_comparison(list, env, st, ctx, COMP_LE);
    if (op_sym == SYM_GE) return eval_numeric_comparison(list, env, st, ctx, COMP_GE);
    return NULL;
}


