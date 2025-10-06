// clojure.core.c

#include "clj_parser.h"
#include "exception.h"
#include "function_call.h"
#include "namespace.h"
#include "runtime.h"
#include "tiny_clj.h"
#include <stdbool.h>
#include <stdio.h>

static bool g_core_quiet = false;


// Clojure core code for Tiny-Clj interpreter
const char *clojure_core_code =

#include "clojure.core.clj"

    ;

static bool eval_core_source(const char *src, EvalState *st) {
  if (!src || !st)
    return false;
  if (!g_core_quiet) {
    printf("[clojure.core] eval_core_source src=%p first=%d\n", (void *)src,
           (int)(unsigned char)src[0]);
  }
  CljObject *form = NULL;
  // Parse the entire source as one expression
  form = parse(src, st);
  if (!form)
    return false;
  CljObject *result = eval_expr_simple(form, st);
  if (result)
    release(result);
  return true;
}

int load_clojure_core() {
  printf("[clojure.core] load_clojure_core start src=%p\n",
         (void *)clojure_core_code);
  if (clojure_core_code) {
    printf("[clojure.core] first chars: %.32s\n", clojure_core_code);
  }
  if (!g_core_quiet)
    printf("=== Loading Clojure Core Functions ===\n");
  if (!clojure_core_code && !g_core_quiet) {
    printf("[clojure.core] source string missing\n");
    return 0;
  }


  EvalState *st = evalstate_new();
  if (!st)
    return 0;

  set_global_eval_state(st);
  evalstate_set_ns(st, "clojure.core");

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
