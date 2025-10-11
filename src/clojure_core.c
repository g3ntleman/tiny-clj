// clojure.core.c

#include "parser.h"
#include "exception.h"
#include "function_call.h"
#include "namespace.h"
#include "runtime.h"
#include "tiny_clj.h"
#include "reader.h"
#include "memory_hooks.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>

static bool g_core_quiet = false;


// Clojure core code for Tiny-Clj interpreter
const char *clojure_core_code =

#include "clojure.core.clj"

    ;

// Forward declaration for make_object_by_parsing_expr
extern CljObject *make_object_by_parsing_expr(Reader *reader, EvalState *st);

static bool eval_core_source(const char *src, EvalState *st) {
  if (!src || !st)
    return false;
  DEBUG_PRINTF("[clojure.core] eval_core_source src=%p first=%d\n", (void *)src,
           (int)(unsigned char)src[0]);
  
  // Use Reader to parse multiple expressions
  Reader reader;
  reader_init(&reader, src);
  
  int expr_count = 0;
  int success_count = 0;
  
  // Parse and evaluate all expressions in the source with TRY/CATCH
  while (!reader_is_eof(&reader)) {
    reader_skip_all(&reader);
    if (reader_is_eof(&reader)) break;
    
    CljObject *form = make_object_by_parsing_expr(&reader, st);
    if (!form) {
      DEBUG_PRINTF("[clojure.core] Failed to parse expression #%d\n", expr_count + 1);
      break;
    }
    
    // Evaluate with exception handling using TRY/CATCH
    TRY {
      CljObject *result = eval_expr_simple(form, st);
      if (result) RELEASE(result);
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
    
    expr_count++;
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
    printf("[clojure.core] load_clojure_core start src=%p\n",
           (void *)clojure_core_code);
    if (clojure_core_code) {
      printf("[clojure.core] first chars: %.32s\n", clojure_core_code);
    }
    printf("=== Loading Clojure Core Functions ===\n");
  }
  if (!clojure_core_code && !g_core_quiet) {
    printf("[clojure.core] source string missing\n");
    return 0;
  }

  bool ok = eval_core_source(clojure_core_code, st);

  if (!ok) {
    CljObject *err = st->last_error;
    if (err) {
      char *msg = pr_str(err);
      if (msg) {
        printf("[clojure.core] load error: %s\n", msg);
        free(msg);
      }
      st->last_error = NULL;
      RELEASE(err);
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
