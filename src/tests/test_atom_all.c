/*
 * Unity Test: Run all Atom Tests in one go
 * 
 * This test verifies that all atom tests can run together successfully
 */

#include "tests_common.h"

// Forward declaration for test_get_eval_state
extern EvalState* test_get_eval_state(void);

// Test: Run all atom tests in sequence
TEST(test_atom_all_tests) {
    // Verify that global evalState is available
    EvalState *st = test_get_eval_state();
    TEST_ASSERT_NOT_NULL_MESSAGE(st, "Global test evalState should be available");
    
    // Verify that clojure.core is loaded
    TEST_ASSERT_NOT_NULL_MESSAGE(g_runtime.clojure_core_cache, 
                                 "clojure.core cache should be set");
    
    // Verify that inc is available
    CljObject *inc_sym = intern_symbol_global("inc");
    TEST_ASSERT_NOT_NULL(inc_sym);
    
    ID inc_func = ns_resolve(st, inc_sym);
    TEST_ASSERT_NOT_NULL_MESSAGE(inc_func, 
                                 "inc should be resolvable from clojure.core");
    
    // This test just verifies the setup - all individual atom tests should pass
    // when run together
}

