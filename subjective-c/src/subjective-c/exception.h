/**
 * @file exception.h
 * @brief Exception handling system with TRY/CATCH macros and assertion functions.
 */

#ifndef SUBJECTIVE_C_EXCEPTION_H
#define SUBJECTIVE_C_EXCEPTION_H

#include "object.h"
#include "memory.h"
#include "common.h"
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

// Forward declaration for CljString
struct CljString;

// CLJException struct definition
// Release: smaller buffers to save RAM (ESP32 etc.); DEBUG keeps full sizes for stacktraces/debugging.
typedef struct {
    CljObject base;
#ifdef DEBUG
    char type[64];
    char message[256];
    char file[128];
#else
    char type[32];
    char message[128];
    char file[64];
#endif
    int line;
    int col;
#ifdef DEBUG
    struct CljString *stacktrace;  // Stacktrace as CljString (can be NULL)
    uintptr_t object;              // Address-only (do not dereference, not retained); 0 if unset
#endif
} CLJException;

// Type-safe casting
static inline CLJException* as_exception(ID obj) {
    return (CLJException*)assert_type((CljObject*)obj, CLJ_EXCEPTION);
}

/** Create a CLJException object (rc=1) with optional data. */
CLJException* make_exception(const char *type, const char *message, const char *file, int line, int col);

/** Print exception details including stacktrace and object (if available).
 *  @param ex Exception to print
 *  @note Only available in DEBUG builds (stacktrace and object fields)
 */
void print_exception(CLJException *ex);

/** Print native stack trace to stderr (used by CLJ_ASSERT). */
void exception_print_native_backtrace(void);

// Exception throwing functions
/** Throw exception via longjmp; transfers ownership to runtime. */
void throw_exception(const char *type, const char *message, const char *file, int line, int col);
/** Throw exception with printf-style formatting; never returns (longjmp). */
void throw_exception_formatted(const char *type, const char *file, int line, int col, const char *format, ...);
/** Re-throw existing exception object; transfers ownership to runtime. */
void throw_exception_object(CLJException *ex);

// ============================================================================
// STATIC OUT OF MEMORY EXCEPTION
// ============================================================================

/** Get static OOM exception (no allocation needed).
 *  This is critical: when we're out of memory, we can't allocate more memory.
 */
extern CLJException *clj_oom_exception;

// ============================================================================
// GLOBAL EXCEPTION STACK (independent of EvalState)
// ============================================================================

// Forward declaration to avoid circular dependency with memory.h
// Forward declaration for autorelease pool vector.
struct CljPersistentVector;

/**
 * @brief Exception handler for TRY/CATCH blocks.
 * Contains jump state and linked list structure for exception handling.
 */
typedef struct ExceptionHandler {
    jmp_buf jump_state;                  // Jump target for longjmp
    struct ExceptionHandler *next;       // Previous handler (stack)
    struct CljPersistentVector *pool;   // Autorelease pool (weak vector) for cleanup after longjmp
    CLJException *exception;             // Exception stored in handler (replaces g_current_exception)
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

#if MEMORY_PROFILING_ENABLED
// -----------------------------------------------------------------------------
// Internal allocations for TRY/CATCH handler nodes (profiling-enabled builds)
// -----------------------------------------------------------------------------
//
// We intentionally keep TRY/CATCH handler allocation "no-throw" (abort on OOM),
// because failing to set up exception handling is a fatal condition.
//

static inline ExceptionHandler* exception_handler_alloc_or_abort(void) {
    ExceptionHandler *h = (ExceptionHandler*)CLJ_MALLOC(sizeof(ExceptionHandler));
    if (!h) {
        fputs("FATAL: CLJ_MALLOC failed in TRY block\n", stderr);
        abort();
    }
    return h;
}

static inline void exception_handler_free(ExceptionHandler *h) {
    if (!h) return;
    CLJ_FREE(h);
}

#define TRY { \
    ExceptionHandler *_h = exception_handler_alloc_or_abort(); \
    _h->next = global_exception_stack.top; \
    _h->exception = NULL; \
    global_exception_stack.top = _h; \
    if (setjmp(_h->jump_state) == 0) {

#define CATCH(ex) \
        /* Success path: pop stack only */ \
        ExceptionHandler *_success_handler = global_exception_stack.top; \
        global_exception_stack.top = _success_handler->next; \
        exception_handler_free(_success_handler); \
    } else { \
        /* Exception path: get exception from handler */ \
        ExceptionHandler *_caught_h = global_exception_stack.top; \
        CLJException *ex = _caught_h ? _caught_h->exception : NULL; \
        global_exception_stack.top = _caught_h->next; \
        exception_handler_free(_caught_h); \
        if (ex) { \
            /* Exception will be manually released in END_TRY */

#else
// -----------------------------------------------------------------------------
// TRY/CATCH handler nodes (production builds: no profiling instrumentation)
// -----------------------------------------------------------------------------

#define TRY { \
    ExceptionHandler *_h = (ExceptionHandler*)CLJ_MALLOC(sizeof(ExceptionHandler)); \
    if (!_h) { \
        fputs("FATAL: CLJ_MALLOC failed in TRY block\n", stderr); \
        abort(); \
    } \
    _h->next = global_exception_stack.top; \
    _h->exception = NULL; \
    global_exception_stack.top = _h; \
    if (setjmp(_h->jump_state) == 0) {

#define CATCH(ex) \
        /* Success path: pop stack only */ \
        ExceptionHandler *_success_handler = global_exception_stack.top; \
        global_exception_stack.top = _success_handler->next; \
        CLJ_FREE(_success_handler); \
    } else { \
        /* Exception path: get exception from handler */ \
        ExceptionHandler *_caught_h = global_exception_stack.top; \
        CLJException *ex = _caught_h ? _caught_h->exception : NULL; \
        global_exception_stack.top = _caught_h->next; \
        CLJ_FREE(_caught_h); \
        if (ex) { \
            /* Exception will be manually released in END_TRY */

#endif // MEMORY_PROFILING_ENABLED

#define END_TRY \
        } \
        /* Exception was AUTORELEASE'd when thrown; pool will release. Do not RELEASE(ex) here. */ \
    } \
}

/** @brief Re-throw existing exception object (convenience macro) */
#define THROW(ex) throw_exception_object(ex)

// Internal helper macro for arity checks (DRY)
#define _CHECK_ARITY_IMPL(condition, func_name, fmt, ...) \
    do { \
        if (condition) { \
            throw_exception_formatted(EXCEPTION_ARITY, __FILE__, __LINE__, 0, \
                                     fmt, func_name, __VA_ARGS__); \
            return NULL; \
        } \
    } while(0)

/** @brief Check arity and throw ArityException if mismatch.
 *  @param argc Actual argument count
 *  @param expected Expected argument count
 *  @param func_name Function name for error message
 *  @return NULL if arity mismatch (exception thrown), otherwise no return value
 *  @note Use as: CHECK_ARITY(argc, expected, "func_name");
 *  @note The macro performs the check internally and returns NULL if mismatch
 */
#define CHECK_ARITY(argc, expected, func_name) \
    _CHECK_ARITY_IMPL((argc) != (expected), func_name, \
                     "%s requires %u argument%s, got %u", \
                     expected, ((expected) == 1 ? "" : "s"), argc)

/** @brief Check maximum arity and throw ArityException if exceeded.
 *  @param argc Actual argument count
 *  @param max Maximum allowed argument count
 *  @param func_name Function name for error message
 *  @return NULL if arity exceeded (exception thrown), otherwise no return value
 */
#define CHECK_ARITY_MAX(argc, max, func_name) \
    _CHECK_ARITY_IMPL((argc) > (max), func_name, \
                     "%s accepts at most %u argument%s, got %u", \
                     max, ((max) == 1 ? "" : "s"), argc)

/** @brief Check minimum arity and throw ArityException if not met.
 *  @param argc Actual argument count
 *  @param min Minimum required argument count
 *  @param func_name Function name for error message
 *  @return NULL if arity not met (exception thrown), otherwise no return value
 */
#define CHECK_ARITY_MIN(argc, min, func_name) \
    _CHECK_ARITY_IMPL((argc) < (min), func_name, \
                     "%s requires at least %u argument%s, got %u", \
                     min, ((min) == 1 ? "" : "s"), argc)

/** @brief Check arity range and throw ArityException if out of range.
 *  @param argc Actual argument count
 *  @param min Minimum required argument count
 *  @param max Maximum allowed argument count
 *  @param func_name Function name for error message
 *  @return NULL if arity out of range (exception thrown), otherwise no return value
 */
#define CHECK_ARITY_RANGE(argc, min, max, func_name) \
    do { \
        _CHECK_ARITY_IMPL((argc) < (min) || (argc) > (max), func_name, \
                         "%s requires %u-%u arguments, got %u", \
                         min, max, argc); \
    } while(0)

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
extern const char *EXCEPTION_EOF_VECTOR;
/** @brief Standard error message: EOF while reading map */
extern const char *EXCEPTION_EOF_MAP;
/** @brief Standard error message: EOF while reading list */
extern const char *EXCEPTION_EOF_LIST;
/** @brief Standard error message: Unmatched delimiter */
extern const char *EXCEPTION_UNMATCHED_DELIMITER;
/** @brief Standard error message: Division by zero */
extern const char *EXCEPTION_DIVISION_BY_ZERO;
/** @brief Standard error message: Invalid syntax */
extern const char *EXCEPTION_INVALID_SYNTAX;
/** @brief Standard error message: Undefined variable */
extern const char *EXCEPTION_UNDEFINED_VARIABLE;
/** @brief Standard error message: Type mismatch */
extern const char *EXCEPTION_TYPE_MISMATCH;
/** @brief Standard error message: Stack overflow */
extern const char *EXCEPTION_STACK_OVERFLOW;
/** @brief Standard error message: Memory allocation failed */
extern const char *EXCEPTION_MEMORY_ALLOCATION;

// ============================================================================
// STATIC EXCEPTION TYPE CONSTANTS
// ============================================================================

/** @brief Static exception type: RuntimeException */
extern const char *EXCEPTION_RUNTIME;

/** @brief Static exception type: ParseError */
extern const char *EXCEPTION_PARSE;

/** @brief Static exception type: IllegalArgumentException */
extern const char *EXCEPTION_ILLEGAL_ARGUMENT;

/** @brief Static exception type: ArityException */
extern const char *EXCEPTION_ARITY;

/** @brief Static exception type: TypeError */
extern const char *EXCEPTION_TYPE;

/** @brief Static exception type: OutOfMemoryError */
extern const char *EXCEPTION_OUT_OF_MEMORY;

/** @brief Static exception type: StackOverflowError */
extern const char *EXCEPTION_STACK_OVERFLOW;

/** @brief Static exception type: DivisionByZeroError */
extern const char *EXCEPTION_DIVISION_BY_ZERO;

/** @brief Static exception type: ZombieAccessException */
extern const char *EXCEPTION_ZOMBIE_ACCESS;

/** @brief Static exception type: IndexOutOfBoundsException */
extern const char *EXCEPTION_INDEX_OUT_OF_BOUNDS;

/** @brief Static exception type: FileNotFoundException */
extern const char *EXCEPTION_FILE_NOT_FOUND;

/** @brief Static exception type: Generic Error */
extern const char *EXCEPTION_ERROR;

#endif // SUBJECTIVE_C_EXCEPTION_H
