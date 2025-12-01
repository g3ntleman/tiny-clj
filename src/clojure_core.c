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
  (void)st;  // original_ns is saved but not used in this function
  
  // CRITICAL: Use the namespace from registry if it exists, otherwise use current_ns
  // This ensures we use the same namespace object that register_builtins() may have created
  CljNamespace *target_ns = ns_find_by_symbol(SYM_CLOJURE_CORE);
  if (target_ns) {
    st->current_ns = target_ns;
  } else {
    target_ns = st->current_ns;
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
      if (!g_core_quiet) {
        const char *error_type = (ex && ex->type[0]) ? ex->type : "Exception";
        const char *error_msg = (ex && ex->message[0]) ? ex->message : "Unknown error";
        const char *error_file = (ex && ex->file[0]) ? ex->file : "<unknown>";
        int error_line = ex ? ex->line : 0;
        fprintf(stderr, "[clojure.core] Failed to eval form #%d%s: %s (%s:%d) [%s]\n",
                expr_count + 1,
                is_def_expr ? " (def)" : "",
                error_msg,
                error_file,
                error_line,
                error_type);
#ifdef DEBUG
        if (is_def_expr && ex) {
          print_exception(ex);
        }
#endif
      }
      
    } END_TRY
    
    // value_by_parsing_expr returns AUTORELEASE object
    
    expr_count++;
  }
  
  // CRITICAL: Keep st->current_ns pointing to target_ns until after verification
  // Don't restore original_ns here - let load_clojure_core handle it after verification
  // This ensures the verification can check the correct namespace
  // target_ns is already registered in ns_registry, no cache needed
  
  if (!g_core_quiet) {
    fprintf(stderr, "[clojure.core] Evaluated %d form(s), %d succeeded.\n",
            expr_count, success_count);
  }

  return success_count > 0;
}

int load_clojure_core(EvalState *st) {
  if (!st) return 0;
  
  if (!clojure_core_code) {
    return 0;
  }

  // CRITICAL: Ensure clojure.core namespace exists
  // evalstate_set_ns will create the namespace if it doesn't exist
  evalstate_set_ns(st, "clojure.core");
  
  // CRITICAL: Always use the namespace from registry for loading
  // This ensures we load into the same namespace that will be used for lookups
  CljNamespace *clojure_core = ns_find_by_symbol(SYM_CLOJURE_CORE);
  if (!clojure_core) {
    // Namespace should have been created by evalstate_set_ns, but if not, fail
    if (st->current_ns && st->current_ns->name == SYM_CLOJURE_CORE) {
      clojure_core = st->current_ns;
    } else {
      return 0;
    }
  }
  
  // CRITICAL: Ensure st->current_ns points to the namespace from registry
  // This ensures all def operations store in the correct namespace
  st->current_ns = clojure_core;

  // Save original namespace to restore after loading
  CljNamespace *original_ns = st->current_ns;
  
  bool ok = eval_core_source(clojure_core_code, st);
  
  // Restore original namespace after loading
  if (original_ns && original_ns != clojure_core) {
    st->current_ns = original_ns;
  }

  (void)ok; // Result is checked below

  // CRITICAL ASSERTION: Verify that 'inc' was loaded successfully
  // This ensures that the loading process actually stored the function
  // NOTE: Use namespace from registry directly, not st->current_ns,
  // because st->current_ns may have been restored to original_ns after eval_core_source
  // clojure_core is already defined above
  if (clojure_core && clojure_core->mappings) {
    CljSymbol *inc_sym = intern_symbol_global("inc");
    if (inc_sym) {
      CljObject *inc_value = (CljObject*)map_get((CljValue)clojure_core->mappings, (CljValue)inc_sym, NULL);
      if (!inc_value) {
        return 0;
      }
      // Verify it's a function
      if (!inc_value || (TAG(inc_value) != CLJ_FUNC && TAG(inc_value) != CLJ_CLOSURE)) {
        return 0;
      }
    }
  } else {
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
