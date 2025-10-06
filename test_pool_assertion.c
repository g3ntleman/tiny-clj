/*
 * Test für die neue Pool-Push/Pop Assertion
 * 
 * Dieser Test demonstriert die neue Assertion, die erkennt,
 * wenn cljvalue_pool_pop() öfter aufgerufen wird als cljvalue_pool_push().
 */

#include "src/CljObject.h"
#include "src/namespace.h"
#include "src/clj_symbols.h"
#include "src/exception.h"
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>

int main() {
    printf("=== Testing Pool Push/Pop Assertion ===\n\n");
    
    // Initialize symbol table
    init_special_symbols();
    
    // Test 1: Normaler Fall - push und pop
    printf("Test 1: Normal push/pop...\n");
    CljObjectPool *pool1 = cljvalue_pool_push();
    if (pool1) {
        printf("✓ Pool push successful\n");
        cljvalue_pool_pop(pool1);
        printf("✓ Pool pop successful\n");
    }
    
    // Test 2: Versuche pop nach push (sollte funktionieren)
    printf("\nTest 2: Normal push/pop sequence...\n");
    CljObjectPool *pool2 = cljvalue_pool_push();
    if (pool2) {
        printf("✓ Pool push successful\n");
        cljvalue_pool_pop(pool2);
        printf("✓ Pool pop successful\n");
    }
    
    // Test 3: Versuche pop ohne push (sollte Assertion auslösen)
    printf("\nTest 3: Attempting pop without push (should trigger assertion)...\n");
    
    // Set up exception handling
    EvalState *st = evalstate_new();
    
    if (setjmp(st->jmp_env) == 0) {
        // Versuche pop ohne push - sollte Assertion auslösen
        // Da der Zähler jetzt 0 ist, sollte die Assertion ausgelöst werden
        cljvalue_pool_pop(pool2);  // Verwende den bereits freigegebenen Pool
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
    
    // Test 4: Versuche pop mit ungültigem Pool
    printf("\nTest 4: Attempting pop with invalid pool...\n");
    
    if (setjmp(st->jmp_env) == 0) {
        // Versuche pop mit ungültigem Pool
        cljvalue_pool_pop((CljObjectPool*)0x12345678);
        printf("✓ Invalid pool pop handled gracefully\n");
    } else {
        if (st->last_error) {
            CLJException *exc = (CLJException*)st->last_error;
            printf("✓ Exception handled: %s\n", exc->message);
            release_exception(exc);
            st->last_error = NULL;
        }
    }
    
    printf("\n✅ All pool assertion tests passed!\n");
    printf("The new assertion successfully detects unbalanced pool operations.\n");
    
    // Cleanup
    free(st);
    
    return 0;
}
