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

#include "clj_parser.h"
#include "map.h"
#include "list_operations.h"
#include "runtime.h"
#include "utf8.h"
#include "vector.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Stack-based parser constants
#define MAX_STACK_VECTOR_SIZE 64
#define MAX_STACK_MAP_PAIRS 32
#define MAX_STACK_LIST_SIZE 64
#define MAX_STACK_STRING_SIZE 256

// Global jump buffer for parse errors

// Error handling - using namespace.h version




/** @brief Check if character is a digit */
static int is_digit(char c) { return c >= '0' && c <= '9'; }

/** @brief Check if character is alphabetic */
static int is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/** @brief Check if character is alphanumeric or symbol character */
static int is_alphanumeric(char c) {
  return is_alpha(c) || is_digit(c) || c == '-' || c == '_' || c == '?' ||
         c == '!' || c == '/';
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
static CljObject *parse_meta(Reader *reader, EvalState *st);
static CljObject *parse_meta_map(Reader *reader, EvalState *st);
static CljObject *parse_vector(Reader *reader, EvalState *st);
static CljObject *parse_map(Reader *reader, EvalState *st);
static CljObject *parse_list(Reader *reader, EvalState *st);
static CljObject *parse_string(Reader *reader, EvalState *st);
static CljObject *parse_symbol(Reader *reader, EvalState *st);
static CljObject *parse_number(Reader *reader, EvalState *st);

/**
 * @brief Internal expression parser using Reader
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Parsed CljObject or NULL on error
 */
static CljObject *parse_expr_internal(Reader *reader, EvalState *st) {
  printf("DEBUG: Before skip_all - index=%zu, char='%c' (0x%02x)\n", 
         reader->index, reader_peek(reader), (unsigned char)reader_peek(reader));
  reader_skip_all(reader);
  printf("DEBUG: After skip_all - index=%zu, char='%c' (0x%02x)\n", 
         reader->index, reader_peek(reader), (unsigned char)reader_peek(reader));
  if (reader_is_eof(reader))
    return NULL;
  char c = reader_current(reader);
  printf("DEBUG: parse_expr_internal got char: '%c' (0x%02x)\n", c, (unsigned char)c);
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
  if (c == '-' || isdigit(c))
    return parse_number(reader, st);
  if (c == ':' || is_alphanumeric(c) || (unsigned char)c >= 0x80)
    return parse_symbol(reader, st);
  if (strchr("+*/=<>", c)) {
    reader_consume(reader);
    char buf[2] = {c, '\0'};
    return (CljObject *)intern_symbol_global(buf);
  }
  return NULL;
}

/**
 * @brief Parse Clojure expression from string input
 * @param input Input string to parse
 * @param st Evaluation state
 * @return Parsed CljObject or NULL on error
 */
CljObject *parse(const char *input, EvalState *st) {
  if (!input)
    return NULL;
  Reader reader;
  reader_init(&reader, input);
  return parse_expr_internal(&reader, st);
}

/**
 * @brief Parse vector literal [a b c] using Reader
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Parsed vector CljObject or NULL on error
 */
static CljObject *parse_vector(Reader *reader, EvalState *st) {
  if (reader_match(reader, '[')) {
    reader_skip_all(reader);
    CljObject *stack[MAX_STACK_VECTOR_SIZE];
    int count = 0;
    while (!reader_eof(reader) && reader_peek_char(reader) != ']') {
      if (count >= MAX_STACK_VECTOR_SIZE)
        return NULL;
      CljObject *value = parse_expr_internal(reader, st);
      if (!value)
        return NULL;
      stack[count++] = value;
      reader_skip_all(reader);
    }
    if (reader_eof(reader) || !reader_match(reader, ']'))
      return NULL;
    return vector_from_stack(stack, count);
  }
  return NULL;
}

/**
 * @brief Parse map literal {k v k v} using Reader
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Parsed map CljObject or NULL on error
 */
static CljObject *parse_map(Reader *reader, EvalState *st) {
  if (!reader_match(reader, '{'))
    return NULL;
  reader_skip_all(reader);
  CljObject *pairs[MAX_STACK_MAP_PAIRS * 2];
  int pair_count = 0;
  while (!reader_eof(reader) && reader_peek_char(reader) != '}') {
    CljObject *key = parse_expr_internal(reader, st);
    if (!key)
      return NULL;
    reader_skip_all(reader);
    CljObject *value = parse_expr_internal(reader, st);
    if (!value)
      return NULL;
    reader_skip_all(reader);
    pairs[pair_count * 2] = key;
    pairs[pair_count * 2 + 1] = value;
    pair_count++;
  }
  if (reader_eof(reader) || !reader_match(reader, '}'))
    return NULL;
  return map_from_stack(pairs, pair_count);
}

/**
 * @brief Parse list literal (a b c) using Reader
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Parsed list CljObject or NULL on error
 */
static CljObject *parse_list(Reader *reader, EvalState *st) {
  if (!reader_match(reader, '('))
    return NULL;
  reader_skip_all(reader);
  CljObject *stack[MAX_STACK_LIST_SIZE];
  int count = 0;
  while (!reader_eof(reader) && reader_peek_char(reader) != ')') {
    CljObject *expr = parse_expr_internal(reader, st);
    if (!expr)
      return NULL;
    stack[count++] = expr;
    reader_skip_all(reader);
  }
  if (reader_eof(reader) || !reader_match(reader, ')'))
    return NULL;
  return list_from_stack(stack, count);
}

/**
 * @brief Parse symbol literal (identifier) using Reader
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Parsed symbol CljObject or NULL on error
 */
static CljObject *parse_symbol(Reader *reader, EvalState *st) {
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
  return (CljObject *)intern_symbol_global(buffer);
}

/**
 * @brief Parse string literal "text" using Reader
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Parsed string CljObject or NULL on error
 */
static CljObject *parse_string(Reader *reader, EvalState *st) {
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
  if (reader_eof(reader) || reader_next(reader) != '"')
    return NULL;
  buf[pos] = '\0';
  if (!utf8valid(buf))
    return NULL;
  return make_string(buf);
}

/**
 * @brief Parse number literal (integer/float) using Reader
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Parsed number CljObject or NULL on error
 */
static CljObject *parse_number(Reader *reader, EvalState *st) {
  (void)st;
  char buf[MAX_STACK_STRING_SIZE];
  int pos = 0;
  if (reader_peek_char(reader) == '-')
    buf[pos++] = reader_next(reader);
  if (!isdigit(reader_peek_char(reader)))
    return NULL;
  while (isdigit(reader_peek_char(reader)) && pos < MAX_STACK_STRING_SIZE - 1)
    buf[pos++] = reader_next(reader);
  if (reader_peek_char(reader) == '.' &&
      isdigit(reader_peek_ahead(reader, 1))) {
    buf[pos++] = reader_next(reader);
    while (isdigit(reader_peek_char(reader)) && pos < MAX_STACK_STRING_SIZE - 1)
      buf[pos++] = reader_next(reader);
  }
  buf[pos] = '\0';
  if (strchr(buf, '.'))
    return make_float(atof(buf));
  return make_int(atoi(buf));
}

/**
 * @brief Parse metadata ^meta using Reader
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Object with applied metadata or NULL on error
 */
static CljObject *parse_meta(Reader *reader, EvalState *st) {
  if (!reader_eof(reader) && reader_next(reader) != '^')
    return NULL;
  reader_skip_all(reader);
  CljObject *meta = parse_expr_internal(reader, st);
  if (!meta)
    return NULL;
  reader_skip_all(reader);
  CljObject *obj = parse_expr_internal(reader, st);
  if (!obj) {
    release(meta);
    return NULL;
  }
  meta_set(obj, meta);
  release(meta);
  return obj;
}

/**
 * @brief Parse metadata map #^{...} using Reader
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Object with applied metadata map or NULL on error
 */
static CljObject *parse_meta_map(Reader *reader,
                                        EvalState *st) {
  if (reader_next(reader) != '#')
    return NULL;
  if (reader_next(reader) != '^')
    return NULL;
  CljObject *meta = parse_map(reader, st);
  if (!meta)
    return NULL;
  reader_skip_all(reader);
  CljObject *obj = parse_expr_internal(reader, st);
  if (!obj) {
    release(meta);
    return NULL;
  }
  meta_set(obj, meta);
  release(meta);
  return obj;
}

