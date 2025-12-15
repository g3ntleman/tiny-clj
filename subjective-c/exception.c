/**
 * @file exception.c
 * @brief Implementation of exception handling system with standard error messages.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <setjmp.h>
#include "exception.h"
#include "error_messages.h"
#include "object.h"
#include "memory.h"
#include "strings.h"  // For to_cstring
#include "value.h"  // For make_string
#include "strings.h"  // For CljString
#include "to_string.h"  // For to_string

// Stacktrace support
#ifdef __APPLE__
#include <execinfo.h>
#elif defined(__linux__)
#include <execinfo.h>
#endif

// Safe string copy helper
static inline void safe_strncpy(char *dest, const char *src, size_t dest_size) {
    if (!dest || !src || dest_size == 0) return;
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

// Shorten file path to show only from /src/ onwards
static inline const char* shorten_file_path(const char *file) {
    if (!file) return "";
    const char *src_pos = strstr(file, "/src/");
    if (src_pos) {
        return src_pos + 1; // Skip the leading "/"
    }
    return file;
}

// Forward declaration for stacktrace function
#ifdef DEBUG
struct CljString* stacktrace(void);
#endif

// Global exception stack (independent of EvalState)
GlobalExceptionStack global_exception_stack = {0};

// ============================================================================
// EXCEPTION CREATION
// ============================================================================

/** @brief Create exception with reference counting */
CLJException* make_exception(const char *type, const char *message, const char *file, int line, int col) {
    if (!type || !message) return NULL;
    
    CLJException *exc = ALLOC(CLJException, 1);
    if (!exc) return NULL;
    
    // Initialize base object
    exc->base.type = CLJ_EXCEPTION;
    exc->base.rc = 1;  // Start with reference count 1
    
    // Copy strings directly into the structure (no strdup needed)
    safe_strncpy(exc->type, type, sizeof(exc->type));
    safe_strncpy(exc->message, message, sizeof(exc->message));
    safe_strncpy(exc->file, file ? file : "", sizeof(exc->file));
    
    exc->line = line;
    exc->col = col;
    
#ifdef DEBUG
    // Always generate stacktrace in DEBUG builds
    exc->stacktrace = stacktrace();  // Can be NULL on error
    exc->object = NULL;  // Initialize to NULL
#else
    // Release builds: no stacktrace field
#endif
    
    return exc;
}

// ============================================================================
// STATIC EXCEPTION TYPE CONSTANTS
// ============================================================================

/** @brief Static exception type: RuntimeException */
const char *EXCEPTION_RUNTIME = "RuntimeException";

/** @brief Static exception type: ParseError */
const char *EXCEPTION_PARSE = "ParseError";

/** @brief Static exception type: IllegalArgumentException */
const char *EXCEPTION_ILLEGAL_ARGUMENT = "IllegalArgumentException";

/** @brief Static exception type: ArityException */
const char *EXCEPTION_ARITY = "ArityException";

/** @brief Static exception type: TypeError */
const char *EXCEPTION_TYPE = "TypeError";

/** @brief Static exception type: OutOfMemoryError */
const char *EXCEPTION_OUT_OF_MEMORY = "OutOfMemoryError";

/** @brief Static exception type: IndexOutOfBoundsException */
const char *EXCEPTION_INDEX_OUT_OF_BOUNDS = "IndexOutOfBoundsException";

/** @brief Static exception type: FileNotFoundException */
const char *EXCEPTION_FILE_NOT_FOUND = "FileNotFoundException";

// ============================================================================
// STATIC OUT OF MEMORY EXCEPTION (no allocation needed)
// ============================================================================

// Static OOM exception singleton - statically initialized, never freed
// This is critical: when we're out of memory, we can't allocate more memory
static CLJException clj_oom_exception_data = {
    .base = { .type = CLJ_EXCEPTION, .rc = SINGLETON_RC },
    .type = "OutOfMemoryError",
    .message = "Out of memory",
    .file = "",
    .line = 0,
    .col = 0
#ifdef DEBUG
    , .stacktrace = NULL,
    .object = NULL
#endif
};

CLJException *clj_oom_exception = &clj_oom_exception_data;

/** @brief Static exception type: StackOverflowError */
const char *EXCEPTION_STACK_OVERFLOW = "StackOverflowError";

/** @brief Static exception type: DivisionByZeroError */
const char *EXCEPTION_DIVISION_BY_ZERO = "DivisionByZeroError";

/** @brief Static exception type: ZombieAccessException */
const char *EXCEPTION_ZOMBIE_ACCESS = "ZombieAccessException";

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
void* throw_exception_formatted(const char *type, const char *file, int line, int code, 
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
    
    // Use generic RuntimeException if type is NULL
    const char *exception_type = (type != NULL) ? type : EXCEPTION_RUNTIME;
    
    // Create exception and use the unified function (file path will be shortened in print_exception)
    CLJException *exception = make_exception(exception_type, message, file, line, code);
    if (!exception) {
#ifdef DEBUG
        fprintf(stderr, "FAILED TO ALLOCATE FORMATTED EXCEPTION\n");
#endif
        exit(1);
    }
    
    throw_exception_object(exception);
    return NULL;  // Never reached (longjmp), but allows return throw_exception_formatted(...);
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

/** @brief Generate stacktrace as CljString
 *  @return CljString* with stacktrace or NULL on error/in Release builds
 *  @note Only available in DEBUG builds
 */
#ifdef DEBUG
struct CljString* stacktrace(void) {
#ifdef __APPLE__
    void *array[32];
    size_t size = backtrace(array, 32);
    char **symbols = backtrace_symbols(array, size);
    
    if (!symbols || size == 0) {
        if (symbols) free(symbols);
        return NULL;
    }
    
    // Calculate total length needed
    size_t total_len = 0;
    for (size_t i = 0; i < size; i++) {
        if (symbols[i]) {
            total_len += strlen(symbols[i]);
            total_len += 1;  // newline
        }
    }
    
    // Allocate buffer for stacktrace string
    char *buffer = (char*)malloc(total_len + 1);
    if (!buffer) {
        free(symbols);
        return NULL;
    }
    
    // Build stacktrace string, skipping the last line (usually contains "dyld" on macOS)
    size_t pos = 0;
    size_t last_index = (size > 0) ? size - 1 : 0;
    for (size_t i = 0; i < last_index; i++) {
        if (symbols[i]) {
            size_t len = strlen(symbols[i]);
            memcpy(buffer + pos, symbols[i], len);
            pos += len;
            buffer[pos++] = '\n';
        }
    }
    buffer[pos] = '\0';
    
    free(symbols);
    
    // Create CljString from buffer
    struct CljString *result = make_string(buffer);
    free(buffer);
    
    return result;
#elif defined(__linux__)
    void *array[32];
    size_t size = backtrace(array, 32);
    char **symbols = backtrace_symbols(array, size);
    
    if (!symbols || size == 0) {
        if (symbols) free(symbols);
        return NULL;
    }
    
    // Calculate total length needed
    size_t total_len = 0;
    for (size_t i = 0; i < size; i++) {
        if (symbols[i]) {
            total_len += strlen(symbols[i]);
            total_len += 1;  // newline
        }
    }
    
    // Allocate buffer for stacktrace string
    char *buffer = (char*)malloc(total_len + 1);
    if (!buffer) {
        free(symbols);
        return NULL;
    }
    
    // Build stacktrace string, skipping the last line (usually contains "dyld" on macOS)
    size_t pos = 0;
    size_t last_index = (size > 0) ? size - 1 : 0;
    for (size_t i = 0; i < last_index; i++) {
        if (symbols[i]) {
            size_t len = strlen(symbols[i]);
            memcpy(buffer + pos, symbols[i], len);
            pos += len;
            buffer[pos++] = '\n';
        }
    }
    buffer[pos] = '\0';
    
    free(symbols);
    
    // Create CljString from buffer
    struct CljString *result = make_string(buffer);
    free(buffer);
    
    return result;
#else
    // No stacktrace support on this platform
    return NULL;
#endif
}
#else
// Release builds: return NULL
struct CljString* stacktrace(void) {
    return NULL;
}
#endif

void exception_print_native_backtrace(void) {
#if defined(DEBUG) && !defined(ESP32_BUILD)
#if defined(__APPLE__) || defined(__linux__)
    void *array[20];
    int size = backtrace(array, 20);
    if (size <= 0) {
        return;
    }

    char **strings = backtrace_symbols(array, size);
    if (!strings) {
        return;
    }

    for (int i = 0; i < size; i++) {
        if (strings[i]) {
            fprintf(stderr, "  %d: %s\n", i, strings[i]);
        }
    }

    free(strings);
#else
    // Platform without execinfo support
#endif
#else
    // Release builds / ESP32 do not emit backtraces here
#endif
}

// print_stacktrace() removed - use stacktrace() function instead

/** @brief Print exception details including stacktrace and object (if available) */
void print_exception(CLJException *ex) {
    if (!ex) return;
    
    // Print basic exception information (compact)
    fprintf(stderr, "%s: %s at %s:%d:%d", 
            ex->type, ex->message, shorten_file_path(ex->file), ex->line, ex->col);
    
#ifdef DEBUG
    // Print object if available (but skip for zombie objects to avoid secondary errors)
    if (ex->object) {
        // Check if object is a zombie before trying to print it
        // Zombie objects have rc == ZOMBIE_RC (-1)
        CljObject *obj = ex->object;
        if (obj->rc != ZOMBIE_RC) {
            CljString *obj_str = to_string(ex->object);
            if (obj_str) {
                fprintf(stderr, " object: %s @%p", string_data(obj_str), (void*)ex->object);
                RELEASE((CljObject*)obj_str);
            }
        } else {
            // Zombie object - print address and type name
            fprintf(stderr, " object: <zombie %s> @%p", clj_type_name(obj->type), (void*)ex->object);
        }
    }
    
    fprintf(stderr, "\n");
    
    // Print stacktrace if available (compact)
    if (ex->stacktrace) {
        fprintf(stderr, "Stack trace:\n");
        // Use string_data macro to get C-string from CljString
        const char *stacktrace_str = string_data((CljString*)ex->stacktrace);
        if (stacktrace_str) {
            fprintf(stderr, "%s", stacktrace_str);
        }
    }
    
    fprintf(stderr, "\n");  // Empty line after exception for readability
#else
    // Release builds: no stacktrace or object fields
    fprintf(stderr, "\n");
#endif
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
        fprintf(stderr, "UNHANDLED: ");
        print_exception(ex);
        
        // No handler - unhandled exception (exit as before)
        // Tests should use TRY/CATCH to catch exceptions
        free(ex); exit(1);
    }
    
    // Don't print exception details if there's a handler (expected exceptions in tests)
    // Only print for unhandled exceptions above
    // Store exception in handler
    global_exception_stack.top->exception = ex;
    longjmp(global_exception_stack.top->jump_state, 1);
}

// ============================================================================
// STANDARD ERROR MESSAGES
// ============================================================================

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

