/*
 * Finaler Test für die Pool-Push/Pop Assertion
 * 
 * Dieser Test ruft pop() direkt auf, ohne vorherigen push(),
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
    printf("=== Testing Final Pool Push/Pop Assertion ===\n\n");
    
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
    
    // Test 2: Versuche pop ohne push (sollte Assertion auslösen)
    printf("\nTest 2: Attempting pop without push (should trigger assertion)...\n");
    
    // Set up exception handling
    EvalState *st = evalstate_new();
    
    if (setjmp(st->jmp_env) == 0) {
        // Versuche pop ohne push - sollte Assertion auslösen
        // Da der Zähler jetzt 0 ist, sollte die Assertion ausgelöst werden
        cljvalue_pool_pop(NULL);
        printf("❌ ERROR: Assertion should have been triggered!\n");
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
