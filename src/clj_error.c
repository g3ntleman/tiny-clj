#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include "exception.h"
#include "runtime.h"

// Assertion implementation using throw_exception for try/catch compatibility

// Standard-Fehlermeldungen als Konstanten (kein Heap-Verbrauch)
const char *ERROR_EOF_VECTOR = "EOF while reading vector";
const char *ERROR_EOF_MAP = "EOF while reading map";
const char *ERROR_EOF_LIST = "EOF while reading list";
const char *ERROR_UNMATCHED_DELIMITER = "Unmatched delimiter";
const char *ERROR_DIVISION_BY_ZERO = "Division by zero";
const char *ERROR_INVALID_SYNTAX = "Invalid syntax";
const char *ERROR_UNDEFINED_VARIABLE = "Undefined variable";
const char *ERROR_TYPE_MISMATCH = "Type mismatch";
const char *ERROR_STACK_OVERFLOW = "Stack overflow";
const char *ERROR_MEMORY_ALLOCATION = "Memory allocation failed";

// Für Standardmeldungen: direkt const Pointer nutzen (minimaler Heap)
CLJException* exception(const char *msg, const char *file, int line, int col) {
    return create_exception("Error", msg, file, line, col, NULL);
}

// Für dynamische Fehlermeldungen (mit Variablen)
CLJException* exception_dynamic(const char *msg, const char *file, int line, int col) {
    return create_exception("Error", msg, file, line, col, NULL);
}

// ============================================================================
// ASSERTION FUNCTIONS (Clojure Core API)
// ============================================================================

// Assert with message - throws exception if condition is false
void clj_assert(bool condition, const char *message) {
    if (!condition) {
        CLJException *e = exception(message, NULL, 0, 0);
        if (e) {
            throw_exception_formatted("AssertionError", e->file, e->line, e->col,
                    "Assertion failed: %s", e->message);
        }
    }
}

// Assert with message and file location
void clj_assert_with_location(bool condition, const char *message, const char *file, int line, int col) {
    if (!condition) {
        CLJException *e = exception(message, file, line, col);
        if (e) {
            throw_exception("AssertionError", e->message, e->file, e->line, e->col);
        }
    }
}

// Assert-args for function parameter validation
void clj_assert_args(const char *function_name, bool condition, const char *message) {
    if (!condition) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Assert failed: %s - %s", function_name, message);
        clj_assert(false, error_msg);
    }
}

// Assert-args with multiple conditions
void clj_assert_args_multiple(const char *function_name, int condition_count, ...) {
    va_list args;
    va_start(args, condition_count);
    
    for (int i = 0; i < condition_count; i++) {
        bool condition = va_arg(args, int);  // bool promoted to int in va_list
        const char *message = va_arg(args, const char*);
        
        if (!condition) {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), "Assert failed: %s - %s", function_name, message);
            clj_assert(false, error_msg);
            break;
        }
    }
    
    va_end(args);
}
