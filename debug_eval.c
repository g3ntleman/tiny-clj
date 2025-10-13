#include <stdio.h>
#include <stdlib.h>
#include "src/tiny_clj.h"
#include "src/memory_hooks.h"
#include "src/symbol.h"
#include "src/builtins.h"

int main() {
    printf("🔍 Debugging eval_string issue...\n");
    
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
    
    // Test simple expression first
    printf("🔍 Testing simple expression: 42\n");
    CljObject *result1 = eval_string("42", st);
    if (result1) {
        printf("✅ Simple expression works: %d\n", result1->type);
    } else {
        printf("❌ Simple expression failed\n");
    }
    
    // Test symbol resolution
    printf("🔍 Testing symbol resolution: +\n");
    CljObject *plus_sym = make_symbol("+", NULL);
    CljObject *plus_func = ns_resolve(st, plus_sym);
    if (plus_func) {
        printf("✅ + symbol resolved: type=%d\n", plus_func->type);
    } else {
        printf("❌ + symbol not found\n");
    }
    
    // Test the problematic expression
    printf("🔍 Testing problematic expression: (+ 2 3)\n");
    CljObject *result2 = eval_string("(+ 2 3)", st);
    if (result2) {
        printf("✅ Expression works: type=%d\n", result2->type);
    } else {
        printf("❌ Expression failed\n");
    }
    
    // Cleanup
    evalstate_free(st);
    autorelease_pool_pop();
    
    printf("🔍 Debug complete\n");
    return 0;
}
