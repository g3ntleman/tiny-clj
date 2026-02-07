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
#include <ctype.h>
#include "format_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

static size_t format_append_hex_byte(char *dest, size_t offset, size_t capacity, unsigned char value) {
  static const char digits[] = "0123456789abcdef";
  offset = format_append_char(dest, offset, capacity, digits[(value >> 4) & 0x0F]);
  offset = format_append_char(dest, offset, capacity, digits[value & 0x0F]);
  return offset;
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
    enum { MSG_LEN = 256 };
    char buffer[MSG_LEN];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    throw_parser_exception(buffer, reader);
#endif
}

// Stack-based parser constants
#define MAX_STACK_VECTOR_SIZE 64
#define MAX_STACK_MAP_PAIRS 32
#define MAX_STACK_LIST_SIZE 64
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
static ID parse_map(Reader *reader, EvalState *st);
static ID parse_set(Reader *reader, EvalState *st);
static ID parse_list(Reader *reader, EvalState *st);
static ID parse_list_rest(Reader *reader, EvalState *st, int open_line, int open_column);
static ID parse_string_internal(Reader *reader, EvalState *st);
static ID parse_symbol(Reader *reader, EvalState *st);

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
static CljObject* make_number_by_parsing(Reader *reader, EvalState *st);

// Parser is single-threaded. Track when we're consuming metadata so we can
// avoid allocations in META-disabled builds.
#if !(defined(META_ENABLED) && META_ENABLED)
static bool parser_in_meta = false;
#endif

// Optional: disable metadata parsing (used during core load diagnostics).
static bool g_parser_disable_meta = false;

void parser_set_disable_meta(bool disable) {
  g_parser_disable_meta = disable;
}


// Ensure that every parse step advances the reader or hits EOF, otherwise throw
static ID parse_expr_with_progress(Reader *reader, EvalState *st) {
  size_t before = reader_offset(reader);
  ID val = parse_expr(reader, st);
  size_t after = reader_offset(reader);
  if (after <= before && !reader_eof(reader)) {
    throw_parser_exception("Parser made no progress while reading expression", reader);
  }
  return val;
}

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
        char invalid_decimal[64];
        int pos = 0;
        invalid_decimal[pos++] = c; // include the '.'
        reader_next(reader); // consume '.'
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
        CljSymbol *nil_sym = intern_symbol_global("nil");
        if (nil_sym == SYM_NIL) {
          return SYM_NIL;
        }
        return AUTORELEASE(nil_sym);
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
      if (!quoted) return NULL;
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
      if (!atom_expr) return NULL;
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
      if (!qq_expr) return NULL;
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
        if (!sq_expr) return NULL;
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
        if (!uq_expr) return NULL;
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
  char msg[256];
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
    if (!expr) return NULL;
    CljPersistentMap *eval_env = env;
    if (!eval_env) {
        CLJ_ASSERT(eval_state->current_ns != NULL);
        eval_env = (CljPersistentMap*)eval_state->current_ns->mappings;
    }
    CljType expr_tag = TAG(expr);
    if (expr_tag == CLJ_AST_CALL)
        return eval_body(expr, eval_env, eval_state, NULL);
    if (is_list_type(expr_tag))
        return eval_list(as_list(expr), eval_env, eval_state, NULL);
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
    if (!parsed_expr) return NULL;
    if (IS_IMMEDIATE(parsed_expr)) return parsed_expr;
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

    // Create transient vector for efficient building
    CljPersistentVector *vec = make_vector(6, STRONG);
    CljTransientVector *tvec = vector_transient(vec);
    RELEASE(vec);  // Release original, use transient

    while (!reader_eof(reader) && reader_peek_char(reader) != ']') {
      size_t before = reader_offset(reader);
      ID value = parse_expr(reader, st);
      size_t after = reader_offset(reader);

      // Check if parser made progress - if not, it's an error
      // If parser made progress, NULL means nil (which is valid)
      if (!value && after <= before && !reader_eof(reader)) {
        RELEASE(tvec);
        return NULL;
      }

      // Use *_inplace to avoid vector_conj()'s unconditional AUTORELEASE.
      vector_push(tvec, value);
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

    return AUTORELEASE(vec);  // No location meta - symbols have inline line/col
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
#if !(defined(META_ENABLED) && META_ENABLED)
  if (parser_in_meta) {
    // Fast path: consume balanced map contents without allocating.
    int depth = 1;
    while (!reader_eof(reader) && depth > 0) {
      char c = reader_current(reader);
      reader_next(reader);
      if (c == '{') depth++;
      else if (c == '}') depth--;
      else if (c == '"') {
        // Skip string literal to ignore braces inside strings
        while (!reader_eof(reader)) {
          char s = reader_current(reader);
          reader_next(reader);
          if (s == '\\' && !reader_eof(reader)) {
            reader_next(reader); // skip escaped char
            continue;
          }
          if (s == '"') break;
        }
      }
    }
    reader_skip_all(reader);
    return NULL; // No map object produced when metadata is disabled
  }
#endif
  // Build the map incrementally to avoid relying on a fixed-size stack buffer.
  // This also matches Clojure semantics for duplicate keys (later entries win).
  CljPersistentMap *map = make_map(8, STRONG);
  while (!reader_eof(reader) && reader_peek_char(reader) != '}') {
    ID key = parse_expr(reader, st);
    // Note: key can be NULL (nil) - that's a valid key in Clojure!
    reader_skip_all(reader);
    ID value = parse_expr(reader, st);
    // Note: value can be NULL (nil) - that's a valid value in Clojure!
    reader_skip_all(reader);
    map_assoc_inplace(&map, key, value);
    if (!map) {
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
    ID value = parse_expr(reader, st);
    reader_skip_all(reader);
    hashset_add_inplace(&set, value);
    if (!set) {
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

  // Handle empty list - return nil (Clojure behavior: () is nil)
  if (reader_peek_char(reader) == ')') {
    reader_next(reader);
    return NULL;  // () is nil in Clojure
  }

  // Parse first element
  ID first = parse_expr_with_progress(reader, st);
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
      ID rest = parse_list_rest(reader, st, nested_open_line, nested_open_column);
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
      ID let_binding_vec = AUTORELEASE(binding_vec);

      // Build (let [binding test] (if binding then else?))
      ID expanded = AUTORELEASE(make_ast_list(SYM_LET,
                                              AUTORELEASE(make_ast_list(let_binding_vec,
                                                                        AUTORELEASE(make_ast_list(if_expr, NULL))))));

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
      return expanded;  // No location meta - symbols have inline line/col
    }
  }

  // Parse rest of the list recursively
  ID rest = parse_list_rest(reader, st, open_line, open_column);

  // Build list from first and rest
  // Return autoreleased object - caller can use until pool is popped
  ID result = AUTORELEASE(make_ast_list(first, (CljList*)rest));
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

  return result;  // No location meta - symbols have inline line/col
}

/**
 * @brief Parse rest of list after first element
 * @param reader Reader instance for input
 * @param st Evaluation state
 * @param open_line Line number where the list was opened (for error messages)
 * @param open_column Column number where the list was opened (for error messages)
 * @return Parsed rest of list or NULL for empty rest
 */
static ID parse_list_rest(Reader *reader, EvalState *st, int open_line, int open_column) {
  reader_skip_all(reader);

  // If EOF reached before ')', this is an unclosed list
  if (reader_eof(reader)) {
    throw_parser_exceptionf(reader, 
                            "Unclosed list - unexpected EOF before ')' (opened at line %d, column %d)",
                            open_line, open_column);
    return NULL;
  }

  // Check if we're at the end of the list
  if (reader_peek_char(reader) == ')') {
    return NULL; // Empty rest
  }

  // Parse next element (ensure forward progress)
  ID element = parse_expr_with_progress(reader, st);

  // Skip whitespace after parsing element
  reader_skip_all(reader);

  // If next is ')', stop recursion early
  if (reader_peek_char(reader) == ')') {
    // Return CLJ_LIST for the tail to prevent double nesting
    return make_list(element, NULL);
  }

  // Parse remaining elements recursively
  ID rest = parse_list_rest(reader, st, open_line, open_column);

  // Build list node - use CLJ_LIST for the tail (not ASTNode) to prevent double nesting.
  // The canonicalization expects this structure: ASTNode head with CLJ_LIST tail.
  CljList *node = make_list(element, rest ? (CljList*)rest : NULL);
  CLJ_ASSERT(!IS_IMMEDIATE((ID)node) && "List nodes must not be immediates");
  RELEASE(rest);
  return node;
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
        if (strcmp(cname, "space") == 0) return character(' ');
        break;
      case 't':
        if (strcmp(cname, "tab") == 0) return character('\t');
        break;
      case 'n':
        if (strcmp(cname, "newline") == 0) return character('\n');
        break;
      case 'r':
        if (strcmp(cname, "return") == 0) return character('\r');
        break;
      case 'b':
        if (strcmp(cname, "backspace") == 0) return character('\b');
        break;
      case 'f':
        if (strcmp(cname, "formfeed") == 0) return character('\f');
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
CljSymbol* resolve_alias_in_namespace(EvalState *st, const char *alias_str) {
    if (!alias_str || alias_str[0] == '\0') {
        return NULL;
    }

    // Built-in alias: Math -> clojure.core (for Math/sqrt style lookups)
    if (strcmp(alias_str, "Math") == 0 && SYM_CLOJURE_CORE) {
        return SYM_CLOJURE_CORE;
    }
    
    if (!st || !st->current_ns) {
        return NULL;
    }
    
    // Check if aliases map exists
    if (!st->current_ns->aliases) {
        return NULL;
    }
    
    CljSymbol *alias_sym = intern_symbol_global(alias_str);
    if (!alias_sym) return NULL;
    
    CljObject *resolved_ns_obj = ns_get_alias(st->current_ns, (CljObject*)alias_sym);
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
  char buffer[MAX_STACK_STRING_SIZE];
  int pos = 0;
  int slash_pos = -1;
  bool auto_qualify = false;  // Track if :: was detected

  // Handle keyword prefix
  if (reader_peek_char(reader) == ':') {
    buffer[pos++] = reader_next(reader);
    if (reader_peek_char(reader) == ':') {
      buffer[pos++] = reader_next(reader);
      auto_qualify = true;  // :: detected - will auto-qualify with current namespace
    }
  }

  while (!reader_eof(reader) && pos < MAX_STACK_STRING_SIZE - 1) {
    int cp = reader_peek_codepoint(reader);
    if (cp < 0) break;

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
      if (pos + bytes_to_copy >= MAX_STACK_STRING_SIZE) break;

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
      const char *alias_str = buffer + 2;  // Skip ::
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
            return AUTORELEASE(kw);  // No location meta for atoms
          }
        }
      }
      // If alias resolution fails, fall through to treat as regular qualified keyword
    } else {
      // ::keyword - auto-qualify with current namespace
      if (st && st->current_ns && st->current_ns->name && st->current_ns->name->cname) {
        const char *current_ns_name = st->current_ns->name->cname;
        const char *keyword_name = buffer + 2;  // Skip ::
        
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
              return AUTORELEASE(kw);  // No location meta for atoms
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
          return AUTORELEASE(sym);  // No location meta for atoms
        }
        // If intern fails, fall through to return unqualified symbol
      } else {
        // Regular qualified symbol (not a keyword)
        CljSymbol *sym = intern_symbol(ns_name_sym, symbol_str);
        if (sym) {
          return AUTORELEASE(sym);  // No location meta for atoms
        }
        // If intern fails, fall through to return unqualified symbol
      }
    }
  }

  return AUTORELEASE(intern_symbol_global(buffer));  // No location meta for atoms
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
        if (pos + bytes_to_copy >= MAX_STACK_STRING_SIZE) break;
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
static CljObject* make_number_by_parsing(Reader *reader, EvalState *st) {
  (void)st;
  char buf[MAX_STACK_STRING_SIZE];
  int pos = 0;
  bool has_digit_before_dot = false;

  if (reader_peek_char(reader) == '-')
    buf[pos++] = reader_next(reader);
  if (!isdigit((unsigned char)reader_peek_char(reader))) {
    // Check if this is a decimal starting with '.' (invalid in Clojure)
    if (reader_peek_char(reader) == '.') {
      throw_parser_exception("Syntax error compiling at (REPL:1:1).\nUnable to resolve symbol: .01 in this context", reader);
      return NULL;
    }
    return NULL;
  }
  while (isdigit((unsigned char)reader_peek_char(reader)) && pos < MAX_STACK_STRING_SIZE - 1) {
    buf[pos++] = reader_next(reader);
    has_digit_before_dot = true;
  }
  if (reader_peek_char(reader) == '.' &&
      isdigit((unsigned char)reader_peek_ahead(reader, 1))) {
    buf[pos++] = reader_next(reader);
    while (isdigit((unsigned char)reader_peek_char(reader)) && pos < MAX_STACK_STRING_SIZE - 1)
      buf[pos++] = reader_next(reader);
  }
  buf[pos] = '\0';

  // Validate: decimal numbers must have at least one digit before the dot
  if (strchr(buf, '.') && !has_digit_before_dot) {
    throw_parser_exception("Unable to resolve symbol: .01 in this context", reader);
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
  if (!reader || !st) return NULL;

  // value_by_parsing_expr already returns AUTORELEASE objects
  // No need for additional WITH_AUTORELEASE_POOL - just return the result
  // The object is already in the caller's autorelease pool
  return value_by_parsing_expr(reader, st);
}

/**
 * @brief Parse Clojure expression from string input (CljValue API)
 * @param input Input string to parse
 * @param st Evaluation state
 * @return Parsed CljValue or NULL on error
 */
ID parse(const char *input, EvalState *st) {
  if (!input || !st) return NULL;

  Reader reader;
  reader_init(&reader, input);
  reader_set_source_name(&reader, "<string input>");

  // Delegate to parse_from_reader (DRY principle)
  // Don't create autorelease pool here - let caller manage memory
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
  ID existing_meta = meta_get((CljObject*)obj);
  if (existing_meta) {
    // Merge existing metadata with new metadata (existing takes precedence)
    CljPersistentMap *existing_map = as_map(existing_meta);
    CljPersistentMap *new_map = as_map(new_meta);
    if (existing_map && new_map) {
      CljPersistentMap *merged_meta = (CljPersistentMap*)meta_merge(existing_map, new_map);
      if (merged_meta) {
        if (merged_meta != existing_meta) {
          // Apply merged metadata to object
          meta_set((CljObject*)obj, (CljObject*)merged_meta);
        }
        RELEASE(merged_meta);
      }
    }
    RELEASE(new_meta);
    return obj;  // Return object (caller will handle AUTORELEASE)
  }

  // No existing metadata, apply new metadata directly
  meta_set((CljObject*)obj, (CljObject*)new_meta);
  RELEASE(new_meta);
  return obj;  // Return object (caller will handle AUTORELEASE)
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
  (void)reader;  // Unused parameter
  (void)st;      // Unused parameter
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
  CljPersistentMap *location_meta = (CljPersistentMap*)make_location_meta(reader, st);
  if (location_meta) {
    // Get current metadata (might be from meta parameter or existing)
    ID current_meta = meta ? meta : meta_get((CljObject*)obj);
    CljPersistentMap *current_map = current_meta ? as_map(current_meta) : NULL;
    if (current_map) {
      // Merge location metadata with existing metadata (doesn't overwrite)
      CljPersistentMap *merged_meta = (CljPersistentMap*)meta_merge(current_map, location_meta);
      if (merged_meta) {
        if (merged_meta != current_meta) {
          // Update meta if it was merged
          meta_set(obj, (CljObject*)merged_meta);
        }
        RELEASE(merged_meta);
      }
    } else {
      // No existing metadata, just set location metadata
      meta_set(obj, (CljObject*)location_meta);
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

  if (g_parser_disable_meta) {
    // Parse and discard the metadata expression, then return the target object.
    reader_skip_all(reader);
    if (!reader_eof(reader) && reader_current(reader) == '#') {
      char next = reader_peek_ahead(reader, 1);
      if (next == '^') {
        reader_next(reader);  // consume '#'
      }
    }
    ID ignored_meta = parse_expr(reader, st);
    RELEASE(ignored_meta);
    reader_skip_all(reader);
    return parse_expr(reader, st);
  }

#if !(defined(META_ENABLED) && META_ENABLED)
  bool was_in_meta = parser_in_meta;
  parser_in_meta = true;

  // Metadata is compiled out: parse the metadata form (without allocating) and ignore it,
  // then parse the target object.
  reader_skip_all(reader);
  if (!reader_eof(reader) && reader_current(reader) == '#') {
    char next = reader_peek_ahead(reader, 1);
    if (next == '^') {
      reader_next(reader);  // consume '#'
      ID result = parse_meta_map(reader, st);
      parser_in_meta = was_in_meta;
      return result;
    }
  }
  // Parse and discard the metadata expression (parse_map will skip allocs when parser_in_meta)
  ID ignored_meta = parse_expr(reader, st);
  RELEASE(ignored_meta);
  reader_skip_all(reader);
  // Reset parser_in_meta BEFORE parsing the target object,
  // otherwise maps in the object body would be skipped too!
  parser_in_meta = was_in_meta;
  ID obj = parse_expr(reader, st);
  return obj;
#else
  // Check if this is ^#^{...} syntax (metadata map)
  reader_skip_all(reader);
  if (!reader_eof(reader) && reader_current(reader) == '#') {
    char next = reader_peek_ahead(reader, 1);
    if (next == '^') {
      // This is ^#^{...} syntax - delegate to parse_meta_map
      // But we need to consume the '#' first
      reader_next(reader);  // Consume '#'
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
    CljPersistentMap *meta_map = make_map(4, STRONG);
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
  ID body = parse_list_rest(reader, st, open_line, open_column);

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
    CljSymbol *fn_sym = intern_symbol_global("fn");
    CljValue empty_vec = AUTORELEASE(make_vector(0, false));
    ID empty_list_val = NULL; // () is nil in Clojure
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
  CljSymbol *fn_sym = intern_symbol_global("fn");
  CljSymbol *percent_sym = intern_symbol_global("%");
  CljPersistentVector *param_vec = AUTORELEASE(make_vector(1, false));
  vector_conj_inplace(&param_vec, percent_sym);

  // Create (fn [%] body); param_vec may have been replaced by vector_conj_inplace, autorelease in same scope
  CljList *body_list = make_ast_list(body, NULL);
  RELEASE(body);
  return AUTORELEASE(make_ast_list(fn_sym,
                                   AUTORELEASE(make_ast_list(AUTORELEASE(param_vec),
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
  bool was_in_meta = parser_in_meta;
  parser_in_meta = true;

  reader_skip_all(reader);
  if (!reader_eof(reader) && reader_current(reader) == '#') {
    // Called from parse_expr - consume '#'
    reader_next(reader);
  }
  reader_skip_all(reader);
  if (reader_eof(reader) || reader_current(reader) != '^') {
    return NULL;
  }
  reader_next(reader);  // Consume '^'

  reader_skip_all(reader);
  if (reader_eof(reader) || reader_current(reader) != '{') {
    ID obj = parse_expr(reader, st);
    parser_in_meta = was_in_meta;
    return obj;
  }
  ID ignored_meta = parse_map(reader, st); // parse_map will skip allocations under parser_in_meta
  RELEASE(ignored_meta);
  reader_skip_all(reader);
  // Reset parser_in_meta BEFORE parsing the target object
  parser_in_meta = was_in_meta;
  ID obj = parse_expr(reader, st);
  return obj;
#else
  // When called from parse_expr, we need to consume '#' and '^'
  // When called from parse_meta, we're already past '#' and at '^'
  reader_skip_all(reader);
  if (!reader_eof(reader) && reader_current(reader) == '#') {
    // Called from parse_expr - consume '#' first
    reader_next(reader);  // Consume '#'
  }
  // Now we should be at '^' (either from parse_expr after '#' or from parse_meta)
  reader_skip_all(reader);
  if (reader_eof(reader) || reader_current(reader) != '^') {
    return NULL;
  }
  reader_next(reader);  // Consume '^'

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
