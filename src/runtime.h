/*
 * Runtime Header
 * 
 * Memory allocation macros and runtime constants for Tiny-Clj:
 * - STACK_ALLOC: Stack allocation using alloca() for temporary data
 * - ALLOC: Heap allocation using malloc() for persistent data
 * - ALLOC_ZERO: Zero-initialized heap allocation using calloc()
 * - Function call limits for STM32 compatibility
 * - Builtin function registration system
 */

#ifndef TINY_CLJ_RUNTIME_H
#define TINY_CLJ_RUNTIME_H

#include "object.h"
#include "memory.h"
#include <alloca.h>
#include <stdlib.h>

// Memory allocation macros
// Allocate `count` objects of type `type` on the stack
#define STACK_ALLOC(type, count) ((type*) alloca(sizeof(type) * (count)))

// Maximum number of function parameters (STM32-safe)
#define MAX_FUNCTION_PARAMS 32

// Maximum stack depth for function calls
#define MAX_CALL_STACK_DEPTH 20

typedef CljObject* (*BuiltinFn)(CljObject **args, int argc);

void register_builtin(const char *name, BuiltinFn fn);
BuiltinFn find_builtin(const char *name);

#endif
