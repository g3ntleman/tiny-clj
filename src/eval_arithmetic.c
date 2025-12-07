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
    RELEASE((CljObject*)value);
    return throw_exception_formatted("WrongArgumentException", __FILE__, __LINE__, 0,
        "String cannot be used as a Number");
}

static inline ID apply_arith_op(ID *args, unsigned int argc, ArithOp op) {
    switch (op) {
        case ARITH_ADD: return native_add_variadic(args, argc);
        case ARITH_SUB: return native_sub_variadic(args, argc);
        case ARITH_MUL: return native_mul_variadic(args, argc);
        case ARITH_DIV: return native_div_variadic(args, argc);
    }
    return NULL;
}

/*
 * Performance analysis (todo1):
 * - The current reducer evaluates every operand into a temporary array and
 *   then iterates a second time inside the native helpers, duplicating work.
 * - Hot arithmetic operators (+, -, *, /) are usually called with <= 2
 *   arguments, yet we still go through the full variadic machinery.
 * - Even when operands are simple immediates we retain/release them again
 *   because we lack a streaming path.
 *
 * Action plan:
 *   a) Introduce explicit fast-paths for arities 0, 1 and 2 that evaluate each
 *      operand once and pass them straight to the native helpers.
 *   b) Share numeric validation helpers between fast-path and generic paths so
 *      that WrongArgumentException behaviour stays identical.
 *   c) Afterwards, revisit the >=3 case to fold evaluation and accumulation
 *      into a single streaming pass.
 */
CljObject* eval_arithmetic_generic_with_context(CljList *list,
                                                CljMap *env,
                                                ArithOp op,
                                                EvalState *st,
                                                const EvalContext *ctx) {
    int argc = list ? list_count(list) - 1 : 0;
    CljList *arg_nodes = (list && list->rest) ? as_list(list->rest) : NULL;

    if (argc == 0) {
        switch (op) {
            case ARITH_ADD:
                return fixnum(0);
            case ARITH_MUL:
                return fixnum(1);
            case ARITH_SUB:
            case ARITH_DIV:
                return throw_exception_formatted(EXCEPTION_ARITY, __FILE__, __LINE__, 0,
                    "Wrong number of args: 0");
        }
    } else if (argc <= 2) {
        ID fast_args[2];
        int evaluated = 0;
        CljList *current = arg_nodes;
        while (evaluated < argc && current) {
            ID expr = current->first;
            ID value = eval_arg_from_expr_with_context(expr, env, st, ctx);
            if (!value) {
                for (int j = 0; j < evaluated; j++) {
                    RELEASE((CljObject*)fast_args[j]);
                }
                return NULL;
            }
            if (!is_numeric_type(value)) {
                for (int j = 0; j < evaluated; j++) {
                    RELEASE((CljObject*)fast_args[j]);
                }
                return throw_non_numeric_argument(value);
            }
            fast_args[evaluated++] = value;
            current = as_list(current ? current->rest : NULL);
        }
        ID result = apply_arith_op(fast_args, argc, op);
        for (int j = 0; j < evaluated; j++) {
            RELEASE((CljObject*)fast_args[j]);
        }
        return AUTORELEASE(result);
    }

    ID args_stack[16];
    ID *args = alloc_obj_array(argc, args_stack);
    if (!args) return NULL;

    CljList *current = arg_nodes;
    for (int i = 0; i < argc; i++) {
        ID expr = current ? current->first : NULL;
        args[i] = eval_arg_from_expr_with_context(expr, env, st, ctx);
        if (!args[i]) {
            for (int j = 0; j < i; j++) {
                RELEASE(args[j]);
            }
            free_obj_array(args, args_stack);
            return NULL;
        }

        if (!is_numeric_type(args[i])) {
            for (int j = 0; j <= i; j++) {
                RELEASE(args[j]);
            }
            free_obj_array(args, args_stack);
            return throw_exception_formatted("WrongArgumentException", __FILE__, __LINE__, 0,
                "String cannot be used as a Number");
        }

        current = current ? as_list(current->rest) : NULL;
    }

    ID result = NULL;
    switch (op) {
        case ARITH_ADD:
            result = native_add_variadic(args, argc);
            break;
        case ARITH_SUB:
            result = native_sub_variadic(args, argc);
            break;
        case ARITH_MUL:
            result = native_mul_variadic(args, argc);
            break;
        case ARITH_DIV:
            result = native_div_variadic(args, argc);
            break;
    }

    for (int i = 0; i < argc; i++) {
        RELEASE(args[i]);
    }
    free_obj_array(args, args_stack);

    return AUTORELEASE(result);
}

