#include "platform.h"
#include "object.h"
#include "parser.h"
#include "namespace.h"
#include "builtins.h"
#include "runtime.h"
#include "memory.h"
#include "function_call.h"
#include "reader.h"
#include "value.h"
#include "debug.h"
#include <stdio.h>
#include <stdlib.h>

// Embedded startup code (like clojure_core.c pattern)
static const char *startup_code = 
#include "startup-code.clj"
    ;

// Forward declaration
extern CljValue make_value_by_parsing_expr(Reader *reader, EvalState *st);

int main() {
    platform_init();
    DEBUG_PRINT("Tiny-Clj ESP32 - Embedded Execution");
    
    // Initialize interpreter
    meta_registry_init();
    init_special_symbols();
    register_builtins();
    
    // Create evaluation state
    EvalState *state = evalstate_new();
    if (!state) {
        DEBUG_PRINT("ERROR: Failed to create eval state");
        return 1;
    }
    
    // Load and execute startup code
    DEBUG_PRINT("Loading startup code...");
    CljObject *result = eval_string(startup_code, state);
    if (!result) {
        DEBUG_PRINT("ERROR: Failed to load startup code");
        evalstate_free(state);
        return 1;
    }
    RELEASE(result);
    DEBUG_PRINT("Startup code executed successfully");
    
    // Cleanup
    DEBUG_PRINT("Done");
    evalstate_free(state);
    symbol_table_cleanup();
    meta_registry_cleanup();
    autorelease_pool_cleanup_all();
    
    return 0;
}
