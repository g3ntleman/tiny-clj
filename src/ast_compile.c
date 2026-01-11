#include "ast_compile.h"

#include "ast.h"
#include "compiled_ast.h"
#include "eval.h" // SYM_*
#include "eval_compiled.h"
#include "list.h"
#include "map.h"
#include "vector.h"

static const CljCompiledPayload k_payload_none = { .kind = CLJ_COMPILED_KIND_NONE, .eval = NULL };
static const CljCompiledPayload k_payload_call = { .kind = CLJ_COMPILED_KIND_CALL, .eval = eval_compiled_call };
static const CljCompiledPayload k_payload_if   = { .kind = CLJ_COMPILED_KIND_IF,   .eval = eval_compiled_if };
static const CljCompiledPayload k_payload_do   = { .kind = CLJ_COMPILED_KIND_DO,   .eval = eval_compiled_do };
static const CljCompiledPayload k_payload_let  = { .kind = CLJ_COMPILED_KIND_LET,  .eval = eval_compiled_let };
static const CljCompiledPayload k_payload_fn   = { .kind = CLJ_COMPILED_KIND_FN,   .eval = eval_compiled_fn };

static const CljCompiledPayload* pick_payload_for_ast_node(const CljASTNode *node) {
    if (!node) return &k_payload_none;
    ID head = node->first;
    if (!head || TAG(head) != CLJ_SYMBOL) return &k_payload_call;

    CljSymbol *sym = as_symbol(head);
    if (sym == SYM_IF)  return &k_payload_if;
    if (sym == SYM_DO)  return &k_payload_do;
    if (sym == SYM_LET) return &k_payload_let;
    if (sym == SYM_FN)  return &k_payload_fn;

    return &k_payload_call;
}

static void ast_compile_expr_inplace(ID expr, EvalState *st);

static void ast_compile_list_inplace(CljList *list, EvalState *st) {
    for (CljList *cur = list; cur; cur = cur->rest ? as_list(cur->rest) : NULL) {
        ast_compile_expr_inplace(cur->first, st);
    }
}

static void ast_compile_vector_inplace(CljVector *vec, EvalState *st) {
    if (!vec) return;
    VECTOR_FOR_EACH(vec, elem) {
        ast_compile_expr_inplace(elem, st);
    }
}

static void ast_compile_map_inplace(CljMap *map, EvalState *st) {
    if (!map) return;
    MAP_FOR_EACH(map, k, v) {
        ast_compile_expr_inplace(k, st);
        ast_compile_expr_inplace(v, st);
    }
}

static void ast_compile_expr_inplace(ID expr, EvalState *st) {
    (void)st; // reserved for future specialization
    if (!expr) return;
    if (IS_IMMEDIATE(expr)) return;

    unsigned char tag = TAG(expr);
    if (tag == CLJ_AST_NODE) {
        CljASTNode *node = as_ast_node(expr);
        if (node) {
            ast_node_set_compiled(node, (void*)pick_payload_for_ast_node(node));
            ast_compile_expr_inplace(node->first, st);
            if (node->rest && list_type_matches(TAG(node->rest))) {
                ast_compile_list_inplace(as_list(node->rest), st);
            } else {
                ast_compile_expr_inplace(node->rest, st);
            }
        }
        return;
    }

    if (tag == CLJ_LIST) {
        ast_compile_list_inplace(as_list(expr), st);
        return;
    }

    if (tag == CLJ_VECTOR) {
        ast_compile_vector_inplace(as_vector(expr), st);
        return;
    }

    if (tag == CLJ_MAP) {
        ast_compile_map_inplace(as_map(expr), st);
        return;
    }
}

void ast_compile_inplace(ID expr, EvalState *st) {
    ast_compile_expr_inplace(expr, st);
}

