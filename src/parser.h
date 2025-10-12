/*
 * Clojure Parser Header
 *
 * Declares parsing functions for Clojure-like syntax including:
 * - Basic data types (symbols, keywords, numbers, strings)
 * - Data structures (lists, vectors, maps)
 * - Meta-data parsing and comment handling
 * - Stack-allocated parsing utilities
 */

#ifndef PARSER_H
#define PARSER_H

#include "object.h"
#include "exception.h"
#include "namespace.h"
#include "reader.h"


// Parser entry points
/**
 * @brief Parse Clojure expression from string input
 * @param input Input string to parse
 * @param st Evaluation state
 * @return Parsed CljObject (caller must release) or NULL on error
 */
CljObject *parse(const char *input, EvalState *st);

// Convenience API

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
