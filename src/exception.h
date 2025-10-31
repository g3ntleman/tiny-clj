/**
 * @file exception.h
 * @brief Exception handling system with TRY/CATCH macros and assertion functions.
 */

#ifndef EXCEPTION_H
#define EXCEPTION_H
#include "object.h"
#include "memory.h"
#include <setjmp.h>
#include <stdbool.h>
#include <stdlib.h>

// CLJException struct definition
typedef struct {
    CljObject base;
    char type[64];
    char message[256];
    char file[128];
    int line;
    int col;
} CLJException;

// Type-safe casting
static inline CLJException* as_exception(ID obj) {
    return (CLJException*)assert_type((CljObject*)obj, CLJ_EXCEPTION);
}

/** Create a CLJException object (rc=1) with optional data. */
CLJException* make_exception(const char *type, const char *message, const char *file, int line, int col);

// Exception throwing functions
/** Throw exception via longjmp; transfers ownership to runtime. */
void throw_exception(const char *type, const char *message, const char *file, int line, int col);
/** Throw exception with printf-style formatting; transfers ownership to runtime. */
void throw_exception_formatted(const char *type, const char *file, int line, int col, const char *format, ...);
/** Re-throw existing exception object; transfers ownership to runtime. */
void throw_exception_object(CLJException *ex);

// ============================================================================
// GLOBAL EXCEPTION STACK (independent of EvalState)
// ============================================================================

// Global storage for current exception (defined in exception.c)
extern CLJException *g_current_exception;

// Forward declaration to avoid circular dependency with memory.h
// Note: CljObjectPool is typedef'd in memory.h
struct CljObjectPool;

/**
 * @brief Exception handler for TRY/CATCH blocks.
 * Contains jump state and linked list structure for exception handling.
 */
typedef struct ExceptionHandler {
    jmp_buf jump_state;                  // Jump target for longjmp
    struct ExceptionHandler *next;       // Previous handler (stack)
    CljObjectPool *pool;                 // Autorelease pool for cleanup after longjmp
} ExceptionHandler;

/**
 * @brief Global exception stack (thread-local if needed).
 * Manages the stack of exception handlers and current exception state.
 */
typedef struct GlobalExceptionStack {
    ExceptionHandler *top;               // Top of exception handler stack
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
    ExceptionHandler *_h = (ExceptionHandler*)malloc(sizeof(ExceptionHandler)); \
    _h->next = global_exception_stack.top; \
    global_exception_stack.top = _h; \
    if (setjmp(_h->jump_state) == 0) {

#define CATCH(ex) \
        /* Success path: pop stack only */ \
        ExceptionHandler *_success_handler = global_exception_stack.top; \
        global_exception_stack.top = _success_handler->next; \
        free(_success_handler); \
    } else { \
        /* Exception path: get exception from global */ \
        extern CLJException *g_current_exception; \
        ExceptionHandler *_caught_h = global_exception_stack.top; \
        CLJException *ex = g_current_exception; \
        global_exception_stack.top = _caught_h->next; \
        free(_caught_h); \
        if (ex) { \
            /* Exception will be manually released in END_TRY */ \

#define END_TRY \
        } \
        /* Exception manually released */ \
        if (ex) { \
            RELEASE((CljObject*)ex); \
        } \
    } \
}

/** @brief Re-throw existing exception object (convenience macro) */
#define THROW(ex) throw_exception_object(ex)

/**
 * @brief Create exception with standard error message.
 * @param msg Error message string
 * @param file Source file name
 * @param line Line number
 * @param col Column number
 * @return New exception object or NULL on failure
 */
CLJException* exception(const char *msg, const char *file, int line, int col);


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

// ============================================================================
// STATIC EXCEPTION TYPE CONSTANTS
// ============================================================================

/** @brief Static exception type: RuntimeException */
extern const char *EXCEPTION_TYPE_RUNTIME;

/** @brief Static exception type: ParseError */
extern const char *EXCEPTION_TYPE_PARSE;

/** @brief Static exception type: IllegalArgumentException */
extern const char *EXCEPTION_TYPE_ILLEGAL_ARGUMENT;

/** @brief Static exception type: ArityException */
extern const char *EXCEPTION_TYPE_ARITY;

/** @brief Static exception type: TypeError */
extern const char *EXCEPTION_TYPE_TYPE;

/** @brief Static exception type: OutOfMemoryError */
extern const char *EXCEPTION_TYPE_OUT_OF_MEMORY;

/** @brief Static exception type: StackOverflowError */
extern const char *EXCEPTION_TYPE_STACK_OVERFLOW;

/** @brief Static exception type: DivisionByZeroError */
extern const char *EXCEPTION_TYPE_DIVISION_BY_ZERO;

/** @brief Static exception type: ArithmeticException */
extern const char *EXCEPTION_ARITHMETIC;

// Short aliases (drop _TYPE_ for brevity)
#define EXCEPTION_RUNTIME EXCEPTION_TYPE_RUNTIME
#define EXCEPTION_PARSE EXCEPTION_TYPE_PARSE
#define EXCEPTION_ILLEGAL_ARGUMENT EXCEPTION_TYPE_ILLEGAL_ARGUMENT
#define EXCEPTION_ARITY EXCEPTION_TYPE_ARITY
#define EXCEPTION_TYPE EXCEPTION_TYPE_TYPE
#define EXCEPTION_OUT_OF_MEMORY EXCEPTION_TYPE_OUT_OF_MEMORY
#define EXCEPTION_STACK_OVERFLOW EXCEPTION_TYPE_STACK_OVERFLOW
#define EXCEPTION_DIVISION_BY_ZERO EXCEPTION_TYPE_DIVISION_BY_ZERO

// ============================================================================
// ERROR MESSAGE CONSTANTS (moved from error_messages.h)
// ============================================================================

/** @brief Error message: Expected number */
extern const char *ERR_EXPECTED_NUMBER;

/** @brief Error message: Wrong number of args: 0 */
extern const char *ERR_WRONG_ARITY_ZERO;

/** @brief Error message: Divide by zero */
extern const char *ERR_DIVIDE_BY_ZERO;

/** @brief Error message: Integer overflow in addition */
extern const char *ERR_INTEGER_OVERFLOW_ADDITION;

/** @brief Error message: Integer underflow in addition */
extern const char *ERR_INTEGER_UNDERFLOW_ADDITION;

/** @brief Error message: Integer overflow in subtraction */
extern const char *ERR_INTEGER_OVERFLOW_SUBTRACTION;

/** @brief Error message: Integer underflow in subtraction */
extern const char *ERR_INTEGER_UNDERFLOW_SUBTRACTION;

/** @brief Error message: Integer overflow in multiplication */
extern const char *ERR_INTEGER_OVERFLOW_MULTIPLICATION;

/** @brief Error message: Fixed-point overflow in multiplication */
extern const char *ERR_FIXED_OVERFLOW_MULTIPLICATION;

/** @brief Error message: Fixed-point overflow in addition */
extern const char *ERR_FIXED_OVERFLOW_ADDITION;

#endif
