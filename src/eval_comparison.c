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
    // Direct access to first and second arguments (O(1) instead of O(1) + O(2) = O(3))
    CljList *rest = as_list(LIST_REST(list));
    if (!rest) return NULL;
    ID a = eval_arg_from_expr_with_context(rest->first, env, st, ctx);
    CljList *rest2 = as_list(LIST_REST(rest));
    if (!rest2) {
        // NOTE: a is AUTORELEASE, no cleanup needed
        return NULL;
    }
    ID b = eval_arg_from_expr_with_context(rest2->first, env, st, ctx);
    if (!a || !b) {
        // NOTE: a, b are AUTORELEASE, no cleanup needed
        return NULL;
    }

    bool result = compare_numeric_values(a, b, op);

    if (!result && op != COMP_EQ) {
        float val_a, val_b;
        if (!extract_numeric_values(a, b, &val_a, &val_b)) {
            // NOTE: a, b are AUTORELEASE, no cleanup needed
            return throw_exception_formatted(EXCEPTION_TYPE, __FILE__, __LINE__, 0, ERR_EXPECTED_NUMBER);
        }
    }

    // NOTE: a, b are AUTORELEASE, no cleanup needed
    return result ? clj_true : clj_false;
}

ID eval_comparison_dispatch(CljList *list,
                             CljMap *env,
                             EvalState *st,
                             const EvalContext *ctx,
                             ID op) {
    CljSymbol *op_sym = (CljSymbol*)op;
    if (op_sym == SYM_EQUALS) {
        // Direct access to first and second arguments (O(1) instead of O(1) + O(2) = O(3))
        CljList *rest = as_list(LIST_REST(list));
        if (!rest) return NULL;
        ID a = eval_arg_from_expr_with_context(rest->first, env, st, ctx);
        CljList *rest2 = as_list(LIST_REST(rest));
        if (!rest2) {
            // NOTE: a is AUTORELEASE, no cleanup needed
            return NULL;
        }
        ID b = eval_arg_from_expr_with_context(rest2->first, env, st, ctx);
        if (!a || !b) {
            // NOTE: a, b are AUTORELEASE, no cleanup needed
            return NULL;
        }

        if (compare_numeric_values(a, b, COMP_EQ)) {
            // NOTE: a, b are AUTORELEASE, no cleanup needed
            return clj_true;
        }

        float val_a, val_b;
        if (extract_numeric_values(a, b, &val_a, &val_b)) {
            // NOTE: a, b are AUTORELEASE, no cleanup needed
            return clj_false;
        }

        bool equal = clj_equal(a, b);
        // NOTE: a, b are AUTORELEASE, no cleanup needed
        return equal ? clj_true : clj_false;
    }

    if (op_sym == SYM_LT) return eval_numeric_comparison(list, env, st, ctx, COMP_LT);
    if (op_sym == SYM_GT) return eval_numeric_comparison(list, env, st, ctx, COMP_GT);
    if (op_sym == SYM_LE) return eval_numeric_comparison(list, env, st, ctx, COMP_LE);
    if (op_sym == SYM_GE) return eval_numeric_comparison(list, env, st, ctx, COMP_GE);
    return NULL;
}


