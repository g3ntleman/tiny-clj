#ifndef EXCEPTION_H
#define EXCEPTION_H
#include "object.h"
#include <setjmp.h>
#include <stdbool.h>

// CLJException is now defined in CljObject.h to avoid circular dependencies

// ============================================================================
// GLOBAL EXCEPTION STACK (independent of EvalState)
// ============================================================================

// Exception handler for TRY/CATCH blocks
typedef struct ExceptionHandler {
    jmp_buf jump_state;                  // Jump target for longjmp
    struct ExceptionHandler *next;       // Previous handler (stack)
    CLJException *exception;             // Caught exception
} ExceptionHandler;

// Global exception stack (thread-local if needed)
typedef struct GlobalExceptionStack {
    ExceptionHandler *top;               // Top of exception handler stack
    CLJException *current_exception;    // Current exception being handled
} GlobalExceptionStack;

// Global exception stack instance
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
