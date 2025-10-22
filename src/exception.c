/**
 * @file exception.c
 * @brief Implementation of exception handling system with standard error messages.
 */

#include <stdlib.h>
#include <string.h>
#include "namespace.h"  // Must be before exception.h for EvalState definition
#include "exception.h"
#include "error_messages.h"
#include "runtime.h"

// Global storage for current exception
CLJException *g_current_exception = NULL;

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

