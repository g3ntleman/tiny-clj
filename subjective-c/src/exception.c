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
#if defined(ESP_PLATFORM)
#include "esp_debug_helpers.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

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
#include <dlfcn.h>
#endif

// Safe string copy helper
static void safe_strncpy(char *dest, const char *src, size_t dest_size) {
    if (!dest || !src || dest_size == 0) return;
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

// During autorelease-pool drain, throw paths must be allocation-free and must
// not call AUTORELEASE again (would recurse through autorelease() guard).
static THREAD_LOCAL CLJException g_drain_exception;

static CLJException *prepare_drain_exception(const char *type,
                                             const char *message,
                                             const char *file,
                                             int line,
                                             int col) {
    memset(&g_drain_exception, 0, sizeof(g_drain_exception));
    g_drain_exception.base.type = CLJ_EXCEPTION;
    g_drain_exception.base.rc = SINGLETON_RC;
    safe_strncpy(g_drain_exception.type,
                 (type && type[0] != '\0') ? type : "RuntimeException",
                 sizeof(g_drain_exception.type));
    safe_strncpy(g_drain_exception.message,
                 (message && message[0] != '\0') ? message : "error",
                 sizeof(g_drain_exception.message));
    safe_strncpy(g_drain_exception.file, file ? file : "", sizeof(g_drain_exception.file));
    g_drain_exception.line = line;
    g_drain_exception.col = col;
#ifdef DEBUG
    g_drain_exception.stacktrace = NULL;
    g_drain_exception.object = 0;
#endif
    return &g_drain_exception;
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

static const char *path_basename_const(const char *path) {
    if (!path || path[0] == '\0') {
        return "";
    }
    const char *last_slash = strrchr(path, '/');
    return last_slash ? (last_slash + 1) : path;
}

#if defined(DEBUG) && (defined(__APPLE__) || defined(__linux__))
static void exception_format_symbolized_frame(char *buf,
                                              size_t buf_size,
                                              int frame_index,
                                              void *addr) {
    if (!buf || buf_size == 0u) {
        return;
    }

    Dl_info info;
    memset(&info, 0, sizeof(info));
    if (dladdr(addr, &info) != 0 && info.dli_sname) {
        const char *image = path_basename_const(info.dli_fname);
        uintptr_t offset = 0u;
        if (info.dli_saddr) {
            offset = (uintptr_t)addr - (uintptr_t)info.dli_saddr;
        }
        (void)mini_snprintf(buf, buf_size,
                            "  %d: %s!%s + 0x%lx [%p]\n",
                            frame_index,
                            (image && image[0] != '\0') ? image : "<image>",
                            info.dli_sname,
                            (unsigned long)offset,
                            addr);
        return;
    }

    (void)mini_snprintf(buf, buf_size, "  %d: %p\n", frame_index, addr);
}
#endif

// Forward declaration for stacktrace function
#ifdef DEBUG
struct CljString* stacktrace(void);
#endif

// Global exception stack (independent of EvalState)
THREAD_LOCAL GlobalExceptionStack global_exception_stack = {0};

#ifdef DEBUG
// Clojure call stack (function names, innermost frame at [depth-1]).
CljCallStack g_clj_callstack = { .depth = 0 };
static char g_clj_stacktrace_buf[CLJ_CALLSTACK_MAX * 40];

/**
 * @brief Build a human-readable Clojure stacktrace from the current call stack.
 * @return New CljString (rc=1) listing frames from innermost to outermost, or NULL if empty.
 */
struct CljString *clj_stacktrace_build(void) {
    int depth = g_clj_callstack.depth;
    if (depth <= 0) return NULL;
    if (depth > CLJ_CALLSTACK_MAX) {
        depth = CLJ_CALLSTACK_MAX;
    }
    char *buf = g_clj_stacktrace_buf;
    int buf_size = (int)sizeof(g_clj_stacktrace_buf);
    int pos = 0;
    pos += mini_snprintf(buf + pos, buf_size - pos, "Clojure call stack:\n");
    for (int i = depth - 1; i >= 0 && pos < buf_size - 1; i--) {
        int frame_num = depth - i;
        const char *name = g_clj_callstack.names[i];
        if (!name || name[0] == '\0') {
            name = "<anonymous>";
        }
        pos += mini_snprintf(buf + pos, buf_size - pos, "  %2d: %s\n",
                             frame_num,
                             name);
    }
    return make_string(buf);
}
#endif

#define THREAD_NAME_MAX 32
static THREAD_LOCAL char g_thread_name[THREAD_NAME_MAX] = {0};

void subjective_c_set_thread_name(const char *name) {
    if (name) {
        strncpy(g_thread_name, name, THREAD_NAME_MAX - 1);
        g_thread_name[THREAD_NAME_MAX - 1] = '\0';
    } else {
        g_thread_name[0] = '\0';
    }
}

const char *subjective_c_get_thread_name(void) {
    return g_thread_name[0] ? g_thread_name : "unknown";
}
static SubjectiveCThreadState subjective_c_main_thread_storage = {0};
static SubjectiveCThreadState subjective_c_interpreter_thread_storage = {0};
const SubjectiveCThreadState *const subjective_c_main_thread = &subjective_c_main_thread_storage;
const SubjectiveCThreadState *const subjective_c_interpreter_thread = &subjective_c_interpreter_thread_storage;

static bool subjective_c_thread_state_matches_current(const SubjectiveCThreadState *state) {
    if (!state || !state->initialized) {
        return false;
    }
#if defined(ESP_PLATFORM)
    return state->task_handle == (void *)xTaskGetCurrentTaskHandle();
#else
    return pthread_equal(state->value, pthread_self()) != 0;
#endif
}

void subjective_c_register_main_thread(void) {
    if (!subjective_c_main_thread_storage.initialized) {
#if defined(ESP_PLATFORM)
        subjective_c_main_thread_storage.task_handle = (void *)xTaskGetCurrentTaskHandle();
#else
        subjective_c_main_thread_storage.value = pthread_self();
#endif
        subjective_c_main_thread_storage.initialized = true;
        subjective_c_set_thread_name("main");
    }
}

void subjective_c_register_interpreter_thread(void) {
#if defined(ESP_PLATFORM)
    subjective_c_interpreter_thread_storage.task_handle = (void *)xTaskGetCurrentTaskHandle();
#else
    subjective_c_interpreter_thread_storage.value = pthread_self();
#endif
    subjective_c_interpreter_thread_storage.initialized = true;
}

void subjective_c_clear_interpreter_thread(void) {
    memset(&subjective_c_interpreter_thread_storage, 0, sizeof(subjective_c_interpreter_thread_storage));
}

typedef struct {
    void *(*start_routine)(void *);
    void *arg;
    char name[THREAD_NAME_MAX];
} SubjectiveCNamedThreadArgs;

static void *subjective_c_named_thread_entry(void *raw_args) {
    SubjectiveCNamedThreadArgs args = *(SubjectiveCNamedThreadArgs *)raw_args;
    free(raw_args);
    subjective_c_set_thread_name(args.name);
    return args.start_routine(args.arg);
}

int subjective_c_pthread_create_named(pthread_t *thread,
                                       const pthread_attr_t *attr,
                                       void *(*start_routine)(void *),
                                       void *arg,
                                       const char *name) {
    SubjectiveCNamedThreadArgs *args = malloc(sizeof(SubjectiveCNamedThreadArgs));
    if (!args) {
        return -1;
    }
    args->start_routine = start_routine;
    args->arg = arg;
    strncpy(args->name, name ? name : "unnamed", THREAD_NAME_MAX - 1);
    args->name[THREAD_NAME_MAX - 1] = '\0';
    int rc = pthread_create(thread, attr, subjective_c_named_thread_entry, args);
    if (rc != 0) {
        free(args);
    }
    return rc;
}

bool subjective_c_has_main_thread(void) {
    return subjective_c_main_thread_storage.initialized;
}

bool subjective_c_has_interpreter_thread(void) {
    return subjective_c_interpreter_thread_storage.initialized;
}

bool subjective_c_is_main_thread(void) {
    return subjective_c_thread_state_matches_current(&subjective_c_main_thread_storage);
}

bool subjective_c_is_interpreter_thread(void) {
    return subjective_c_thread_state_matches_current(&subjective_c_interpreter_thread_storage);
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

/** @brief Static exception type: NotImplementedException */
const char *EXCEPTION_NOT_IMPLEMENTED = "NotImplementedException";

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

static inline bool is_out_of_memory_exception_instance(const CLJException *ex) {
    return ex && strcmp(ex->type, EXCEPTION_OUT_OF_MEMORY) == 0;
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
    // Prefer Clojure-level call stack (useful to end users).
    // Avoid native backtrace symbolization here: in deep/error-heavy paths this can
    // exhaust the remaining stack and crash while constructing the exception object.
    exc->stacktrace = clj_stacktrace_build();
    if (!exc->stacktrace) {
        exc->stacktrace = make_string("Clojure call stack unavailable");
    }
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

    if (is_autorelease_pool_draining()) {
        char message[256];
        message[0] = '\0';
#if defined(STRING_FORMATTING_ENABLED) && !STRING_FORMATTING_ENABLED
        (void)mini_snprintf(message, sizeof(message), "%s", (format != NULL) ? format : "Err");
#else
        va_list args;
        va_start(args, format);
        (void)mini_vsnprintf(message, sizeof(message), format ? format : "Err", args);
        va_end(args);
#endif
        throw_exception_object(prepare_drain_exception(exception_type, message, file, line, code));
        return;
    }

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

    if (is_autorelease_pool_draining()) {
        throw_exception_object(prepare_drain_exception(exception_type,
                                                       message ? message : "error",
                                                       file, line, col));
        return;
    }

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

    if (size == 0) {
        return NULL;
    }

    // Keep exception stacktrace capture lightweight and avoid symbolication.
    // dladdr()-based symbolization can recurse into dyld internals and blow
    // small stacks in stress paths.
    size_t total_len = 0;
    char line_buf[64];
    size_t last_index = (size > 0u) ? (size - 1u) : 0u;
    for (size_t i = 0; i < last_index; i++) {
        (void)mini_snprintf(line_buf, sizeof(line_buf), "  %d: %p\n", (int)i, array[i]);
        total_len += strlen(line_buf);
    }

    // Allocate buffer for stacktrace string
    char *buffer = (char*)CLJ_MALLOC(total_len + 1);
    if (!buffer) {
        return NULL;
    }

    // Build stacktrace string, skipping the last line (often contains loader frames).
    size_t pos = 0;
    for (size_t i = 0; i < last_index; i++) {
        (void)mini_snprintf(line_buf, sizeof(line_buf), "  %d: %p\n", (int)i, array[i]);
        size_t len = strlen(line_buf);
        memcpy(buffer + pos, line_buf, len);
        pos += len;
    }
    buffer[pos] = '\0';

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
#if defined(ESP_PLATFORM)
    (void)esp_backtrace_print(100);
#elif defined(DEBUG) && !defined(ESP32_BUILD)
#if defined(__APPLE__) || defined(__linux__)
    void *array[20];
    int size = backtrace(array, 20);
    if (size <= 0) {
        return;
    }
    /* Keep this path allocation-free and symbol-resolution-free.
     * backtrace_symbols()/dladdr() can fault when memory is already corrupted
     * or when called from stressed worker threads during exception storms. */
    for (int i = 0; i < size; i++) {
        errf("  %d: %p\n", i, array[i]);
    }
#else
    // Platform without execinfo support
#endif
#else
    // Release builds / ESP32 do not emit backtraces here
#endif
}

void exception_print_native_backtrace_symbolized(void) {
#if defined(ESP_PLATFORM)
    (void)esp_backtrace_print(100);
#elif defined(DEBUG) && !defined(ESP32_BUILD)
#if defined(__APPLE__) || defined(__linux__)
    void *array[32];
    int size = backtrace(array, 32);
    if (size <= 0) {
        return;
    }
    char line_buf[512];
    for (int i = 0; i < size; i++) {
        exception_format_symbolized_frame(line_buf, sizeof(line_buf), i, array[i]);
        fputs(line_buf, stderr);
    }
#else
    exception_print_native_backtrace();
#endif
#else
    exception_print_native_backtrace();
#endif
}

// print_stacktrace() removed - use stacktrace() function instead

/** @brief Print exception details including stacktrace and object (if available) */
void print_exception(CLJException *ex) {
    if (!ex) return;
    const bool is_oom = is_out_of_memory_exception_instance(ex);

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

    if (is_oom) {
        fputs("Native stack trace (OOM):\n", stderr);
        exception_print_native_backtrace();
    }

    fputs("\n", stderr);  // Empty line after exception for readability
#else
    // Release builds: no stacktrace or object fields
    fputs("\n", stderr);
    if (is_oom) {
        fputs("Native stack trace (OOM):\n", stderr);
        exception_print_native_backtrace();
        fputs("\n", stderr);
    }
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
        RELEASE((CljObject *)ex);
        fflush(stderr);
        _Exit(1);
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
