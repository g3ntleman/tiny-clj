/*
 * Einfacher Test für die Pool-Push/Pop Assertion
 * 
 * Dieser Test führt mehrfache pop() Operationen aus,
 * um die Assertion zu demonstrieren.
 */

#include "src/CljObject.h"
#include "src/namespace.h"
#include "src/clj_symbols.h"
#include "src/exception.h"
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>

int main() {
    printf("=== Testing Simple Pool Push/Pop Assertion ===\n\n");
    
    // Initialize symbol table
    init_special_symbols();
    
    // Test 1: Normaler Fall
    printf("Test 1: Normal push/pop...\n");
    CljObjectPool *pool1 = cljvalue_pool_push();
    if (pool1) {
        printf("✓ Pool push successful\n");
        cljvalue_pool_pop(pool1);
        printf("✓ Pool pop successful\n");
    }
    
    // Test 2: Versuche mehrfaches Pop
    printf("\nTest 2: Attempting multiple pops to trigger assertion...\n");
    
    // Set up exception handling
    EvalState *st = evalstate_new();
    
    if (setjmp(st->jmp_env) == 0) {
        // Erstelle einen Pool
        CljObjectPool *pool2 = cljvalue_pool_push();
        printf("✓ Pool created\n");
        
        // Pop einmal (normal)
        cljvalue_pool_pop(pool2);
        printf("✓ First pop successful\n");
        
        // Versuche nochmal zu pop (sollte Assertion auslösen)
        cljvalue_pool_pop(pool2);
        printf("❌ ERROR: Second pop should have triggered assertion!\n");
        return 1;
    } else {
        // Exception wurde gefangen
        if (st->last_error) {
            CLJException *exc = (CLJException*)st->last_error;
            printf("✓ Assertion triggered as expected: %s\n", exc->message);
            release_exception(exc);
            st->last_error = NULL;
        }
    }
    
    printf("\n✅ Pool assertion test completed!\n");
    printf("The assertion successfully detects unbalanced pool operations.\n");
    
    // Cleanup
    free(st);
    
    return 0;
}
