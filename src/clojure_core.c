// clojure.core.c

#include "exception.h"
#include "namespace.h"
#include "tiny_clj.h"
#include "reader.h"
#include "memory.h"
#include "value.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static bool g_core_quiet = false;


// Clojure core code for Tiny-Clj interpreter
const char *clojure_core_code =

#include "clojure.core.clj"

    ;

// Forward declaration for make_value_by_parsing_expr
extern CljValue make_value_by_parsing_expr(Reader *reader, EvalState *st);

static bool eval_core_source(const char *src, EvalState *st) {
  if (!src || !st)
    return false;
  
  // Switch to clojure.core namespace for loading core functions
  const char *original_ns = st->current_ns ? st->current_ns->name ? as_symbol(st->current_ns->name)->name : NULL : NULL;
  evalstate_set_ns(st, "clojure.core");
  
  // Use Reader to parse multiple expressions
  Reader reader;
  reader_init(&reader, src);
  
  int expr_count = 0;
  int success_count = 0;
  
  // Parse and evaluate all expressions in the source with TRY/CATCH
  while (!reader_is_eof(&reader)) {
    reader_skip_all(&reader);
    if (reader_is_eof(&reader)) break;
    
    CljValue form = make_value_by_parsing_expr(&reader, st);
    if (!form) {
      DEBUG_PRINTF("[clojure.core] Failed to parse expression #%d (returned nil)\n", expr_count + 1);
      // Continue to next expression instead of breaking
      expr_count++;
      continue;
    }
    
    // Evaluate with exception handling using TRY/CATCH
    TRY {
      CljValue result = eval_expr_simple((CljObject*)form, st);
      if (result) {
        RELEASE((CljObject*)result);
      }
      success_count++;
    } CATCH(ex) {
      // Exception occurred during evaluation
      DEBUG_PRINTF("[clojure.core] Exception in expression #%d\n", expr_count + 1);
      char *err_str = pr_str((CljObject*)ex);
      if (err_str) {
        DEBUG_PRINTF("[clojure.core] Error: %s\n", err_str);
        free(err_str);
      }
    } END_TRY
    
    RELEASE((CljObject*)form);
    expr_count++;
  }
  
  // Switch back to original namespace
  if (original_ns) {
    evalstate_set_ns(st, original_ns);
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

  bool ok = eval_core_source(clojure_core_code, st);

  if (!ok) {
    // Note: last_error removed - Exception handling now uses global exception stack
    printf("[clojure.core] load error: Exception occurred during core loading\n");
  }

  return ok ? 1 : 0;
}

void clojure_core_set_quiet(bool quiet) {
  g_core_quiet = quiet;
  // Note: load_clojure_core() now requires EvalState parameter
  // Called from REPL main() instead
}

void clojure_core_set_source(const char *src) { clojure_core_code = src; }
