/**
 * @file exception.c
 * @brief Implementation of exception handling system with standard error messages.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <setjmp.h>
#include "namespace.h"  // Must be before exception.h for EvalState definition
#include "exception.h"
#include "runtime.h"
#include "object.h"  // For make_exception
#include "clj_strings.h"  // For to_string

// Global storage for current exception
CLJException *g_current_exception = NULL;

// Global exception stack (independent of EvalState)
GlobalExceptionStack global_exception_stack = {0};

// ============================================================================
// STATIC EXCEPTION TYPE CONSTANTS
// ============================================================================

/** @brief Static exception type: RuntimeException */
const char *EXCEPTION_TYPE_RUNTIME = "RuntimeException";

/** @brief Static exception type: ParseError */
const char *EXCEPTION_TYPE_PARSE = "ParseError";

/** @brief Static exception type: IllegalArgumentException */
const char *EXCEPTION_TYPE_ILLEGAL_ARGUMENT = "IllegalArgumentException";

/** @brief Static exception type: ArityException */
const char *EXCEPTION_TYPE_ARITY = "ArityException";

/** @brief Static exception type: TypeError */
const char *EXCEPTION_TYPE_TYPE = "TypeError";

/** @brief Static exception type: OutOfMemoryError */
const char *EXCEPTION_TYPE_OUT_OF_MEMORY = "OutOfMemoryError";

/** @brief Static exception type: StackOverflowError */
const char *EXCEPTION_TYPE_STACK_OVERFLOW = "StackOverflowError";

/** @brief Static exception type: DivisionByZeroError */
const char *EXCEPTION_TYPE_DIVISION_BY_ZERO = "DivisionByZeroError";

/** @brief Static exception type: ArithmeticException */
const char *EXCEPTION_ARITHMETIC = "ArithmeticException";

// ============================================================================
// ERROR MESSAGE CONSTANTS (consolidated from error_messages.c)
// ============================================================================

/** @brief Error message: Expected number */
const char *ERR_EXPECTED_NUMBER = "Expected number";

/** @brief Error message: Wrong number of args: 0 */
const char *ERR_WRONG_ARITY_ZERO = "Wrong number of args: 0";

/** @brief Error message: Divide by zero */
const char *ERR_DIVIDE_BY_ZERO = "Divide by zero";

/** @brief Error message: Integer overflow in addition */
const char *ERR_INTEGER_OVERFLOW_ADDITION = "Integer overflow in addition: %d + %d would exceed INT_MAX";

/** @brief Error message: Integer underflow in addition */
const char *ERR_INTEGER_UNDERFLOW_ADDITION = "Integer underflow in addition: %d + %d would exceed INT_MIN";

/** @brief Error message: Integer overflow in subtraction */
const char *ERR_INTEGER_OVERFLOW_SUBTRACTION = "Integer overflow in subtraction: %d - %d would exceed INT_MAX";

/** @brief Error message: Integer underflow in subtraction */
const char *ERR_INTEGER_UNDERFLOW_SUBTRACTION = "Integer underflow in subtraction: %d - %d would exceed INT_MIN";

/** @brief Error message: Integer overflow in multiplication */
const char *ERR_INTEGER_OVERFLOW_MULTIPLICATION = "Integer overflow in multiplication: %d * %d would exceed INT_MAX";

/** @brief Error message: Fixed-point overflow in multiplication */
const char *ERR_FIXED_OVERFLOW_MULTIPLICATION = "Fixed-point overflow in multiplication would exceed representable range";

/** @brief Error message: Fixed-point overflow in addition */
const char *ERR_FIXED_OVERFLOW_ADDITION = "Fixed-point overflow in addition would exceed representable range";

// ============================================================================
// EXCEPTION THROWING FUNCTIONS
// ============================================================================

/**
 * Convenience function for throwing exceptions with printf-style formatting
 * 
 * @param type Exception type (NULL for generic "RuntimeException")
 * @param file Source file name (use __FILE__)
 * @param line Line number (use __LINE__)
 * @param code Error code (use 0 for most cases)
 * @param format printf-style format string
 * @param ... Variable arguments for formatting
 */
void throw_exception_formatted(const char *type, const char *file, int line, int code, 
                              const char *format, ...) {
    char message[512];  // Increased buffer size for longer messages
    va_list args;
    
    va_start(args, format);
    int result = vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    // Additional safety: ensure null termination if message was truncated
    if (result >= (int)sizeof(message)) {
        // Message was truncated - ensure null termination
        message[sizeof(message)-1] = '\0';
    }
    
    // Shorten file path to show only from /src/ onwards
    const char *short_file = file;
    const char *src_pos = strstr(file, "/src/");
    if (src_pos) {
        short_file = src_pos + 1; // Skip the leading "/"
    }
    
    // Use generic RuntimeException if type is NULL
    const char *exception_type = (type != NULL) ? type : EXCEPTION_TYPE_RUNTIME;
    
    // Create exception and use the unified function
    CLJException *exception = make_exception(exception_type, message, short_file, line, code);
    if (!exception) {
#ifdef DEBUG
        fprintf(stderr, "FAILED TO ALLOCATE FORMATTED EXCEPTION\n");
#endif
        exit(1);
    }
    
    throw_exception_object(exception);
}

/** @brief Throw an exception with type, message, and location */
void throw_exception(const char *type, const char *message, const char *file, int line, int col) {
    CLJException *exception = make_exception(type, message, file, line, col);
    if (!exception) {
#ifdef DEBUG
        fprintf(stderr, "FAILED TO ALLOCATE EXCEPTION\n");
#endif
        exit(1);
    }
    
    // Use the new unified function
    throw_exception_object(exception);
}

/** @brief Re-throw an existing exception object */
void throw_exception_object(CLJException *ex) {
    if (!ex) {
#ifdef DEBUG
        fprintf(stderr, "FAILED TO RE-THROW NULL EXCEPTION\n");
#endif
        exit(1);
    }
    
    if (!global_exception_stack.top) {
        // No handler - unhandled exception
#ifdef DEBUG
        char *str = to_string((CljObject*)ex); 
        fprintf(stderr, "UNHANDLED: %s\n", str ? str : "<null>");
        free(str);
#endif
        free(ex); exit(1);
    }
    
    // Store existing exception in thread-local variable
    // Note: We don't create a new exception, we reuse the existing one
    g_current_exception = ex;
    longjmp(global_exception_stack.top->jump_state, 1);
}

// ============================================================================
// STANDARD ERROR MESSAGES
// ============================================================================

/** @brief Standard error message: EOF while reading vector */
const char *ERROR_EOF_VECTOR = "EOF while reading vector";

/** @brief Standard error message: EOF while reading map */
const char *ERROR_EOF_MAP = "EOF while reading map";

/** @brief Standard error message: EOF while reading list */
const char *ERROR_EOF_LIST = "EOF while reading list";

/** @brief Standard error message: Unmatched delimiter */
const char *ERROR_UNMATCHED_DELIMITER = "Unmatched delimiter";

/** @brief Standard error message: Division by zero */
const char *ERROR_DIVISION_BY_ZERO = "Division by zero";

/** @brief Standard error message: Invalid syntax */
const char *ERROR_INVALID_SYNTAX = "Invalid syntax";

/** @brief Standard error message: Undefined variable */
const char *ERROR_UNDEFINED_VARIABLE = "Undefined variable";

/** @brief Standard error message: Type mismatch */
const char *ERROR_TYPE_MISMATCH = "Type mismatch";

/** @brief Standard error message: Stack overflow */
const char *ERROR_STACK_OVERFLOW = "Stack overflow";

/** @brief Standard error message: Memory allocation failed */
const char *ERROR_MEMORY_ALLOCATION = "Memory allocation failed";

/**
 * @brief Create exception with standard error message.
 * @param msg Error message string
 * @param file Source file name
 * @param line Line number
 * @param col Column number
 * @return New exception object or NULL on failure
 */
CLJException* exception(const char *msg, const char *file, int line, int col) {
    return make_exception("Error", msg, file, line, col);
}

