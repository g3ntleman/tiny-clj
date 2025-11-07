// clojure.core.c

#include "exception.h"
#include "namespace.h"
#include "tiny_clj.h"
#include "reader.h"
#include "symbol.h"
#include "value.h"  // For IS_IMMEDIATE macro
#include "runtime.h" // For g_runtime
#include "list.h"    // For LIST_FIRST
#include "function_call.h"  // For SYM_DEF
#include "map.h"     // For map_get
#include "parser.h"  // For eval_parsed
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static bool g_core_quiet = false;


// Clojure core code for Tiny-Clj interpreter
const char *clojure_core_code =

#include "clojure.core.clj"

    ;

// Forward declaration for value_by_parsing_expr
extern CljValue value_by_parsing_expr(Reader *reader, EvalState *st);

static bool eval_core_source(const char *src, EvalState *st) {
  if (!src || !st)
    return false;
  
  // CRITICAL: Save the original namespace object (not just the name)
  // This ensures we use the same namespace object that was cached
  CljNamespace *original_ns = st->current_ns;
  
  // CRITICAL: Use the cached namespace if it exists, otherwise get/create it
  // This ensures we use the same namespace object that register_builtins() may have created
  // NOTE: load_clojure_core() already set st->current_ns to clojure.core,
  // so we just need to ensure we're using the cached one
  if (g_runtime.clojure_core_cache) {
    st->current_ns = (CljNamespace*)g_runtime.clojure_core_cache;
  }
  
  // Use Reader to parse multiple expressions
  Reader reader;
  reader_init(&reader, src);
  
  int expr_count = 0;
  int success_count = 0;
  
  // Parse and evaluate all expressions in the source with TRY/CATCH
  while (!reader_is_eof(&reader)) {
    reader_skip_all(&reader);
    if (reader_is_eof(&reader)) break;
    
    CljValue form = value_by_parsing_expr(&reader, st);
    if (!form) {
      // Continue to next expression instead of breaking
      expr_count++;
      continue;
    }
    
    // Evaluate with exception handling using TRY/CATCH
    TRY {
      ID result = eval_parsed((CljObject*)form, st, NULL);
      // Don't RELEASE result - eval_parsed already returns AUTORELEASE
      // result can be NULL if nil was evaluated (legitimate case)
      // eval_parsed should throw exceptions for errors, not return NULL
      if (result) {
        success_count++;
      } else {
        // NULL result could be nil (legitimate) or evaluation failure
        // For def expressions, the symbol should be stored even if result is NULL
        // Check if this was a def expression that might have stored something
        if (is_type(form, CLJ_LIST)) {
          CljList *list = as_list(form);
          CljObject *first = LIST_FIRST(list);
          if (first == SYM_DEF) {
            // def returns the symbol, not the value
            // Even if value evaluation failed, def might have stored nil
            success_count++;
          }
        }
      }
    } CATCH(ex) {
      // Exception occurred during evaluation
      // Log the exception for debugging (always log for def expressions to catch silent failures)
      bool is_def_expr = false;
      if (is_type(form, CLJ_LIST)) {
        CljList *list = as_list(form);
        CljObject *first = LIST_FIRST(list);
        if (first == SYM_DEF) {
          is_def_expr = true;
        }
      }
      
      // Always log exceptions for def expressions, even in quiet mode
      // This helps catch silent failures during core loading
      if (ex && ex->message != NULL && (!g_core_quiet || is_def_expr)) {
        printf("[clojure.core] Exception loading expression: %s\n", ex->message);
      }
    } END_TRY
    
    // CRITICAL: Release form after evaluation
    // value_by_parsing_expr returns object with rc=1
    RELEASE((CljObject*)form);
    
    // Don't RELEASE form here - it's already managed by the parser
    // RELEASE((CljObject*)form);
    expr_count++;
  }
  
  // Switch back to original namespace object (not just the name)
  // This ensures we restore the exact same namespace object
  if (original_ns) {
    st->current_ns = original_ns;
  }
  
  if (!g_core_quiet) {
    printf("[clojure.core] Loaded %d/%d expressions successfully\n", 
           success_count, expr_count);
  }
  
  return success_count > 0;
}

int load_clojure_core(EvalState *st) {
  if (!st) return 0;
  
  if (!g_core_quiet) {
    printf("=== Loading Clojure Core Functions ===\n");
  }
  if (!clojure_core_code && !g_core_quiet) {
    printf("[clojure.core] source string missing\n");
    return 0;
  }

  // Ensure clojure.core namespace exists and cache is set
  // evalstate_set_ns will create the namespace if it doesn't exist
  evalstate_set_ns(st, "clojure.core");
  
  // CRITICAL: Use the cached namespace if it exists, otherwise cache the new one
  // This ensures that if register_builtins() already created clojure.core,
  // we use the same namespace object for loading
  if (g_runtime.clojure_core_cache && st->current_ns != (CljNamespace*)g_runtime.clojure_core_cache) {
    // Cache already exists but points to different namespace - use cached one
    st->current_ns = (CljNamespace*)g_runtime.clojure_core_cache;
  } else if (st->current_ns && !g_runtime.clojure_core_cache) {
    // No cache yet - set it
    g_runtime.clojure_core_cache = (void*)st->current_ns;
  }

  bool ok = eval_core_source(clojure_core_code, st);

  if (!ok) {
    // Note: last_error removed - Exception handling now uses global exception stack
    printf("[clojure.core] load error: Exception occurred during core loading\n");
  }

  // CRITICAL ASSERTION: Verify that 'inc' was loaded successfully
  // This ensures that the loading process actually stored the function
  if (st->current_ns && g_runtime.clojure_core_cache) {
    CljNamespace *clojure_core = (CljNamespace*)g_runtime.clojure_core_cache;
    if (clojure_core && clojure_core->mappings) {
      CljObject *inc_sym = intern_symbol_global("inc");
      if (inc_sym) {
        CljObject *inc_value = (CljObject*)map_get((CljValue)clojure_core->mappings, (CljValue)inc_sym);
        if (!inc_value) {
          // inc not found - this is a critical error
          fprintf(stderr, "[clojure.core] CRITICAL: 'inc' not found in mappings after loading!\n");
          // Don't fail silently - abort or return error
          return 0;
        }
        // Verify it's a function
        if (!is_type(inc_value, CLJ_FUNC) && !is_type(inc_value, CLJ_CLOSURE)) {
          fprintf(stderr, "[clojure.core] CRITICAL: 'inc' is not a function (type: %d)!\n", ((CljObject*)inc_value)->type);
          return 0;
        }
      }
    }
  }

  return ok ? 1 : 0;
}

void clojure_core_set_quiet(bool quiet) {
  g_core_quiet = quiet;
  // Note: load_clojure_core() now requires EvalState parameter
  // Called from REPL main() instead
}

void clojure_core_set_source(const char *src) { clojure_core_code = src; }
