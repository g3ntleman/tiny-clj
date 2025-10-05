#ifndef TINY_CLJ_H
#define TINY_CLJ_H

#include <setjmp.h>
#include "CljObject.h"
#include "exception.h"
#include "namespace.h"

extern const char *clojure_core_code;

// Clojure Core Funktionen
int load_clojure_core();
void clojure_core_set_quiet(bool quiet);
CljObject* call_clojure_core_function(const char *name, int argc, CljObject **argv);
CljNamespace* get_clojure_core_namespace();
void cleanup_clojure_core();

// CLJException is defined in exception.h
// EvalState is defined in namespace.h

// ---------------- Try/Catch Macros ----------------
// Funktionsfähige Variante: nutzt den jmp_buf innerhalb von EvalState
#define try(state_ptr) \
    { \
        EvalState* __err_state__ = (state_ptr); \
        int __err_code__ = setjmp(__err_state__->err_jmp); \
        if(__err_code__ == 0)

#define catch(err_var) \
        else \
        for(CLJException* err_var = __err_state__->current_error; err_var; err_var = NULL)

// throw-Makro ruft die Fehlerfunktion mit dem aktiven State auf
#define throw(fmt, ...) throw_error(__err_state__, fmt, ##__VA_ARGS__)

#endif // TINY_CLJ_H
