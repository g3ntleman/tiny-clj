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
#include <setjmp.h>

// Stack-based parser constants
#define MAX_STACK_VECTOR_SIZE 64
#define MAX_STACK_MAP_PAIRS 32
#define MAX_STACK_LIST_SIZE 64
#define MAX_STACK_STRING_SIZE 256

// Parser functions
CljObject* parse(const char *input, EvalState *st);
CljObject* parse_expr(const char **input, EvalState *st);
CljObject* parse_vector(const char **input, EvalState *st);
CljObject* parse_map(const char **input, EvalState *st);
CljObject* parse_list(const char **input, EvalState *st);
CljObject* parse_symbol(const char **input, EvalState *st);
CljObject* parse_string(const char **input, EvalState *st);
CljObject* parse_number(const char **input, EvalState *st);

// Meta-reader parser functions
CljObject* parse_meta_reader(const char **input, EvalState *st);
CljObject* parse_meta_map_reader(const char **input, EvalState *st);
CljObject* parse_with_meta(const char **input, EvalState *st);

// Stack-based helper functions
CljObject* vector_from_stack(CljObject **stack, int count);
CljObject* map_from_stack(CljObject **pairs, int pair_count);
CljObject* list_from_stack(CljObject **stack, int count);

// Utility functions
void skip_whitespace(const char **input);
void skip_comments(const char **input);
int is_whitespace(char c);
int is_digit(char c);
int is_alpha(char c);
int is_alphanumeric(char c);

// Error handling - using namespace.h version

#endif
