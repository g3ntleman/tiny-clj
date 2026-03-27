#ifndef AST_CANON_H
#define AST_CANON_H

#include "object.h"
#include "namespace.h"

/**
 * @brief Canonicalize a parsed AST
 * 
 * Converts CljString symbol names to CljSymbol objects and transforms
 * CljList structures to ASTNode structures.
 * 
 * @param parsed_expr Parsed expression (may contain CljString symbols)
 * @param st Evaluation state for namespace resolution
 * @return Canonicalized expression with CljSymbol objects and ASTNodes
 */
ID canonicalize_ast(ID parsed_expr, EvalState *st);
// Canonicalize data forms without turning lists into AST calls (EDN/data use).
ID canonicalize_ast_as_data(ID parsed_expr, EvalState *st);
void ast_canon_reset_caches(void);

/**
 * @brief One step of macro expansion (same rules as canonicalize/preprocess).
 *
 * Invokes the macro via eval_function_call in the macro's defining namespace (not via Clojure apply).
 * Returns form unchanged (same pointer, retained +1) when not a macro call.
 */
ID ast_canon_macroexpand_1(EvalState *st, ID form);

#endif // AST_CANON_H



