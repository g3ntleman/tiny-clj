#include "src/tests/tests_common.h"

TEST(debug_simple_test) {
    printf("=== Debug Simple Test ===\n");
    
    // Test ohne parse() - nur einfache Objekte
    WITH_AUTORELEASE_POOL({
        CljValue num = fixnum(42);
        printf("Created fixnum: %d\n", as_fixnum(num));
        TEST_ASSERT_EQUAL_INT(42, as_fixnum(num));
    });
    
    printf("✓ Simple test completed\n");
}

TEST(debug_parse_test) {
    printf("=== Debug Parse Test ===\n");
    
    WITH_AUTORELEASE_POOL({
        EvalState *eval_state = evalstate_new();
        printf("Created eval_state\n");
        
        // Test parse() function
        CljObject *result = parse("42", eval_state);
        printf("Parse result: %p\n", (void*)result);
        
        if (result) {
            printf("Result type: %d\n", result->type);
            TEST_ASSERT_EQUAL_INT(CLJ_FIXNUM, result->type);
        }
        
        evalstate_free(eval_state);
        printf("Freed eval_state\n");
    });
    
    printf("✓ Parse test completed\n");
}
