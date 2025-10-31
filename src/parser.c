/*
 * Clojure Parser Implementation
 * 
 * Features:
 * - Parses Clojure-like syntax (lists, vectors, maps, symbols, keywords,
 * numbers, strings)
 * - Supports meta-data parsing (^metadata, #^{...}, (with-meta obj meta))
 * - Handles comments (line comments ; and block comments #| ... |#)
 * - Stack-allocated parsing for memory efficiency
 * - STM32-compatible implementation
 */

#include "parser.h"
#include "function_call.h"
#include "list.h"
#include "vector.h"
#include <string.h>
#include "map.h"
#include <stdbool.h>
#include "memory.h"
#include "utf8.h"
#include "vector.h"
#include "value.h"
#include "symbol.h"
#include "meta.h"
#include "exception.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>


// Helper function for parser exceptions
static void throw_parser_exception(const char *message, Reader *reader) {
    throw_exception(EXCEPTION_TYPE_PARSE, message, "parser", reader->line, reader->column);
}

// Stack-based parser constants
#define MAX_STACK_VECTOR_SIZE 256
#define MAX_STACK_MAP_PAIRS 32
#define MAX_STACK_LIST_SIZE 64
#define MAX_STACK_STRING_SIZE 4096

// Global jump buffer for parse errors

// Error handling - using namespace.h version




/** @brief Check if character is a digit */
static bool is_digit(char c) { return c >= '0' && c <= '9'; }

/** @brief Check if character is alphabetic */
static bool is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/** @brief Check if character is alphanumeric or symbol character */
static bool is_alphanumeric(char c) {
  return is_alpha(c) || is_digit(c) || c == '-' || c == '_' || c == '?' ||
         c == '!' || c == '/' || c == '.';
}


// Forward declarations for Reader-based parser functions


/**
 * @brief Parse list literal (a b c) using const char** input
 * @param input Pointer to current position in input string
 * @param st Evaluation state
 * @return Parsed list CljObject or NULL on error
 */

/**
 * @brief Parse symbol literal (identifier) using const char** input
 * @param input Pointer to current position in input string
 * @param st Evaluation state
 * @return Parsed symbol CljObject or NULL on error
 */

/**
 * @brief Parse string literal "text" using const char** input
 * @param input Pointer to current position in input string
 * @param st Evaluation state
 * @return Parsed string CljObject or NULL on error
 */

/**
 * @brief Parse number literal (integer/float) using const char** input
 * @param input Pointer to current position in input string
 * @param st Evaluation state
 * @return Parsed number CljObject or NULL on error
 */

/**
 * @brief Parse metadata ^meta using const char** input
 * @param input Pointer to current position in input string
 * @param st Evaluation state
 * @return Object with applied metadata or NULL on error
 */

/**
 * @brief Parse metadata map #^{...} using const char** input
 * @param input Pointer to current position in input string
 * @param st Evaluation state
 * @return Object with applied metadata map or NULL on error
 */





/**
 * @brief Reader helper: advance and return character
 * @param reader Reader instance
 * @return Next character from input
 */
static char reader_consume(Reader *reader) {
  return reader_next(reader);
}

// Forward declarations for Reader-based parser functions
static ID parse_meta(Reader *reader, EvalState *st);
static ID parse_meta_map(Reader *reader, EvalState *st);
static ID parse_vector(Reader *reader, EvalState *st);
static ID parse_map(Reader *reader, EvalState *st);
static ID parse_list(Reader *reader, EvalState *st);
static ID parse_list_rest(Reader *reader, EvalState *st);
static ID parse_string(Reader *reader, EvalState *st);
static ID parse_symbol(Reader *reader, EvalState *st);
static CljObject* parse_number(Reader *reader, EvalState *st);

// Ensure that every parse step advances the reader or hits EOF, otherwise throw
static ID parse_expr_owned(Reader *reader, EvalState *st); // forward

static ID parse_expr_with_progress(Reader *reader, EvalState *st) {
  size_t before = reader_offset(reader);
  ID val = parse_expr_owned(reader, st);
  size_t after = reader_offset(reader);
  if (after <= before && !reader_eof(reader)) {
    throw_parser_exception(ERROR_INVALID_SYNTAX, reader);
    return NULL;
  }
  return val;
}

/**
 * @brief Create CljObject by parsing expression from Reader
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return New CljObject with RC=1 or NULL on error (caller must release)
 */
static ID parse_expr_owned(Reader *reader, EvalState *st) {
  reader_skip_all(reader);
  if (reader_is_eof(reader)) {
    throw_parser_exception(ERROR_INVALID_SYNTAX, reader);
    return NULL;
  }
  char c = reader_current(reader);
  if (c == '^')
    return parse_meta(reader, st);
  if (c == '#' && reader_peek_ahead(reader, 1) == '^')
    return parse_meta_map(reader, st);
  if (c == '[')
    return parse_vector(reader, st);
  if (c == '{')
    return parse_map(reader, st);
  if (c == '(')
    return parse_list(reader, st);
  if (c == '"')
    return parse_string(reader, st);
  if (c == '-' && isdigit(reader_peek_ahead(reader, 1)))
    return parse_number(reader, st);
  if (isdigit(c))
    return parse_number(reader, st);
  // Check for invalid decimal syntax like .01 (should be 0.01)
  if (c == '.' && isdigit(reader_peek_ahead(reader, 1))) {
    // Read the full invalid decimal to include in error message
    char invalid_decimal[64];
    int pos = 0;
    invalid_decimal[pos++] = c; // include the '.'
    reader_next(reader); // consume '.'
    while (isdigit(reader_peek_char(reader)) && pos < (int)sizeof(invalid_decimal) - 1) {
      invalid_decimal[pos++] = reader_next(reader);
    }
    invalid_decimal[pos] = '\0';
    
    throw_exception_formatted(EXCEPTION_TYPE_PARSE, __FILE__, __LINE__, 0, 
        "Syntax error compiling.\nUnable to resolve symbol: %s in this context", invalid_decimal);
    return NULL;
  }
  // Handle nil literal
  if (c == 'n' && reader_peek_ahead(reader, 1) == 'i' && 
      reader_peek_ahead(reader, 2) == 'l' && 
      !is_alphanumeric(reader_peek_ahead(reader, 3))) {
    reader_consume(reader); // 'n'
    reader_consume(reader); // 'i'
    reader_consume(reader); // 'l'
    return NULL;
  }
  
  // Handle true literal
  if (c == 't' && reader_peek_ahead(reader, 1) == 'r' && 
      reader_peek_ahead(reader, 2) == 'u' && 
      reader_peek_ahead(reader, 3) == 'e' && 
      !is_alphanumeric(reader_peek_ahead(reader, 4))) {
    reader_consume(reader); // 't'
    reader_consume(reader); // 'r'
    reader_consume(reader); // 'u'
    reader_consume(reader); // 'e'
    return make_special(SPECIAL_TRUE);
  }
  
  // Handle false literal
  if (c == 'f' && reader_peek_ahead(reader, 1) == 'a' && 
      reader_peek_ahead(reader, 2) == 'l' && 
      reader_peek_ahead(reader, 3) == 's' && 
      reader_peek_ahead(reader, 4) == 'e' && 
      !is_alphanumeric(reader_peek_ahead(reader, 5))) {
    reader_consume(reader); // 'f'
    reader_consume(reader); // 'a'
    reader_consume(reader); // 'l'
    reader_consume(reader); // 's'
    reader_consume(reader); // 'e'
    return make_special(SPECIAL_FALSE);
  }
  
  // Handle quote 'x => (quote x)
  if (c == '\'') {
    reader_consume(reader); // consume '
    reader_skip_all(reader);
    size_t qb_before = reader_offset(reader);
    ID quoted = parse_expr_owned(reader, st);
    size_t qb_after = reader_offset(reader);
    if (qb_after <= qb_before && !reader_eof(reader)) {
      throw_parser_exception(ERROR_INVALID_SYNTAX, reader);
      return NULL;
    }
    // Create (quote <expr>) list: (quote expr)
    // Build list using the same pattern as parse_list
    CljObject *quote_sym = intern_symbol_global("quote");
    ID quoted_val = quoted;
    if (!quoted) {
      // Check if quoted is nil
      if (reader_eof(reader)) {
        throw_parser_exception(ERROR_INVALID_SYNTAX, reader);
        return NULL;
      }
      // If quoted is NULL, it should be nil - create (quote nil)
      // nil is represented as NULL, so quoted_val stays NULL
    }
    ID elements[2] = {(CljValue)quote_sym, quoted_val};
    return make_list_from_stack((CljValue*)elements, 2);
  }
  
  if (c == ':' || is_alphanumeric(c) || c == '.' || (unsigned char)c >= 0x80)
    return parse_symbol(reader, st);
  if (strchr("+*/=<>", c)) {
    // Check if next character is also a symbol character (e.g., *ns* not just *)
    char next = reader_peek_ahead(reader, 1);
    if (next && (is_alphanumeric(next) || next == '*' || next == '+' || next == '/' || next == '=' || next == '<' || next == '>' || next == '-' || next == '_' || next == '?' || next == '!' || (unsigned char)next >= 0x80)) {
      // Multi-character symbol like *ns* or *out*
      return parse_symbol(reader, st);
    }
    // Single character operator
    reader_consume(reader);
    char buf[2] = {c, '\0'};
    return intern_symbol_global(buf);
  }
  
  // Unknown character - throw exception with helpful message
  char msg[256];
  snprintf(msg, sizeof(msg), "Unexpected character '%c' (0x%02x) at position %zu (line %d, col %d)", 
           (c >= 32 && c < 127) ? c : '?', (unsigned char)c, reader->index, reader->line, reader->column);
  throw_parser_exception(msg, reader);
  return NULL;
}

ID parse_expr(Reader *reader, EvalState *st) {
  ID result = parse_expr_owned(reader, st);
  if (result && !IS_IMMEDIATE(result)) {
    return AUTORELEASE(result);
  }
  return result;
}

/**
 * @brief Parse Clojure expression from string input
 * @param input Input string to parse
 * @param st Evaluation state
 * @return Parsed CljObject (caller must release) or NULL on error
 */


/**
 * @brief Evaluate a parsed Clojure expression
 * @param parsed_expr The parsed AST
 * @param eval_state The evaluation state
 * @return The evaluated result (autoreleased) or NULL only if result is nil
 */
CljObject* eval_parsed(CljObject *parsed_expr, EvalState *eval_state) {
    CLJ_ASSERT(parsed_expr != NULL);
    CLJ_ASSERT(eval_state != NULL);
    
    CljObject *result = NULL;
    
    // Don't catch exceptions here - let them propagate to the caller
    // Check if parsed_expr is an immediate value first
    if (IS_IMMEDIATE(parsed_expr)) {
        // For immediate values, return them as CljObject* (they're already evaluated)
        result = parsed_expr;
    } else if (is_type(parsed_expr, CLJ_LIST)) {
        CLJ_ASSERT(eval_state->current_ns != NULL);
        CljObject *env = eval_state->current_ns->mappings;
        result = eval_list(as_list(parsed_expr), (CljMap*)env, eval_state);
        // eval_list returns mixed results - don't autorelease objects we didn't create
    } else {
        // Handle other types with eval_expr_simple
        result = eval_expr_simple(parsed_expr, eval_state);
        // eval_expr_simple already returns autoreleased object
    }
    
    // result can be NULL only if the evaluation result is nil
    // If evaluation fails, it should throw an exception, not return NULL
    return result;
}

/**
 * @brief Parse and evaluate a Clojure expression from a string (convenience)
 * @param expr_str The Clojure expression as a string
 * @param eval_state The evaluation state
 * @return The evaluated result (autoreleased) or NULL only if result is nil
 */
ID eval_string(const char* expr_str, EvalState *eval_state) {
    CLJ_ASSERT(expr_str != NULL);
    CLJ_ASSERT(eval_state != NULL);
    
    CljValue parsed = parse(expr_str, eval_state);
    if (parsed == NULL) {
        // Check if this is a valid nil result vs a parsing error
        // If the input is exactly "nil", then NULL is a valid result
        if (strcmp(expr_str, "nil") == 0) {
            return NULL; // nil is represented as NULL in our system
        }
        
        // Otherwise, this is a parsing error - throw exception
        throw_exception(EXCEPTION_TYPE_PARSE, ERROR_INVALID_SYNTAX, __FILE__, __LINE__, 0);
        // Execution continues after longjmp, so we never reach here in normal flow
        // But for safety, we still return NULL (though it shouldn't be used)
        return NULL;
    }
    
    // Check if parsed is an immediate value
    if (IS_IMMEDIATE(parsed)) {
        // For immediate values, return them as CljObject* (they're already evaluated)
        return parsed;
    }
    
    // For heap objects, evaluate them
    ID result = eval_parsed(parsed, eval_state);
    
    // result can be NULL only if the evaluation result is nil
    // If eval_parsed fails, it should throw an exception, not return NULL
    return result;
}

#define MAX_VECTOR_RECURSION_DEPTH 1000

/**
 * @brief Recursively parse vector elements: parse, count, and create vector in one step
 * @param reader Reader instance
 * @param st Evaluation state
 * @param count Total element count (output parameter)
 * @param depth Current recursion depth
 * @return Vector with parsed elements, or NULL on error
 */
static ID parse_vector_elements(Reader *reader, EvalState *st, int *count, int depth) {
  if (depth > MAX_VECTOR_RECURSION_DEPTH) {
    throw_parser_exception(ERROR_STACK_OVERFLOW, reader);
    return NULL;
  }
  
  reader_skip_all(reader);
  if (reader_eof(reader) || reader_peek_char(reader) == ']') {
    *count = 0;
    return make_vector(0, false);
  }
  
  ID value = parse_expr_owned(reader, st);
  if (!value) {
    reader_skip_all(reader);
    if (reader_eof(reader) || reader_peek_char(reader) == ']') {
      *count = 0;
      return make_vector(0, false);
    }
    // If we couldn't parse an element and we're not at EOF or ']', this is an error
    throw_parser_exception(ERROR_INVALID_SYNTAX, reader);
    return NULL;
  }
  
  reader_skip_all(reader);
  int remaining_count = 0;
  ID remaining_vec = parse_vector_elements(reader, st, &remaining_count, depth + 1);
  
  if (!remaining_vec) {
    RELEASE(value);
    throw_parser_exception(ERROR_INVALID_SYNTAX, reader);
    return NULL;
  }
  
  int total_count = remaining_count + 1;
  CljValue vec = make_vector((unsigned int)total_count, false);
  if (!vec) {
    RELEASE(value);
    RELEASE(remaining_vec);
    throw_oom(CLJ_VECTOR);
    return NULL;
  }
  
  CljPersistentVector *v = as_vector((CljObject*)vec);
  if (!v) {
    RELEASE(value);
    RELEASE(remaining_vec);
    RELEASE(vec);
    return NULL;
  }
  
  // First element goes at index 0, rest follows at indices 1..remaining_count
  v->data[0] = (CljObject*)value;
  
  if (remaining_count > 0) {
    CljPersistentVector *remaining_v = as_vector((CljObject*)remaining_vec);
    for (int i = 0; i < remaining_count; i++) {
      v->data[i + 1] = remaining_v->data[i];
      RETAIN(v->data[i + 1]);
    }
    for (int i = 0; i < remaining_count; i++) {
      RELEASE(remaining_v->data[i]);
    }
    RELEASE(remaining_vec);
  }
  
  v->count = total_count;
  *count = total_count;
  
  return vec;
}

/**
 * @brief Parse vector using single recursive function: parse, count, and fill in one step
 * @param reader Reader instance
 * @param st Evaluation state
 * @return Parsed vector CljObject or NULL on error
 */
static ID parse_vector(Reader *reader, EvalState *st) {
  if (reader_match(reader, '[')) {
    reader_skip_all(reader);
    
    if (reader_peek_char(reader) == ']') {
      reader_next(reader);
      return make_vector(0, false);
    }
    
    int count = 0;
    ID vec = parse_vector_elements(reader, st, &count, 0);
    
    if (!vec) {
      throw_parser_exception(ERROR_INVALID_SYNTAX, reader);
      return NULL;
    }
    
    if (reader_peek_char(reader) != ']') {
      RELEASE(vec);
      throw_parser_exception(ERROR_INVALID_SYNTAX, reader);
      return NULL;
    }
    reader_next(reader);
    
    return vec;
  }
  // Not a vector - return NULL (parser will try other types)
  return NULL;
}

/**
 * @brief Parse map literal {k v k v} using Reader
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Parsed map CljObject or NULL on error
 */
static ID parse_map(Reader *reader, EvalState *st) {
  if (!reader_match(reader, '{'))
    return NULL; // Not a map - let other parsers try
  reader_skip_all(reader);
  ID pairs[MAX_STACK_MAP_PAIRS * 2];
  int pair_count = 0;
  while (!reader_eof(reader) && reader_peek_char(reader) != '}') {
    ID key = parse_expr_owned(reader, st);
    if (!key) {
      // Only allow NULL if it's nil
      reader_skip_all(reader);
      if (!reader_eof(reader) && reader_peek_char(reader) != '}') {
        throw_parser_exception(ERROR_INVALID_SYNTAX, reader);
        return NULL;
      }
      // If at EOF or '}', key was nil - that's OK for map keys
    }
    reader_skip_all(reader);
    ID value = parse_expr_owned(reader, st);
    if (!value) {
      // value can be nil - that's OK for map values
      // But if we're not at '}', it's an error
      reader_skip_all(reader);
      if (!reader_eof(reader) && reader_peek_char(reader) != '}') {
        throw_parser_exception(ERROR_INVALID_SYNTAX, reader);
        return NULL;
      }
    }
    reader_skip_all(reader);
    pairs[pair_count * 2] = (key);
    pairs[pair_count * 2 + 1] = (value);
    pair_count++;
  }
  if (reader_eof(reader) || !reader_match(reader, '}')) {
    throw_parser_exception(ERROR_INVALID_SYNTAX, reader);
    return NULL;
  }
  // Use constructor API (owned) and return autoreleased
  return make_map_from_stack((CljObject**)pairs, pair_count);
}

/**
 * @brief Parse list literal (a b c) using Reader
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Parsed list CljObject or NULL on error
 */
static ID parse_list(Reader *reader, EvalState *st) {
  if (!reader_match(reader, '('))
    return NULL; // Not a list - let other parsers try
  reader_skip_all(reader);
  
  // Handle empty list
  if (reader_peek_char(reader) == ')') {
    reader_next(reader);
    return NULL;
  }
  
  // Parse first element
  ID first = parse_expr_with_progress(reader, st);
  reader_skip_all(reader);
  
  // Parse rest of the list recursively
  ID rest = parse_list_rest(reader, st);
  
  // Build list from first and rest
  CljValue result = make_list(first, (CljList*)rest);
  
  if (reader_eof(reader) || !reader_match(reader, ')')) {
    RELEASE(result);
    throw_parser_exception(ERROR_INVALID_SYNTAX, reader);
    return NULL;
  }
  
  return result;
}

/**
 * @brief Parse rest of list after first element
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Parsed rest of list or NULL for empty rest
 */
static ID parse_list_rest(Reader *reader, EvalState *st) {
  reader_skip_all(reader);

  // If EOF reached before ')', this is an unclosed list
  if (reader_eof(reader)) {
    throw_parser_exception(ERROR_INVALID_SYNTAX, reader);
    return NULL;
  }

  // Check if we're at the end of the list
  if (reader_peek_char(reader) == ')') {
    return NULL; // Empty rest
  }

  // Parse next element (ensure forward progress)
  ID element = parse_expr_with_progress(reader, st);

  // If next is ')', stop recursion early
  if (reader_peek_char(reader) == ')') {
    return make_list(element, NULL);
  }

  // Parse remaining elements recursively
  ID rest = parse_list_rest(reader, st);

  // Build list node
  return make_list(element, (CljList*)rest);
}

/**
 * @brief Parse symbol literal (identifier) using Reader
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Parsed symbol CljObject or NULL on error
 */
static ID parse_symbol(Reader *reader, EvalState *st) {
  (void)st;
  char buffer[MAX_STACK_STRING_SIZE];
  int pos = 0;
  // Validate that current position starts a symbol (allow ':' for keywords)
  uint32_t start_cp = reader_peek_codepoint(reader);
  char start_ch = reader_current(reader);
  if (start_cp == READER_EOF || start_cp == READER_UTF8_ERROR || 
      !(start_ch == ':' || utf8_is_symbol_char((int)start_cp))) {
    throw_parser_exception(ERROR_INVALID_SYNTAX, reader);
    return NULL;
  }
  
  // Handle keyword prefix
  if (reader_peek_char(reader) == ':') {
    buffer[pos++] = reader_next(reader);
    if (reader_peek_char(reader) == ':')
      buffer[pos++] = reader_next(reader);
  }
  
  while (!reader_eof(reader) && pos < MAX_STACK_STRING_SIZE - 1) {
    uint32_t cp = reader_peek_codepoint(reader);
    if (cp == READER_EOF) break;
    if (cp == READER_UTF8_ERROR) {
      throw_parser_exception(ERROR_INVALID_SYNTAX, reader);
      return NULL;
    }
    
    if (utf8_is_symbol_char(cp)) {
      // Get the UTF-8 bytes for this codepoint
      const char *current = reader->src + reader->index;
      const char *next = utf8codepoint(current, NULL);
      if (!next || next <= current) {
        // Notbremse: Fortschritt sicherstellen
        CLJ_ASSERT(next && next > current);
        // Fallback: einen Byte voranschreiten, um Hänger zu vermeiden
        next = current + 1;
      }
      
      size_t bytes_to_copy = next - current;
      if (pos + bytes_to_copy >= MAX_STACK_STRING_SIZE) break;
      
      // Copy UTF-8 bytes
      for (size_t i = 0; i < bytes_to_copy; i++) {
        buffer[pos++] = current[i];
      }
      
      // Advance reader by codepoint
      size_t before = reader_offset(reader);
      reader_next_codepoint(reader);
      size_t after = reader_offset(reader);
      // Notbremse: Fortschritt garantiert
      CLJ_ASSERT(after > before);
    } else {
      break;
    }
  }
  
  buffer[pos] = '\0';
  // Leere Symbole sind ungültig – Exception werfen
  if (pos == 0) {
    if (!reader_eof(reader)) {
      // Ensure forward progress to avoid parser stalls
      reader_next_codepoint(reader);
    }
    throw_parser_exception(ERROR_INVALID_SYNTAX, reader);
    return NULL;
  }
  if (!utf8valid(buffer)) {
    throw_parser_exception(ERROR_INVALID_SYNTAX, reader);
    return NULL;
  }
  // Qualified symbol split: ns/name → intern with namespace
  if (buffer[0] != ':') {
    char *slash = strchr(buffer, '/');
    if (slash && slash != buffer && *(slash + 1) != '\0') {
      *slash = '\0';
      const char *ns_part = buffer;
      const char *name_part = slash + 1;
      return intern_symbol(ns_part, name_part);
    }
  }
  return intern_symbol_global(buffer);
}

/**
 * @brief Parse string literal "text" using Reader
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Parsed string CljObject or NULL on error
 */
static ID parse_string(Reader *reader, EvalState *st) {
  (void)st;
  if (reader_next(reader) != '"') {
    throw_parser_exception(ERROR_INVALID_SYNTAX, reader);
    return NULL;
  }
  char buf[MAX_STACK_STRING_SIZE];
  int pos = 0;
  while (!reader_eof(reader) && reader_peek_char(reader) != '"' &&
         pos < MAX_STACK_STRING_SIZE - 1) {
    char c = reader_next(reader);
    if (c == '\\') {
      c = reader_next(reader);
      switch (c) {
      case 'n':
        buf[pos++] = '\n';
        break;
      case 't':
        buf[pos++] = '\t';
        break;
      case 'r':
        buf[pos++] = '\r';
        break;
      case '\\':
        buf[pos++] = '\\';
        break;
      case '"':
        buf[pos++] = '"';
        break;
      default:
        buf[pos++] = c;
        break;
      }
    } else {
      buf[pos++] = c;
    }
  }
  // After loop, we should be at closing quote or EOF
  if (reader_eof(reader)) {
    throw_parser_exception(ERROR_INVALID_SYNTAX, reader);
    return NULL;
  }
  // Consume closing quote
  if (reader_peek_char(reader) == '"') {
    reader_next(reader); // consume closing quote
  } else {
    throw_parser_exception(ERROR_INVALID_SYNTAX, reader);
    return NULL;
  }
  buf[pos] = '\0';
  if (!utf8valid(buf)) {
    throw_parser_exception(ERROR_INVALID_SYNTAX, reader);
    return NULL;
  }
  ID result = make_string(buf);
  return result;
}

/**
 * @brief Parse number literal (integer/float) using Reader
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Parsed number CljObject or NULL on error
 */
static CljObject* parse_number(Reader *reader, EvalState *st) {
  (void)st;
  char buf[MAX_STACK_STRING_SIZE];
  int pos = 0;
  bool has_digit_before_dot = false;
  
  if (reader_peek_char(reader) == '-')
    buf[pos++] = reader_next(reader);
  if (!isdigit(reader_peek_char(reader))) {
    // Check if this is a decimal starting with '.' (invalid in Clojure)
    if (reader_peek_char(reader) == '.') {
      throw_parser_exception("Syntax error compiling at (REPL:1:1).\nUnable to resolve symbol: .01 in this context", reader);
      return NULL;
    }
    throw_parser_exception(ERROR_INVALID_SYNTAX, reader);
    return NULL;
  }
  while (isdigit(reader_peek_char(reader)) && pos < MAX_STACK_STRING_SIZE - 1) {
    buf[pos++] = reader_next(reader);
    has_digit_before_dot = true;
  }
  if (reader_peek_char(reader) == '.' &&
      isdigit(reader_peek_ahead(reader, 1))) {
    buf[pos++] = reader_next(reader);
    while (isdigit(reader_peek_char(reader)) && pos < MAX_STACK_STRING_SIZE - 1)
      buf[pos++] = reader_next(reader);
  }
  buf[pos] = '\0';
  
  // Validate: decimal numbers must have at least one digit before the dot
  if (strchr(buf, '.') && !has_digit_before_dot) {
    throw_parser_exception("Unable to resolve symbol: .01 in this context", reader);
    return NULL;
  }
  
  if (strchr(buf, '.'))
    return fixed((float)atof(buf));
  return fixnum(atoi(buf));
}

/**
 * @brief Create CljValue by parsing number from Reader (Phase 1: Immediates)
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Parsed number CljValue or NULL on error
 */

/**
 * @brief Create CljValue by parsing expression from Reader (Phase 1: Immediates)
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return New CljValue or NULL on error
 */
CljValue value_by_parsing_expr(Reader *reader, EvalState *st) {
  // Delegate to make_object_by_parsing_expr to avoid code duplication
  // Both functions do the same thing, just with different return types
  return (CljValue)parse_expr(reader, st);
}

/**
 * @brief Parse Clojure expression from Reader (CljValue API)
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Parsed CljValue or NULL on error
 */
CljValue parse_from_reader(Reader *reader, EvalState *st) {
  if (!reader || !st) {
    throw_exception(EXCEPTION_TYPE_PARSE, "Invalid arguments to parse_from_reader", __FILE__, __LINE__, 0);
    return NULL;
  }
  
  CljValue result = NULL;
  
  WITH_AUTORELEASE_POOL({
    result = value_by_parsing_expr(reader, st);
    
    if (result && !IS_IMMEDIATE(result)) {
      RETAIN(result);
    }
  });
  
  return AUTORELEASE(result);
}

/**
 * @brief Parse Clojure expression from string input (CljValue API)
 * @param input Input string to parse
 * @param st Evaluation state
 * @return Parsed CljValue or NULL on error
 */
CljValue parse(const char *input, EvalState *st) {
  if (!input || !st) {
    throw_exception(EXCEPTION_TYPE_PARSE, "Invalid arguments to parse - input or state is NULL", __FILE__, __LINE__, 0);
    return NULL;
  }
  
  Reader reader;
  reader_init(&reader, input);
  
  return parse_from_reader(&reader, st);
}


/**
 * @brief Parse metadata ^meta using Reader
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Object with applied metadata or NULL on error
 */
static ID parse_meta(Reader *reader, EvalState *st) {
  if (!reader_eof(reader) && reader_next(reader) != '^') {
    throw_parser_exception(ERROR_INVALID_SYNTAX, reader);
    return NULL;
  }
  reader_skip_all(reader);
  ID meta = parse_expr_owned(reader, st);
  if (!meta) {
    throw_parser_exception(ERROR_INVALID_SYNTAX, reader);
    return NULL;
  }
  reader_skip_all(reader);
  ID obj = parse_expr_owned(reader, st);
  if (!obj) {
    RELEASE(meta);
    throw_parser_exception(ERROR_INVALID_SYNTAX, reader);
    return NULL;
  }
  meta_set(obj, meta);
  RELEASE(meta);
  return obj;
}

/**
 * @brief Parse metadata map #^{...} using Reader
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Object with applied metadata map or NULL on error
 */
static ID parse_meta_map(Reader *reader,
                                        EvalState *st) {
  if (reader_next(reader) != '#') {
    throw_parser_exception(ERROR_INVALID_SYNTAX, reader);
    return NULL;
  }
  if (reader_next(reader) != '^') {
    throw_parser_exception(ERROR_INVALID_SYNTAX, reader);
    return NULL;
  }
  ID meta = parse_map(reader, st);
  if (!meta) {
    throw_parser_exception(ERROR_INVALID_SYNTAX, reader);
    return NULL;
  }
  reader_skip_all(reader);
  ID obj = parse_expr_owned(reader, st);
  if (!obj) {
    RELEASE(meta);
    throw_parser_exception(ERROR_INVALID_SYNTAX, reader);
    return NULL;
  }
  meta_set(obj, meta);
  RELEASE(meta);
  return obj;
}

