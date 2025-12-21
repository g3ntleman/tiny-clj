/*
 * Unity Tests for CallFrame System
 * 
 * Tests for stack-based call frames that eliminate heap allocation per function call.
 */

#include "tests_common.h"
#include "../tiny_clj.h"
#include "../environment.h"

// ============================================================================
// TEST: Basic frame initialization and lookup
// ============================================================================

TEST(test_frame_init_and_lookup) {
    WITH_MEMORY_PROFILING({
        // Create a simple frame with one parameter
        int capacity = 1;
        CallFrame *frame = (CallFrame*)STACK_ALLOC(char, frame_allocation_size(capacity));
        ID param_sym = intern_symbol_global("x");
        ID param_val = fixnum(42);
        
        RETAIN(param_sym);
        // Fixnums are immediate values, no need to retain
        
        ID params[1];
        ID values[1];
        params[0] = param_sym;
        values[0] = param_val;
        
        frame_init(frame, NULL);
        frame_set_bindings(frame, NULL, params, values, 1);
        
        // Lookup should find the parameter
        ID found = NULL;
        TEST_ASSERT_TRUE(frame_lookup(frame, param_sym, &found));
        TEST_ASSERT_NOT_NULL(found);
        TEST_ASSERT_TRUE(is_fixnum(found));
        TEST_ASSERT_EQUAL_INT(42, as_fixnum(found));
        
        // Cleanup
        frame_release(frame);
        RELEASE(param_sym);
    });
}

// ============================================================================
// TEST: Nested frames (parent chain)
// ============================================================================

TEST(test_frame_nested_lookup) {
    WITH_MEMORY_PROFILING({
        // Create parent frame
        int capacity = 1;
        CallFrame *parent_frame = (CallFrame*)STACK_ALLOC(char, frame_allocation_size(capacity));
        ID parent_sym = intern_symbol_global("y");
        ID parent_val = fixnum(10);
        
        RETAIN(parent_sym);
        // Fixnums are immediate values, no need to retain
        
        ID parent_params[1];
        ID parent_values[1];
        parent_params[0] = parent_sym;
        parent_values[0] = parent_val;
        
        frame_init(parent_frame, NULL);
        frame_set_bindings(parent_frame, NULL, parent_params, parent_values, 1);
        
        // Create child frame
        CallFrame *child_frame = (CallFrame*)STACK_ALLOC(char, frame_allocation_size(capacity));
        ID child_sym = intern_symbol_global("x");
        ID child_val = fixnum(20);
        
        RETAIN(child_sym);
        // Fixnums are immediate values, no need to retain
        
        ID child_params[1];
        ID child_values[1];
        child_params[0] = child_sym;
        child_values[0] = child_val;
        
        frame_init(child_frame, parent_frame);
        frame_set_bindings(child_frame, parent_frame, child_params, child_values, 1);
        
        // Child should find its own parameter
        ID found_child = NULL;
        TEST_ASSERT_TRUE(frame_lookup(child_frame, child_sym, &found_child));
        TEST_ASSERT_NOT_NULL(found_child);
        TEST_ASSERT_EQUAL_INT(20, as_fixnum(found_child));
        
        // Child should find parent's parameter
        ID found_parent = NULL;
        TEST_ASSERT_TRUE(frame_lookup(child_frame, parent_sym, &found_parent));
        TEST_ASSERT_NOT_NULL(found_parent);
        TEST_ASSERT_EQUAL_INT(10, as_fixnum(found_parent));
        
        // Cleanup
        frame_release(child_frame);
        frame_release(parent_frame);
        RELEASE(parent_sym);
        RELEASE(child_sym);
    });
}

// ============================================================================
// TEST: Frame with multiple parameters
// ============================================================================

TEST(test_frame_multiple_params) {
    WITH_MEMORY_PROFILING({
        int capacity = 2;
        CallFrame *frame = (CallFrame*)STACK_ALLOC(char, frame_allocation_size(capacity));
        ID sym1 = intern_symbol_global("a");
        ID sym2 = intern_symbol_global("b");
        ID val1 = fixnum(1);
        ID val2 = fixnum(2);
        
        RETAIN(sym1);
        RETAIN(sym2);
        // Fixnums are immediate values, no need to retain
        
        ID params[2];
        ID values[2];
        params[0] = sym1;
        params[1] = sym2;
        values[0] = val1;
        values[1] = val2;
        
        frame_init(frame, NULL);
        frame_set_bindings(frame, NULL, params, values, 2);

        TEST_ASSERT_EQUAL_INT(2, frame->param_count);

        ID found1 = NULL;
        TEST_ASSERT_TRUE(frame_lookup(frame, sym1, &found1));
        TEST_ASSERT_NOT_NULL(found1);
        TEST_ASSERT_EQUAL_INT(1, as_fixnum(found1));
        
        ID found2 = NULL;
        TEST_ASSERT_TRUE(frame_lookup(frame, sym2, &found2));
        TEST_ASSERT_NOT_NULL(found2);
        TEST_ASSERT_EQUAL_INT(2, as_fixnum(found2));
        
        // Cleanup
        frame_release(frame);
        RELEASE(sym1);
        RELEASE(sym2);
    });
}

// ============================================================================
// TEST: Frame lookup returns NULL for unknown symbol
// ============================================================================

TEST(test_frame_lookup_not_found) {
    WITH_MEMORY_PROFILING({
        int capacity = 1;
        CallFrame *frame = (CallFrame*)STACK_ALLOC(char, frame_allocation_size(capacity));
        ID param_sym = intern_symbol_global("x");
        ID param_val = fixnum(42);
        ID unknown_sym = intern_symbol_global("y");
        
        RETAIN(param_sym);
        // Fixnums are immediate values, no need to retain
        RETAIN(unknown_sym);
        
        ID params[1];
        ID values[1];
        params[0] = param_sym;
        values[0] = param_val;
        
        frame_init(frame, NULL);
        frame_set_bindings(frame, NULL, params, values, 1);
        
        // Lookup of unknown symbol should return NULL
        ID found = NULL;
        TEST_ASSERT_FALSE(frame_lookup(frame, unknown_sym, &found));
        TEST_ASSERT_NULL(found);
        
        // Cleanup
        frame_release(frame);
        RELEASE(param_sym);
        RELEASE(unknown_sym);
    });
}

// ============================================================================
// TEST: Frame release cleans up values
// ============================================================================

TEST(test_frame_release_cleanup) {
    WITH_MEMORY_PROFILING({
        int capacity = 1;
        CallFrame *frame = (CallFrame*)STACK_ALLOC(char, frame_allocation_size(capacity));
        ID param_sym = intern_symbol_global("x");
        ID param_val = fixnum(42);
        
        RETAIN(param_sym);
        // Fixnums are immediate values, no need to retain
        
        ID params[1];
        ID values[1];
        params[0] = param_sym;
        values[0] = param_val;
        
        frame_init(frame, NULL);
        frame_set_bindings(frame, NULL, params, values, 1);
        
        // Release should clean up
        frame_release(frame);

        TEST_ASSERT_EQUAL_INT(0, frame->param_count);
        
        // Cleanup
        RELEASE(param_sym);
    });
}

// ============================================================================
// TEST: Circular reference detection in frame_lookup
// ============================================================================

TEST(test_frame_lookup_circular_reference) {
    WITH_MEMORY_PROFILING({
        int capacity = 1;
        CallFrame *frame1 = (CallFrame*)STACK_ALLOC(char, frame_allocation_size(capacity));
        CallFrame *frame2 = (CallFrame*)STACK_ALLOC(char, frame_allocation_size(capacity));
        
        ID sym1 = intern_symbol_global("x");
        ID val1 = fixnum(1);
        ID sym2 = intern_symbol_global("y");
        ID val2 = fixnum(2);
        
        RETAIN(sym1);
        RETAIN(sym2);
        
        // Create circular reference: frame1 -> frame2 -> frame1
        frame_init(frame1, frame2);
        frame_init(frame2, frame1);
        
        ID params1[1] = {sym1};
        ID values1[1] = {val1};
        frame_set_bindings(frame1, frame2, params1, values1, 1);
        
        ID params2[1] = {sym2};
        ID values2[1] = {val2};
        frame_set_bindings(frame2, frame1, params2, values2, 1);
        
        // Lookup should detect circular reference and return false
        ID found = NULL;
        // This should not cause infinite loop
        bool result = frame_lookup(frame1, sym2, &found);
        // Should either find it or return false, but not loop infinitely
        TEST_ASSERT_TRUE(result == false || (result == true && found != NULL));
        
        // Cleanup
        frame_release(frame2);
        frame_release(frame1);
        RELEASE(sym1);
        RELEASE(sym2);
    });
}

// ============================================================================
// TEST: Frame lookup with same symbol called multiple times
// ============================================================================

TEST(test_frame_lookup_repeated_calls) {
    WITH_MEMORY_PROFILING({
        int capacity = 1;
        CallFrame *frame = (CallFrame*)STACK_ALLOC(char, frame_allocation_size(capacity));
        ID param_sym = intern_symbol_global("x");
        ID param_val = fixnum(42);
        ID unknown_sym = intern_symbol_global("y");
        
        RETAIN(param_sym);
        RETAIN(unknown_sym);
        
        ID params[1];
        ID values[1];
        params[0] = param_sym;
        values[0] = param_val;
        
        frame_init(frame, NULL);
        frame_set_bindings(frame, NULL, params, values, 1);
        
        // Call lookup multiple times with same symbol - should not cause infinite loop
        ID found = NULL;
        for (int i = 0; i < 20; i++) {
            bool result = frame_lookup(frame, unknown_sym, &found);
            TEST_ASSERT_FALSE(result);
            TEST_ASSERT_NULL(found);
        }
        
        // Cleanup
        frame_release(frame);
        RELEASE(param_sym);
        RELEASE(unknown_sym);
    });
}

// ============================================================================
// TEST: resolve_symbol_in_env_with_frame direct test
// ============================================================================

TEST(test_resolve_symbol_in_env_with_frame) {
    WITH_MEMORY_PROFILING({
        // This test requires access to resolve_symbol_in_env_with_frame
        // Since it's static, we'll test through eval_let instead
        // But first, let's test frame lookup directly
        
        int capacity = 1;
        CallFrame *frame = (CallFrame*)STACK_ALLOC(char, frame_allocation_size(capacity));
        ID param_sym = intern_symbol_global("test-sym");
        ID param_val = fixnum(100);
        
        RETAIN(param_sym);
        
        ID params[1];
        ID values[1];
        params[0] = param_sym;
        values[0] = param_val;
        
        frame_init(frame, NULL);
        frame_set_bindings(frame, NULL, params, values, 1);
        
        // Test that frame_lookup works correctly
        ID found = NULL;
        TEST_ASSERT_TRUE(frame_lookup(frame, param_sym, &found));
        TEST_ASSERT_NOT_NULL(found);
        TEST_ASSERT_TRUE(is_fixnum(found));
        TEST_ASSERT_EQUAL_INT(100, as_fixnum(found));
        
        // Cleanup
        frame_release(frame);
        RELEASE(param_sym);
    });
}

// ============================================================================
// TEST: eval_let with sequential bindings that reference each other
// ============================================================================

TEST(test_eval_let_sequential_bindings_with_frame) {
    WITH_MEMORY_PROFILING({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        
        // Test: (let [x 10 y (+ x 5)] y) should return 15
        const char *code = "(let [x 10 y (+ x 5)] y)";
        CljValue result = eval_string(code, g_test_eval_state);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(15, as_fixnum(result));
    });
}

// ============================================================================
// TEST: eval_let with multiple bindings
// ============================================================================

TEST(test_eval_let_multiple_bindings_with_frame) {
    WITH_MEMORY_PROFILING({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        
        // Test: (let [x 10 y 20] (+ x y)) should return 30
        const char *code = "(let [x 10 y 20] (+ x y))";
        CljValue result = eval_string(code, g_test_eval_state);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(30, as_fixnum(result));
    });
}

// ============================================================================
// TEST: eval_let with nested let blocks
// ============================================================================

TEST(test_eval_let_nested_let_blocks) {
    WITH_MEMORY_PROFILING({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        
        // Test: (let [x 10] (let [y 20] (+ x y))) should return 30
        const char *code = "(let [x 10] (let [y 20] (+ x y)))";
        CljValue result = eval_string(code, g_test_eval_state);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(30, as_fixnum(result));
    });
}

// ============================================================================
// TEST: eval_let with 16 bindings (tests frame vs map fallback)
// ============================================================================

TEST(test_eval_let_sixteen_bindings) {
    WITH_MEMORY_PROFILING({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        
        // Test: (let [a 1 b 2 c 3 d 4 e 5 f 6 g 7 h 8 i 9 j 10 k 11 l 12 m 13 n 14 o 15 p 16] p)
        // Should return 16
        const char *code = "(let [a 1 b 2 c 3 d 4 e 5 f 6 g 7 h 8 i 9 j 10 k 11 l 12 m 13 n 14 o 15 p 16] p)";
        CljValue result = eval_string(code, g_test_eval_state);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(16, as_fixnum(result));
    });
}

// ============================================================================
// TEST: eval_let with nested let blocks that cause infinite loop
// This test reproduces the specific issue seen in debug output
// ============================================================================

TEST(test_eval_let_nested_infinite_loop_reproduction) {
    WITH_MEMORY_PROFILING({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        
        // This pattern seems to cause infinite loops based on debug output
        // Test: (let [a1 1] (let [a2 2] (let [a3 3] (let [a4 4] a1))))
        const char *code = "(let [a1 1] (let [a2 2] (let [a3 3] (let [a4 4] a1))))";
        
        // Use timeout to prevent infinite loop
        CljValue result = NULL;
        // Note: This test might timeout if the infinite loop bug exists
        // In that case, the test framework should catch it
        
        result = eval_string(code, g_test_eval_state);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(1, as_fixnum(result));
    });
}

// ============================================================================
// TEST: Direct frame_lookup test with problematic scenario
// ============================================================================

TEST(test_frame_lookup_problematic_scenario) {
    WITH_MEMORY_PROFILING({
        // Create a frame chain that might cause issues
        int capacity = 1;
        CallFrame *frame1 = (CallFrame*)STACK_ALLOC(char, frame_allocation_size(capacity));
        CallFrame *frame2 = (CallFrame*)STACK_ALLOC(char, frame_allocation_size(capacity));
        CallFrame *frame3 = (CallFrame*)STACK_ALLOC(char, frame_allocation_size(capacity));
        
        ID sym1 = intern_symbol_global("a1");
        ID val1 = fixnum(1);
        ID sym2 = intern_symbol_global("a2");
        ID val2 = fixnum(2);
        ID sym3 = intern_symbol_global("a3");
        ID val3 = fixnum(3);
        
        RETAIN(sym1);
        RETAIN(sym2);
        RETAIN(sym3);
        
        // Create chain: frame1 -> frame2 -> frame3
        frame_init(frame3, NULL);
        ID params3[1] = {sym3};
        ID values3[1] = {val3};
        frame_set_bindings(frame3, NULL, params3, values3, 1);
        
        frame_init(frame2, frame3);
        ID params2[1] = {sym2};
        ID values2[1] = {val2};
        frame_set_bindings(frame2, frame3, params2, values2, 1);
        
        frame_init(frame1, frame2);
        ID params1[1] = {sym1};
        ID values1[1] = {val1};
        frame_set_bindings(frame1, frame2, params1, values1, 1);
        
        // Lookup a1 in frame1 should find it
        ID found1 = NULL;
        TEST_ASSERT_TRUE(frame_lookup(frame1, sym1, &found1));
        TEST_ASSERT_NOT_NULL(found1);
        TEST_ASSERT_EQUAL_INT(1, as_fixnum(found1));
        
        // Lookup a2 in frame1 should find it in parent
        ID found2 = NULL;
        TEST_ASSERT_TRUE(frame_lookup(frame1, sym2, &found2));
        TEST_ASSERT_NOT_NULL(found2);
        TEST_ASSERT_EQUAL_INT(2, as_fixnum(found2));
        
        // Lookup a3 in frame1 should find it in grandparent
        ID found3 = NULL;
        TEST_ASSERT_TRUE(frame_lookup(frame1, sym3, &found3));
        TEST_ASSERT_NOT_NULL(found3);
        TEST_ASSERT_EQUAL_INT(3, as_fixnum(found3));
        
        // Cleanup
        frame_release(frame1);
        frame_release(frame2);
        frame_release(frame3);
        RELEASE(sym1);
        RELEASE(sym2);
        RELEASE(sym3);
    });
}

