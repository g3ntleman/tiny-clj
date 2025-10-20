#ifndef FUNCTION_CALL_H
#define FUNCTION_CALL_H

#include "object.h"
#include "namespace.h"


// Erweiterte Funktionsaufruf-Funktionen
CljObject* eval_function_call(CljObject *fn, CljObject **args, int argc, CljMap *env);
CljObject* eval_body(CljObject *body, CljMap *env, EvalState *st);
CljObject* eval_list(CljObject *list, CljMap *env, EvalState *st);

// Arithmetische Operationen
CljObject* eval_add(CljObject *list, CljMap *env);
CljObject* eval_sub(CljObject *list, CljMap *env);
CljObject* eval_mul(CljObject *list, CljMap *env);
CljObject* eval_div(CljObject *list, CljMap *env);
CljObject* eval_equal(CljObject *list, CljMap *env);
CljObject* eval_println(CljObject *list, CljMap *env);

// Definition und Funktionen
CljObject* eval_def(CljObject *list, CljMap *env, EvalState *st);
CljObject* eval_ns(CljObject *list, CljMap *env, EvalState *st);
CljObject* eval_list_function(CljObject *list, CljMap *env);
CljObject* eval_fn(CljObject *list, CljMap *env);
CljObject* eval_symbol(CljObject *symbol, EvalState *st);

// Weitere Built-in Funktionen
CljObject* eval_str(CljObject *list, CljMap *env);
CljObject* eval_prn(CljObject *list, CljMap *env);
ID eval_count(CljObject *list, CljMap *env);
CljObject* eval_first(CljObject *list, CljMap *env);
CljObject* eval_rest(CljObject *list, CljMap *env);
CljObject* eval_cons(CljObject *list, CljMap *env);
CljObject* eval_seq(CljObject *list, CljMap *env);

// For-loop functions
CljObject* eval_for(CljObject *list, CljMap *env);
CljObject* eval_doseq(CljObject *list, CljMap *env);
CljObject* eval_dotimes(CljObject *list, CljMap *env);

// Hilfsfunktionen
CljObject* eval_arg(CljObject *list, int index, CljMap *env);
CljObject* eval_arg_retained(CljObject *list, int index, CljMap *env);
bool is_symbol(CljObject *v, const char *name);

#endif
