#ifndef FUNCTION_CALL_H
#define FUNCTION_CALL_H

#include "CljObject.h"

// Erweiterte Funktionsaufruf-Funktionen
CljObject* eval_function_call(CljObject *fn, CljObject **args, int argc, CljObject *env);
CljObject* eval_body(CljObject *body, CljObject *env);
CljObject* eval_list(CljObject *list, CljObject *env);

// Arithmetische Operationen
CljObject* eval_add(CljObject *list, CljObject *env);
CljObject* eval_sub(CljObject *list, CljObject *env);
CljObject* eval_mul(CljObject *list, CljObject *env);
CljObject* eval_div(CljObject *list, CljObject *env);
CljObject* eval_println(CljObject *list, CljObject *env);

// Weitere Built-in Funktionen
CljObject* eval_str(CljObject *list, CljObject *env);
CljObject* eval_prn(CljObject *list, CljObject *env);
CljObject* eval_count(CljObject *list, CljObject *env);
CljObject* eval_first(CljObject *list, CljObject *env);
CljObject* eval_rest(CljObject *list, CljObject *env);

// Hilfsfunktionen
CljObject* eval_arg(CljObject *list, int index, CljObject *env);
int is_symbol(CljObject *v, const char *name);

#endif
