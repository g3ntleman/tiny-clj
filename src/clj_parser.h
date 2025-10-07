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

#include "object.h"
#include "exception.h"
#include "namespace.h"
#include "reader.h"
#include <setjmp.h>


// Parser entry points
CljObject *parse(const char *input, EvalState *st);

// Convenience API
/**
 * @brief Parse a Clojure expression from a string
 * @param expr_str The Clojure expression as a string
 * @param eval_state The evaluation state
 * @return The parsed AST (caller must release) or NULL on error
 */
CljObject* parse_string(const char* expr_str, EvalState *eval_state);

/**
 * @brief Evaluate a parsed Clojure expression
 * @param parsed_expr The parsed AST
 * @param eval_state The evaluation state
 * @return The evaluated result (autoreleased) or NULL on error
 */
CljObject* eval_parsed(CljObject *parsed_expr, EvalState *eval_state);

/**
 * @brief Parse and evaluate a Clojure expression from a string (convenience)
 * @param expr_str The Clojure expression as a string
 * @param eval_state The evaluation state
 * @return The evaluated result (autoreleased) or NULL on error
 */
CljObject* eval_string(const char* expr_str, EvalState *eval_state);


#endif
