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
#include "value.h"


// === Legacy API (deprecated - use CljValue API) ===
/**
 * @deprecated Use parse_v() instead. Parse Clojure expression from string input
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

// === CljValue API (Phase 1: Immediates) ===

/**
 * @brief Create CljValue by parsing expression from Reader (Phase 1: Immediates)
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return New CljValue or NULL on error
 */
CljValue make_value_by_parsing_expr(Reader *reader, EvalState *st);

/**
 * @brief Parse Clojure expression from string input (CljValue API)
 * @param input Input string to parse
 * @param st Evaluation state
 * @return Parsed CljValue or NULL on error
 */
CljValue parse_v(const char *input, EvalState *st);


#endif
