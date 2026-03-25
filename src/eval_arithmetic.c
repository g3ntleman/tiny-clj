#include "eval_arithmetic.h"
#include "builtins.h"
#include "exception.h"
#include "error_messages.h"
#include "numeric_utils.h"
#include "strings.h"

static inline bool is_numeric_type(ID value) {
    if (!value) return false;
    uint16_t tag = TAG((CljObject*)value);
    return tag == CLJ_INT || tag == CLJ_FLOAT;
}

static inline CljObject* throw_non_numeric_argument(ID value) {
    RELEASE(value);
    throw_exception_formatted("WrongArgumentException", __FILE__, __LINE__, 0,
        "String cannot be used as a Number");
    return NULL;
}

typedef ID (*ArithVariadicFn)(ID *args, unsigned int argc);

static ArithVariadicFn g_arith_variadic_fns[] = {
    native_add_variadic,
    native_sub_variadic,
    native_mul_variadic,
    native_div_variadic,
};

static inline ID apply_arith_op(ID *args, unsigned int argc, ArithOp op) {
    if ((unsigned int)op >= (sizeof(g_arith_variadic_fns) / sizeof(g_arith_variadic_fns[0]))) {
        return NULL;
    }
    return g_arith_variadic_fns[op](args, argc);
}

CljObject* eval_arithmetic_generic_with_context(CljList *list,
                                                CljPersistentMap *env,
                                                ArithOp op,
                                                EvalState *st,
                                                const EvalContext *ctx) {
    if (!list || !list->rest) {
        // argc == 0
        switch (op) {
            case ARITH_ADD: return fixnum(0);
            case ARITH_MUL: return fixnum(1);
            default:
                throw_exception_formatted(EXCEPTION_ARITY, __FILE__, __LINE__, 0,
                    "Wrong number of args: 0");
                return NULL;
        }
    }

    CljList *arg1_node = as_list(list->rest);
    ID arg1_expr = arg1_node->first;
    CljList *arg2_node = arg1_node->rest ? as_list(arg1_node->rest) : NULL;

    // Ultra-fast path: exactly 2 args (most common case: (+ a b), (- a b))
    if (arg2_node && !arg2_node->rest) {
        ID a = eval_arg_from_expr_with_context(arg1_expr, env, st, ctx);
        if (!a) return NULL;
        unsigned char tag_a = TAG(a);

        ID b = eval_arg_from_expr_with_context(arg2_node->first, env, st, ctx);
        if (!b) return NULL;  // a is AUTORELEASE, no cleanup needed
        unsigned char tag_b = TAG(b);

        // Fixnum fast-path: both are fixnums (immediates, no RELEASE needed)
        // Direct inline arithmetic for small values (no overflow risk)
        // For large values, fall through to native helpers which handle overflow
        if (tag_a == TAG_FIXNUM && tag_b == TAG_FIXNUM) {
            int va = as_fixnum(a), vb = as_fixnum(b);
            // Safe range: abs(value) < 2^20 guarantees no overflow for +,-,*
            #define SAFE_RANGE (1 << 20)
            if (va > -SAFE_RANGE && va < SAFE_RANGE && vb > -SAFE_RANGE && vb < SAFE_RANGE) {
                switch (op) {
                    case ARITH_ADD: return fixnum(va + vb);
                    case ARITH_SUB: return fixnum(va - vb);
                    case ARITH_MUL: return fixnum(va * vb);
                    case ARITH_DIV:
                        if (vb == 0) {
                            throw_exception_formatted(EXCEPTION_DIVISION_BY_ZERO, __FILE__, __LINE__, 0,
                                "Division by zero: %d / %d", va, vb);
                            return NULL;
                        }
                        return fixnum(va / vb);
                }
            }
            #undef SAFE_RANGE
            // Large values: use native helpers for overflow handling
            ID fast_args[2] = {a, b};
            return AUTORELEASE(apply_arith_op(fast_args, 2, op));
        }

        // Generic 2-arg path for mixed types
        // NOTE: a, b are AUTORELEASE values from eval_arg_from_expr_with_context
        if (!is_numeric_type(a)) return throw_non_numeric_argument(a);
        if (!is_numeric_type(b)) return throw_non_numeric_argument(b);
        ID fast_args[2] = {a, b};
        return AUTORELEASE(apply_arith_op(fast_args, 2, op));
    }

    // 1-arg path (a is AUTORELEASE, no cleanup needed)
    if (!arg2_node) {
        ID a = eval_arg_from_expr_with_context(arg1_expr, env, st, ctx);
        if (!a) return NULL;
        if (!is_numeric_type(a)) return throw_non_numeric_argument(a);
        return AUTORELEASE(apply_arith_op(&a, 1, op));
    }

    // 3+ args: count while traversing
    int argc = 2;
    CljList *count_rest = arg2_node->rest ? as_list(arg2_node->rest) : NULL;
    while (count_rest) { argc++; count_rest = count_rest->rest ? as_list(count_rest->rest) : NULL; }

    ID args_stack[16];
    ID *args = alloc_obj_array(argc, args_stack);
    if (!args) return NULL;

    // NOTE: All args are AUTORELEASE from eval_arg_from_expr_with_context
    CljList *current = arg1_node;
    for (int i = 0; i < argc; i++) {
        ID expr = current ? current->first : NULL;
        args[i] = eval_arg_from_expr_with_context(expr, env, st, ctx);
        if (!args[i]) {
            free_obj_array(args, args_stack);
            return NULL;
        }
        if (!is_numeric_type(args[i])) {
            free_obj_array(args, args_stack);
            throw_exception_formatted("WrongArgumentException", __FILE__, __LINE__, 0,
                "String cannot be used as a Number");
            return NULL;
        }
        current = current->rest ? as_list(current->rest) : NULL;
    }

    ID result = NULL;
    result = apply_arith_op(args, (unsigned int)argc, op);

    free_obj_array(args, args_stack);
    return AUTORELEASE(result);
}

