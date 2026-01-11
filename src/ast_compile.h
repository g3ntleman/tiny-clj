#ifndef TINY_CLJ_AST_COMPILE_H
#define TINY_CLJ_AST_COMPILE_H

#include "namespace.h" // EvalState
#include "value.h"

// Attach (optional) compiled payloads to canonicalized AST nodes.
// No behavior change by itself; runtime execution must opt-in.
void ast_compile_inplace(ID expr, EvalState *st);

#endif
