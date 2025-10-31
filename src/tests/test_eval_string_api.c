#include <stdio.h>
#include <stdlib.h>
#include "minunit.h"
#include "tiny_clj.h"
#include "memory_hooks.h"

// Test the new eval_string API
static char* test_eval_string_basic() {
    EvalState *eval_state = evalstate_new();
    mu_assert("Should create eval state", eval_state != NULL);
    
    // Test string evaluation
    CljObject *str_result = eval_string("\"hello world\"", eval_state);
    mu_assert_obj_string(str_result, "hello world");
    
    // Test vector evaluation
    CljObject *vec_result = eval_string("[1 2 3]", eval_state);
    mu_assert_obj_type(vec_result, CLJ_VECTOR);
    
    free(eval_state);
    return 0;
}

static char* test_eval_string_error_handling() {
    EvalState *eval_state = evalstate_new();
    mu_assert("Should create eval state", eval_state != NULL);
    
    // Test invalid syntax - should return a symbol, not NULL
    CljObject *result = eval_string("invalid-syntax", eval_state);
    mu_assert_obj_type(result, CLJ_SYMBOL);
    
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
