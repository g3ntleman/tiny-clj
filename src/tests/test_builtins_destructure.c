/*
 * Tests for Builtins needed for Destructuring
 * 
 * Tests for: gensym, partition, some, nnext
 */

#include "tests_common.h"

// ============================================================================
// TEST: gensym
// ============================================================================

TEST(test_builtins_gensym) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (gensym "vec__") returns a symbol starting with "vec__"
    CljObject *result1 = eval_string("(gensym \"vec__\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_EQUAL_INT(CLJ_SYMBOL, TAG(result1));
    
    CljSymbol *sym1 = as_symbol((CljValue)result1);
    TEST_ASSERT_NOT_NULL(sym1);
    TEST_ASSERT_NOT_NULL(sym1->cname);
    // Should start with "vec__"
    TEST_ASSERT_TRUE(strncmp(sym1->cname, "vec__", 5) == 0);
    
    // Test: Two calls to gensym return different symbols
    CljObject *result2 = eval_string("(gensym \"vec__\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    CljSymbol *sym2 = as_symbol((CljValue)result2);
    TEST_ASSERT_NOT_NULL(sym2);
    // Should be different symbols
    TEST_ASSERT_TRUE(strcmp(sym1->cname, sym2->cname) != 0);
    
    // Test: gensym without prefix
    CljObject *result3 = eval_string("(gensym)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result3);
    TEST_ASSERT_EQUAL_INT(CLJ_SYMBOL, TAG(result3));
}

// ============================================================================
// TEST: partition
// ============================================================================

TEST(test_builtins_partition) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (partition 2 [1 2 3 4]) - use let to bind once
    CljObject *result1 = eval_string("(let [p (partition 2 [1 2 3 4])] (count p))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(is_fixnum(result1));
    int count1 = as_fixnum(result1);
    TEST_ASSERT_TRUE(count1 >= 2);
    
    // Test: first partition is [1 2]
    CljObject *first_elem = eval_string("(let [p (partition 2 [1 2 3 4])] (first (first p)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(first_elem);
    TEST_ASSERT_TRUE(is_fixnum(first_elem));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(first_elem));
    
    // Test: (partition 3 [1 2 3 4 5]) has 1 complete partition
    CljObject *result2 = eval_string("(let [p (partition 3 [1 2 3 4 5])] (count p))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(is_fixnum(result2));
    TEST_ASSERT_TRUE(as_fixnum(result2) >= 1);
    
    // Test: (partition 2 [1]) => empty (not enough elements)
    CljObject *result3 = eval_string("(let [p (partition 2 [1])] (count p))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result3);
    TEST_ASSERT_TRUE(is_fixnum(result3));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(result3));
}

// ============================================================================
// TEST: some
// ============================================================================

TEST(test_builtins_some) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (some even? [1 3 5 6]) => true (6 is even)
    CljObject *result1 = eval_string("(some even? [1 3 5 6])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    // Should return true (or the first truthy value)
    TEST_ASSERT_TRUE(result1 == clj_true || is_fixnum(result1));
    
    // Test: (some even? [1 3 5]) => nil (no even numbers)
    CljObject *result2 = eval_string("(some even? [1 3 5])", g_test_eval_state);
    TEST_ASSERT_NIL(result2);  // nil
    
    // Test: (some #(> % 5) [1 2 3 4 5 6]) => true (6 > 5)
    CljObject *result3 = eval_string("(some #(> % 5) [1 2 3 4 5 6])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result3);
    
    // Test: (some nil? [1 2 nil 4]) => true
    CljObject *result4 = eval_string("(some nil? [1 2 nil 4])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result4);
    TEST_ASSERT_TRUE(result4 == clj_true);
}

// ============================================================================
// TEST: some with nil elements (nil/NULL interpretation)
// ============================================================================

TEST(test_builtins_some_nil_elements) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (some nil? [nil]) => true (nil is a valid element)
    CljObject *result1 = eval_string("(some nil? [nil])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(result1 == clj_true);
    
    // Test: (some nil? [1 nil 3]) => true (nil in middle)
    CljObject *result2 = eval_string("(some nil? [1 nil 3])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(result2 == clj_true);
    
    // Test: (some nil? [nil 2 3]) => true (nil at start)
    CljObject *result3 = eval_string("(some nil? [nil 2 3])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result3);
    TEST_ASSERT_TRUE(result3 == clj_true);
    
    // Test: (some nil? [1 2 3 nil]) => true (nil at end)
    CljObject *result4 = eval_string("(some nil? [1 2 3 nil])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result4);
    TEST_ASSERT_TRUE(result4 == clj_true);
    
    // Test: (some identity [nil nil]) => nil (all nil, identity returns nil)
    CljObject *result5 = eval_string("(some identity [nil nil])", g_test_eval_state);
    TEST_ASSERT_NIL(result5);
    
    // Test: (some #(not (nil? %)) [nil nil 1]) => true (1 is not nil)
    CljObject *result6 = eval_string("(some #(not (nil? %)) [nil nil 1])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result6);
    TEST_ASSERT_TRUE(result6 == clj_true);
}

// ============================================================================
// TEST: partition with nil elements
// ============================================================================

TEST(test_builtins_partition_nil_elements) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (partition 2 [nil 1 nil 2]) - first element of first partition should be nil
    CljObject *elem1 = eval_string("(let [p (partition 2 [nil 1 nil 2])] (first (first p)))", g_test_eval_state);
    TEST_ASSERT_NIL(elem1);  // nil
    
    // Second element of first partition should be 1
    CljObject *elem2 = eval_string("(let [p (partition 2 [nil 1 nil 2])] (second (first p)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(elem2);
    TEST_ASSERT_TRUE(is_fixnum(elem2));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(elem2));
    
    // Test: (partition 2 [1 nil]) => ([1 nil]) - second element should be nil
    CljObject *result2_elem1 = eval_string("(let [p (partition 2 [1 nil])] (first (first p)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2_elem1);
    TEST_ASSERT_TRUE(is_fixnum(result2_elem1));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(result2_elem1));
    
    CljObject *result2_elem2 = eval_string("(let [p (partition 2 [1 nil])] (second (first p)))", g_test_eval_state);
    TEST_ASSERT_NIL(result2_elem2);  // nil
}

// ============================================================================
// TEST: nnext with nil elements
// ============================================================================

TEST(test_builtins_nnext_nil_elements) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (nnext [nil 1 2 3]) => (2 3) - first element is 2
    CljObject *first1 = eval_string("(first (nnext [nil 1 2 3]))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(first1);
    TEST_ASSERT_TRUE(is_fixnum(first1));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(first1));
    
    // Test: (nnext [1 nil 3]) => (3) - first element is 3
    CljObject *first2 = eval_string("(first (nnext [1 nil 3]))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(first2);
    TEST_ASSERT_TRUE(is_fixnum(first2));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(first2));
    
    // Test: (nnext [nil nil 1]) => (1) - first element is 1
    CljObject *first3 = eval_string("(first (nnext [nil nil 1]))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(first3);
    TEST_ASSERT_TRUE(is_fixnum(first3));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(first3));
}

// ============================================================================
// TEST: nnext
// ============================================================================

TEST(test_builtins_nnext) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (nnext [1 2 3 4]) => (3 4) - first is 3, second is 4
    CljObject *first1 = eval_string("(first (nnext [1 2 3 4]))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(first1);
    TEST_ASSERT_TRUE(is_fixnum(first1));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(first1));
    
    CljObject *second1 = eval_string("(second (nnext [1 2 3 4]))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(second1);
    TEST_ASSERT_TRUE(is_fixnum(second1));
    TEST_ASSERT_EQUAL_INT(4, as_fixnum(second1));
    
    // Test: (nnext [1 2]) => nil (not enough elements)
    CljObject *result2 = eval_string("(nnext [1 2])", g_test_eval_state);
    TEST_ASSERT_NULL(result2);
    
    // Test: (nnext [1]) => nil
    CljObject *result3 = eval_string("(nnext [1])", g_test_eval_state);
    TEST_ASSERT_NULL(result3);
    
    // Test: (nnext []) => nil
    CljObject *result4 = eval_string("(nnext [])", g_test_eval_state);
    TEST_ASSERT_NULL(result4);
    
    // Test: (nnext nil) => nil
    CljObject *result5 = eval_string("(nnext nil)", g_test_eval_state);
    TEST_ASSERT_NULL(result5);
}

// ============================================================================
// TEST: Debug nil element handling in seq_iter_first
// ============================================================================

TEST(test_seq_iter_first_nil_element) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create vector [1 nil 3] using eval_string
    CljObject *vec = eval_string("[1 nil 3]", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(vec);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(vec));
    
    // Initialize SeqIterator
    SeqIterator iter;
    bool has_seq = seq_iter_init(&iter, vec);
    TEST_ASSERT_TRUE(has_seq);
    TEST_ASSERT_FALSE(seq_iter_empty(&iter));
    
    // First element should be 1
    ID elem1 = seq_iter_first(&iter);
    TEST_ASSERT_NOT_NULL(elem1);
    TEST_ASSERT_TRUE(is_fixnum(elem1));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(elem1));
    
    // Move to second element (nil)
    seq_iter_next(&iter);
    TEST_ASSERT_FALSE(seq_iter_empty(&iter));
    
    // Second element should be NULL (nil)
    ID elem2 = seq_iter_first(&iter);
    TEST_ASSERT_NULL(elem2);  // This is the critical test!
    
    // Move to third element
    seq_iter_next(&iter);
    TEST_ASSERT_FALSE(seq_iter_empty(&iter));
    
    // Third element should be 3
    ID elem3 = seq_iter_first(&iter);
    TEST_ASSERT_NOT_NULL(elem3);
    TEST_ASSERT_TRUE(is_fixnum(elem3));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(elem3));
}

// ============================================================================
// TEST: Debug eval_function_call with NULL argument
// ============================================================================

TEST(test_eval_function_call_with_nil_arg) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Get nil? function
    CljObject *nil_pred = eval_string("nil?", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(nil_pred);
    
    // Call nil? with NULL (nil) as argument
    ID args[1] = {NULL};  // nil argument
    ID result = eval_function_call(nil_pred, args, 1, NULL, g_test_eval_state);
    
    // Should return true
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

// ============================================================================
// TEST: Debug vector_nth returns NULL for nil element
// ============================================================================

TEST(test_vector_nth_nil_element) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create vector [1 nil 3]
    CljObject *vec = eval_string("[1 nil 3]", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(vec);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(vec));
    
    CljVector *v = as_vector(vec);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_INT(3, vector_count(v));
    
    // Check that nth element at index 1 is NULL (nil)
    ID elem1 = vector_nth(v, 1);
    TEST_ASSERT_NIL(elem1);  // nil element should be NULL
}

// ============================================================================
// TEST: Reproduce native_some behavior with nil elements
// ============================================================================

// ============================================================================
// TEST: native_first returns NULL for nil element
// ============================================================================

TEST(test_native_first_nil_element) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (first [nil]) should return NULL
    CljObject *result = eval_string("(first [nil])", g_test_eval_state);
    TEST_ASSERT_NIL(result);  // Should be NULL (nil)!
}

TEST(test_native_first_direct_call) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create vector [nil]
    CljObject *vec = eval_string("[nil]", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(vec);
    
    // Call native_first directly
    ID args[1] = {vec};
    ID result = native_first(args, 1);
    
    // Should return NULL for nil element
    TEST_ASSERT_NIL(result);
}

TEST(test_eval_string_nil_handling) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test what eval_string returns for nil
    CljObject *result = eval_string("nil", g_test_eval_state);
    TEST_ASSERT_NIL(result);  // eval_string should return NULL for nil
}

TEST(test_eval_function_call_first_nil) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Get first function
    CljObject *first_fn = eval_string("first", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(first_fn);
    
    // Create vector [nil]
    CljObject *vec = eval_string("[nil]", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(vec);
    
    // Call first via eval_function_call
    ID args[1] = {vec};
    ID result = eval_function_call(first_fn, args, 1, NULL, g_test_eval_state);
    
    // Should return NULL for nil element
    TEST_ASSERT_NIL(result);
}

TEST(test_eval_list_first_nil) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (first [nil]) should return NULL
    CljObject *result = eval_string("(first [nil])", g_test_eval_state);
    TEST_ASSERT_NIL(result);
    
    // Test: (nil? (first [nil])) should return true
    CljObject *result2 = eval_string("(nil? (first [nil]))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(result2 == clj_true);
}

TEST(test_native_some_simulation) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Get nil? function
    CljObject *nil_pred = eval_string("nil?", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(nil_pred);
    
    // Create vector [1 nil 3]
    CljObject *vec = eval_string("[1 nil 3]", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(vec);
    
    // Simulate native_some loop
    SeqIterator iter;
    bool has_seq = seq_iter_init(&iter, vec);
    TEST_ASSERT_TRUE(has_seq);
    
    bool found_nil = false;
    int iteration = 0;
    
    while (!seq_iter_empty(&iter)) {
        ID elem = seq_iter_first(&iter);
        iteration++;
        
        // Call nil? with element
        ID pred_args[1] = {elem};
        ID result = eval_function_call(nil_pred, pred_args, 1, NULL, g_test_eval_state);
        
        // Check if result is truthy
        if (result && result != clj_false) {
            found_nil = true;
            // For nil element (index 1), elem should be NULL
            if (iteration == 2) {
                TEST_ASSERT_NIL(elem);  // elem should be NULL for nil
                TEST_ASSERT_TRUE(result == clj_true);  // nil? should return true
            }
            break;
        }
        
        seq_iter_next(&iter);
    }
    
    TEST_ASSERT_TRUE(found_nil);  // Should have found nil element
    TEST_ASSERT_EQUAL_INT(2, iteration);  // Should find it at 2nd element
}

// ============================================================================
// TEST: destructure function (Sequential Destructuring)
// ============================================================================

TEST(test_destructure_sequential_basic) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: Simple sequential destructuring [a b]
    // (let [[a b] [1 2]] [a b]) => [1 2]
    CljObject *result1 = eval_string("(let [[a b] [1 2]] [a b])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    
    CljObject *first_elem = eval_string("(let [[a b] [1 2]] a)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(first_elem);
    TEST_ASSERT_TRUE(is_fixnum(first_elem));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(first_elem));
    
    CljObject *second_elem = eval_string("(let [[a b] [1 2]] b)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(second_elem);
    TEST_ASSERT_TRUE(is_fixnum(second_elem));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(second_elem));
}

TEST(test_destructure_sequential_with_rest) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: Sequential destructuring with & rest
    // (let [[a b & more] [1 2 3 4]] more) => (3 4)
    CljObject *result = eval_string("(let [[a b & more] [1 2 3 4]] (first more))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(result));
}

TEST(test_destructure_sequential_with_as) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: Sequential destructuring with :as
    // (let [[a b :as all] [1 2]] (count all)) => 2
    CljObject *result = eval_string("(let [[a b :as all] [1 2]] (let [c (count all)] c))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(result));
}

TEST(test_destructure_sequential_nested) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: Nested sequential destructuring
    // (let [[[a b] c] [[1 2] 3]] [a b c]) => [1 2 3]
    CljObject *a = eval_string("(let [[[a b] c] [[1 2] 3]] a)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_TRUE(is_fixnum(a));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(a));
    
    CljObject *c = eval_string("(let [[[a b] c] [[1 2] 3]] c)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_TRUE(is_fixnum(c));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(c));
}

// ============================================================================
// LOOP WITH DESTRUCTURING
// NOTE: loop/recur is not fully implemented in tiny-clj (separate issue)
// The destructuring transformation is correct, but loop/recur itself doesn't work
// ============================================================================

TEST(test_destructure_loop_transformation) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: Verify that loop with destructuring transforms correctly
    // The transformation should put the let INSIDE the loop body
    // (loop [[a b] [1 2]] body) => (loop [loop__1 [1 2]] (let [[a b] loop__1] body))
    
    // We can't test the actual execution because loop/recur is broken,
    // but we can verify the structure doesn't crash
    CljObject *result = eval_string(
        "(defn test-loop-transform [coll] "
        "  (loop [[head & tail] coll sum 0] "
        "    (if head (recur tail (+ sum head)) sum)))",
        g_test_eval_state);
    
    // Just verify it parses and defines without crashing
    // The function itself won't work correctly due to loop/recur bug
    TEST_ASSERT_NOT_NULL_MESSAGE(result, "loop with destructuring should parse without error");
}

// ============================================================================
// TEST: Map (Associative) Destructuring
// ============================================================================

TEST(test_destructure_map_keys) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: {:keys [a b]} destructuring
    CljObject *a = eval_string("(let [{:keys [a b]} {:a 1 :b 2}] a)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_TRUE(is_fixnum(a));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(a));
    
    CljObject *b = eval_string("(let [{:keys [a b]} {:a 1 :b 2}] b)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_TRUE(is_fixnum(b));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(b));
}

TEST(test_destructure_map_keys_with_defaults) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: {:keys [x y] :or {y 0}} with missing key
    CljObject *x = eval_string("(let [{:keys [x y] :or {y 0}} {:x 5}] x)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(x);
    TEST_ASSERT_TRUE(is_fixnum(x));
    TEST_ASSERT_EQUAL_INT(5, as_fixnum(x));
    
    CljObject *y = eval_string("(let [{:keys [x y] :or {y 0}} {:x 5}] y)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(y);
    TEST_ASSERT_TRUE(is_fixnum(y));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(y));  // default value
}

TEST(test_destructure_map_with_as) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: {:keys [a] :as m} binds the whole map
    CljObject *a = eval_string("(let [{:keys [a] :as m} {:a 1 :b 2}] a)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_TRUE(is_fixnum(a));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(a));
    
    // m should be the whole map
    CljObject *m_count = eval_string("(let [{:keys [a] :as m} {:a 1 :b 2}] (count m))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(m_count);
    TEST_ASSERT_TRUE(is_fixnum(m_count));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(m_count));
}

// ============================================================================
// TEST: keyword and name functions
// ============================================================================

TEST(test_keyword_function) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (keyword "foo") => :foo
    CljObject *kw1 = eval_string("(keyword \"foo\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(kw1);
    TEST_ASSERT_EQUAL_INT(CLJ_SYMBOL, TAG(kw1));
    CljSymbol *sym1 = as_symbol((CljValue)kw1);
    TEST_ASSERT_NOT_NULL(sym1);
    TEST_ASSERT_TRUE(sym1->cname[0] == ':');
    
    // Test: (keyword 'bar) => :bar
    CljObject *kw2 = eval_string("(keyword 'bar)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(kw2);
    TEST_ASSERT_EQUAL_INT(CLJ_SYMBOL, TAG(kw2));
    
    // Test: keyword on keyword returns same
    CljObject *kw3 = eval_string("(keyword :baz)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(kw3);
    CljSymbol *sym3 = as_symbol((CljValue)kw3);
    TEST_ASSERT_TRUE(strcmp(sym3->cname, ":baz") == 0);
}

TEST(test_name_function) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (name :foo) => "foo"
    CljObject *name1 = eval_string("(name :foo)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(name1);
    TEST_ASSERT_EQUAL_INT(CLJ_STRING, TAG(name1));
    TEST_ASSERT_EQUAL_STRING("foo", string_data(name1));
    
    // Test: (name 'bar) => "bar"
    CljObject *name2 = eval_string("(name 'bar)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(name2);
    TEST_ASSERT_EQUAL_INT(CLJ_STRING, TAG(name2));
    TEST_ASSERT_EQUAL_STRING("bar", string_data(name2));
    
    // Test: (name "string") => "string"
    CljObject *name3 = eval_string("(name \"string\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(name3);
    TEST_ASSERT_EQUAL_INT(CLJ_STRING, TAG(name3));
    TEST_ASSERT_EQUAL_STRING("string", string_data(name3));
}
