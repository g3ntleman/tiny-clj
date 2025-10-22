#ifndef TINY_CLJ_H
#define TINY_CLJ_H

#include "object.h"
#include "exception.h"
#include "namespace.h"

extern const char *clojure_core_code;

// Clojure Core Funktionen
int load_clojure_core(EvalState *st);
void clojure_core_set_quiet(bool quiet);
ID call_clojure_core_function(const char *name, int argc, ID *argv);
CljNamespace* get_clojure_core_namespace();
void cleanup_clojure_core();

// Convenience API für String-Evaluation
/**
 * @brief Evaluate a Clojure expression from a string
 * @param expr_str The Clojure expression as a string
 * @param eval_state The evaluation state
 * @return The evaluated result (autoreleased) or NULL on error
 */
ID eval_string(const char* expr_str, EvalState *eval_state);

// CLJException is defined in exception.h
// EvalState is defined in namespace.h

// Old exception system removed - use TRY/CATCH/END_TRY from exception.h instead

#endif // TINY_CLJ_H
