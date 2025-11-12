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
static ID parse_list_rest(Reader *reader, EvalState *st);
static ID parse_string_internal(Reader *reader, EvalState *st);
static ID parse_symbol(Reader *reader, EvalState *st);
static CljObject* make_number_by_parsing(Reader *reader, EvalState *st);
// static CljObject* make_number_by_parsing_old(Reader *reader, EvalState *st); // Removed unused function

// Ensure that every parse step advances the reader or hits EOF, otherwise throw
static ID parse_expr_with_progress(Reader *reader, EvalState *st) {
  size_t before = reader_offset(reader);
  ID val = parse_expr(reader, st);
  size_t after = reader_offset(reader);
  if (after <= before && !reader_eof(reader)) {
    throw_parser_exception("Parser made no progress while reading expression", reader);
    return NULL;
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
  // Handle nil literal - parse as SYM_NIL symbol (not NULL)
  // This allows nil to be properly handled in expressions like (do 42 nil)
  // The symbol will be evaluated to NULL in eval_body
  if (c == 'n' && reader_peek_ahead(reader, 1) == 'i' && 
      reader_peek_ahead(reader, 2) == 'l' && 
      !is_alphanumeric(reader_peek_ahead(reader, 3))) {
    reader_consume(reader); // 'n'
    reader_consume(reader); // 'i'
    reader_consume(reader); // 'l'
    CljSymbol *nil_sym = intern_symbol_global("nil");
    // If SYM_NIL is initialized, it should match the interned symbol
    if (nil_sym == SYM_NIL) {
        return SYM_NIL;
    }
    // Return the interned symbol (will be SYM_NIL once initialized)
    return AUTORELEASE(nil_sym);
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
    return clj_true;
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
    return clj_false;
  }
  
  // Handle quote 'x => (quote x)
  if (c == '\'') {
    reader_consume(reader); // consume '
    reader_skip_all(reader);
    size_t qb_before = reader_offset(reader);
    ID quoted = parse_expr(reader, st);
    size_t qb_after = reader_offset(reader);
    if (qb_after <= qb_before && !reader_eof(reader)) {
      throw_parser_exception("Parser made no progress after quote", reader);
      return NULL;
    }
    if (!quoted) return NULL;
    // Create (quote <expr>) list: (quote expr)
    // Build list using the same pattern as parse_list
    ID elements[2] = {SYM_QUOTE, quoted};
    return AUTORELEASE(make_list_from_stack((CljValue*)elements, 2));
  }
  
  // Handle deref @x => (deref x)
  if (c == '@') {
    reader_consume(reader); // consume @
    reader_skip_all(reader);
    size_t db_before = reader_offset(reader);
    ID atom_expr = parse_expr(reader, st);
    size_t db_after = reader_offset(reader);
    if (db_after <= db_before && !reader_eof(reader)) {
      throw_parser_exception("Parser made no progress after @", reader);
      return NULL;
    }
    if (!atom_expr) return NULL;
    // Create (deref <expr>) list: (deref expr)
    // Build list using the same pattern as parse_list
    ID elements[2] = {SYM_DEREF, atom_expr};
    return AUTORELEASE(make_list_from_stack((CljValue*)elements, 2));
  }
  
  if (c == ':' || is_alphanumeric(c) || c == '.' || (unsigned char)c >= 0x80) {
    // For colon, we need to ensure parse_symbol sees it correctly
    // reader_current already peeked the character, so parse_symbol should see the same
    return parse_symbol(reader, st);
  }
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
 * @param env Optional environment (if NULL, uses eval_state->current_ns->mappings)
 * @return The evaluated result (autoreleased) or NULL only if result is nil
 */
ID eval_parsed(CljObject *parsed_expr, EvalState *eval_state, CljMap *env) {
    CLJ_ASSERT(parsed_expr != NULL);
    CLJ_ASSERT(eval_state != NULL);
    
    CljObject *result = NULL;
    
    // Don't catch exceptions here - let them propagate to the caller
    // Check if parsed_expr is an immediate value first
    if (IS_IMMEDIATE(parsed_expr)) {
        // For immediate values, return them as CljObject* (they're already evaluated)
        result = parsed_expr;
    } else if (parsed_expr && TAG(parsed_expr) == CLJ_LIST) {
        // Use provided env or fall back to current_ns->mappings
        CljMap *eval_env = env;
        if (!eval_env) {
            CLJ_ASSERT(eval_state->current_ns != NULL);
            eval_env = (CljMap*)eval_state->current_ns->mappings;
        }
        result = eval_list(as_list(parsed_expr), eval_env, eval_state, NULL);
        // eval_list returns AUTORELEASE objects
    } else if (parsed_expr && TAG(parsed_expr) == CLJ_SYMBOL) {
        // For symbols, use eval_symbol (uses current_ns->mappings internally)
        result = (CljObject*)eval_symbol((ID)parsed_expr, eval_state);
        // eval_symbol already returns autoreleased object
    } else {
        // Literal value (vector, map, etc.) - return as-is
        // parsed_expr is already AUTORELEASEd by parse()
        result = parsed_expr;
    }
    
    // result can be NULL only if the evaluation result is nil
    // If evaluation fails, it should throw an exception, not return NULL
    return result;
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
    CljValue vec = make_vector(6, false);
    CljValue tvec = transient((ID)vec);
    RELEASE((CljObject*)vec);  // Release original, use transient
    
    while (!reader_eof(reader) && reader_peek_char(reader) != ']') {
      size_t before = reader_offset(reader);
      ID value = parse_expr(reader, st);
      size_t after = reader_offset(reader);
      
      // Check if parser made progress - if not, it's an error
      // If parser made progress, NULL means nil (which is valid)
      if (!value && after <= before && !reader_eof(reader)) {
        RELEASE((CljObject*)tvec);
        return NULL;
      }
      
      // Use clj_conj for transient vector (guaranteed in-place)
      tvec = clj_conj((ID)tvec, value);
      reader_skip_all(reader);
    }
    
    // Convert back to persistent vector
    vec = persistent((ID)tvec);
    RELEASE((CljObject*)tvec);
    
    if (reader_eof(reader) || !reader_match(reader, ']')) {
      RELEASE((CljObject*)vec);
      throw_parser_exception("Unclosed vector - missing closing ']'", reader);
      return NULL;
    }
    
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
    ID key = parse_expr(reader, st);
    if (!key)
      return NULL;
    reader_skip_all(reader);
    ID value = parse_expr(reader, st);
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
  // Use constructor API (owned) and return autoreleased
  return AUTORELEASE(make_map_from_stack((CljObject**)pairs, pair_count));
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
    if (sym && sym->name && strcmp(sym->name, "if-let") == 0) {
      // Macro expansion: (if-let [binding test] then else?)
      // => (let [binding test] (if binding then else?))
      
      // Parse rest of the list: [binding test], then, else?
      ID rest = parse_list_rest(reader, st);
      if (!rest) {
        throw_parser_exception("if-let requires at least binding vector and then expression", reader);
        return NULL;
      }
      
      // Extract binding vector [binding test] from rest
      CljList *rest_list = as_list((ID)rest);
      if (!rest_list || !rest_list->first) {
        throw_parser_exception("if-let requires binding vector as first argument", reader);
        return NULL;
      }
      
      ID binding_vec = rest_list->first;
      if (!binding_vec || TAG(binding_vec) != CLJ_VECTOR) {
        throw_parser_exception("if-let binding must be a vector", reader);
        return NULL;
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
      CljList *rest_after_binding = as_list((ID)rest_list->rest);
      if (!rest_after_binding || !rest_after_binding->first) {
        throw_parser_exception("if-let requires then expression", reader);
        return NULL;
      }
      
      ID then_expr = rest_after_binding->first;
      
      // Extract else expression (optional)
      ID else_expr = NULL;
      CljList *rest_after_then = as_list((ID)rest_after_binding->rest);
      if (rest_after_then && rest_after_then->first) {
        else_expr = rest_after_then->first;
      }
      
      // Build expansion: (let [binding test] (if binding then else?))
      // First, build (if binding then else?)
      ID if_args[4];
      if_args[0] = SYM_IF;
      if_args[1] = binding;
      if_args[2] = then_expr;
      int if_arg_count = 3;
      if (else_expr) {
        if_args[3] = else_expr;
        if_arg_count = 4;
      }
      ID if_expr = AUTORELEASE(make_list_from_stack((CljValue*)if_args, if_arg_count));
      
      // Build binding vector for let: [binding test]
      ID let_binding_vec = AUTORELEASE(binding_vec);
      
      // Build (let [binding test] (if binding then else?))
      ID let_args[3];
      let_args[0] = SYM_LET;
      let_args[1] = let_binding_vec;
      let_args[2] = if_expr;
      ID expanded = AUTORELEASE(make_list_from_stack((CljValue*)let_args, 3));
      
      // Skip whitespace before checking for closing parenthesis
      reader_skip_all(reader);
      
      if (reader_eof(reader) || !reader_match(reader, ')')) {
        RELEASE(expanded);
        throw_parser_exception("Unclosed list - missing closing ')'", reader);
        return NULL;
      }
      
      return expanded;
    }
  }
  
  // Parse rest of the list recursively
  ID rest = parse_list_rest(reader, st);
  
  // Build list from first and rest
  // Return autoreleased object - caller can use until pool is popped
  CljValue result = AUTORELEASE(make_list(first, (CljList*)rest));
  
  // Skip whitespace before checking for closing parenthesis
  reader_skip_all(reader);
  
  if (reader_eof(reader) || !reader_match(reader, ')')) {
    RELEASE(result);
    throw_parser_exception("Unclosed list - missing closing ')'", reader);
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
    throw_parser_exception("Unclosed list - unexpected EOF before ')'", reader);
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
    return AUTORELEASE(make_list(element, NULL));
  }

  // Parse remaining elements recursively
  ID rest = parse_list_rest(reader, st);

  // Build list node
  return AUTORELEASE(make_list(element, (CljList*)rest));
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
  // Leere Symbole sind ungültig – statt abzubrechen, sauber fehlschlagen
  if (pos == 0) {
    if (!reader_eof(reader)) {
      // Ensure forward progress to avoid parser stalls
      reader_next_codepoint(reader);
    }
    throw_parser_exception("Expected symbol", reader);
    return NULL;
  }
  // Keywords must have at least one character after the colon
  if (pos == 1 && buffer[0] == ':') {
    throw_parser_exception("Expected symbol after ':'", reader);
    return NULL;
  }
  if (!utf8valid(buffer))
    throw_parser_exception("Invalid UTF-8 in symbol", reader);
  
  // Check for namespace-qualified symbol: alias/symbol
  if (slash_pos > 0 && slash_pos < pos - 1 && st && st->current_ns) {
    // Save original buffer for fallback
    char original_buffer[MAX_STACK_STRING_SIZE];
    strncpy(original_buffer, buffer, sizeof(original_buffer));
    original_buffer[sizeof(original_buffer) - 1] = '\0';
    
    // Split buffer at '/': alias and symbol
    buffer[slash_pos] = '\0';
    const char *alias_str = buffer;
    const char *symbol_str = buffer + slash_pos + 1;
    
    if (alias_str[0] != '\0' && symbol_str[0] != '\0') {
      // Create alias symbol
      CljSymbol *alias_sym = intern_symbol_global(alias_str);
      if (!alias_sym) {
        // Restore original buffer and return original symbol
        return AUTORELEASE(intern_symbol_global(original_buffer));
      }
      
      // Look up alias in current namespace
      CljObject *ns_name_sym = ns_get_alias(st->current_ns, (CljObject*)alias_sym);
      if (ns_name_sym && TAG(ns_name_sym) == CLJ_SYMBOL) {
        // Get namespace name from symbol
        CljSymbol *ns_sym = as_symbol(ns_name_sym);
        if (ns_sym && ns_sym->name) {
          // Find namespace object
          CljNamespace *target_ns = ns_find(ns_sym->name);
          if (target_ns && target_ns->mappings) {
            // Create symbol symbol
            CljSymbol *symbol_sym = intern_symbol_global(symbol_str);
            if (symbol_sym) {
              // Look up symbol in target namespace
              CljObject *resolved = (CljObject*)map_get((CljMap*)target_ns->mappings, (CljValue)symbol_sym);
              if (resolved) {
                // Return resolved value (already retained by map_get)
                return AUTORELEASE(RETAIN(resolved));
              }
            }
          }
        }
      }
      // If resolution fails, return original symbol (alias/symbol)
      // This allows partial failures without breaking parsing
      return AUTORELEASE(intern_symbol_global(original_buffer));
    }
  }
  
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
    return NULL;
  }
  // Create string object - memory is managed via AUTORELEASE
  ID result = AUTORELEASE(make_string(buf));
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
  
  // Delegate to parse_from_reader (DRY principle)
  // Don't create autorelease pool here - let caller manage memory
  return (ID)parse_from_reader(&reader, st);
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
  ID meta = parse_expr(reader, st);
  if (!meta)
    return NULL;
  reader_skip_all(reader);
  ID obj = parse_expr(reader, st);
  if (!obj) {
    RELEASE(meta);
    return NULL;
  }
  meta_set(obj, meta);
  
#ifdef ENABLE_META
  // Automatically add source code location metadata
  CljObject *location_meta = make_location_meta(reader, st);
  if (location_meta) {
    // Merge location metadata with existing metadata (doesn't overwrite)
    CljObject *merged_meta = meta_merge((CljObject*)meta, location_meta);
    if (merged_meta != (CljObject*)meta) {
      // Update meta if it was merged
      meta_set(obj, merged_meta);
      RELEASE(merged_meta);
    }
    RELEASE(location_meta);
  }
#endif // ENABLE_META
  
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
  ID obj = parse_expr(reader, st);
  if (!obj) {
    RELEASE(meta);
    return NULL;
  }
  meta_set(obj, meta);
  
#ifdef ENABLE_META
  // Automatically add source code location metadata
  CljObject *location_meta = make_location_meta(reader, st);
  if (location_meta) {
    // Merge location metadata with existing metadata (doesn't overwrite)
    CljObject *merged_meta = meta_merge((CljObject*)meta, location_meta);
    if (merged_meta != (CljObject*)meta) {
      // Update meta if it was merged
      meta_set(obj, merged_meta);
      RELEASE(merged_meta);
    }
    RELEASE(location_meta);
  }
#endif // ENABLE_META
  
  RELEASE(meta);
  return AUTORELEASE(obj);
}

