#include <stdlib.h>
#include <string.h>
#include "namespace.h"  // Must be before exception.h for EvalState definition
#include "exception.h"
#include "runtime.h"

// ============================================================================
// STANDARD ERROR MESSAGES
// ============================================================================

// Standard error messages as constants (no heap usage)
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

// For standard messages: use direct const pointers (minimal heap)
CLJException* exception(const char *msg, const char *file, int line, int col) {
    CLJException *e = ALLOC(CLJException, 1);
    if (!e) return NULL;
    
    e->type = "Error";
    e->data = NULL;
    
    // For standard messages: use direct const pointers
    e->message = msg;   // No strdup() - direct pointer to const string
    e->file = file;     // optional, may be NULL
    e->line = line;
    e->col = col;
    
    return e;
}

// For dynamic error messages (with variables)
CLJException* exception_dynamic(const char *msg, const char *file, int line, int col) {
    CLJException *e = ALLOC(CLJException, 1);
    if (!e) return NULL;
    
    e->type = "Error";
    e->data = NULL;
    
    // For dynamic messages: use strdup()
    e->message = strdup(msg);
    e->file = file ? strdup(file) : NULL;
    e->line = line;
    e->col = col;
    
    return e;
}
