/*
 * Tail Call Optimization (TCO) for Tiny-CLJ
 * 
 * This module provides functions to detect and transform recursive tail calls
 * into explicit `recur` calls, following the Clojure approach.
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

// Transform recursive tail calls to recur
// This is the main entry point for TCO transformation
CljObject* transform_recursive_tail_calls(CljObject *body, CljObject *func_name, 
                                          CljObject **params, int param_count,
                                          CljObject *parent_body);

#endif // OPTIMIZE_H

