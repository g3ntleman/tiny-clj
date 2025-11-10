/*
 * Debug Tests for While Loop with Atoms
 * 
 * Small tests to isolate the problem with hanging while loops.
 */

#include "tests_common.h"
#include "../atom.h"

// ============================================================================
// THESIS 1: Atom creation and deref work correctly
// ============================================================================

TEST(thesis_atom_creation) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (atom 0) => atom with value 0
    ID atom = eval_string("(atom 0)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(atom);
    TEST_ASSERT_TRUE(atom && TAG(atom) == CLJ_ATOM);
}

TEST(thesis_atom_deref) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (let [i (atom 0)] @i) => 0
    ID result = eval_string("(let [i (atom 0)] @i)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(result));
}

// ============================================================================
// THESIS 2: swap! updates atom correctly
// ============================================================================

TEST(thesis_swap_updates_atom) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (let [i (atom 0)] (swap! i inc) @i) => 1
    ID result = eval_string("(let [i (atom 0)] (swap! i inc) @i)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(result));
}

// ============================================================================
// THESIS 3: Condition evaluation with atoms works
// ============================================================================

TEST(thesis_condition_with_atom) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (let [i (atom 0)] (< @i 1)) => true
    ID result = eval_string("(let [i (atom 0)] (< @i 1))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(clj_is_truthy(result));
}

TEST(thesis_condition_with_atom_false) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (let [i (atom 1)] (< @i 1)) => false
    ID result = eval_string("(let [i (atom 1)] (< @i 1))", g_test_eval_state);
    TEST_ASSERT_TRUE(result == NULL || !clj_is_truthy(result));
}

// ============================================================================
// THESIS 4: Condition evaluation in while loop works
// ============================================================================

TEST(thesis_while_condition_evaluation) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (let [i (atom 0)] (while (< @i 1) (swap! i inc)) @i)
    // This should execute once and then stop
    ID result = eval_string("(let [i (atom 0)] (while (< @i 1) (swap! i inc)) @i)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(result));
}

// ============================================================================
// THESIS 5: swap! in while body updates atom correctly
// ============================================================================

TEST(thesis_swap_in_while_body) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (let [i (atom 0)] (swap! i inc) (while (< @i 1) (swap! i inc)) @i)
    // First swap! sets i to 1, then while should not execute
    ID result = eval_string("(let [i (atom 0)] (swap! i inc) (while (< @i 1) (swap! i inc)) @i)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(result));
}

// ============================================================================
// THESIS 6: Condition re-evaluation in while loop
// ============================================================================

TEST(thesis_while_condition_reevaluation) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (let [i (atom 0)] (while (< @i 2) (swap! i inc)) @i)
    // This should execute twice: i=0 -> i=1 -> i=2, then stop
    ID result = eval_string("(let [i (atom 0)] (while (< @i 2) (swap! i inc)) @i)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(result));
}

// ============================================================================
// THESIS 7: Simple while without atoms works
// ============================================================================

TEST(thesis_while_simple_condition) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (while false 42) => nil
    ID result = eval_string("(while false 42)", g_test_eval_state);
    TEST_ASSERT_NULL(result);
}

// ============================================================================
// THESIS 8: let binding with atom works
// ============================================================================

TEST(thesis_let_with_atom) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (let [i (atom 0)] @i) => 0
    ID result = eval_string("(let [i (atom 0)] @i)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(result));
}

// ============================================================================
// THESIS 9: deref in condition works
// ============================================================================

TEST(thesis_deref_in_condition) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (let [i (atom 0)] (< @i 1)) => true
    ID result = eval_string("(let [i (atom 0)] (< @i 1))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(clj_is_truthy(result));
}

// ============================================================================
// THESIS 10: Multiple swap! calls work
// ============================================================================

TEST(thesis_multiple_swap_calls) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (let [i (atom 0)] (swap! i inc) (swap! i inc) @i) => 2
    ID result = eval_string("(let [i (atom 0)] (swap! i inc) (swap! i inc) @i)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(result));
}

// ============================================================================
// THESIS 11: Symbol resolution in let environment
// ============================================================================

TEST(thesis_symbol_resolution_in_let) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (let [i (atom 0)] i) => atom
    ID result = eval_string("(let [i (atom 0)] i)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result && TAG(result) == CLJ_ATOM);
}

// ============================================================================
// THESIS 12: swap! with symbol from let environment
// ============================================================================

TEST(thesis_swap_with_symbol_from_let) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (let [i (atom 0)] (swap! i inc)) => 1
    ID result = eval_string("(let [i (atom 0)] (swap! i inc))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(result));
}

// ============================================================================
// THESIS 13: deref with symbol from let environment
// ============================================================================

TEST(thesis_deref_with_symbol_from_let) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (let [i (atom 0)] @i) => 0
    ID result = eval_string("(let [i (atom 0)] @i)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(result));
}

// ============================================================================
// LOW-LEVEL TEST: Symbol resolution in eval_arg
// ============================================================================

TEST(lowlevel_eval_arg_symbol_resolution) {
    // Create a let_env manually
    CljMap *let_env = (CljMap*)make_map(4);
    TEST_ASSERT_NOT_NULL(let_env);
    
    // Create a symbol "i" (interned)
    CljSymbol *i_sym = intern_symbol_global("i");
    TEST_ASSERT_NOT_NULL(i_sym);
    
    // Create an atom value
    CljAtom *atom = make_atom(fixnum(0));
    TEST_ASSERT_NOT_NULL(atom);
    
    // Store i -> atom in let_env
    CljMap *new_let_env = map_assoc(let_env, i_sym, (CljObject*)atom);
    ASSIGN(let_env, new_let_env);
    
    // Verify i is in let_env
    CljValue found = map_get((CljMap*)let_env, (CljValue)i_sym);
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_TRUE(found && TAG(found) == CLJ_ATOM);
    
    // Create a list (swap! i inc) to test eval_arg using parser
    ID parsed = parse("(swap! i inc)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(parsed);
    TEST_ASSERT_TRUE(parsed && TAG(parsed) == CLJ_LIST);
    CljList *list = as_list(parsed);
    TEST_ASSERT_NOT_NULL(list);
    
    // Test eval_arg with index 1 (should be "i")
    // eval_arg should resolve "i" from let_env
    ID result = eval_arg(list, 1, let_env, g_test_eval_state);
    
    // result should be the atom
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result && TAG(result) == CLJ_ATOM);
    TEST_ASSERT_EQUAL_PTR(atom, result);
    
    // Cleanup
    RELEASE((CljObject*)let_env);
    RELEASE((CljObject*)atom);
    // parsed is autoreleased, no need to RELEASE
}

TEST(lowlevel_eval_arg_symbol_resolution_direct) {
    // Create a let_env manually
    CljMap *let_env = (CljMap*)make_map(4);
    TEST_ASSERT_NOT_NULL(let_env);
    
    // Create a symbol "i" (interned)
    CljSymbol *i_sym = intern_symbol_global("i");
    TEST_ASSERT_NOT_NULL(i_sym);
    
    // Create an atom value
    CljAtom *atom = make_atom(fixnum(0));
    TEST_ASSERT_NOT_NULL(atom);
    
    // Store i -> atom in let_env
    CljMap *new_let_env = map_assoc(let_env, i_sym, (CljObject*)atom);
    ASSIGN(let_env, new_let_env);
    
    // Verify i is in let_env using map_contains
    bool contains = map_contains((CljMap*)let_env, (CljValue)i_sym);
    TEST_ASSERT_TRUE_MESSAGE(contains, "map_contains should find symbol 'i' in let_env");
    
    // Verify i is in let_env using map_get
    CljValue found = map_get((CljMap*)let_env, (CljValue)i_sym);
    TEST_ASSERT_NOT_NULL_MESSAGE(found, "map_get should find symbol 'i' in let_env");
    TEST_ASSERT_TRUE(found && TAG(found) == CLJ_ATOM);
    
    // Create another "i" symbol (should be same pointer if interned)
    CljSymbol *i_sym2 = intern_symbol_global("i");
    TEST_ASSERT_NOT_NULL(i_sym2);
    
    // Verify both symbols are the same pointer (interned)
    TEST_ASSERT_EQUAL_PTR_MESSAGE(i_sym, i_sym2, "Symbol 'i' should be interned (same pointer)");
    
    // Test map_contains with second symbol
    bool contains2 = map_contains((CljMap*)let_env, (CljValue)i_sym2);
    TEST_ASSERT_TRUE_MESSAGE(contains2, "map_contains should find symbol 'i' (second instance) in let_env");
    
    // Test map_get with second symbol
    CljValue found2 = map_get((CljMap*)let_env, (CljValue)i_sym2);
    TEST_ASSERT_NOT_NULL_MESSAGE(found2, "map_get should find symbol 'i' (second instance) in let_env");
    TEST_ASSERT_TRUE(found2 && TAG(found2) == CLJ_ATOM);
    
    // Create list (swap! i inc) using parser to test eval_arg
    // For eval_arg, index 0 is the first element after the function
    // So we need a list like (swap! i inc) where index 1 is "i"
    ID parsed = parse("(swap! i inc)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(parsed);
    TEST_ASSERT_TRUE(parsed && TAG(parsed) == CLJ_LIST);
    CljList *list = as_list(parsed);
    TEST_ASSERT_NOT_NULL(list);
    
    // Test eval_arg with index 1 (should be "i" - second element after swap!)
    ID result = eval_arg(list, 1, let_env, g_test_eval_state);
    
    // result should be the atom
    TEST_ASSERT_NOT_NULL_MESSAGE(result, "eval_arg should resolve symbol 'i' from let_env");
    TEST_ASSERT_TRUE_MESSAGE(result && TAG(result) == CLJ_ATOM, "eval_arg should return atom for symbol 'i'");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(atom, result, "eval_arg should return the same atom");
    
    // Cleanup
    RELEASE((CljObject*)let_env);
    RELEASE((CljObject*)atom);
    // parsed is autoreleased, no need to RELEASE
}

