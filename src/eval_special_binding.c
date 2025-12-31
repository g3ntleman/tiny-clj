#include "eval_special_forms.h"
#include "common.h"
#include "exception.h"
#include "memory.h"
#include "strings.h"
#include "vector.h"

#include <string.h>

ID eval_special_binding(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    CLJ_ASSERT(list != NULL && "eval_special_binding: list must not be NULL");
    int argc = list_count(list);
    if (argc < 2) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "binding expects a bindings vector");
        return NULL;
    }

    if (!st || !st->dynamic_bindings) {
        throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                  "binding requires an evaluation state with dynamic bindings");
        return NULL;
    }

    // Base env for evaluating init forms and body (match other wrappers).
    CljMap *base_env = env;
    if (!base_env && st->current_ns) {
        base_env = st->current_ns->mappings;
    }

    ID bindings_obj = list_get_element(list, 1);
    if (!bindings_obj || TAG(bindings_obj) != CLJ_VECTOR) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "binding expects a vector of bindings");
        return NULL;
    }

    CljVector *bindings_vec = as_vector(bindings_obj);
    unsigned int bind_count = vector_count(bindings_vec);
    if ((bind_count % 2) != 0) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "binding vector must contain an even number of forms");
        return NULL;
    }

    unsigned int base_depth = vector_count(st->dynamic_bindings);
    CljNamespace *saved_ns = st->current_ns;

    // Build a single frame map: Symbol -> value (value may be NULL/nil).
    // IMPORTANT: Use NOT_FOUND sentinel when looking up so NULL values remain distinguishable from missing.
    CljMap *frame = make_map((int)(bind_count / 2));
    if (!frame) {
        return NULL;
    }

    CljNamespace *bound_ns = NULL;

    // Evaluate init forms in the *current* dynamic context (before pushing the new frame).
    for (unsigned int i = 0; i < bind_count; i += 2) {
        ID sym_id = vector_nth(bindings_vec, i);
        ID expr_id = vector_nth(bindings_vec, i + 1);

        if (!sym_id || TAG(sym_id) != CLJ_SYMBOL) {
            RELEASE(frame);
            throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                      "binding keys must be symbols");
            return NULL;
        }

        CljSymbol *sym = as_symbol(sym_id);
        if (!is_earmuffed_dynamic_symbol(sym)) {
            const char *name = sym && sym->cname ? sym->cname : "<unknown>";
            RELEASE(frame);
            throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                      "binding requires dynamic vars (earmuffed symbols), got %s", name);
            return NULL;
        }

        ID value = expr_id ? eval_body(expr_id, base_env, st, ctx) : NULL;

        // If binding *ns*, accept namespace object (preferred) or resolve symbol/string to namespace.
        if (sym == SYM_NS_STAR) {
            if (!value) {
                RELEASE(frame);
                throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                          "*ns* cannot be bound to nil");
                return NULL;
            }
            int tag = TAG(value);
            if (tag == CLJ_NAMESPACE) {
                bound_ns = (CljNamespace*)value;
            } else if (tag == CLJ_SYMBOL) {
                bound_ns = ns_find_by_symbol(as_symbol(value));
            } else if (tag == CLJ_STRING) {
                bound_ns = ns_find(string_data(value));
            } else {
                RELEASE(frame);
                throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                          "*ns* must be a namespace, symbol, or string");
                return NULL;
            }
            if (!bound_ns) {
                RELEASE(frame);
                throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                          "Namespace not found");
                return NULL;
            }
            value = (ID)bound_ns;
        }

        map_assoc_inplace(&frame, sym_id, value);
        RELEASE(value);
    }

    // Push the new frame and run body; unwind stack even if an exception escapes.
    vector_conj_inplace(&st->dynamic_bindings, frame);
    RELEASE(frame);

    if (bound_ns) {
        st->current_ns = bound_ns;
    }

    ID result = NULL;
    TRY {
        for (int i = 2; i < argc; i++) {
            ID expr = list_get_element(list, i);
            if (!expr) {
                ASSIGN(result, NULL);
                continue;
            }
            ASSIGN(result, eval_body(expr, base_env, st, ctx));
        }
        evalstate_pop_dynamic_bindings_to(st, base_depth);
        st->current_ns = saved_ns;
        return result;
    } CATCH(ex) {
        evalstate_pop_dynamic_bindings_to(st, base_depth);
        st->current_ns = saved_ns;
        // Re-throw after cleanup.
        throw_exception(ex->type[0] != '\0' ? ex->type : "Error",
                        ex->message[0] != '\0' ? ex->message : "Unknown error",
                        ex->file, ex->line, ex->col);
    } END_TRY

    return NULL;
}
