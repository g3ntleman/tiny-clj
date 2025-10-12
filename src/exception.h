/**
 * @file exception.h
 * @brief Exception handling system with TRY/CATCH macros and assertion functions.
 */

#ifndef EXCEPTION_H
#define EXCEPTION_H
#include "object.h"
#include <setjmp.h>
#include <stdbool.h>

// CLJException is now defined in CljObject.h to avoid circular dependencies

// ============================================================================
// GLOBAL EXCEPTION STACK (independent of EvalState)
// ============================================================================

/**
 * @brief Exception handler for TRY/CATCH blocks.
 * Contains jump state and linked list structure for exception handling.
 */
typedef struct ExceptionHandler {
    jmp_buf jump_state;                  // Jump target for longjmp
    struct ExceptionHandler *next;       // Previous handler (stack)
    CLJException *exception;             // Caught exception
} ExceptionHandler;

/**
 * @brief Global exception stack (thread-local if needed).
 * Manages the stack of exception handlers and current exception state.
 */
typedef struct GlobalExceptionStack {
    ExceptionHandler *top;               // Top of exception handler stack
    CLJException *current_exception;    // Current exception being handled
} GlobalExceptionStack;

/** @brief Global exception stack instance. */
extern GlobalExceptionStack global_exception_stack;

// ============================================================================
// TRY/CATCH MACROS (Objective-C style, efficient by design)
// ============================================================================

// Usage:
//   TRY {
//       risky_code();
//   } CATCH(ex) {
//       handle_error(ex);
//       // Exception is automatically released!
//   } END_TRY
//
// Features:
// - Exception auto-released (no manual release_exception call)
// - Supports nesting (exception handler stack)
// - Supports re-throw (throw_exception in CATCH goes to outer handler)
// - Pure C99, embedded-friendly
// - Simple, debuggable macro expansion
//
// Note: END_TRY is required (like Objective-C NS_ENDHANDLER)

#define TRY { \
    ExceptionHandler _h = {.next = global_exception_stack.top, .exception = NULL}; \
    global_exception_stack.top = &_h; \
    if (setjmp(_h.jump_state) == 0) {

#define CATCH(ex) \
        global_exception_stack.top = _h.next; \
    } else { \
        CLJException *ex = _h.exception; \
        global_exception_stack.top = _h.next; \
        if (ex) { \
            global_exception_stack.current_exception = ex; \

#define END_TRY \
            release_exception(ex); \
            global_exception_stack.current_exception = NULL; \
        } \
    } \
}

/**
 * @brief Create exception with standard error message.
 * @param msg Error message string
 * @param file Source file name
 * @param line Line number
 * @param col Column number
 * @return New exception object or NULL on failure
 */
CLJException* exception(const char *msg, const char *file, int line, int col);

/**
 * @brief Create exception with dynamic error message.
 * @param msg Error message string (will be duplicated)
 * @param file Source file name
 * @param line Line number
 * @param col Column number
 * @return New exception object or NULL on failure
 */
CLJException* exception_dynamic(const char *msg, const char *file, int line, int col);

// ============================================================================
// ASSERTION FUNCTIONS (Clojure Core API)
// ============================================================================

/**
 * @brief Assert with message - throws exception if condition is false.
 * @param condition Boolean condition to check
 * @param message Error message if assertion fails
 */
void clj_assert(bool condition, const char *message);

/**
 * @brief Assert with message and file location.
 * @param condition Boolean condition to check
 * @param message Error message if assertion fails
 * @param file Source file name
 * @param line Line number
 * @param col Column number
 */
void clj_assert_with_location(bool condition, const char *message, const char *file, int line, int col);

/**
 * @brief Assert-args for function parameter validation.
 * @param function_name Name of the function for error reporting
 * @param condition Boolean condition to check
 * @param message Error message if assertion fails
 */
void clj_assert_args(const char *function_name, bool condition, const char *message);

/**
 * @brief Assert-args with multiple conditions.
 * @param function_name Name of the function for error reporting
 * @param condition_count Number of conditions to check
 * @param ... Variable arguments: condition1, message1, condition2, message2, ...
 */
void clj_assert_args_multiple(const char *function_name, int condition_count, ...);

/** @brief Standard error message: EOF while reading vector */
extern const char *ERROR_EOF_VECTOR;
/** @brief Standard error message: EOF while reading map */
extern const char *ERROR_EOF_MAP;
/** @brief Standard error message: EOF while reading list */
extern const char *ERROR_EOF_LIST;
/** @brief Standard error message: Unmatched delimiter */
extern const char *ERROR_UNMATCHED_DELIMITER;
/** @brief Standard error message: Division by zero */
extern const char *ERROR_DIVISION_BY_ZERO;
/** @brief Standard error message: Invalid syntax */
extern const char *ERROR_INVALID_SYNTAX;
/** @brief Standard error message: Undefined variable */
extern const char *ERROR_UNDEFINED_VARIABLE;
/** @brief Standard error message: Type mismatch */
extern const char *ERROR_TYPE_MISMATCH;
/** @brief Standard error message: Stack overflow */
extern const char *ERROR_STACK_OVERFLOW;
/** @brief Standard error message: Memory allocation failed */
extern const char *ERROR_MEMORY_ALLOCATION;

#endif
