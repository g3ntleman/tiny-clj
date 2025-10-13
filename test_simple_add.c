#include <stdio.h>
#include <stdlib.h>
#include "src/tiny_clj.h"
#include "src/memory_hooks.h"
#include "src/symbol.h"
#include "src/builtins.h"
#include "src/function_call.h"

int main() {
    printf("🔍 Testing simple addition...\n");
    
    // Initialize memory system
    autorelease_pool_push();
    
    // Initialize special symbols
    init_special_symbols();
    
    // Initialize meta registry
    meta_registry_init();
    
    // Register builtin functions
    register_builtins();
    
    // Create evaluation state
    EvalState *st = evalstate();
    printf("✅ EvalState created\n");
    
    // Test if + symbol is registered
    CljObject *plus_sym = intern_symbol(NULL, "+");
    CljObject *plus_func = ns_resolve(st, plus_sym);
    if (plus_func) {
        printf("✅ + symbol found: type=%d\n", plus_func->type);
    } else {
        printf("❌ + symbol not found\n");
        return 1;
    }
    
    // Test simple arithmetic without eval_string
    printf("🔍 Testing manual addition...\n");
    
    // Create arguments manually
    CljObject *arg1 = make_int(2);
    CljObject *arg2 = make_int(3);
    CljObject *args[] = {arg1, arg2};
    
    // Call the function directly
    if (is_type(plus_func, CLJ_FUNC)) {
        printf("✅ + is a function, calling it...\n");
        CljObject *result = native_add(args, 2);
        if (result) {
            printf("✅ Result: type=%d, value=%d\n", result->type, result->as.i);
        } else {
            printf("❌ Function call failed\n");
        }
    } else {
        printf("❌ + is not a function: type=%d\n", plus_func->type);
    }
    
    // Cleanup
    evalstate_free(st);
    autorelease_pool_pop();
    
    printf("🔍 Test complete\n");
    return 0;
}
