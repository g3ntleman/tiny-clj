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
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

// Helper function for parser exceptions
static void throw_parser_exception(const char *message, Reader *reader) {
    throw_exception("ParseError", message, "parser", reader->line, reader->column);
}

// Stack-based parser constants
#define MAX_STACK_VECTOR_SIZE 64
#define MAX_STACK_MAP_PAIRS 32
#define MAX_STACK_LIST_SIZE 64
#define MAX_STACK_STRING_SIZE 256

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
static ID parse_string_internal(Reader *reader, EvalState *st);
static ID parse_symbol(Reader *reader, EvalState *st);
static CljObject* make_number_by_parsing(Reader *reader, EvalState *st);
// static CljObject* make_number_by_parsing_old(Reader *reader, EvalState *st); // Removed unused function

/**
 * @brief Create CljObject by parsing expression from Reader
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return New CljObject with RC=1 or NULL on error (caller must release)
 */
ID make_object_by_parsing_expr(Reader *reader, EvalState *st) {
  reader_skip_all(reader);
  if (reader_is_eof(reader))
    return NULL;
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
    return parse_string_internal(reader, st);
  if (c == '-' && isdigit(reader_peek_ahead(reader, 1)))
    return make_number_by_parsing(reader, st);
  if (isdigit(c))
    return make_number_by_parsing(reader, st);
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
    
    throw_exception_formatted("ParseError", __FILE__, __LINE__, 0, 
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
    ID quoted = make_object_by_parsing_expr(reader, st);
    if (!quoted) return NULL;
    // Create (quote <expr>) list: (quote expr)
    // Build list using the same pattern as parse_list
    CljObject *quote_sym = intern_symbol_global("quote");
    ID elements[2] = {(CljValue)quote_sym, quoted};
    return make_list_from_stack((CljValue*)elements, 2);
  }
  
  if (c == ':' || is_alphanumeric(c) || (unsigned char)c >= 0x80)
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
        // eval_list already returns autoreleased object
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
        
        // Otherwise, this is a parsing error
        throw_exception("ParseError", "Failed to parse expression", __FILE__, __LINE__, 0);
        return NULL;
    }
    
    // Check if parsed is an immediate value
    if (IS_IMMEDIATE(parsed)) {
        // For immediate values, return them as CljObject* (they're already evaluated)
        return parsed;
    }
    
    // For heap objects, evaluate them
    CljObject *result = eval_parsed(parsed, eval_state);
    
    // result can be NULL only if the evaluation result is nil
    // If eval_parsed fails, it should throw an exception, not return NULL
    return (ID)result;
}

/**
 * @brief Parse vector literal [a b c] using Reader
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Parsed vector CljObject or NULL on error
 */
static ID parse_vector(Reader *reader, EvalState *st) {
  if (reader_match(reader, '[')) {
    reader_skip_all(reader);
    ID stack[MAX_STACK_VECTOR_SIZE];
    int count = 0;
    while (!reader_eof(reader) && reader_peek_char(reader) != ']') {
      if (count >= MAX_STACK_VECTOR_SIZE)
        return NULL;
      ID value = make_object_by_parsing_expr(reader, st);
      if (!value)
        return NULL;
      stack[count++] = value;
      reader_skip_all(reader);
    }
  if (reader_eof(reader) || !reader_match(reader, ']')) {
    throw_parser_exception("Unclosed vector - missing closing ']'", reader);
    return NULL;
  }
    // Create mutable vector for efficient parsing with expected capacity
    unsigned int expected_capacity = count > 0 ? count : 5;  // Default capacity for empty vectors
    CljValue vec = make_vector(expected_capacity, true);  // mutable = true
    CljPersistentVector *v = as_vector((CljObject*)vec);
    
    // Add elements directly to mutable vector (O(1) per element)
    for (int i = 0; i < count; i++) {
      v->data[i] = (CljObject*)stack[i];
      RETAIN(stack[i]);  // Retain the element
    }
    v->count = count;
    
    // Convert to immutable at the end
    v->mutable_flag = false;
    
    return AUTORELEASE(vec);
  }
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
    return NULL;
  reader_skip_all(reader);
  ID pairs[MAX_STACK_MAP_PAIRS * 2];
  int pair_count = 0;
  while (!reader_eof(reader) && reader_peek_char(reader) != '}') {
    ID key = make_object_by_parsing_expr(reader, st);
    if (!key)
      return NULL;
    reader_skip_all(reader);
    ID value = make_object_by_parsing_expr(reader, st);
    if (!value)
      return NULL;
    reader_skip_all(reader);
    pairs[pair_count * 2] = (key);
    pairs[pair_count * 2 + 1] = (value);
    pair_count++;
  }
  if (reader_eof(reader) || !reader_match(reader, '}')) {
    throw_parser_exception("Unclosed map - missing closing '}'", reader);
    return NULL;
  }
  // Use the new API for CljValue map parsing
  return map_from_stack((CljObject**)pairs, pair_count);
}

/**
 * @brief Parse list literal (a b c) using Reader
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Parsed list CljObject or NULL on error
 */
static ID parse_list(Reader *reader, EvalState *st) {
  if (!reader_match(reader, '('))
    return NULL;
  reader_skip_all(reader);
  
  // Handle empty list
  if (reader_peek_char(reader) == ')') {
    reader_next(reader);
    return NULL;
  }
  
  // Parse elements iteratively and build list from start to end
  ID elements[MAX_STACK_LIST_SIZE];
  int count = 0;
  
  while (!reader_eof(reader) && reader_peek_char(reader) != ')') {
    ID element = make_object_by_parsing_expr(reader, st);
    // element can be NULL for nil, which is valid
    reader_skip_all(reader);
    
    // Store element in array (including NULL for nil)
    if (count >= MAX_STACK_LIST_SIZE) {
      // Clean up already parsed elements
      for (int i = 0; i < count; i++) {
        RELEASE(elements[i]);
      }
      if (element) RELEASE(element);
      return NULL;
    }
    elements[count++] = element;
  }
  
  // Build list from array in correct order
  CljValue result = AUTORELEASE(make_list_from_stack((CljValue*)elements, count));
  
  if (reader_eof(reader) || !reader_match(reader, ')')) {
    RELEASE(result);
    throw_parser_exception("Unclosed list - missing closing ')'", reader);
    return NULL;
  }
  
  return result;
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
  
  // Handle keyword prefix
  if (reader_peek_char(reader) == ':') {
    buffer[pos++] = reader_next(reader);
    if (reader_peek_char(reader) == ':')
      buffer[pos++] = reader_next(reader);
  }
  
  while (!reader_eof(reader) && pos < MAX_STACK_STRING_SIZE - 1) {
    int cp = reader_peek_codepoint(reader);
    if (cp < 0) break;
    
    if (utf8_is_symbol_char(cp)) {
      // Get the UTF-8 bytes for this codepoint
      const char *current = reader->src + reader->index;
      const char *next = utf8codepoint(current, NULL);
      if (!next) break;
      
      size_t bytes_to_copy = next - current;
      if (pos + bytes_to_copy >= MAX_STACK_STRING_SIZE) break;
      
      // Copy UTF-8 bytes
      for (size_t i = 0; i < bytes_to_copy; i++) {
        buffer[pos++] = current[i];
      }
      
      // Advance reader by codepoint
      reader_next_codepoint(reader);
    } else {
      break;
    }
  }
  
  buffer[pos] = '\0';
  if (!utf8valid(buffer))
    return NULL;
  return AUTORELEASE(intern_symbol_global(buffer));
}

/**
 * @brief Parse string literal "text" using Reader
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Parsed string CljObject or NULL on error
 */
static ID parse_string_internal(Reader *reader, EvalState *st) {
  (void)st;
  if (reader_next(reader) != '"')
    return NULL;
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
  if (reader_eof(reader) || reader_next(reader) != '"') {
    throw_parser_exception("Unclosed string - missing closing '\"'", reader);
    return NULL;
  }
  buf[pos] = '\0';
  if (!utf8valid(buf))
    return NULL;
  ID result = AUTORELEASE(make_string_impl(buf));
  return result;
}

/**
 * @brief Parse number literal (integer/float) using Reader
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Parsed number CljObject or NULL on error
 */
static CljObject* make_number_by_parsing(Reader *reader, EvalState *st) {
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
// Removed unused function make_number_by_parsing_v

/**
 * @brief Create CljValue by parsing expression from Reader (Phase 1: Immediates)
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return New CljValue or NULL on error
 */
CljValue value_by_parsing_expr(Reader *reader, EvalState *st) {
  // Delegate to make_object_by_parsing_expr to avoid code duplication
  // Both functions do the same thing, just with different return types
  return (CljValue)make_object_by_parsing_expr(reader, st);
}

/**
 * @brief Parse Clojure expression from Reader (CljValue API)
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Parsed CljValue or NULL on error
 */
CljValue parse_from_reader(Reader *reader, EvalState *st) {
  if (!reader || !st) return NULL;
  
  CljValue result = NULL;
  
  // Create autorelease pool for parse operations
  WITH_AUTORELEASE_POOL({
    // Don't catch exceptions - let them propagate
    result = value_by_parsing_expr(reader, st);
    
    // Result is already autoreleased by the pool
  });
  
  return result;
}

/**
 * @brief Parse Clojure expression from string input (CljValue API)
 * @param input Input string to parse
 * @param st Evaluation state
 * @return Parsed CljValue or NULL on error
 */
CljValue parse(const char *input, EvalState *st) {
  if (!input || !st) return NULL;
  
  CljValue result = NULL;
  
  // Create autorelease pool for parse operations
  WITH_AUTORELEASE_POOL({
    Reader reader;
    reader_init(&reader, input);
    
    // Delegate to parse_from_reader (DRY principle)
    result = parse_from_reader(&reader, st);
    
    // Result is already autoreleased by the pool
  });
  
  return result;
}


/**
 * @brief Parse metadata ^meta using Reader
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Object with applied metadata or NULL on error
 */
static ID parse_meta(Reader *reader, EvalState *st) {
  if (!reader_eof(reader) && reader_next(reader) != '^')
    return NULL;
  reader_skip_all(reader);
  ID meta = make_object_by_parsing_expr(reader, st);
  if (!meta)
    return NULL;
  reader_skip_all(reader);
  ID obj = make_object_by_parsing_expr(reader, st);
  if (!obj) {
    RELEASE(meta);
    return NULL;
  }
  meta_set(obj, meta);
  RELEASE(meta);
  return AUTORELEASE(obj);
}

/**
 * @brief Parse metadata map #^{...} using Reader
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Object with applied metadata map or NULL on error
 */
static ID parse_meta_map(Reader *reader,
                                        EvalState *st) {
  if (reader_next(reader) != '#')
    return NULL;
  if (reader_next(reader) != '^')
    return NULL;
  ID meta = parse_map(reader, st);
  if (!meta)
    return NULL;
  reader_skip_all(reader);
  ID obj = make_object_by_parsing_expr(reader, st);
  if (!obj) {
    RELEASE(meta);
    return NULL;
  }
  meta_set(obj, meta);
  RELEASE(meta);
  return AUTORELEASE(obj);
}

