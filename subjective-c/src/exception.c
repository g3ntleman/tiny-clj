/**
 * @file exception.c
 * @brief Implementation of exception handling system with standard error messages.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <setjmp.h>
#include "mini_format.h"
#include "exception.h"
#include "error_messages.h"
#include "value.h"   /* IS_IMMEDIATE for AUTORELEASE macro */
#include "memory.h"
#include "strings.h"

static void errf(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    (void)mini_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fputs(buf, stderr);
}

// Stacktrace support
#if defined(DEBUG) && (defined(__APPLE__) || defined(__linux__))
#include <execinfo.h>
#endif

// Safe string copy helper
static void safe_strncpy(char *dest, const char *src, size_t dest_size) {
    if (!dest || !src || dest_size == 0) return;
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

// Shorten file path to show only from /src/ onwards
static const char* shorten_file_path(const char *file) {
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

/** @brief Static exception type: StackOverflowError */
const char *EXCEPTION_STACK_OVERFLOW = "StackOverflowError";

/** @brief Static exception type: DivisionByZeroError */
const char *EXCEPTION_DIVISION_BY_ZERO = "DivisionByZeroError";

/** @brief Static exception type: ZombieAccessException */
const char *EXCEPTION_ZOMBIE_ACCESS = "ZombieAccessException";

/** @brief Static exception type: Generic Error */
const char *EXCEPTION_ERROR = "Error";

static inline bool is_out_of_memory_exception_type(const char *type) {
    // Size-optimized fast path: compare pointer to interned const string.
    // This relies on callsites using EXCEPTION_OUT_OF_MEMORY.
    return type == EXCEPTION_OUT_OF_MEMORY;
}

// ============================================================================
// EXCEPTION CREATION
// ============================================================================

/** @brief Create exception with reference counting */
CLJException* make_exception(const char *type, const char *message, const char *file, int line, int col) {
    if (!type || !message) return NULL;

    // CRITICAL: OutOfMemoryError must never allocate.
    // Use the static singleton and avoid stacktrace generation.
    if (is_out_of_memory_exception_type(type)) {
        if (!clj_oom_exception) return NULL;

        safe_strncpy(clj_oom_exception->type, EXCEPTION_OUT_OF_MEMORY, sizeof(clj_oom_exception->type));
        safe_strncpy(clj_oom_exception->message, message, sizeof(clj_oom_exception->message));
        safe_strncpy(clj_oom_exception->file, file ? file : "", sizeof(clj_oom_exception->file));
        clj_oom_exception->line = line;
        clj_oom_exception->col = col;
#ifdef DEBUG
        clj_oom_exception->stacktrace = NULL;
        clj_oom_exception->object = 0;
#endif
        return clj_oom_exception;
    }

    CLJException *exc = ALLOC(CLJException, 1);
    if (!exc) return NULL;

    // Initialize base object
    exc->base.type = CLJ_EXCEPTION;
    // rc already set to 1 by ALLOC

    // Copy strings directly into the structure (no strdup needed)
    safe_strncpy(exc->type, type, sizeof(exc->type));
    safe_strncpy(exc->message, message, sizeof(exc->message));
    safe_strncpy(exc->file, file ? file : "", sizeof(exc->file));

    exc->line = line;
    exc->col = col;

#ifdef DEBUG
    // Always generate stacktrace in DEBUG builds; exception must retain it
    exc->stacktrace = stacktrace();
    exc->object = 0;  // Initialize to 0 (unset)
#else
    // Release builds: no stacktrace field
#endif

    return exc;
}

// ============================================================================
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
    .object = 0
#endif
};

CLJException *clj_oom_exception = &clj_oom_exception_data;

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
    // Use generic RuntimeException if type is NULL
    const char *exception_type = (type != NULL) ? type : EXCEPTION_RUNTIME;

    // CRITICAL: OutOfMemoryError must never allocate, even if memory is still available.
    if (is_out_of_memory_exception_type(exception_type)) {
        char message[256];
        message[0] = '\0';
#if defined(STRING_FORMATTING_ENABLED) && !STRING_FORMATTING_ENABLED
        (void)mini_snprintf(message, sizeof(message), "%s", (format != NULL) ? format : "Out of memory");
#else
        va_list args;
        va_start(args, format);
        (void)mini_vsnprintf(message, sizeof(message), format ? format : "Out of memory", args);
        va_end(args);
#endif
        CLJException *oom = make_exception(EXCEPTION_OUT_OF_MEMORY, message, file, line, code);
        throw_exception_object(AUTORELEASE(oom ? oom : clj_oom_exception));  /* Singleton: no-op */
        return;
    }

#if defined(STRING_FORMATTING_ENABLED) && !STRING_FORMATTING_ENABLED
    // Size-focused builds: keep callsites/API but sacrifice formatted messages.
    const char *msg = (format != NULL) ? format : "Err";
    CLJException *exception = make_exception(exception_type, msg, file, line, code);
#else
    // Use mini_format everywhere (host + embedded) to keep formatter complexity low.
    char message[512];
    va_list args;
    va_start(args, format);
    (void)mini_vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    CLJException *exception = make_exception(exception_type, message, file, line, code);
#endif
    if (!exception) {
        throw_exception_object(AUTORELEASE(clj_oom_exception));  /* Singleton: no-op */
    }
    throw_exception_object(AUTORELEASE(exception));
}

/** @brief Throw an exception with type, message, and location */
void throw_exception(const char *type, const char *message, const char *file, int line, int col) {
    const char *exception_type = (type != NULL) ? type : EXCEPTION_RUNTIME;

    // CRITICAL: OutOfMemoryError must never allocate.
    if (is_out_of_memory_exception_type(exception_type)) {
        CLJException *oom = make_exception(EXCEPTION_OUT_OF_MEMORY,
                                           message ? message : "Out of memory",
                                           file, line, col);
        throw_exception_object(AUTORELEASE(oom ? oom : clj_oom_exception));
        return;
    }
    CLJException *exception = make_exception(exception_type, message, file, line, col);
    if (!exception) {
        throw_exception_object(AUTORELEASE(clj_oom_exception));
    }
    throw_exception_object(AUTORELEASE(exception));
    return;
}

/** @brief Generate stacktrace as CljString
 *  @return CljString* with stacktrace or NULL on error/in Release builds
 *  @note Only available in DEBUG builds
 */
#ifdef DEBUG
struct CljString* stacktrace(void) {
#if defined(__APPLE__) || defined(__linux__)
    void *array[32];
    size_t size = backtrace(array, 32);
    char **symbols = backtrace_symbols(array, size);

    if (!symbols || size == 0) {
        if (symbols) CLJ_FREE(symbols);
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
    char *buffer = (char*)CLJ_MALLOC(total_len + 1);
    if (!buffer) {
        CLJ_FREE(symbols);
        return NULL;
    }

    // Build stacktrace string, skipping the last line (often contains loader frames).
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

    CLJ_FREE(symbols);

    // Create CljString from buffer
    struct CljString *result = make_string(buffer);
    CLJ_FREE(buffer);

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
            errf("  %d: %s\n", i, strings[i]);
        }
    }

    CLJ_FREE(strings);
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

    // Print basic exception information (compact) using mini_format everywhere.
    errf("%s: %s at %s:%d:%d",
         ex->type, ex->message, shorten_file_path(ex->file), ex->line, ex->col);

#ifdef DEBUG
    // Print object if available (but skip for zombie objects to avoid secondary errors)
    if (ex->object != 0) {
        // Address-only: never dereference here (may be a zombie/invalid pointer)
        errf(" object: @%p", (void*)(uintptr_t)ex->object);
    }

    fputc('\n', stderr);

    // Print stacktrace if available (compact)
    // Default: enabled (Clojure/JVM-like). Can be disabled via env var.
    bool print_stacktrace = true;
#if !defined(ESP32_BUILD)
    static int s_print_stacktrace = -1;
    if (s_print_stacktrace < 0) {
        const char *v = getenv("TINY_CLJ_PRINT_STACKTRACE");
        s_print_stacktrace = (!v || !v[0] || strcmp(v, "0") != 0) ? 1 : 0;
    }
    print_stacktrace = (s_print_stacktrace == 1);
#endif

    if (print_stacktrace && ex->stacktrace) {
        fputs("Stack trace:\n", stderr);
        // Use string_data macro to get C-string from CljString
        const char *stacktrace_str = string_data((CljString*)ex->stacktrace);
        if (stacktrace_str) {
            fputs(stacktrace_str, stderr);
        }
    }

    fputs("\n", stderr);  // Empty line after exception for readability
#else
    // Release builds: no stacktrace or object fields
    fputs("\n", stderr);
#endif
}

/** @brief Re-throw an existing exception object */
void throw_exception_object(CLJException *ex) {
    if (!ex) {
#ifdef DEBUG
        fputs("FAILED TO RE-THROW NULL EXCEPTION\n", stderr);
#endif
        exit(1);
    }

    if (!global_exception_stack.top) {
        // No handler - unhandled exception
        fputs("UNHANDLED: ", stderr);
        print_exception(ex);

        // No handler - unhandled exception (exit as before)
        // Tests should use TRY/CATCH to catch exceptions
        CLJ_FREE(ex); exit(1);
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
    return make_exception(EXCEPTION_ERROR, msg, file, line, col);
}
