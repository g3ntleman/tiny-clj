#ifndef REPL_H
#define REPL_H

#include <stdbool.h>
#include "namespace.h"  // For EvalState

/**
 * @brief Evaluate Clojure code from a string with escape sequence support
 * @param raw_code The Clojure code as a string (may contain escape sequences like \n)
 * @param st The evaluation state
 * @return true on success, false on error
 */
bool repl_eval_arg(const char *raw_code, EvalState *st);

#endif // REPL_H

