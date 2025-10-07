#include <stdio.h>
#include <stdlib.h>
#include "minunit.h"
#include "tiny_clj.h"
#include "memory_hooks.h"
#include "clj_symbols.h"

// Test the new eval_string API
static char* test_eval_string_basic() {
    EvalState *eval_state = evalstate_new();
    mu_assert("Should create eval state", eval_state != NULL);
    
    // Initialize special symbols
    init_special_symbols();
    
    // Test string evaluation
    CljObject *str_result = eval_string("\"hello world\"", eval_state);
    mu_assert("str_result should not be NULL", str_result != NULL);
    mu_assert("str_result should be CLJ_STRING", str_result->type == CLJ_STRING);
    mu_assert("str_result data should not be NULL", str_result->as.data != NULL);
    mu_assert("wrong string value", strcmp((char*)str_result->as.data, "hello world") == 0);
    
    // Test vector evaluation
    CljObject *vec_result = eval_string("[1 2 3]", eval_state);
    mu_assert_obj_type(vec_result, CLJ_VECTOR);
    
    free(eval_state);
    return 0;
}

static char* test_eval_string_error_handling() {
    EvalState *eval_state = evalstate_new();
    mu_assert("Should create eval state", eval_state != NULL);
    
    // Initialize special symbols
    init_special_symbols();
    
    // Test invalid syntax - should throw an exception
    // Use TRY/CATCH to test exception handling
    EvalState *st = eval_state;  // Alias for macro compatibility
    TRY {
        CljObject *result = eval_string("invalid-syntax", eval_state);
        mu_assert("Should not reach here - exception should be thrown", false);
    } CATCH(ex) {
        // Exception should be caught here
        mu_assert("Exception should be caught", ex != NULL);
        mu_assert("Exception should have message", ex->message != NULL);
    } END_TRY
    
    // Test NULL input
    CljObject *null_result = eval_string(NULL, eval_state);
    mu_assert("Should handle NULL input", null_result == NULL);
    
    free(eval_state);
    return 0;
}

static char* all_tests() {
    mu_run_test(test_eval_string_basic);
    mu_run_test(test_eval_string_error_handling);
    return 0;
}

// Export for unified test runner
char *run_eval_string_api_tests(void) {
    return all_tests();
}

#ifndef UNIFIED_TEST_RUNNER
// Standalone mode
int main() {
    printf("🧪 === eval_string API Tests ===\n\n");
    
    char *result = run_eval_string_api_tests();
    if (result != 0) {
        printf("❌ %s\n", result);
    } else {
        printf("✅ SUITE PASSED: All tests passed\n");
    }
    
    return result != 0;
}
#endif
