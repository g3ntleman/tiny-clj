/*
 * Direkter Test für die Pool-Push/Pop Assertion
 * 
 * Dieser Test manipuliert direkt den internen Zähler,
 * um die Assertion zu demonstrieren.
 */

#include "src/CljObject.h"
#include "src/namespace.h"
#include "src/clj_symbols.h"
#include "src/exception.h"
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>

// Externe Variable für den Push-Counter (normalerweise static)
extern int g_pool_push_count;

int main() {
    printf("=== Testing Direct Pool Push/Pop Assertion ===\n\n");
    
    // Initialize symbol table
    init_special_symbols();
    
    // Test 1: Normaler Fall
    printf("Test 1: Normal push/pop...\n");
    CljObjectPool *pool1 = cljvalue_pool_push();
    if (pool1) {
        printf("✓ Pool push successful (count: %d)\n", g_pool_push_count);
        cljvalue_pool_pop(pool1);
        printf("✓ Pool pop successful (count: %d)\n", g_pool_push_count);
    }
    
    // Test 2: Manipuliere den Zähler direkt
    printf("\nTest 2: Manipulating counter to trigger assertion...\n");
    
    // Set up exception handling
    EvalState *st = evalstate_new();
    
    if (setjmp(st->jmp_env) == 0) {
        // Manipuliere den Zähler, um eine unausgewogene Situation zu simulieren
        g_pool_push_count = 0;  // Setze Zähler auf 0
        
        // Erstelle einen neuen Pool
        CljObjectPool *pool2 = cljvalue_pool_push();
        printf("✓ Pool created (count: %d)\n", g_pool_push_count);
        
        // Jetzt versuche pop - sollte Assertion auslösen, da Zähler 0 war
        cljvalue_pool_pop(pool2);
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
