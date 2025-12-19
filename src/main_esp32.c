#include "platform.h"
#include "object.h"
#include "parser.h"
#include "namespace.h"
#include "builtins.h"
#include "runtime.h"
#include "memory.h"
#include "eval.h"
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
extern CljValue value_by_parsing_expr(Reader *reader, EvalState *st);

int main() {
    platform_init();
    DEBUG_PRINT("Tiny-Clj ESP32 - Embedded Execution");
    
    // Initialize interpreter
    register_builtins();
    
    // Get evaluation state
    EvalState *state = get_global_eval_state();

    // Load and execute startup code
    DEBUG_PRINT("Loading startup code...");
    CljObject *result = eval_string(startup_code, state);
    if (!result) {
        DEBUG_PRINT("ERROR: Failed to load startup code");
        return 1;
    }
    RELEASE(result);
    DEBUG_PRINT("Startup code executed successfully");
    
    // Cleanup
    DEBUG_PRINT("Done");
    autorelease_pool_cleanup_all();
    
    return 0;
}
