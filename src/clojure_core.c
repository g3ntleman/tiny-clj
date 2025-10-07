// clojure.core.c

#include "clj_parser.h"
#include "exception.h"
#include "function_call.h"
#include "namespace.h"
#include "runtime.h"
#include "tiny_clj.h"
#include "reader.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>

static bool g_core_quiet = false;


// Clojure core code for Tiny-Clj interpreter
const char *clojure_core_code =

#include "clojure.core.clj"

    ;

// Forward declaration for parse_expr_internal
extern CljObject *parse_expr_internal(Reader *reader, EvalState *st);

static bool eval_core_source(const char *src, EvalState *st) {
  if (!src || !st)
    return false;
  if (!g_core_quiet) {
    printf("[clojure.core] eval_core_source src=%p first=%d\n", (void *)src,
           (int)(unsigned char)src[0]);
  }
  
  // Use Reader to parse multiple expressions
  Reader reader;
  reader_init(&reader, src);
  
  int expr_count = 0;
  int success_count = 0;
  
  // Save current exception handler
  jmp_buf saved_env;
  memcpy(saved_env, st->jmp_env, sizeof(jmp_buf));
  
  // Parse and evaluate all expressions in the source
  while (!reader_is_eof(&reader)) {
    reader_skip_all(&reader);
    if (reader_is_eof(&reader)) break;
    
    CljObject *form = parse_expr_internal(&reader, st);
    if (!form) {
      if (!g_core_quiet) {
        printf("[clojure.core] Failed to parse expression #%d\n", expr_count + 1);
      }
      break;
    }
    
    // Evaluate with exception handling
    if (setjmp(st->jmp_env) == 0) {
      CljObject *result = eval_expr_simple(form, st);
      if (result) release(result);
      success_count++;
    } else {
      // Exception occurred during evaluation
      if (!g_core_quiet) {
        printf("[clojure.core] Exception in expression #%d\n", expr_count + 1);
        if (st->last_error) {
          char *err_str = pr_str(st->last_error);
          if (err_str) {
            printf("[clojure.core] Error: %s\n", err_str);
            free(err_str);
          }
        }
      }
      // Continue with next expression
    }
    
    // Restore exception handler for next iteration
    memcpy(st->jmp_env, saved_env, sizeof(jmp_buf));
    expr_count++;
  }
  
  if (!g_core_quiet) {
    printf("[clojure.core] Loaded %d/%d expressions successfully\n", 
           success_count, expr_count);
  }
  
  return success_count > 0;
}

int load_clojure_core() {
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


  EvalState *st = evalstate_new();
  if (!st)
    return 0;

  set_global_eval_state(st);
  evalstate_set_ns(st, "user");  // Load into user namespace so functions are accessible

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
      release(err);
    }
  }

  set_global_eval_state(NULL);
  evalstate_free(st);

  return ok ? 1 : 0;
}

void clojure_core_set_quiet(bool quiet) {
  g_core_quiet = quiet;
  if (!g_core_quiet) {
    load_clojure_core();
  }
}

void clojure_core_set_source(const char *src) { clojure_core_code = src; }
