// clojure.core.c

#include "exception.h"
#include "symbol.h"  // Must be included before namespace.h for CljSymbol definition
#include "namespace.h"
#include "tiny_clj.h"
#include "reader.h"
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
  
  // CRITICAL: Use the cached namespace if it exists, otherwise use current_ns
  // This ensures we use the same namespace object that register_builtins() may have created
  // NOTE: load_clojure_core() already set st->current_ns to clojure.core and cached it,
  // so we should use the cached one to ensure consistency
  CljNamespace *target_ns = st->current_ns;
  if (g_runtime.clojure_core_cache) {
    target_ns = (CljNamespace*)g_runtime.clojure_core_cache;
    st->current_ns = target_ns;
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
        if (form && TAG(form) == CLJ_LIST) {
          CljList *list = as_list(form);
          CljObject *first = LIST_FIRST(list);
          if (first && TAG(first) == CLJ_SYMBOL && as_symbol(first) == SYM_DEF) {
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
        if (form && TAG(form) == CLJ_LIST) {
          CljList *list = as_list(form);
          CljObject *first = LIST_FIRST(list);
          if (first && TAG(first) == CLJ_SYMBOL && as_symbol(first) == SYM_DEF) {
          is_def_expr = true;
        }
      }
      
      // Always log exceptions for def expressions, even in quiet mode
      // This helps catch silent failures during core loading
      if (ex && ex->message[0] != '\0' && (!g_core_quiet || is_def_expr)) {
        printf("[clojure.core] Exception loading expression: %s\n", ex->message);
      }
    } END_TRY
    
    // value_by_parsing_expr returns AUTORELEASE object
    
    expr_count++;
  }
  
  // CRITICAL: Ensure cache is set to the namespace we just loaded into
  // This ensures that the cache points to the namespace with the loaded functions
  // NOTE: target_ns is the namespace we loaded into (either cache or current_ns)
  // We must ensure cache points to target_ns, not original_ns
  if (target_ns) {
    // Always update cache to point to target_ns (the namespace we loaded into)
    g_runtime.clojure_core_cache = target_ns;
    // CRITICAL: Keep st->current_ns pointing to target_ns until after verification
    // Don't restore original_ns here - let load_clojure_core handle it after verification
    // This ensures the verification can check the correct namespace
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

  // CRITICAL: Ensure clojure.core namespace exists and cache is set
  // evalstate_set_ns will create the namespace if it doesn't exist and set the cache
  evalstate_set_ns(st, "clojure.core");
  
  // CRITICAL: Always use the cached namespace for loading
  // This ensures we load into the same namespace that will be used for lookups
  // evalstate_set_ns should have set both st->current_ns and g_runtime.clojure_core_cache
  if (!g_runtime.clojure_core_cache) {
    // Cache should have been set by evalstate_set_ns, but if not, set it now
    if (st->current_ns && st->current_ns->name == SYM_CLOJURE_CORE) {
      g_runtime.clojure_core_cache = st->current_ns;
    } else {
      fprintf(stderr, "[clojure.core] CRITICAL: Failed to get clojure.core namespace!\n");
      return 0;
    }
  }
  
  // CRITICAL: Ensure st->current_ns points to the cached namespace
  // This ensures all def operations store in the cached namespace
  st->current_ns = g_runtime.clojure_core_cache;

  // Save original namespace to restore after loading
  CljNamespace *original_ns = st->current_ns;
  
  bool ok = eval_core_source(clojure_core_code, st);
  
  // Restore original namespace after loading (but cache still points to clojure.core)
  if (original_ns && original_ns != g_runtime.clojure_core_cache) {
    st->current_ns = original_ns;
  }

  if (!ok) {
    // Note: last_error removed - Exception handling now uses global exception stack
    printf("[clojure.core] load error: Exception occurred during core loading\n");
  }

  // CRITICAL ASSERTION: Verify that 'inc' was loaded successfully
  // This ensures that the loading process actually stored the function
  // NOTE: Use g_runtime.clojure_core_cache directly, not st->current_ns,
  // because st->current_ns may have been restored to original_ns after eval_core_source
  if (g_runtime.clojure_core_cache) {
    CljNamespace *clojure_core = (CljNamespace*)g_runtime.clojure_core_cache;
    if (clojure_core && clojure_core->mappings) {
      CljSymbol *inc_sym = intern_symbol_global("inc");
      if (inc_sym) {
        CljObject *inc_value = (CljObject*)map_get((CljMap*)clojure_core->mappings, (CljValue)inc_sym);
        if (!inc_value) {
          // inc not found - this is a critical error
          fprintf(stderr, "[clojure.core] CRITICAL: 'inc' not found in mappings after loading!\n");
          // Don't fail silently - abort or return error
          return 0;
        }
        // Verify it's a function
        if (!inc_value || (TAG(inc_value) != CLJ_FUNC && TAG(inc_value) != CLJ_CLOSURE)) {
          fprintf(stderr, "[clojure.core] CRITICAL: 'inc' is not a function (type: %d)!\n", ((CljObject*)inc_value)->type);
          return 0;
        }
      }
    } else {
      fprintf(stderr, "[clojure.core] CRITICAL: clojure_core_cache has no mappings!\n");
      return 0;
    }
  } else {
    fprintf(stderr, "[clojure.core] CRITICAL: clojure_core_cache is NULL after loading!\n");
    return 0;
  }

  return ok ? 1 : 0;
}

void clojure_core_set_quiet(bool quiet) {
  g_core_quiet = quiet;
  // Note: load_clojure_core() now requires EvalState parameter
  // Called from REPL main() instead
}

void clojure_core_set_source(const char *src) { clojure_core_code = src; }
