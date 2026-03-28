/*
 * Clojure Parser Implementation
 *
 * Features:
 * - Parses Clojure-like syntax (lists, vectors, maps, symbols, keywords,
 * numbers, strings)
 * - Supports meta-data parsing (^metadata, #^{...}, (with-meta obj meta))
 * - Handles comments (line comments ; and block comments #| ... |#)
 * - Stack-allocated parsing for memory efficiency
 * - Embedded-friendly implementation
 */

#include "parser.h"
#include "eval.h"
#include "list.h"
#include "ast.h"
#include "vector.h"
#include <string.h>
#ifdef PROFILE_STARTUP
#include <time.h>
#endif
#include "map.h"
#include "hashset.h"
#include <stdbool.h>
#include "memory.h"
#include "utf8.h"
#include "value.h"
#include "symbol.h"
#include "meta.h"
#include "strings.h"
#include "ast_canon.h"
#include "instant.h"
#include "uuid.h"
#include "platform.h"
#include <ctype.h>
#include "format_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <limits.h>

static ID g_parser_sym_fn = NULL;
static ID g_parser_sym_percent = NULL;
static const IdSymbolCacheEntry g_parser_symbol_cache[] = {
    {&g_parser_sym_fn, "fn"},
    {&g_parser_sym_percent, "%"},
};

static inline bool parser_symbols_ready(void) {
  return id_symbol_cache_init_global(g_parser_symbol_cache,
                                     sizeof(g_parser_symbol_cache) / sizeof(g_parser_symbol_cache[0]));
}

static size_t format_append_hex_byte(char *dest, size_t offset, size_t capacity, unsigned char value) {
  static const char digits[] = "0123456789abcdef";
  offset = format_append_char(dest, offset, capacity, digits[(value >> 4) & 0x0F]);
  offset = format_append_char(dest, offset, capacity, digits[value & 0x0F]);
  return offset;
}

static bool parser_debug_heap_enabled(void) {
  static int enabled = -1;
  if (enabled == -1) {
    const char *v = getenv("TINYCLJ_DEBUG_REQUIRE_HEAP");
    if (v && v[0] && strcmp(v, "0") != 0) {
      enabled = 1;
    } else {
#ifdef ESP_PLATFORM
      enabled = 1;
#else
      enabled = 0;
#endif
    }
  }
  return enabled != 0;
}

// Helper function for parser exceptions
static void throw_parser_exception(const char *message, Reader *reader) {
  if (reader && reader->src) {
    enum { PREVIEW_LEN = 64 };
    size_t start = reader->index;
    if (start > reader->length) {
      start = reader->length;
    }
    char preview[PREVIEW_LEN + 1];
    size_t copied = 0;
    for (size_t i = 0; i < PREVIEW_LEN && (start + i) < reader->length; i++) {
      char c = reader->src[start + i];
      if (c == '\n' || c == '\r') {
        break;
      }
      if ((unsigned char)c < 32 || (unsigned char)c == 127) {
        c = '?';
      }
      preview[copied++] = c;
    }
    preview[copied] = '\0';
    const char *source_name = reader_get_source_name(reader);
    if (!source_name || !source_name[0]) {
      source_name = "parser";
    }
    throw_exception_formatted(EXCEPTION_PARSE, source_name, reader->line, reader->column,
                              "%s (line %d, column %d, near \"%s\")",
                              message, reader->line, reader->column, preview);
  } else {
    throw_exception(EXCEPTION_PARSE, message, "parser", reader ? reader->line : 0, reader ? reader->column : 0);
  }
}

static void throw_parser_exceptionf(Reader *reader, const char *format, ...) {
#if defined(STRING_FORMATTING_ENABLED) && !STRING_FORMATTING_ENABLED
  // Embedded size build: avoid pulling in printf/vsnprintf formatting code.
  // Keep the API but sacrifice formatted messages.
  (void)reader;
  throw_parser_exception((format != NULL) ? format : "Parse error", reader);
#else
  enum { MSG_LEN = 128 };
  char buffer[MSG_LEN];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  throw_parser_exception(buffer, reader);
#endif
}

// Parser buffer constants (keep stack use bounded on embedded targets).
#define PARSER_SYMBOL_BUF 128 /* symbols limited by SYMBOL_NAME_MAX_LEN (64) */
#define PARSER_NUMBER_BUF 128 /* number literals are short */
#define MAX_STACK_STRING_SIZE 2048

/** @brief Check if character is a digit */
static bool is_digit(char c) { return c >= '0' && c <= '9'; }

/** @brief Check if character is alphabetic */
static bool is_alpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/** @brief Check if character is alphanumeric or symbol character */
static bool is_alphanumeric(char c) {
  return is_alpha(c) || is_digit(c) || c == '-' || c == '_' || c == '?' || c == '&' ||
         c == '!' || c == '/' || c == '.';
}

// Forward declarations for Reader-based parser functions

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
#if defined(META_ENABLED) && META_ENABLED
static ID apply_metadata_to_object(Reader *reader, EvalState *st, ID meta, ID obj);
#endif
static ID parse_anon_fn(Reader *reader, EvalState *st);
static ID parse_vector(Reader *reader, EvalState *st);
static ID parse_vector_with_stack(Reader *reader, EvalState *st, CljTransientVector *stack);
static ID parse_map(Reader *reader, EvalState *st);
static ID parse_map_with_stack(Reader *reader, EvalState *st, CljTransientVector *stack);
static ID parse_set(Reader *reader, EvalState *st);
static ID parse_set_with_stack(Reader *reader, EvalState *st, CljTransientVector *stack);
static ID parse_list(Reader *reader, EvalState *st);
static ID parse_expr_with_stack(Reader *reader, EvalState *st, CljTransientVector *stack);
static ID parse_list_rest(Reader *reader, EvalState *st, int open_line, int open_column,
                          CljTransientVector *stack);
static ID parse_string_internal(Reader *reader, EvalState *st);
static ID parse_symbol(Reader *reader, EvalState *st);
static bool skip_form_no_alloc(Reader *reader);

static ID parse_tagged_literal(Reader *reader, EvalState *st) {
  // Parse: #<tag> <value>
  reader_consume(reader); // '#'
  reader_skip_all(reader);

  ID tag_obj = parse_symbol(reader, st);
  if (!tag_obj || TAG(tag_obj) != CLJ_SYMBOL) {
    throw_parser_exception("Invalid tagged literal: expected symbol tag", reader);
  }
  const char *tag = as_symbol((CljValue)tag_obj)->cname;
  if (!tag) {
    throw_parser_exception("Invalid tagged literal: missing tag name", reader);
  }

  reader_skip_all(reader);
  ID val = parse_expr(reader, st);
  if (!val) {
    throw_parser_exception("Invalid tagged literal: missing value", reader);
  }

  if (strcmp(tag, "uuid") == 0) {
    if (TAG(val) != CLJ_STRING) {
      throw_parser_exception("#uuid expects a string", reader);
    }
    const char *s = clj_string_data(as_clj_string((CljValue)val));
    ID u = AUTORELEASE(clj_uuid_from_string(s));
    if (!u) {
      throw_parser_exception("Invalid #uuid string", reader);
    }
    return u;
  }

  if (strcmp(tag, "inst") == 0) {
    if (TAG(val) != CLJ_STRING) {
      throw_parser_exception("#inst expects a string", reader);
    }
    const char *s = clj_string_data(as_clj_string((CljValue)val));
    const char *err = NULL;
    ID inst = make_instant_from_iso8601_utc_string(s, &err);
    if (!inst) {
      throw_parser_exception(err ? err : "Invalid #inst string", reader);
      return NULL;
    }
    return AUTORELEASE(inst);
  }

  throw_parser_exception("Unknown tagged literal", reader);
  return NULL;
}
static ID parse_character(Reader *reader, EvalState *st);
static CljObject *make_number_by_parsing(Reader *reader, EvalState *st);

// Ensure that every parse step advances the reader or hits EOF, otherwise throw
static ID parse_expr_with_progress(Reader *reader, EvalState *st, CljTransientVector *stack) {
  size_t before = reader_offset(reader);
  ID val = stack ? parse_expr_with_stack(reader, st, stack) : parse_expr(reader, st);
  size_t after = reader_offset(reader);
  if (after <= before && !reader_eof(reader)) {
    throw_parser_exception("Parser made no progress while reading expression", reader);
  }
  return val;
}

// Skip one form without constructing runtime objects (used when metadata is disabled).
static bool skip_string_no_alloc(Reader *reader) {
  if (!reader || reader_current(reader) != '"')
    return false;
  reader_next(reader); // opening quote
  while (!reader_eof(reader)) {
    char c = reader_next(reader);
    if (c == '\\' && !reader_eof(reader)) {
      reader_next(reader); // escaped byte
      continue;
    }
    if (c == '"')
      return true;
  }
  return false;
}

static bool skip_token_no_alloc(Reader *reader) {
  if (!reader)
    return false;
  while (!reader_eof(reader)) {
    char c = reader_current(reader);
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',' ||
        c == ';' || c == '(' || c == ')' || c == '[' || c == ']' ||
        c == '{' || c == '}' || c == '"') {
      break;
    }
    int32_t cp = reader_next_codepoint(reader);
    if (cp < 0) {
      reader_next(reader); // forward progress fallback for invalid UTF-8
    }
  }
  return true;
}

static bool skip_delimited_form_no_alloc(Reader *reader, char open, char close) {
  if (!reader || reader_current(reader) != open)
    return false;
  reader_next(reader); // consume opener
  for (;;) {
    reader_skip_all(reader);
    if (reader_eof(reader))
      return false;
    if (reader_current(reader) == close) {
      reader_next(reader);
      return true;
    }
    if (!skip_form_no_alloc(reader))
      return false;
  }
}

static bool skip_dispatch_form_no_alloc(Reader *reader) {
  if (!reader || reader_current(reader) != '#')
    return false;
  reader_next(reader); // consume '#'
  if (reader_eof(reader))
    return false;

  char c = reader_current(reader);
  switch (c) {
  case '|':
    // Block comment reader macro (#| ... |#): consume comment, then skip next real form.
    if (!reader_skip_block_comment(reader))
      return false;
    return skip_form_no_alloc(reader);
  case '{':
    return skip_delimited_form_no_alloc(reader, '{', '}');
  case '(':
    return skip_delimited_form_no_alloc(reader, '(', ')');
  case '"':
    return skip_string_no_alloc(reader); // regex literal #"..."
  case '^':
    // Metadata reader macro #^meta obj: skip meta and target.
    reader_next(reader); // consume '^'
    if (!skip_form_no_alloc(reader))
      return false;
    return skip_form_no_alloc(reader);
  case '_':
    // Discard next form.
    reader_next(reader); // consume '_'
    return skip_form_no_alloc(reader);
  case ':':
    // Namespaced map literal #:ns {...} / #::{...}
    reader_next(reader); // first ':'
    if (!reader_eof(reader) && reader_current(reader) == ':') {
      reader_next(reader); // optional second ':'
    }
    (void)skip_token_no_alloc(reader); // optional namespace token
    return skip_form_no_alloc(reader); // usually a map form
  default:
    // Tagged literal: #inst "...", #uuid "...", #foo/bar value
    (void)skip_token_no_alloc(reader);
    return skip_form_no_alloc(reader);
  }
}

static bool skip_form_no_alloc(Reader *reader) {
  if (!reader)
    return false;
  reader_skip_all(reader);
  if (reader_eof(reader))
    return false;

  char c = reader_current(reader);
  switch (c) {
  case '"':
    return skip_string_no_alloc(reader);
  case '(':
    return skip_delimited_form_no_alloc(reader, '(', ')');
  case '[':
    return skip_delimited_form_no_alloc(reader, '[', ']');
  case '{':
    return skip_delimited_form_no_alloc(reader, '{', '}');
  case '\'':
  case '`':
  case '@':
    reader_next(reader); // quote/syntax-quote/deref prefix
    return skip_form_no_alloc(reader);
  case '~':
    reader_next(reader); // unquote prefix
    if (!reader_eof(reader) && reader_current(reader) == '@') {
      reader_next(reader); // unquote-splicing
    }
    return skip_form_no_alloc(reader);
  case '^':
    reader_next(reader); // metadata prefix
    if (!skip_form_no_alloc(reader))
      return false;                    // metadata payload
    return skip_form_no_alloc(reader); // target object
  case '#':
    return skip_dispatch_form_no_alloc(reader);
  default:
    return skip_token_no_alloc(reader);
  }
}

#if !(defined(META_ENABLED) && META_ENABLED)
static ID parse_after_skipping_meta_payload(Reader *reader, EvalState *st) {
  if (!skip_form_no_alloc(reader)) {
    return NULL;
  }
  reader_skip_all(reader);
  return parse_expr(reader, st);
}
#endif

/**
 * @brief Create CljObject by parsing expression from Reader
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Autoreleased object or NULL (nil) - throws exception on error (no manual RELEASE needed)
 */
ID parse_expr(Reader *reader, EvalState *st) {
  reader_skip_all(reader);
  if (reader_is_eof(reader))
    return NULL;
  char c = reader_current(reader);

  switch (c) {
  case '^':
    return parse_meta(reader, st);

  case '#': {
    char next = reader_peek_ahead(reader, 1);
    if (next == '^')
      return parse_meta_map(reader, st);
    if (next == '(')
      return parse_anon_fn(reader, st);
    if (next == '{')
      return parse_set(reader, st);
    return parse_tagged_literal(reader, st);
  }

  case '[':
    return parse_vector(reader, st);

  case '{':
    return parse_map(reader, st);

  case '(':
    return parse_list(reader, st);

  case '"':
    return parse_string_internal(reader, st);

  case '-':
    if (isdigit((unsigned char)reader_peek_ahead(reader, 1)))
      return make_number_by_parsing(reader, st);
    break;

  case '.':
    if (isdigit((unsigned char)reader_peek_ahead(reader, 1))) {
      // Check for invalid decimal syntax like .01 (should be 0.01)
      char invalid_decimal[32];
      int pos = 0;
      invalid_decimal[pos++] = c; // include the '.'
      reader_next(reader);        // consume '.'
      while (isdigit((unsigned char)reader_peek_char(reader)) && pos < (int)sizeof(invalid_decimal) - 1) {
        invalid_decimal[pos++] = reader_next(reader);
      }
      invalid_decimal[pos] = '\0';

      throw_parser_exceptionf(reader,
                              "Syntax error compiling.\nUnable to resolve symbol: %s in this context",
                              invalid_decimal);
      return NULL;
    }
    break;

  case 'n':
    // Handle nil literal - parse as SYM_NIL symbol (not NULL)
    if (reader_peek_ahead(reader, 1) == 'i' &&
        reader_peek_ahead(reader, 2) == 'l' &&
        !is_alphanumeric(reader_peek_ahead(reader, 3))) {
      reader_consume(reader); // 'n'
      reader_consume(reader); // 'i'
      reader_consume(reader); // 'l'
      return SYM_NIL;
    }
    break;

  case 't':
    // Handle true literal
    if (reader_peek_ahead(reader, 1) == 'r' &&
        reader_peek_ahead(reader, 2) == 'u' &&
        reader_peek_ahead(reader, 3) == 'e' &&
        !is_alphanumeric(reader_peek_ahead(reader, 4))) {
      reader_consume(reader); // 't'
      reader_consume(reader); // 'r'
      reader_consume(reader); // 'u'
      reader_consume(reader); // 'e'
      return clj_true;
    }
    break;

  case 'f':
    // Handle false literal
    if (reader_peek_ahead(reader, 1) == 'a' &&
        reader_peek_ahead(reader, 2) == 'l' &&
        reader_peek_ahead(reader, 3) == 's' &&
        reader_peek_ahead(reader, 4) == 'e' &&
        !is_alphanumeric(reader_peek_ahead(reader, 5))) {
      reader_consume(reader); // 'f'
      reader_consume(reader); // 'a'
      reader_consume(reader); // 'l'
      reader_consume(reader); // 's'
      reader_consume(reader); // 'e'
      return clj_false;
    }
    break;

  case '\'':
    // Handle quote 'x => (quote x)
    reader_consume(reader); // consume '
    reader_skip_all(reader);
    size_t qb_before = reader_offset(reader);
    ID quoted = parse_expr(reader, st);
    size_t qb_after = reader_offset(reader);
    if (qb_after <= qb_before && !reader_eof(reader)) {
      throw_parser_exception("Parser made no progress after quote", reader);
    }
    if (!quoted)
      return NULL;
    // Create (quote <expr>) list: (quote expr)
    return AUTORELEASE(make_ast_list(SYM_QUOTE, AUTORELEASE(make_ast_list(quoted, NULL))));

  case '@':
    // Handle deref @x => (deref x)
    reader_consume(reader); // consume @
    reader_skip_all(reader);
    size_t db_before = reader_offset(reader);
    ID atom_expr = parse_expr(reader, st);
    size_t db_after = reader_offset(reader);
    if (db_after <= db_before && !reader_eof(reader)) {
      throw_parser_exception("Parser made no progress after @", reader);
    }
    if (!atom_expr)
      return NULL;
    // Create (deref <expr>) list: (deref expr)
    return AUTORELEASE(make_ast_list(SYM_DEREF, AUTORELEASE(make_ast_list(atom_expr, NULL))));

  case '`':
    // Handle quasiquote `x => (quasiquote x)
    reader_consume(reader); // consume `
    reader_skip_all(reader);
    size_t qq_before = reader_offset(reader);
    ID qq_expr = parse_expr(reader, st);
    size_t qq_after = reader_offset(reader);
    if (qq_after <= qq_before && !reader_eof(reader)) {
      throw_parser_exception("Parser made no progress after quasiquote", reader);
    }
    if (!qq_expr)
      return NULL;
    // Create (quasiquote <expr>) list
    return AUTORELEASE(make_ast_list(SYM_QUASIQUOTE, AUTORELEASE(make_ast_list(qq_expr, NULL))));

  case '~':
    // Handle unquote ~x => (unquote x) or unquote-splice ~@x => (unquote-splice x)
    reader_consume(reader); // consume ~
    if (reader_peek_char(reader) == '@') {
      // unquote-splice ~@x
      reader_consume(reader); // consume @
      reader_skip_all(reader);
      size_t sq_before = reader_offset(reader);
      ID sq_expr = parse_expr(reader, st);
      size_t sq_after = reader_offset(reader);
      if (sq_after <= sq_before && !reader_eof(reader)) {
        throw_parser_exception("Parser made no progress after unquote-splice", reader);
      }
      if (!sq_expr)
        return NULL;
      // Create (unquote-splice <expr>) list
      return AUTORELEASE(make_ast_list(SYM_UNQUOTE_SPLICE, AUTORELEASE(make_ast_list(sq_expr, NULL))));
    } else {
      // unquote ~x
      reader_skip_all(reader);
      size_t uq_before = reader_offset(reader);
      ID uq_expr = parse_expr(reader, st);
      size_t uq_after = reader_offset(reader);
      if (uq_after <= uq_before && !reader_eof(reader)) {
        throw_parser_exception("Parser made no progress after unquote", reader);
      }
      if (!uq_expr)
        return NULL;
      // Create (unquote <expr>) list
      return AUTORELEASE(make_ast_list(SYM_UNQUOTE, AUTORELEASE(make_ast_list(uq_expr, NULL))));
    }

  case '\\':
    // Handle character literals: \a, \space, \tab, \newline, \return, etc.
    return parse_character(reader, st);

  default:
    break;
  }

  // Handle digits
  if (isdigit(c))
    return make_number_by_parsing(reader, st);

  // Handle symbols starting with :, alphanumeric, ., %, or Unicode
  if (c == ':' || is_alphanumeric(c) || c == '.' || c == '%' || (unsigned char)c >= 0x80) {
    return parse_symbol(reader, st);
  }

  // Handle single-character operators
  if (strchr("+*/=<>", c)) {
    char next = reader_peek_ahead(reader, 1);
    if (next && (is_alphanumeric(next) || next == '*' || next == '+' || next == '/' || next == '=' || next == '<' || next == '>' || next == '-' || next == '_' || next == '?' || next == '!' || next == '%' || (unsigned char)next >= 0x80)) {
      // Multi-character symbol like *ns* or *out*
      return parse_symbol(reader, st);
    }
    // Single character operator
    reader_consume(reader);
    char buf[2] = {c, '\0'};
    return intern_symbol_global(buf);
  }

  // Unknown character - throw exception with helpful message
  char msg[128];
  size_t msg_pos = 0;
  msg_pos = format_append(msg, msg_pos, sizeof(msg), "Unexpected character '");
  msg_pos = format_append_char(msg, msg_pos, sizeof(msg), (c >= 32 && c < 127) ? c : '?');
  msg_pos = format_append(msg, msg_pos, sizeof(msg), "' (0x");
  msg_pos = format_append_hex_byte(msg, msg_pos, sizeof(msg), (unsigned char)c);
  msg_pos = format_append(msg, msg_pos, sizeof(msg), ") at position ");
  msg_pos = format_append_ulong(msg, msg_pos, sizeof(msg), (unsigned long)reader->index);
  msg_pos = format_append(msg, msg_pos, sizeof(msg), " (line ");
  msg_pos = format_append_int(msg, msg_pos, sizeof(msg), reader->line);
  msg_pos = format_append(msg, msg_pos, sizeof(msg), ", col ");
  msg_pos = format_append_int(msg, msg_pos, sizeof(msg), reader->column);
  (void)format_append_char(msg, msg_pos, sizeof(msg), ')');
  throw_parser_exception(msg, reader);
  return NULL;
}

/**
 * @brief Create CljObject by parsing expression from Reader with stack
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @param stack Parser stack for temporary storage
 * @return Autoreleased object or NULL (nil) - throws exception on error (no manual RELEASE needed)
 */
static ID parse_expr_with_stack(Reader *reader, EvalState *st, CljTransientVector *stack) {
  reader_skip_all(reader);
  if (reader_is_eof(reader))
    return NULL;
  char c = reader_current(reader);

  switch (c) {
  case '^':
    return parse_meta(reader, st);

  case '#': {
    char next = reader_peek_ahead(reader, 1);
    if (next == '^')
      return parse_meta_map(reader, st);
    if (next == '(')
      return parse_anon_fn(reader, st);
    if (next == '{')
      return parse_set_with_stack(reader, st, stack);
    return parse_tagged_literal(reader, st);
  }

  case '[':
    return parse_vector_with_stack(reader, st, stack);

  case '{':
    return parse_map_with_stack(reader, st, stack);

  case '(':
    return parse_list(reader, st); // Listen noch nicht mit Stack

  case '"':
    return parse_string_internal(reader, st);

  case '-':
    if (isdigit((unsigned char)reader_peek_ahead(reader, 1)))
      return make_number_by_parsing(reader, st);
    break;

  case '.':
    if (isdigit((unsigned char)reader_peek_ahead(reader, 1))) {
      char invalid_decimal[32];
      int pos = 0;
      invalid_decimal[pos++] = c;
      reader_next(reader);
      while (isdigit((unsigned char)reader_peek_char(reader)) && pos < (int)sizeof(invalid_decimal) - 1) {
        invalid_decimal[pos++] = reader_next(reader);
      }
      invalid_decimal[pos] = '\0';
      throw_parser_exceptionf(reader,
                              "Syntax error compiling.\nUnable to resolve symbol: %s in this context",
                              invalid_decimal);
      return NULL;
    }
    break;

  case 'n':
    if (reader_peek_ahead(reader, 1) == 'i' &&
        reader_peek_ahead(reader, 2) == 'l' &&
        !is_alphanumeric(reader_peek_ahead(reader, 3))) {
      reader_consume(reader);
      reader_consume(reader);
      reader_consume(reader);
      return SYM_NIL;
    }
    break;

  case 't':
    if (reader_peek_ahead(reader, 1) == 'r' &&
        reader_peek_ahead(reader, 2) == 'u' &&
        reader_peek_ahead(reader, 3) == 'e' &&
        !is_alphanumeric(reader_peek_ahead(reader, 4))) {
      reader_consume(reader);
      reader_consume(reader);
      reader_consume(reader);
      reader_consume(reader);
      return clj_true;
    }
    break;

  case 'f':
    if (reader_peek_ahead(reader, 1) == 'a' &&
        reader_peek_ahead(reader, 2) == 'l' &&
        reader_peek_ahead(reader, 3) == 's' &&
        reader_peek_ahead(reader, 4) == 'e' &&
        !is_alphanumeric(reader_peek_ahead(reader, 5))) {
      reader_consume(reader);
      reader_consume(reader);
      reader_consume(reader);
      reader_consume(reader);
      reader_consume(reader);
      return clj_false;
    }
    break;

  case '\'':
    reader_consume(reader);
    reader_skip_all(reader);
    size_t qb_before = reader_offset(reader);
    ID quoted = parse_expr_with_stack(reader, st, stack);
    size_t qb_after = reader_offset(reader);
    if (qb_after <= qb_before && !reader_eof(reader)) {
      throw_parser_exception("Parser made no progress after quote", reader);
    }
    if (!quoted) return NULL;
    return AUTORELEASE(make_ast_list(SYM_QUOTE, AUTORELEASE(make_ast_list(quoted, NULL))));

  case '@':
    reader_consume(reader);
    reader_skip_all(reader);
    size_t db_before = reader_offset(reader);
    ID atom_expr = parse_expr_with_stack(reader, st, stack);
    size_t db_after = reader_offset(reader);
    if (db_after <= db_before && !reader_eof(reader)) {
      throw_parser_exception("Parser made no progress after @", reader);
    }
    if (!atom_expr) return NULL;
    return AUTORELEASE(make_ast_list(SYM_DEREF, AUTORELEASE(make_ast_list(atom_expr, NULL))));

  case '`':
    reader_consume(reader);
    reader_skip_all(reader);
    size_t qq_before = reader_offset(reader);
    ID qq_expr = parse_expr_with_stack(reader, st, stack);
    size_t qq_after = reader_offset(reader);
    if (qq_after <= qq_before && !reader_eof(reader)) {
      throw_parser_exception("Parser made no progress after quasiquote", reader);
    }
    if (!qq_expr) return NULL;
    return AUTORELEASE(make_ast_list(SYM_QUASIQUOTE, AUTORELEASE(make_ast_list(qq_expr, NULL))));

  case '~':
    reader_consume(reader);
    if (reader_peek_char(reader) == '@') {
      reader_consume(reader);
      reader_skip_all(reader);
      size_t sq_before = reader_offset(reader);
      ID sq_expr = parse_expr_with_stack(reader, st, stack);
      size_t sq_after = reader_offset(reader);
      if (sq_after <= sq_before && !reader_eof(reader)) {
        throw_parser_exception("Parser made no progress after unquote-splice", reader);
      }
      if (!sq_expr) return NULL;
      return AUTORELEASE(make_ast_list(SYM_UNQUOTE_SPLICE, AUTORELEASE(make_ast_list(sq_expr, NULL))));
    } else {
      reader_skip_all(reader);
      size_t uq_before = reader_offset(reader);
      ID uq_expr = parse_expr_with_stack(reader, st, stack);
      size_t uq_after = reader_offset(reader);
      if (uq_after <= uq_before && !reader_eof(reader)) {
        throw_parser_exception("Parser made no progress after unquote", reader);
      }
      if (!uq_expr) return NULL;
      return AUTORELEASE(make_ast_list(SYM_UNQUOTE, AUTORELEASE(make_ast_list(uq_expr, NULL))));
    }

  case '\\':
    return parse_character(reader, st);

  default:
    break;
  }

  if (isdigit(c))
    return make_number_by_parsing(reader, st);

  if (c == ':' || is_alphanumeric(c) || c == '.' || c == '%' || (unsigned char)c >= 0x80) {
    return parse_symbol(reader, st);
  }

  if (strchr("+*/=<>", c)) {
    char next = reader_peek_ahead(reader, 1);
    if (next && (is_alphanumeric(next) || next == '*' || next == '+' || next == '/' || next == '=' || next == '<' || next == '>' || next == '-' || next == '_' || next == '?' || next == '!' || next == '%' || (unsigned char)next >= 0x80)) {
      return parse_symbol(reader, st);
    }
    reader_consume(reader);
    char buf[2] = {c, '\0'};
    return intern_symbol_global(buf);
  }

  char msg[128];
  size_t msg_pos = 0;
  msg_pos = format_append(msg, msg_pos, sizeof(msg), "Unexpected character '");
  msg_pos = format_append_char(msg, msg_pos, sizeof(msg), (c >= 32 && c < 127) ? c : '?');
  msg_pos = format_append(msg, msg_pos, sizeof(msg), "' (0x");
  msg_pos = format_append_hex_byte(msg, msg_pos, sizeof(msg), (unsigned char)c);
  msg_pos = format_append(msg, msg_pos, sizeof(msg), ") at position ");
  msg_pos = format_append_ulong(msg, msg_pos, sizeof(msg), (unsigned long)reader->index);
  msg_pos = format_append(msg, msg_pos, sizeof(msg), " (line ");
  msg_pos = format_append_int(msg, msg_pos, sizeof(msg), reader->line);
  msg_pos = format_append(msg, msg_pos, sizeof(msg), ", col ");
  msg_pos = format_append_int(msg, msg_pos, sizeof(msg), reader->column);
  (void)format_append_char(msg, msg_pos, sizeof(msg), ')');
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
 * @brief Evaluate an already-canonical expression (same dispatch as after canonicalize in require/load).
 * @param expr Canonical AST (e.g. from canonicalize_ast)
 * @param eval_state The evaluation state
 * @param env Optional environment (if NULL, uses eval_state->current_ns->mappings)
 * @return The evaluated result (autoreleased) or NULL only if result is nil
 */
ID eval_canonical_form(ID expr, EvalState *eval_state, CljPersistentMap *env) {
  CLJ_ASSERT(eval_state != NULL);
  if (!expr)
    return NULL;
  CljPersistentMap *eval_env = env;
  if (!eval_env) {
    CLJ_ASSERT(eval_state->current_ns != NULL);
    eval_env = (CljPersistentMap *)eval_state->current_ns->mappings;
  }
  CljType expr_tag = TAG(expr);
  if (expr_tag == CLJ_AST_CALL)
    return eval_body(expr, eval_env, eval_state, NULL);
  if (is_list_type(expr_tag))
    return eval_body(expr, eval_env, eval_state, NULL);
  if (expr_tag == CLJ_SYMBOL)
    return eval_symbol(as_symbol(expr), eval_state);
  if (TAG(expr) == CLJ_MAP_PERSISTENT || TAG(expr) == CLJ_VECTOR_PERSISTENT)
    return eval_body(expr, eval_env, eval_state, NULL);
  return expr;
}

/**
 * @brief Evaluate a parsed Clojure expression
 * @param parsed_expr The parsed AST (expected autoreleased, e.g. from parse(); canonicalize may replace it; old AST is cleaned by caller's autorelease pool)
 * @param eval_state The evaluation state
 * @param env Optional environment (if NULL, uses eval_state->current_ns->mappings)
 * @return The evaluated result (autoreleased) or NULL only if result is nil
 */
ID eval_parsed(ID parsed_expr, EvalState *eval_state, CljPersistentMap *env) {
  CLJ_ASSERT(eval_state != NULL);
  if (!parsed_expr)
    return NULL;
  if (IS_IMMEDIATE(parsed_expr))
    return parsed_expr;
  // Same canonicalization as require/load: convert symbol tokens, quote, and non-special calls to AST_CALL
#ifdef PROFILE_STARTUP
  extern double g_canon_time_ms;
  clock_t canon_start = clock();
  parsed_expr = canonicalize_ast(parsed_expr, eval_state);
  g_canon_time_ms += (double)(clock() - canon_start) * 1000.0 / CLOCKS_PER_SEC;
#else
  parsed_expr = canonicalize_ast(parsed_expr, eval_state);
#endif
  return eval_canonical_form(parsed_expr, eval_state, env);
}

/**
 * @brief Parse vector literal [a b c] using Reader
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Parsed vector CljObject or NULL on error
 *
 * Note: Vector size is limited by available heap memory (ESP32: ~520KB total RAM).
 * Nesting depth is limited by stack size (typically several KB).
 */
static ID parse_vector(Reader *reader, EvalState *st) {
  if (reader_match(reader, '[')) {
    reader_skip_all(reader);

    // Create transient vector for efficient building - use empty_vector() singleton to avoid heap allocation
    CljPersistentVector *vec = empty_vector();
    CljTransientVector *tvec = make_vector_transient(vec);

    while (!reader_eof(reader) && reader_peek_char(reader) != ']') {
      size_t before = reader_offset(reader);
      size_t after = before;
      bool no_progress = false;

      WITH_AUTORELEASE_POOL({
        ID value = parse_expr(reader, st);
        after = reader_offset(reader);

        // Check if parser made progress - if not, it's an error
        // If parser made progress, NULL means nil (which is valid)
        if (!value && after <= before && !reader_eof(reader)) {
          no_progress = true;
        } else {
          ID normalized = (value == SYM_NIL) ? NULL : value;
          vector_push(tvec, normalized);
        }
      });

      if (no_progress) {
        RELEASE(tvec);
        return NULL;
      }

      reader_skip_all(reader);
    }

    // Convert back to persistent vector
    vec = vector_persistent(tvec);
    if (vec) {
      RETAIN(vec);
    }
    RELEASE(tvec);

    if (reader_eof(reader) || !reader_match(reader, ']')) {
      RELEASE(vec);
      throw_parser_exception("Unclosed vector - missing closing ']'", reader);
      return NULL;
    }

    return AUTORELEASE(vec); // No location meta - symbols have inline line/col
  }
  return NULL;
}

/**
 * @brief Parse vector literal [a b c] using Reader with parser stack
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @param stack Parser stack for temporary storage
 * @return Parsed vector (pool-managed) or NULL on error
 *
 * Uses the parser stack to build the vector, then creates it with exact capacity.
 * This avoids unnecessary capacity growth in the final vector.
 */
static ID parse_vector_with_stack(Reader *reader, EvalState *st, CljTransientVector *stack) {
  if (!reader_match(reader, '[')) {
    return NULL;
  }
  reader_skip_all(reader);

  // Merke die Position auf dem Stack, wo unsere Elemente beginnen
  CljPersistentVector *backing = vector_persistent(stack);
  unsigned int base_index = vector_count(backing);

  while (!reader_eof(reader) && reader_peek_char(reader) != ']') {
    size_t before = reader_offset(reader);
    bool no_progress = false;

    WITH_AUTORELEASE_POOL({
      ID value = parse_expr(reader, st);
      size_t after = reader_offset(reader);

      // Check if parser made progress - if not, it's an error
      if (!value && after <= before && !reader_eof(reader)) {
        no_progress = true;
      } else {
        // Push das Element auf den Parser-Stack
        ID normalized = (value == SYM_NIL) ? NULL : value;
        vector_push(stack, normalized);
      }
    });

    if (no_progress) {
      vector_truncate_transient(stack, base_index);
      return NULL;
    }

    reader_skip_all(reader);
  }

  if (reader_eof(reader) || !reader_match(reader, ']')) {
    throw_parser_exception("Unclosed vector - missing closing ']'", reader);
    vector_truncate_transient(stack, base_index);
    return NULL;
  }

  // Berechne die exakte Anzahl der Elemente
  backing = vector_persistent(stack);
  unsigned int end_index = vector_count(backing);
  unsigned int count = end_index - base_index;
  if (count >= 256 && parser_debug_heap_enabled()) {
    const char *source_name = reader_get_source_name(reader);
    size_t heap_free = platform_heap_bytes_free();
    fprintf(stderr,
            "[parser-vector] src=%s count=%u base=%u end=%u ar=%lu heap-free=%zu\n",
            source_name ? source_name : "<unknown>", count, base_index,
            end_index, (unsigned long)autorelease_pool_depth(), heap_free);
  }

  ID *items = count > 0 ? backing->data + base_index : NULL;
  CljPersistentVector *final_vec = make_vector_from_stack(items, count);

  // Setze den Stack zurück (entferne unsere Elemente)
  vector_truncate_transient(stack, base_index);

  if (!final_vec) {
    return NULL;
  }

  return AUTORELEASE(final_vec);
}

/**
 * @brief Parse map literal {k v k v} using Reader with parser stack
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @param stack Parser stack for temporary storage
 * @return Parsed map (pool-managed) or NULL on error
 */
static ID parse_map_with_stack(Reader *reader, EvalState *st, CljTransientVector *stack) {
  if (!reader_match(reader, '{'))
    return NULL;
  reader_skip_all(reader);

  // Merke die Position auf dem Stack
  CljPersistentVector *backing = vector_persistent(stack);
  unsigned int base_index = vector_count(backing);

  while (!reader_eof(reader) && reader_peek_char(reader) != '}') {
    WITH_AUTORELEASE_POOL({
      ID key = parse_expr(reader, st);
      reader_skip_all(reader);
      ID value = parse_expr(reader, st);
      reader_skip_all(reader);

      // Push key-value pair as two consecutive elements
      ID normalized_key = (key == SYM_NIL) ? NULL : key;
      ID normalized_value = (value == SYM_NIL) ? NULL : value;
      vector_push(stack, normalized_key);
      vector_push(stack, normalized_value);
    });
  }

  if (reader_eof(reader) || !reader_match(reader, '}')) {
    throw_parser_exception("Unclosed map - missing closing '}'", reader);
    vector_truncate_transient(stack, base_index);
    return NULL;
  }

  // Berechne die Anzahl der Key-Value-Paare
  backing = vector_persistent(stack);
  unsigned int end_index = vector_count(backing);
  unsigned int pair_count = (end_index - base_index) / 2;

  // Erstelle die Map mit exakter Kapazität - use 0 for empty singleton
  CljPersistentMap *map = make_map(pair_count > 0 ? pair_count * 2 : 0, STRONG);

  // Kopiere die Key-Value-Paare
  for (unsigned int i = 0; i < pair_count; i++) {
    ID key = vector_nth(backing, base_index + i * 2);
    ID value = vector_nth(backing, base_index + i * 2 + 1);
    map_assoc_inplace(&map, key, value);
  }

  // Setze den Stack zurück
  vector_truncate_transient(stack, base_index);

  return AUTORELEASE(map);
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
  // Build the map incrementally to avoid relying on a fixed-size stack buffer.
  // This also matches Clojure semantics for duplicate keys (later entries win).
  // Use capacity=0 to get empty-map singleton and avoid heap allocation
  CljPersistentMap *map = make_map(0, STRONG);
  while (!reader_eof(reader) && reader_peek_char(reader) != '}') {
    bool failed = false;

    WITH_AUTORELEASE_POOL({
      ID key = parse_expr(reader, st);
      // Note: key can be NULL (nil) - that's a valid key in Clojure!
      reader_skip_all(reader);
      ID value = parse_expr(reader, st);
      // Note: value can be NULL (nil) - that's a valid value in Clojure!
      reader_skip_all(reader);
      ID normalized_key = (key == SYM_NIL) ? NULL : key;
      ID normalized_value = (value == SYM_NIL) ? NULL : value;
      map_assoc_inplace(&map, normalized_key, normalized_value);
      failed = map == NULL;
    });

    if (failed) {
      throw_parser_exception("Failed to build map", reader);
      return NULL;
    }
  }
  if (reader_eof(reader) || !reader_match(reader, '}')) {
    RELEASE(map);
    throw_parser_exception("Unclosed map - missing closing '}'", reader);
  }
  // Return autoreleased object - caller can use until pool is popped
  // No location meta - symbols have inline line/col
  return AUTORELEASE(map);
}

/**
 * @brief Parse set literal #{a b c} using Reader
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Parsed set CljObject or NULL on error
 */
static ID parse_set(Reader *reader, EvalState *st) {
  // Consume '#'
  if (!reader_match(reader, '#'))
    return NULL;
  if (!reader_match(reader, '{')) {
    throw_parser_exception("Invalid set literal - expected '{' after '#'", reader);
    return NULL;
  }
  reader_skip_all(reader);

  CljHashSet *set = make_hashset(8);
  while (!reader_eof(reader) && reader_peek_char(reader) != '}') {
    bool failed = false;

    WITH_AUTORELEASE_POOL({
      ID value = parse_expr(reader, st);
      reader_skip_all(reader);
      hashset_add_inplace(&set, value);
      failed = set == NULL;
    });

    if (failed) {
      throw_parser_exception("Failed to build set", reader);
      return NULL;
    }
  }
  if (reader_eof(reader) || !reader_match(reader, '}')) {
    RELEASE(set);
    throw_parser_exception("Unclosed set - missing closing '}'", reader);
  }
  return AUTORELEASE(set);
}

/**
 * @brief Parse set literal #{a b c} using Reader with parser stack
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @param stack Parser stack for temporary storage
 * @return Parsed set (pool-managed) or NULL on error
 */
static ID parse_set_with_stack(Reader *reader, EvalState *st, CljTransientVector *stack) {
  // Consume '#'
  if (!reader_match(reader, '#'))
    return NULL;
  if (!reader_match(reader, '{')) {
    throw_parser_exception("Invalid set literal - expected '{' after '#'", reader);
    return NULL;
  }
  reader_skip_all(reader);

  // Merke die Position auf dem Stack
  CljPersistentVector *backing = vector_persistent(stack);
  unsigned int base_index = vector_count(backing);

  while (!reader_eof(reader) && reader_peek_char(reader) != '}') {
    WITH_AUTORELEASE_POOL({
      ID value = parse_expr(reader, st);
      reader_skip_all(reader);
      if (value) vector_push(stack, value);
    });
  }

  if (reader_eof(reader) || !reader_match(reader, '}')) {
    throw_parser_exception("Unclosed set - missing closing '}'", reader);
    vector_truncate_transient(stack, base_index);
    return NULL;
  }

  // Berechne die Anzahl der Elemente
  backing = vector_persistent(stack);
  unsigned int end_index = vector_count(backing);
  unsigned int count = end_index - base_index;

  // Erstelle das Set mit passender Größe
  CljHashSet *set = make_hashset(count > 0 ? count : 4);

  // Füge alle Elemente hinzu
  for (unsigned int i = 0; i < count; i++) {
    ID elem = vector_nth(backing, base_index + i);
    hashset_add_inplace(&set, elem);
  }

  // Setze den Stack zurück
  vector_truncate_transient(stack, base_index);

  return AUTORELEASE(set);
}

/**
 * @brief Parse list literal (a b c) using Reader
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Parsed list CljObject or NULL on error
 */
static ID parse_list(Reader *reader, EvalState *st) {
  // Save position of opening parenthesis for better error messages
  // Position before reader_match is the position of '(' itself
  int open_line = reader->line;
  int open_column = reader->column;
  if (!reader_match(reader, '('))
    return NULL;
  // After reader_match, line/column point to after '(', so adjust to position of '(' itself
  if (open_column > 1) {
    open_column--; // Position of '(' itself
  }
  // If column was 1, '(' is at start of line, which is correct
  reader_skip_all(reader);

  // Handle empty list - return empty list singleton (Clojure: () is the empty list, not nil)
  if (reader_peek_char(reader) == ')') {
    reader_next(reader);
    return (ID)empty_list();
  }

  CljTransientVector *stack = AUTORELEASE(make_vector_transient(empty_vector()));
  if (!stack) {
    return NULL;
  }

  // Parse first element
  ID first = parse_expr_with_progress(reader, st, stack);
  reader_skip_all(reader);

  // Check if first element is if-let symbol for macro expansion
  if (first && TAG(first) == CLJ_SYMBOL) {
    CljSymbol *sym = as_symbol((CljValue)first);
    if (sym && sym->cname && strcmp(sym->cname, "if-let") == 0) {
      // Macro expansion: (if-let [binding test] then else?)
      // => (let [binding test] (if binding then else?))

      // Parse rest of the list: [binding test], then, else?
      // For if-let macro expansion, we don't have the original open position,
      // so use current position as approximation (this is a nested list within the if-let)
      int nested_open_line = reader->line;
      int nested_open_column = reader->column > 1 ? reader->column - 1 : 1;
      ID rest = parse_list_rest(reader, st, nested_open_line, nested_open_column, stack);
      if (!rest) {
        throw_parser_exception("if-let requires at least binding vector and then expression", reader);
      }

      // Extract binding vector [binding test] from rest
      CljList *rest_list = as_list(rest);
      if (!rest_list || !rest_list->first) {
        throw_parser_exception("if-let requires binding vector as first argument", reader);
        return NULL;
      }

      ID binding_vec = rest_list->first;
      if (!binding_vec || TAG(binding_vec) != CLJ_VECTOR_PERSISTENT) {
        throw_parser_exception("if-let binding must be a vector", reader);
      }

      // Extract binding and test from vector [binding test]
      CljPersistentVector *vec = as_vector((CljValue)binding_vec);
      if (!vec || vector_count(vec) < 2) {
        throw_parser_exception("if-let binding vector must have exactly 2 elements", reader);
        return NULL;
      }

      ID binding = vector_nth(vec, 0);
      // Note: test is in binding_vec, we use binding_vec directly in the expansion

      // Extract then expression
      CljList *rest_after_binding = as_list(rest_list->rest);
      if (!rest_after_binding || !rest_after_binding->first) {
        throw_parser_exception("if-let requires then expression", reader);
      }

      ID then_expr = rest_after_binding->first;

      // Extract else expression (optional)
      ID else_expr = NULL;
      CljList *rest_after_then = as_list(rest_after_binding->rest);
      if (rest_after_then && rest_after_then->first) {
        else_expr = rest_after_then->first;
      }

      // Build expansion: (let [binding test] (if binding then else?))
      // First, build (if binding then else?)
      CljList *if_expr;
      if (else_expr) {
        // (if binding then else)
        if_expr = AUTORELEASE(make_ast_list(SYM_IF,
                                            AUTORELEASE(make_ast_list(binding,
                                                                      AUTORELEASE(make_ast_list(then_expr,
                                                                                                AUTORELEASE(make_ast_list(else_expr, NULL))))))));
      } else {
        // (if binding then)
        if_expr = AUTORELEASE(make_ast_list(SYM_IF,
                                            AUTORELEASE(make_ast_list(binding,
                                                                      AUTORELEASE(make_ast_list(then_expr, NULL))))));
      }

      // Build binding vector for let: [binding test]
      ID let_binding_vec = binding_vec;

      // Build (let [binding test] (if binding then else?))
      ID expanded = make_ast_list(SYM_LET,
                                  AUTORELEASE(make_ast_list(let_binding_vec,
                                                            AUTORELEASE(make_ast_list(if_expr, NULL)))));

      // Skip whitespace before checking for closing parenthesis
      reader_skip_all(reader);

      if (reader_eof(reader) || !reader_match(reader, ')')) {
        RELEASE(expanded);
        RELEASE(rest);
        // For if-let macro expansion, use current position as approximation
        int nested_open_line = reader->line;
        int nested_open_column = reader->column > 1 ? reader->column - 1 : 1;
        throw_parser_exceptionf(reader,
                                "Unclosed list - missing closing ')' (opened at line %d, column %d)",
                                nested_open_line, nested_open_column);
        return NULL;
      }

      RELEASE(rest);
      return AUTORELEASE(expanded); // No location meta - symbols have inline line/col
    }
  }

  // Parse rest of the list recursively
  ID rest = parse_list_rest(reader, st, open_line, open_column, stack);

  // Build list from first and rest
  // Return autoreleased object - caller can use until pool is popped
  ID result = make_ast_list(first, (CljList *)rest);
  RELEASE(rest);

  // Skip whitespace before checking for closing parenthesis
  reader_skip_all(reader);

  if (reader_eof(reader) || !reader_match(reader, ')')) {
    RELEASE(result);
    // Include position of opening parenthesis in error message
    throw_parser_exceptionf(reader,
                            "Unclosed list - missing closing ')' (opened at line %d, column %d)",
                            open_line, open_column);
    return NULL;
  }

  return AUTORELEASE(result); // No location meta - symbols have inline line/col
}

/**
 * @brief Parse rest of list after first element
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @param open_line Line number where the list was opened (for error messages)
 * @param open_column Column number where the list was opened (for error messages)
 * @param stack Optional parser stack for nested collection parsing
 * @return Parsed rest of list or NULL for empty rest
 */
static ID parse_list_rest(Reader *reader, EvalState *st, int open_line, int open_column,
                          CljTransientVector *stack) {
  reader_skip_all(reader);

  if (reader_eof(reader)) {
    throw_parser_exceptionf(reader,
                            "Unclosed list - unexpected EOF before ')' (opened at line %d, column %d)",
                            open_line, open_column);
    return NULL;
  }
  if (reader_peek_char(reader) == ')')
    return NULL;

  CljList *head = NULL;
  WITH_AUTORELEASE_POOL({
    ID element = parse_expr_with_progress(reader, st, stack);
    reader_skip_all(reader);
    head = make_list(element, NULL);
  });

  if (reader_peek_char(reader) == ')')
    return (ID)head;

  CljList *tail = head;

  while (!reader_eof(reader) && reader_peek_char(reader) != ')') {
    CljList *node = NULL;
    WITH_AUTORELEASE_POOL({
      ID element = parse_expr_with_progress(reader, st, stack);
      reader_skip_all(reader);
      node = make_list(element, NULL);
      ASSIGN(tail->rest, node);
    });
    RELEASE(node);
    tail = as_list(tail->rest);
  }
  return (ID)head;
}

/**
 * @brief Parse character literal (\a, \space, \tab, \newline, etc.)
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Parsed character CljValue or NULL on error
 */
static ID parse_character(Reader *reader, EvalState *st) {
  (void)st;
  reader_consume(reader);

  if (reader_eof(reader)) {
    throw_parser_exception("Unexpected end of input after '\\'", reader);
  }

  char cname[32];
  int pos = 0;
  while (!reader_eof(reader) && pos < 31 && is_alphanumeric(reader_peek_char(reader))) {
    cname[pos++] = reader_next(reader);
  }
  cname[pos] = '\0';

  if (pos == 1) {
    // Single-character literal (e.g. \a, \1, \n)
    return character((unsigned char)cname[0]);
  }

  if (pos > 1) {
    // Named character literal - use switch on first char for performance
    switch (cname[0]) {
    case 's':
      if (strcmp(cname, "space") == 0)
        return character(' ');
      break;
    case 't':
      if (strcmp(cname, "tab") == 0)
        return character('\t');
      break;
    case 'n':
      if (strcmp(cname, "newline") == 0)
        return character('\n');
      break;
    case 'r':
      if (strcmp(cname, "return") == 0)
        return character('\r');
      break;
    case 'b':
      if (strcmp(cname, "backspace") == 0)
        return character('\b');
      break;
    case 'f':
      if (strcmp(cname, "formfeed") == 0)
        return character('\f');
      break;
    }
    char msg[128];
    size_t msg_pos = 0;
    msg_pos = format_append(msg, msg_pos, sizeof(msg), "Unknown named character: \\");
    (void)format_append(msg, msg_pos, sizeof(msg), cname);
    throw_parser_exception(msg, reader);
    return NULL;
  } else {
    char c = reader_next(reader);
    switch (c) {
    case 'n':
      return character('\n');
    case 't':
      return character('\t');
    case 'r':
      return character('\r');
    case '\\':
      return character('\\');
    case 'b':
      return character('\b');
    case 'f':
      return character('\f');
    default:
      return character((unsigned char)c);
    }
  }
}

/**
 * @brief Resolve namespace alias to actual namespace name symbol
 * @param st Evaluation state
 * @param alias_str Alias string (without ':' prefix)
 * @return Resolved namespace name symbol, or NULL if alias not found
 */
CljSymbol *resolve_alias_in_namespace(EvalState *st, const char *alias_str) {
  if (!alias_str || alias_str[0] == '\0') {
    return NULL;
  }

  // Built-in alias: Math -> clojure.core (for Math/sqrt style lookups)
  if (strcmp(alias_str, "Math") == 0 && SYM_CLOJURE_CORE) {
    return SYM_CLOJURE_CORE;
  }

  if (!st) {
    return NULL;
  }

  CljNamespace *ns = st->resolve_ns ? st->resolve_ns : st->current_ns;
  if (!ns) {
    return NULL;
  }

  // Check if aliases map exists
  if (!ns->aliases) {
    return NULL;
  }

  CljSymbol *alias_sym = intern_symbol_global(alias_str);
  if (!alias_sym)
    return NULL;

  CljObject *resolved_ns_obj = ns_get_alias(ns, alias_sym);
  if (resolved_ns_obj && TAG(resolved_ns_obj) == CLJ_SYMBOL) {
    return as_symbol(resolved_ns_obj);
  }

  return NULL;
}

/**
 * @brief Parse symbol literal (identifier) using Reader
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Parsed symbol CljObject (interned via intern_symbol_global) or NULL on error
 *
 * IMPORTANT: All symbols returned by the parser (directly or indirectly) are interned.
 * This ensures pointer equality for the same symbol names, which is critical for
 * map lookups and namespace resolution.
 */
static ID parse_symbol(Reader *reader, EvalState *st) {
  char buffer[PARSER_SYMBOL_BUF];
  int pos = 0;
  int slash_pos = -1;
  bool auto_qualify = false; // Track if :: was detected

  // Handle keyword prefix
  if (reader_peek_char(reader) == ':') {
    buffer[pos++] = reader_next(reader);
    if (reader_peek_char(reader) == ':') {
      buffer[pos++] = reader_next(reader);
      auto_qualify = true; // :: detected - will auto-qualify with current namespace
    }
  }

  while (!reader_eof(reader) && pos < PARSER_SYMBOL_BUF - 1) {
    int cp = reader_peek_codepoint(reader);
    if (cp < 0)
      break;

    if (utf8_is_symbol_char(cp)) {
      // Track position of '/' for namespace-qualified symbols
      if (cp == '/') {
        slash_pos = pos;
      }

      // Get the UTF-8 bytes for this codepoint
      const char *current = reader->src + reader->index;
      const char *next = utf8codepoint(current, NULL);
      if (!next || next <= current) {
        // Notbremse: Fortschritt sicherstellen
        CLJ_ASSERT(next && next > current);
        // Fallback: advance one byte to avoid hanging
        next = current + 1;
      }

      size_t bytes_to_copy = next - current;
      if (pos + bytes_to_copy >= PARSER_SYMBOL_BUF)
        break;

      // Copy UTF-8 bytes
      for (size_t i = 0; i < bytes_to_copy; i++) {
        buffer[pos++] = current[i];
      }

      // Advance reader by codepoint
#if defined(DEBUG)
      size_t before = reader_offset(reader);
#endif
      reader_next_codepoint(reader);
#if defined(DEBUG)
      size_t after = reader_offset(reader);
      // Notbremse: Fortschritt garantiert
      CLJ_ASSERT(after > before);
#endif
    } else {
      break;
    }
  }

  buffer[pos] = '\0';
  // Empty symbols are invalid - fail gracefully instead of aborting
  if (pos == 0) {
    if (!reader_eof(reader)) {
      // Ensure forward progress to avoid parser stalls
      reader_next_codepoint(reader);
    }
    throw_parser_exception("Expected symbol", reader);
  }
  // Keywords must have at least one character after the colon
  if (pos == 1 && buffer[0] == ':') {
    throw_parser_exception("Expected symbol after ':'", reader);
  }
  if (!utf8valid(buffer))
    throw_parser_exception("Invalid UTF-8 in symbol", reader);

  // Defensive literal handling: ensure we never end up interning these as regular symbols.
  // These are EDN/Clojure literals and must evaluate/parse to their immediate/special values.
  // (Normally handled in parse_expr's fast-path switch, but keep this as a safety net.)
  if (buffer[0] != ':' && !auto_qualify) {
    if (strcmp(buffer, "true") == 0) {
      return clj_true;
    }
    if (strcmp(buffer, "false") == 0) {
      return clj_false;
    }
    if (strcmp(buffer, "nil") == 0) {
      return SYM_NIL;
    }
  }

  // Handle auto-qualified keywords: ::keyword or ::alias/keyword
  if (auto_qualify) {
    if (slash_pos > 0 && slash_pos < pos - 1) {
      // ::alias/keyword - auto-qualify with alias namespace
      buffer[slash_pos] = '\0';
      const char *alias_str = buffer + 2; // Skip ::
      const char *keyword_name = buffer + slash_pos + 1;

      if (alias_str[0] != '\0' && keyword_name[0] != '\0') {
        CljSymbol *ns_name_sym = resolve_alias_in_namespace(st, alias_str);
        if (ns_name_sym && ns_name_sym->cname) {
          // For keywords, keep the ':' prefix in cname for IS_KEYWORD to work
          char keyword_with_colon[SYMBOL_NAME_MAX_LEN];
          size_t kw_pos = 0;
          kw_pos = format_append_char(keyword_with_colon, kw_pos, sizeof(keyword_with_colon), ':');
          (void)format_append(keyword_with_colon, kw_pos, sizeof(keyword_with_colon), keyword_name);
          CljSymbol *kw = intern_symbol(ns_name_sym, keyword_with_colon);
          if (kw) {
            return AUTORELEASE(kw); // No location meta for atoms
          }
        }
      }
      // If alias resolution fails, fall through to treat as regular qualified keyword
    } else {
      // ::keyword - auto-qualify with current namespace
      if (st && st->current_ns && st->current_ns->name && st->current_ns->name->cname) {
        const char *current_ns_name = st->current_ns->name->cname;
        const char *keyword_name = buffer + 2; // Skip ::

        if (keyword_name[0] != '\0') {
          CljSymbol *ns_name_sym = intern_symbol_global(current_ns_name);
          if (ns_name_sym) {
            // For keywords, keep the ':' prefix in cname for IS_KEYWORD to work
            char keyword_with_colon[SYMBOL_NAME_MAX_LEN];
            size_t kw_pos = 0;
            kw_pos = format_append_char(keyword_with_colon, kw_pos, sizeof(keyword_with_colon), ':');
            (void)format_append(keyword_with_colon, kw_pos, sizeof(keyword_with_colon), keyword_name);
            CljSymbol *kw = intern_symbol(ns_name_sym, keyword_with_colon);
            if (kw) {
              return AUTORELEASE(kw); // No location meta for atoms
            }
          }
        }
      }
      // If auto-qualification fails, fall through to return unqualified keyword
    }
  }

  // Check for namespace-qualified symbol: namespace/symbol or alias/symbol
  if (slash_pos > 0 && slash_pos < pos - 1) {
    // Split buffer at '/': namespace/alias and symbol
    buffer[slash_pos] = '\0';
    const char *ns_str = buffer;
    const char *symbol_str = buffer + slash_pos + 1;

    if (ns_str[0] != '\0' && symbol_str[0] != '\0') {
      // CRITICAL: Create symbol with namespace set (not full string name)
      // This allows eval_symbol to quickly check symbol->ns instead of parsing in hot-path

      // For keywords (ns_str starts with ':'), extract namespace name without ':'
      // and keep ':' prefix in symbol_str for IS_KEYWORD to work
      bool is_keyword_symbol = (ns_str && ns_str[0] == ':');

      // Resolve alias if available (for both keywords and regular symbols)
      CljSymbol *resolved_ns_name_sym = NULL;
      if (is_keyword_symbol) {
        // For keywords, skip ':' prefix before alias resolution
        const char *actual_ns_str = ns_str + 1;
        resolved_ns_name_sym = resolve_alias_in_namespace(st, actual_ns_str);
      } else {
        // For regular symbols, try alias resolution
        resolved_ns_name_sym = resolve_alias_in_namespace(st, ns_str);
      }

      // Use resolved namespace if found, otherwise use original ns_str
      CljSymbol *ns_name_sym = resolved_ns_name_sym;
      if (!ns_name_sym) {
        if (is_keyword_symbol) {
          ns_name_sym = (ns_str && ns_str[1] != '\0') ? intern_symbol_global(ns_str + 1) : NULL;
        } else {
          ns_name_sym = ns_str ? intern_symbol_global(ns_str) : NULL;
        }
      }

      if (is_keyword_symbol) {
        // Add ':' prefix to symbol name for IS_KEYWORD to work
        char keyword_with_colon[SYMBOL_NAME_MAX_LEN];
        size_t kw_pos = 0;
        kw_pos = format_append_char(keyword_with_colon, kw_pos, sizeof(keyword_with_colon), ':');
        (void)format_append(keyword_with_colon, kw_pos, sizeof(keyword_with_colon), symbol_str);
        CljSymbol *sym = intern_symbol(ns_name_sym, keyword_with_colon);
        if (sym) {
          return AUTORELEASE(sym); // No location meta for atoms
        }
        // If intern fails, fall through to return unqualified symbol
      } else {
        // Regular qualified symbol (not a keyword)
        CljSymbol *sym = intern_symbol(ns_name_sym, symbol_str);
        if (sym) {
          return AUTORELEASE(sym); // No location meta for atoms
        }
        // If intern fails, fall through to return unqualified symbol
      }
    }
  }

  return AUTORELEASE(intern_symbol_global(buffer)); // No location meta for atoms
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
  while (!reader_eof(reader) && reader_peek(reader) != '"' &&
         pos < MAX_STACK_STRING_SIZE - 1) {
    // Check for escape sequence
    if (reader_peek(reader) == '\\') {
      reader_next(reader); // consume '\'
      char c = reader_next(reader);
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
      // Read UTF-8 codepoint as complete sequence
      const char *current = reader->src + reader->index;
      const char *next = utf8codepoint(current, NULL);
      if (!next || next <= current) {
        // Invalid UTF-8 sequence - validate before copying to avoid corrupt buffer
        // Check if we have enough bytes for a valid sequence
        unsigned char first = (unsigned char)*current;
        int expected_len = utf8_sequence_length(first);
        if (expected_len > 0 && expected_len <= 4) {
          // Try to read the complete sequence even if utf8codepoint failed
          size_t bytes_to_copy = expected_len;
          // Check if we have enough bytes in the source
          if (reader->index + bytes_to_copy <= reader->length) {
            // Validate continuation bytes before copying
            bool valid = true;
            for (size_t i = 1; i < bytes_to_copy; i++) {
              if (reader->index + i >= reader->length ||
                  !utf8_is_continuation_byte((unsigned char)reader->src[reader->index + i])) {
                valid = false;
                break;
              }
            }
            if (valid && pos + bytes_to_copy < MAX_STACK_STRING_SIZE) {
              // Copy complete UTF-8 sequence
              for (size_t i = 0; i < bytes_to_copy; i++) {
                buf[pos++] = current[i];
              }
              // Advance reader by codepoint
              for (size_t i = 0; i < bytes_to_copy; i++) {
                if (current[i] == '\n') {
                  reader->line++;
                  reader->column = 1;
                } else {
                  reader->column++;
                }
              }
              reader->index += bytes_to_copy;
              continue;
            }
          }
        }
        // Fallback: skip one byte to avoid infinite loop
        buf[pos++] = reader_next(reader);
      } else {
        // Copy complete UTF-8 sequence
        size_t bytes_to_copy = next - current;
        if (pos + bytes_to_copy >= MAX_STACK_STRING_SIZE)
          break;
        for (size_t i = 0; i < bytes_to_copy; i++) {
          buf[pos++] = current[i];
        }
        // Advance reader by codepoint
        // Update line/column tracking for newlines
        for (size_t i = 0; i < bytes_to_copy; i++) {
          if (current[i] == '\n') {
            reader->line++;
            reader->column = 1;
          } else {
            reader->column++;
          }
        }
        reader->index += bytes_to_copy;
      }
    }
  }
  if (reader_eof(reader) || reader_next(reader) != '"') {
    throw_parser_exception("Unclosed string - missing closing '\"'", reader);
    return NULL;
  }
  buf[pos] = '\0';
  // Validate UTF-8 before creating string object
  // This ensures we don't create a string with invalid UTF-8 data
  if (!utf8valid(buf)) {
    throw_parser_exception("Invalid UTF-8 in string", reader);
  }
  // Create string object - memory is managed via AUTORELEASE
  // No location meta for atoms (strings) - only forms need location info
  return AUTORELEASE(make_string(buf));
}

/**
 * @brief Parse number literal (integer/float) using Reader
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Parsed number CljObject or NULL on error
 */
static CljObject *make_number_by_parsing(Reader *reader, EvalState *st) {
  (void)st;
  char buf[PARSER_NUMBER_BUF];
  int pos = 0;
  bool has_digit_before_dot = false;
  bool has_exp = false;

  if (reader_peek_char(reader) == '-')
    buf[pos++] = reader_next(reader);

  // Hex integer literal support: 0xFF / -0xFF
  if (reader_peek_char(reader) == '0' &&
      (reader_peek_ahead(reader, 1) == 'x' || reader_peek_ahead(reader, 1) == 'X')) {
    if (!isxdigit((unsigned char)reader_peek_ahead(reader, 2))) {
      throw_parser_exception("Invalid hex number literal", reader);
      return NULL;
    }
    buf[pos++] = reader_next(reader); // 0
    buf[pos++] = reader_next(reader); // x/X
    while (isxdigit((unsigned char)reader_peek_char(reader)) && pos < PARSER_NUMBER_BUF - 1) {
      buf[pos++] = reader_next(reader);
    }
    buf[pos] = '\0';

    char *digits = buf;
    bool negative = false;
    if (buf[0] == '-') {
      negative = true;
      digits++;
    }
    // Skip 0x/0X prefix for conversion
    digits += 2;

    long parsed = strtol(digits, NULL, 16);
    if (negative) {
      parsed = -parsed;
    }
    if (parsed > INT_MAX || parsed < INT_MIN) {
      throw_parser_exception("Hex number out of integer range", reader);
      return NULL;
    }
    return fixnum((int)parsed);
  }

  if (!isdigit((unsigned char)reader_peek_char(reader))) {
    // Check if this is a decimal starting with '.' (invalid in Clojure)
    if (reader_peek_char(reader) == '.') {
      throw_parser_exception("Syntax error compiling at (REPL:1:1).\nUnable to resolve symbol: .01 in this context", reader);
      return NULL;
    }
    return NULL;
  }
  while (isdigit((unsigned char)reader_peek_char(reader)) && pos < PARSER_NUMBER_BUF - 1) {
    buf[pos++] = reader_next(reader);
    has_digit_before_dot = true;
  }
  if (reader_peek_char(reader) == '.' &&
      isdigit((unsigned char)reader_peek_ahead(reader, 1))) {
    buf[pos++] = reader_next(reader);
    while (isdigit((unsigned char)reader_peek_char(reader)) && pos < PARSER_NUMBER_BUF - 1)
      buf[pos++] = reader_next(reader);
  }

  // Optional exponent part: e[+/-]digits
  if ((reader_peek_char(reader) == 'e' || reader_peek_char(reader) == 'E') &&
      pos < PARSER_NUMBER_BUF - 1) {
    has_exp = true;
    buf[pos++] = reader_next(reader);
    if ((reader_peek_char(reader) == '+' || reader_peek_char(reader) == '-') &&
        pos < PARSER_NUMBER_BUF - 1) {
      buf[pos++] = reader_next(reader);
    }
    if (!isdigit((unsigned char)reader_peek_char(reader))) {
      throw_parser_exception("Invalid exponent in number literal", reader);
      return NULL;
    }
    while (isdigit((unsigned char)reader_peek_char(reader)) && pos < PARSER_NUMBER_BUF - 1)
      buf[pos++] = reader_next(reader);
  }
  buf[pos] = '\0';

  // Validate: decimal numbers must have at least one digit before the dot
  if (strchr(buf, '.') && !has_digit_before_dot) {
    throw_parser_exception("Unable to resolve symbol: .01 in this context", reader);
  }

  if (strchr(buf, '.') || has_exp)
    return fixed((float)strtod(buf, NULL));
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
 * @return Autoreleased object or NULL (nil) - throws exception on error
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
  if (!reader || !st)
    return NULL;

  // Erstelle den Parser-Stack für Heap-effizientes Parsen
  CljTransientVector *stack = make_vector_transient(empty_vector());

  // Parse mit Stack
  ID result = parse_expr_with_stack(reader, st, stack);

  // Stack aufräumen
  RELEASE(stack);

  // Heap objects returned from stack-based parsing are pool-managed.
  return result;
}

/**
 * @brief Parse Clojure expression from string input (CljValue API)
 * @param input Input string to parse
 * @param st Evaluation state
 * @return Parsed CljValue or NULL on error
 */
ID parse(const char *input, EvalState *st) {
  if (!input || !st)
    return NULL;

  Reader reader;
  reader_init(&reader, input);
  reader_set_source_name(&reader, "<string input>");

  // Delegate to parse_from_reader (DRY principle)
  // Don't create autorelease pool here - let caller manage memory
  return parse_from_reader(&reader, st);
}

ID parse_from_string(CljString *str, EvalState *st) {
  if (!str || !st)
    return NULL;
  if (TAG((ID)str) != CLJ_STRING) {
    throw_exception(EXCEPTION_TYPE, "parse_from_string expects a string", __FILE__, __LINE__, 0);
    return NULL;
  }

  Reader reader;
  reader_init_with_length(&reader, string_data((ID)str), (size_t)string_length((ID)str));
  reader_set_source_name(&reader, "<string input>");
  return parse_from_reader(&reader, st);
}

/**
 * @brief Merge new metadata with existing metadata on object (DRY helper)
 * @param obj Object that might have existing metadata
 * @param new_meta New metadata to merge (will be released)
 * @return Object with merged metadata or NULL on error (caller must handle RELEASE/AUTORELEASE)
 */
#if defined(META_ENABLED) && META_ENABLED
static ID merge_metadata_with_object(ID obj, ID new_meta) {
  if (!obj || !new_meta) {
    RELEASE(new_meta);
    RELEASE(obj);
    return NULL;
  }

  // Check if the object already has metadata (from nested metadata parsing)
  ID existing_meta = meta_get((CljObject *)obj);
  if (existing_meta) {
    // Merge existing metadata with new metadata (existing takes precedence)
    CljPersistentMap *existing_map = as_map(existing_meta);
    CljPersistentMap *new_map = as_map(new_meta);
    if (existing_map && new_map) {
      CljPersistentMap *merged_meta = (CljPersistentMap *)meta_merge(existing_map, new_map);
      if (merged_meta) {
        if (merged_meta != existing_meta) {
          // Apply merged metadata to object
          meta_set((CljObject *)obj, (CljObject *)merged_meta);
        }
      }
    }
    RELEASE(new_meta);
    return obj; // Return object (caller will handle AUTORELEASE)
  }

  // No existing metadata, apply new metadata directly
  meta_set((CljObject *)obj, (CljObject *)new_meta);
  RELEASE(new_meta);
  return obj; // Return object (caller will handle AUTORELEASE)
}
#endif

/**
 * @brief Apply metadata to object and merge location metadata (DRY helper)
 * @param reader Reader instance for location metadata
 * @param st Evaluation state
 * @param meta Metadata to apply (will be released)
 * @param obj Object to apply metadata to
 * @return Object with applied metadata or NULL on error
 */
#if defined(META_ENABLED) && META_ENABLED
static ID apply_metadata_to_object(Reader *reader, EvalState *st, ID meta, ID obj) {
  (void)reader; // Unused parameter
  (void)st;     // Unused parameter
  if (!obj) {
    RELEASE(meta);
    return NULL;
  }

  // Apply metadata if provided (propagates to eval_def for functions)
  if (meta) {
    meta_set(obj, meta);
  }

#if defined(META_ENABLED) && META_ENABLED
  // Automatically add source code location metadata
  CljPersistentMap *location_meta = (CljPersistentMap *)make_location_meta(reader, st);
  if (location_meta) {
    // Get current metadata (might be from meta parameter or existing)
    ID current_meta = meta ? meta : meta_get((CljObject *)obj);
    CljPersistentMap *current_map = current_meta ? as_map(current_meta) : NULL;
    if (current_map) {
      // Merge location metadata with existing metadata (doesn't overwrite)
      CljPersistentMap *merged_meta = (CljPersistentMap *)meta_merge(current_map, location_meta);
      if (merged_meta) {
        if (merged_meta != current_meta) {
          // Update meta if it was merged
          meta_set(obj, (CljObject *)merged_meta);
        }
      }
    } else {
      // No existing metadata, just set location metadata
      meta_set(obj, (CljObject *)location_meta);
    }
    RELEASE(location_meta);
  }
#endif // META_ENABLED

  RELEASE(meta);
  return obj;
}
#endif

/**
 * @brief Parse metadata ^meta using Reader
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Object with applied metadata or NULL on error
 */
static ID parse_meta(Reader *reader, EvalState *st) {
  // Consume the '^' character (we know it's '^' because parse_expr checked it)
  reader_next(reader);

#if !(defined(META_ENABLED) && META_ENABLED)
  // Metadata is compiled out: skip metadata form without allocations/interning.
  reader_skip_all(reader);
  if (!reader_eof(reader) && reader_current(reader) == '#') {
    char next = reader_peek_ahead(reader, 1);
    if (next == '^') {
      reader_next(reader); // consume '#'
      return parse_meta_map(reader, st);
    }
  }
  return parse_after_skipping_meta_payload(reader, st);
#else
  // Check if this is ^#^{...} syntax (metadata map)
  reader_skip_all(reader);
  if (!reader_eof(reader) && reader_current(reader) == '#') {
    char next = reader_peek_ahead(reader, 1);
    if (next == '^') {
      // This is ^#^{...} syntax - delegate to parse_meta_map
      // But we need to consume the '#' first
      reader_next(reader); // Consume '#'
      return parse_meta_map(reader, st);
    }
  }

  // Check if this is ^:keyword syntax (shorthand for ^{:keyword true})
  reader_skip_all(reader);
  if (!reader_eof(reader) && reader_current(reader) == ':') {
    // Parse the keyword
    ID keyword_meta = parse_expr(reader, st);
    if (!keyword_meta)
      return NULL;

    // Convert keyword to metadata map {:keyword true}
    // In Clojure, ^:keyword means ^{:keyword true}
    CljPersistentMap *meta_map = make_map(0, STRONG);
    if (!meta_map) {
      RELEASE(keyword_meta);
      return NULL;
    }

    // Associate keyword with true
    map_assoc_inplace(&meta_map, keyword_meta, clj_true);
    RELEASE(keyword_meta);

    // Parse the object (which might have more metadata)
    reader_skip_all(reader);
    ID obj = parse_expr(reader, st);
    if (!obj) {
      RELEASE(meta_map);
      return NULL;
    }

    // Merge metadata with object (handles existing metadata)
    ID result = merge_metadata_with_object(obj, meta_map);
    if (!result) {
      return NULL;
    }
    // Apply location metadata if enabled
    return apply_metadata_to_object(reader, st, NULL, result);
  }

  // Regular ^meta syntax (map or other expression)
  reader_skip_all(reader);
  ID meta;
  if (!reader_eof(reader) && reader_peek_char(reader) == '{') {
    meta = parse_map(reader, st);
  } else {
    meta = parse_expr(reader, st);
  }
  if (!meta)
    return NULL;
  if (!IS_IMMEDIATE(meta)) {
    RETAIN(meta); // take ownership; apply_metadata_to_object will release
  }
  reader_skip_all(reader);
  ID obj = parse_expr(reader, st);
  if (!obj) {
    RELEASE(meta);
    return NULL;
  }

  // Merge metadata with object (handles existing metadata)
  ID result = merge_metadata_with_object(obj, meta);
  if (!result) {
    return NULL;
  }
  // Apply location metadata if enabled
  return apply_metadata_to_object(reader, st, NULL, result);
#endif
}

/**
 * @brief Parse anonymous function #(...) => (fn [params...] ...)
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Parsed (fn [params...] body) list or NULL on error
 */
static ID parse_anon_fn(Reader *reader, EvalState *st) {
  // Consume '#'
  if (reader_next(reader) != '#')
    return NULL;
  // Save position of opening parenthesis (before consuming it)
  int open_line = reader->line;
  int open_column = reader->column;
  // Consume '('
  if (reader_next(reader) != '(')
    return NULL;
  // After reader_next, line/column point to after '(', so adjust to position of '(' itself
  if (open_column > 1) {
    open_column--; // Position of '(' itself
  }
  // If column was 1, '(' is at start of line, which is correct

  reader_skip_all(reader);

  // Parse the body (list contents)
  // Note: parse_list_rest does NOT consume the closing ')', so we need to do it
  ID body = parse_list_rest(reader, st, open_line, open_column, NULL);

  // Consume closing ')'
  reader_skip_all(reader);
  if (reader_peek_char(reader) != ')') {
    throw_parser_exception("Unclosed anonymous function - expected ')'", reader);
    RELEASE(body);
    return NULL;
  }
  reader_next(reader); // Consume ')'

  if (!body) {
    // Empty function body - return (fn [] ())
    if (!parser_symbols_ready()) {
      RELEASE(body);
      return NULL;
    }
    CljSymbol *fn_sym = g_parser_sym_fn;
    CljValue empty_vec = AUTORELEASE(make_vector(0, false));
    ID empty_list_val = (ID)empty_list();
    return AUTORELEASE(make_ast_list(fn_sym,
                                     AUTORELEASE(make_ast_list(empty_vec,
                                                               AUTORELEASE(make_ast_list(empty_list_val, NULL))))));
  }

  // Collect all % and %N references in the body to determine parameters
  // For simplicity, we'll scan for % and create parameters [% %1 %2 ...]
  // This is a simplified implementation - full version would need proper AST traversal

  // For now, create a simple version that handles % and %1, %2, etc.
  // We'll create parameters based on what we find
  // This is a simplified approach - in a full implementation, we'd traverse the AST

  // Simple approach: create (fn [%] body) for #(...)
  // Note: Full implementation would scan body for %1, %2, etc. and create appropriate params
  if (!parser_symbols_ready()) {
    RELEASE(body);
    return NULL;
  }
  CljSymbol *fn_sym = g_parser_sym_fn;
  CljSymbol *percent_sym = g_parser_sym_percent;
  CljPersistentVector *param_vec = AUTORELEASE(make_vector(1, false));
  vector_conj_inplace(&param_vec, percent_sym);

  // Create (fn [%] body); param_vec may have been replaced by vector_conj_inplace, autorelease in same scope
  CljList *body_list = make_ast_list(body, NULL);
  RELEASE(body);
  return AUTORELEASE(make_ast_list(fn_sym,
                                   AUTORELEASE(make_ast_list(param_vec,
                                                             AUTORELEASE(body_list)))));
}

/**
 * @brief Parse metadata map #^{...} using Reader
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @return Object with applied metadata map or NULL on error
 */
static ID parse_meta_map(Reader *reader,
                         EvalState *st) {
#if !(defined(META_ENABLED) && META_ENABLED)
  reader_skip_all(reader);
  if (!reader_eof(reader) && reader_current(reader) == '#') {
    // Called from parse_expr - consume '#'
    reader_next(reader);
  }
  reader_skip_all(reader);
  if (reader_eof(reader) || reader_current(reader) != '^') {
    return NULL;
  }
  reader_next(reader); // Consume '^'

  // Skip metadata payload without constructing objects.
  return parse_after_skipping_meta_payload(reader, st);
#else
  // When called from parse_expr, we need to consume '#' and '^'
  // When called from parse_meta, we're already past '#' and at '^'
  reader_skip_all(reader);
  if (!reader_eof(reader) && reader_current(reader) == '#') {
    // Called from parse_expr - consume '#' first
    reader_next(reader); // Consume '#'
  }
  // Now we should be at '^' (either from parse_expr after '#' or from parse_meta)
  reader_skip_all(reader);
  if (reader_eof(reader) || reader_current(reader) != '^') {
    return NULL;
  }
  reader_next(reader); // Consume '^'

  reader_skip_all(reader);
  ID meta = parse_map(reader, st);
  if (!meta)
    return NULL;
  if (!IS_IMMEDIATE(meta)) {
    RETAIN(meta); // take ownership; apply_metadata_to_object will release
  }
  reader_skip_all(reader);
  ID obj = parse_expr(reader, st);
  if (!obj) {
    RELEASE(meta);
    return NULL;
  }

  return apply_metadata_to_object(reader, st, meta, obj);
#endif
}
