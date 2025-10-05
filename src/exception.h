#ifndef EXCEPTION_H
#define EXCEPTION_H
#include "CljObject.h"

// CLJException is now defined in CljObject.h to avoid circular dependencies

// Wenn message dynamisch ist, kann strdup optional genutzt werden
CLJException* exception(const char *msg, const char *file, int line, int col);

// Für dynamische Fehlermeldungen (mit Variablen)
CLJException* exception_dynamic(const char *msg, const char *file, int line, int col);

// ============================================================================
// ASSERTION FUNCTIONS (Clojure Core API)
// ============================================================================

// Assert with message - throws exception if condition is false
void clj_assert(bool condition, const char *message);

// Assert with message and file location
void clj_assert_with_location(bool condition, const char *message, const char *file, int line, int col);

// Assert-args for function parameter validation
void clj_assert_args(const char *function_name, bool condition, const char *message);

// Assert-args with multiple conditions
void clj_assert_args_multiple(const char *function_name, int condition_count, ...);

// Standard-Fehlermeldungen als Konstanten
extern const char *ERROR_EOF_VECTOR;
extern const char *ERROR_EOF_MAP;
extern const char *ERROR_EOF_LIST;
extern const char *ERROR_UNMATCHED_DELIMITER;
extern const char *ERROR_DIVISION_BY_ZERO;
extern const char *ERROR_INVALID_SYNTAX;
extern const char *ERROR_UNDEFINED_VARIABLE;
extern const char *ERROR_TYPE_MISMATCH;
extern const char *ERROR_STACK_OVERFLOW;
extern const char *ERROR_MEMORY_ALLOCATION;

#endif
