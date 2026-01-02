#ifndef AST_CANON_H
#define AST_CANON_H

#include <subjective-c/object.h>
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

#endif // AST_CANON_H




