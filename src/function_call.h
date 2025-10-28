#ifndef FUNCTION_CALL_H
#define FUNCTION_CALL_H

#include "object.h"
#include "namespace.h"
#include "map.h"
#include "list.h"


// Erweiterte Funktionsaufruf-Funktionen
ID eval_function_call(ID fn, ID *args, int argc, CljMap *env);
ID eval_body(ID body, CljMap *env, EvalState *st);
ID eval_list(CljList *list, CljMap *env, EvalState *st);

ID eval_equal(CljList *list, CljMap *env);
ID eval_println(CljList *list, CljMap *env);

// Definition und Funktionen
ID eval_def(CljList *list, CljMap *env, EvalState *st);
ID eval_ns(CljList *list, CljMap *env, EvalState *st);
ID eval_var(CljList *list, CljMap *env, EvalState *st);
ID eval_list_function(CljList *list, CljMap *env);
ID eval_fn(CljList *list, CljMap *env);
ID eval_symbol(ID symbol, EvalState *st);

// Weitere Built-in Funktionen
ID eval_str(CljList *list, CljMap *env);
ID eval_prn(CljList *list, CljMap *env);
ID eval_count(CljList *list, CljMap *env);
ID eval_first(CljList *list, CljMap *env);
ID eval_rest(CljList *list, CljMap *env);
ID eval_cons(CljList *list, CljMap *env);
ID eval_seq(CljList *list, CljMap *env);

// For-loop functions
ID eval_for(CljList *list, CljMap *env);
ID eval_doseq(CljList *list, CljMap *env);

// Let bindings
ID eval_let(CljList *list, CljMap *env, EvalState *st);

// Function definition macro
ID eval_defn(CljList *list, CljMap *env, EvalState *st);

// Hilfsfunktionen
ID eval_arg(CljList *list, int index, CljMap *env);
ID eval_arg_retained(CljList *list, int index, CljMap *env);
bool is_symbol(ID v, const char *name);

#endif
