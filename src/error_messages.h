/* Centralized error message constants for consistency and maintainability */

#ifndef TINY_CLJ_ERROR_MESSAGES_H
#define TINY_CLJ_ERROR_MESSAGES_H

#ifdef __cplusplus
extern "C" {
#endif

extern const char *ERR_EXPECTED_NUMBER;       /* "Expected number" */
extern const char *ERR_WRONG_ARITY_ZERO;      /* "Wrong number of args: 0" */
extern const char *ERR_DIVIDE_BY_ZERO;        /* "Divide by zero" */

extern const char *ERROR_EOF_VECTOR;          /* "EOF while reading vector" */
extern const char *ERROR_EOF_MAP;             /* "EOF while reading map" */
extern const char *ERROR_EOF_LIST;            /* "EOF while reading list" */
extern const char *ERROR_UNMATCHED_DELIMITER; /* "Unmatched delimiter" */
extern const char *ERROR_DIVISION_BY_ZERO;    /* "Division by zero" */
extern const char *ERROR_INVALID_SYNTAX;      /* "Invalid syntax" */
extern const char *ERROR_UNDEFINED_VARIABLE;  /* "Undefined variable" */
extern const char *ERROR_TYPE_MISMATCH;       /* "Type mismatch" */
extern const char *ERROR_STACK_OVERFLOW;      /* "Stack overflow" */
extern const char *ERROR_MEMORY_ALLOCATION;  /* "Memory allocation failed" */

#ifdef __cplusplus
}
#endif

#endif /* TINY_CLJ_ERROR_MESSAGES_H */


