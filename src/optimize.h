/*
 * Function body optimization walk for Tiny-CLJ.
 *
 * This module exposes a single recursive walk that applies multiple
 * optimization rules in one traversal.
 */

#ifndef OPTIMIZE_H
#define OPTIMIZE_H

#include "common.h"
#include "object.h"
#include "symbol.h"
#include "exception.h"
#include "list.h"
#include "memory.h"

// Check if an expression is in tail position within a body
bool is_tail_position(CljObject *expr, CljObject *body);

// Check if a function call is recursive (calls the same function)
bool is_recursive_call(CljObject *call_expr, CljObject *func_name);

// Validate that all recur calls are in tail position
void validate_recur_positions(CljObject *body, CljObject *parent_body);

// Optimize a function body expression via a recursive walk.
// Current optimizations include:
// - recursive tail-call rewrite to `recur`
// - record constant-key lookup rewrite to index lookups
CljObject* optimize_function_body_walk(CljObject *body, CljObject *func_name,
                                       CljObject **params, int param_count,
                                       CljObject *parent_body);

#endif // OPTIMIZE_H
