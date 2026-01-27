#include "ast_compile.h"

#include "ast.h"
#include "list.h"
#include "map.h"
#include "vector.h"

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
            // Compiled AST payload storage is not wired up in CljASTNode yet.
            // Keep traversal only; evaluation falls back to the existing evaluator.
            ast_compile_expr_inplace(node->first, st);
            if (node->rest && is_list_type(TAG(node->rest))) {
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

    if (tag == CLJ_VECTOR_PERSISTENT) {
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

