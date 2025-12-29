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
#include "reader.h"
#include "value.h"  // Must be included before namespace.h (value.h includes symbol.h)
#include "namespace.h"
#include <subjective-c/map.h>  // For CljMap


// === Legacy API (deprecated - use ID API) ===
// Note: parse() can return both objects (CljObject*) and immediate values (CljValue)
// Use ID as the return type to handle both cases

// Convenience API

/**
 * @brief Evaluate a parsed Clojure expression
 * @param parsed_expr The parsed AST
 * @param eval_state The evaluation state
 * @param env Optional environment (if NULL, uses eval_state->current_ns->mappings)
 * @return The evaluated result (autoreleased) or NULL on error
 */
ID eval_parsed(ID parsed_expr, EvalState *eval_state, CljMap *env);

// === CljValue API (Phase 1: Immediates) ===

/**
 * @brief Create CljValue by parsing expression from Reader (Phase 1: Immediates)
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Autoreleased object or NULL (nil) - throws exception on error
 */
CljValue value_by_parsing_expr(Reader *reader, EvalState *st);

/**
 * @brief Parse Clojure expression from Reader
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Autoreleased object or NULL (nil) - throws exception on error (no manual RELEASE needed)
 */
ID parse_expr(Reader *reader, EvalState *st);

/**
 * @brief Parse Clojure expression from Reader (CljValue API)
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Parsed CljValue or NULL on error
 */
CljValue parse_from_reader(Reader *reader, EvalState *st);

/**
 * @brief Parse Clojure expression from string input
 * @param input Input string to parse
 * @param st Evaluation state
 * @return Parsed ID (can be CljObject* for objects or CljValue for immediates) or NULL on error
 */
ID parse(const char *input, EvalState *st);

/**
 * @brief Resolve a namespace alias in the current namespace
 * @param st Evaluation state (for current namespace context)
 * @param alias_str Alias string (without ':' prefix)
 * @return Resolved namespace name symbol, or NULL if alias not found
 */
CljSymbol* resolve_alias_in_namespace(EvalState *st, const char *alias_str);

#endif
