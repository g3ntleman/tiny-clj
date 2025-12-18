#include "eval_comparison.h"
#include "eval.h"
#include "error_messages.h"
#include "exception.h"
#include "strings.h"

typedef enum { COMP_LT, COMP_GT, COMP_LE, COMP_GE, COMP_EQ } ComparisonOp;

// Fixnum fast-path comparison (most common case)
static inline bool compare_fixnums(int va, int vb, ComparisonOp op) {
    switch (op) {
        case COMP_LT: return va < vb;
        case COMP_GT: return va > vb;
        case COMP_LE: return va <= vb;
        case COMP_GE: return va >= vb;
        case COMP_EQ: return va == vb;
    }
    return false;
}

static inline bool extract_numeric_value(ID obj, float *val) {
    unsigned char tag = TAG(obj);
    if (tag == CLJ_INT) {
        *val = (float)as_fixnum((CljValue)obj);
        return true;
    }
    if (tag == CLJ_FLOAT) {
        *val = as_fixed((CljValue)obj);
        return true;
    }
    return false;
}

static CljObject* eval_numeric_comparison(CljList *list,
                                          CljMap *env,
                                          EvalState *st,
                                          const EvalContext *ctx,
                                          ComparisonOp op) {
    CLJ_ASSERT(env != NULL);
    CljList *rest = as_list(LIST_REST(list));
    if (!rest) return NULL;

    ID a = eval_arg_from_expr_with_context(rest->first, env, st, ctx);
    CljList *rest2 = as_list(LIST_REST(rest));
    if (!rest2) return NULL;

    ID b = eval_arg_from_expr_with_context(rest2->first, env, st, ctx);
    if (!a || !b) return NULL;

    // Fixnum fast-path
    if (TAG(a) == TAG_FIXNUM && TAG(b) == TAG_FIXNUM) {
        return compare_fixnums(as_fixnum(a), as_fixnum(b), op) ? clj_true : clj_false;
    }

    // Generic numeric comparison
    float val_a, val_b;
    if (!extract_numeric_value(a, &val_a) || !extract_numeric_value(b, &val_b)) {
        return throw_exception_formatted(EXCEPTION_TYPE, __FILE__, __LINE__, 0, ERR_EXPECTED_NUMBER);
    }

    bool result;
    switch (op) {
        case COMP_LT: result = val_a < val_b; break;
        case COMP_GT: result = val_a > val_b; break;
        case COMP_LE: result = val_a <= val_b; break;
        case COMP_GE: result = val_a >= val_b; break;
        default: result = val_a == val_b; break;
    }
    return result ? clj_true : clj_false;
}

ID eval_comparison_dispatch(CljList *list,
                             CljMap *env,
                             EvalState *st,
                             const EvalContext *ctx,
                             ID op) {
    CljSymbol *op_sym = (CljSymbol*)op;

    // Numeric comparisons: <, >, <=, >=
    if (op_sym == SYM_LT) return eval_numeric_comparison(list, env, st, ctx, COMP_LT);
    if (op_sym == SYM_GT) return eval_numeric_comparison(list, env, st, ctx, COMP_GT);
    if (op_sym == SYM_LE) return eval_numeric_comparison(list, env, st, ctx, COMP_LE);
    if (op_sym == SYM_GE) return eval_numeric_comparison(list, env, st, ctx, COMP_GE);

    // Equality: = (handles both numeric and generic equality)
    CljList *rest = as_list(LIST_REST(list));
    if (!rest) return NULL;

    ID a = eval_arg_from_expr_with_context(rest->first, env, st, ctx);
    CljList *rest2 = as_list(LIST_REST(rest));
    if (!rest2) return NULL;

    ID b = eval_arg_from_expr_with_context(rest2->first, env, st, ctx);
    if (!a || !b) return NULL;

    // Fixnum fast-path (most common case)
    if (TAG(a) == TAG_FIXNUM && TAG(b) == TAG_FIXNUM) {
        return (as_fixnum(a) == as_fixnum(b)) ? clj_true : clj_false;
    }

    // Numeric comparison for floats
    float val_a, val_b;
    if (extract_numeric_value(a, &val_a) && extract_numeric_value(b, &val_b)) {
        return (val_a == val_b) ? clj_true : clj_false;
    }

    // Generic equality (strings, collections, etc.)
    return clj_equal(a, b) ? clj_true : clj_false;
}
