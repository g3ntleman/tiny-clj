/*
 * Clojure Parser Header
 *
 * Declares parsing functions for Clojure-like syntax including:
 * - Basic data types (symbols, keywords, numbers, strings)
 * - Data structures (lists, vectors, maps)
 * - Meta-data parsing and comment handling
 * - Stack-allocated parsing utilities
 */

#ifndef CLJ_PARSER_H
#define CLJ_PARSER_H

#include "CljObject.h"
#include "exception.h"
#include "namespace.h"
#include "reader.h"
#include <setjmp.h>


// Parser entry points
CljObject *parse(const char *input, EvalState *st);


#endif
