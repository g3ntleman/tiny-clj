/*
 * Unity Tests for Atom Implementation in Tiny-CLJ
 * 
 * Test-First: Tests for atom, deref, reset!, swap! functionality
 */

#include "tests_common.h"
#include "../tiny_clj.h"
#include "../memory.h"
#include "../strings.h"
#include "../to_string.h"
#include "../object.h"
#include "../value.h"
#include "../runtime.h"
#include "../atom.h"
#include "../namespace.h"
#include "../symbol.h"
#include <subjective-c/map.h>
#include "../kv_macros.h"
#include "../reader.h"
#include "../eval.h"
#include "../list.h"
#include "../builtins.h"

// Forward declaration for clojure_core_code
extern const char *clojure_core_code;

// Get the global test evalState (with inc available)
extern EvalState* test_get_eval_state(void);

// ============================================================================
// TEST: Atom Creation
// ============================================================================

TEST(test_atom_create_with_value) {
    CljAtom *atom = make_atom(fixnum(42));
    TEST_ASSERT_NOT_NULL(atom);
    TEST_ASSERT_EQUAL(CLJ_ATOM, TAG((CljObject*)atom));
    TEST_ASSERT_EQUAL(42, as_fixnum((CljValue)atom->value));
    RELEASE(atom);
}

TEST(test_atom_create_with_nil) {
    CljAtom *atom = make_atom(NULL);
    TEST_ASSERT_NOT_NULL(atom);
    TEST_ASSERT_EQUAL(CLJ_ATOM, TAG((CljObject*)atom));
    TEST_ASSERT_NULL(atom->value);
    RELEASE(atom);
}

TEST(test_atom_create_with_string) {
    CljObject *str = (CljObject*)make_string("hello");
    CljAtom *atom = make_atom(str);
    TEST_ASSERT_NOT_NULL(atom);
    TEST_ASSERT_NOT_NULL(atom->value);
    RELEASE(atom);
    RELEASE(str);
}

// ============================================================================
// TEST: Atom Deref (@)
// ============================================================================

TEST(test_atom_deref_returns_value) {
    CljAtom *atom = make_atom(fixnum(42));
    ID value = atom_deref(atom);
    TEST_ASSERT_NOT_NULL(value);
    TEST_ASSERT_EQUAL(42, as_fixnum((CljValue)value));
    RELEASE(value);
    RELEASE(atom);
}

TEST(test_atom_deref_nil) {
    CljAtom *atom = make_atom(NULL);
    ID value = atom_deref(atom);
    TEST_ASSERT_NULL(value);
    RELEASE(atom);
}

// ============================================================================
// TEST: Atom Reset (reset!)
// ============================================================================

TEST(test_atom_reset_changes_value) {
    CljAtom *atom = make_atom(fixnum(42));
    ID new_value = atom_reset(atom, fixnum(100));
    TEST_ASSERT_NOT_NULL(new_value);
    TEST_ASSERT_EQUAL(100, as_fixnum((CljValue)new_value));
    TEST_ASSERT_EQUAL(100, as_fixnum((CljValue)atom->value));
    RELEASE(new_value);
    RELEASE(atom);
}

TEST(test_atom_reset_to_nil) {
    CljAtom *atom = make_atom(fixnum(42));
    ID new_value = atom_reset(atom, NULL);
    TEST_ASSERT_NULL(new_value);
    TEST_ASSERT_NULL(atom->value);
    RELEASE(atom);
}

// ============================================================================
// TEST: Atom Swap (swap!)
// ============================================================================

TEST(test_atom_swap_simple) {
    CljAtom *atom = make_atom(fixnum(42));
    
    // Get 'inc' function from clojure.core using global test evalState
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    CljSymbol *inc_sym = intern_symbol_global("inc");
    ID inc_func = ns_resolve(g_test_eval_state, inc_sym);
    
    if (inc_func) {
        ID args[] = {};
        ID result = atom_swap(atom, inc_func, args, 0);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum((CljValue)result));
        TEST_ASSERT_EQUAL(43, as_fixnum((CljValue)result));
        // Don't RELEASE result - atom_swap returns autoreleased object
    } else {
        TEST_FAIL_MESSAGE("Could not resolve 'inc' function from clojure.core");
    }
    
    RELEASE(atom);
    // Don't free st - it's the global test evalState
}

TEST(test_atom_swap_with_args) {
    CljAtom *atom = make_atom(fixnum(10));
    
    // Get '+' function from clojure.core using global test evalState
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    CljSymbol *plus_sym = intern_symbol_global("+");
    ID plus_func = ns_resolve(g_test_eval_state, plus_sym);
    
    if (plus_func) {
        ID args[] = {fixnum(5)};
        ID result = atom_swap(atom, plus_func, args, 1);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum((CljValue)result));
        TEST_ASSERT_EQUAL(15, as_fixnum((CljValue)result));
        RELEASE(result);
    } else {
        TEST_FAIL_MESSAGE("Could not resolve '+' function from clojure.core");
    }
    
    RELEASE(atom);
    // Don't free st - it's the global test evalState
}

TEST(test_atom_swap_persists) {
    CljAtom *atom = make_atom(fixnum(42));
    
    // Get 'inc' function using global test evalState
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    CljSymbol *inc_sym = intern_symbol_global("inc");
    ID inc_func = ns_resolve(g_test_eval_state, inc_sym);
    
    if (inc_func) {
        ID args[] = {};
        atom_swap(atom, inc_func, args, 0);
        
        // Verify value persisted
        ID value = atom_deref(atom);
        TEST_ASSERT_NOT_NULL(value);
        TEST_ASSERT_EQUAL(43, as_fixnum((CljValue)value));
        RELEASE(value);
    }
    
    RELEASE(atom);
    // Don't free st - it's the global test evalState
}

TEST(test_atom_swap_multiple_times) {
    CljAtom *atom = make_atom(fixnum(0));
    
    // Get 'inc' function using global test evalState
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    CljSymbol *inc_sym = intern_symbol_global("inc");
    ID inc_func = ns_resolve(g_test_eval_state, inc_sym);
    
    if (inc_func) {
        ID args[] = {};
        for (int i = 0; i < 5; i++) {
            ID result = atom_swap(atom, inc_func, args, 0);
            RELEASE(result);
        }
        
        ID value = atom_deref(atom);
        TEST_ASSERT_NOT_NULL(value);
        TEST_ASSERT_EQUAL(5, as_fixnum((CljValue)value));
        RELEASE(value);
    }
    
    RELEASE(atom);
    // Don't free st - it's the global test evalState
}

// ============================================================================
// TEST: Built-in Functions (atom, deref, reset!, swap!)
// ============================================================================

TEST(test_atom_builtin_creates_atom) {
    ID args[] = {fixnum(42)};
    ID result = native_atom(args, 1);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result && TAG(result) == CLJ_ATOM);
    RELEASE(result);
}

TEST(test_atom_builtin_deref) {
    CljAtom *atom = make_atom(fixnum(42));
    ID args[] = {atom};
    ID result = native_deref(args, 1);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL(42, as_fixnum((CljValue)result));
    RELEASE(result);
    RELEASE(atom);
}

TEST(test_atom_builtin_reset_bang) {
    CljAtom *atom = make_atom(fixnum(42));
    ID args[] = {atom, fixnum(100)};
    ID result = native_reset_bang(args, 2);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL(100, as_fixnum((CljValue)result));
    TEST_ASSERT_EQUAL(100, as_fixnum((CljValue)atom->value));
    RELEASE(result);
    RELEASE(atom);
}

TEST(test_atom_builtin_swap_bang) {
    CljAtom *atom = make_atom(fixnum(42));
    
    // Get 'inc' function using global test evalState
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    CljSymbol *inc_sym = intern_symbol_global("inc");
    ID inc_func = ns_resolve(g_test_eval_state, inc_sym);
    
    if (inc_func) {
        ID args[] = {atom, inc_func};
        ID result = native_swap_bang(args, 2);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_EQUAL(43, as_fixnum((CljValue)result));
        RELEASE(result);
    }
    
    RELEASE(atom);
    // Don't free st - it's the global test evalState
}

// ============================================================================
// TEST: Memory Management
// ============================================================================

TEST(test_atom_memory_management) {
    CljObject *str = (CljObject*)make_string("test");
    CljAtom *atom = make_atom(str);
    
    // Atom should retain the string (rc=1 from make_string, +1 from make_atom)
    TEST_ASSERT_EQUAL(2, ((CljObject*)str)->rc);
    
    RELEASE(atom);
    // After releasing atom, atom's value should be released, so str->rc should be 1
    // (only the original reference from make_string remains)
    TEST_ASSERT_EQUAL(1, ((CljObject*)str)->rc);
    
    RELEASE(str);
}

// ============================================================================
// TEST: Reference Sharing
// ============================================================================

TEST(test_atom_reference_sharing) {
    CljAtom *atom1 = make_atom(fixnum(42));
    CljAtom *atom2 = make_atom(fixnum(42));
    
    // Atoms should be independent
    atom_reset(atom1, fixnum(100));
    ID value2 = atom_deref(atom2);
    TEST_ASSERT_EQUAL(42, as_fixnum((CljValue)value2));
    RELEASE(value2);
    
    RELEASE(atom1);
    RELEASE(atom2);
}

// ============================================================================
// TEST: Print Representation
// ============================================================================

TEST(test_atom_print_representation) {
    CljAtom *atom = make_atom(fixnum(42));
    CljString *str = pr_str(atom);
    TEST_ASSERT_NOT_NULL(str);
    TEST_ASSERT_NOT_NULL(strstr(string_data(str), "Atom"));
    RELEASE(atom);
}

// ============================================================================
// TEST: Error Handling
// ============================================================================

TEST(test_atom_deref_invalid) {
    ID args[] = {fixnum(42)};
    TRY {
        (void)native_deref(args, 1);
        // Should not reach here - exception should be thrown
        TEST_FAIL_MESSAGE("deref should throw exception for invalid argument");
    } CATCH(ex) {
        // Exception expected for invalid argument
        TEST_ASSERT_NOT_NULL_MESSAGE(ex, "Exception should be thrown");
        TEST_ASSERT_NOT_NULL_MESSAGE(ex->message, "Exception message should be set");
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(ex->message, "atom"), "Exception should mention 'atom'");
    } END_TRY
}

TEST(test_atom_reset_invalid) {
    ID args[] = {fixnum(42), fixnum(100)};
    TRY {
        (void)native_reset_bang(args, 2);
        // Should not reach here - exception should be thrown
        TEST_FAIL_MESSAGE("reset! should throw exception for invalid argument");
    } CATCH(ex) {
        // Exception expected for invalid argument
        TEST_ASSERT_NOT_NULL_MESSAGE(ex, "Exception should be thrown");
        TEST_ASSERT_NOT_NULL_MESSAGE(ex->message, "Exception message should be set");
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(ex->message, "atom"), "Exception should mention 'atom'");
    } END_TRY
}

TEST(test_atom_swap_invalid_atom) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    CljSymbol *inc_sym = intern_symbol_global("inc");
    ID inc_func = ns_resolve(g_test_eval_state, inc_sym);
    
    if (inc_func) {
        ID args[] = {fixnum(42), inc_func};
        TRY {
            (void)native_swap_bang(args, 2);
            // Should not reach here - exception should be thrown
            TEST_FAIL_MESSAGE("swap! should throw exception for invalid atom argument");
        } CATCH(ex) {
            // Exception expected for invalid argument
            TEST_ASSERT_NOT_NULL_MESSAGE(ex, "Exception should be thrown");
            TEST_ASSERT_NOT_NULL_MESSAGE(ex->message, "Exception message should be set");
            TEST_ASSERT_NOT_NULL_MESSAGE(strstr(ex->message, "atom"), "Exception should mention 'atom'");
        } END_TRY
    }
    
    // Don't free st - it's the global test evalState
}

// ============================================================================
// TEST: Real-world Usage
// ============================================================================

TEST(test_atom_real_world_usage) {
    // Create atom
    CljAtom *counter = make_atom(fixnum(0));
    
    // Get 'inc' function using global test evalState
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    CljSymbol *inc_sym = intern_symbol_global("inc");
    ID inc_func = ns_resolve(g_test_eval_state, inc_sym);
    
    if (inc_func) {
        ID args[] = {};
        // Increment multiple times
        for (int i = 0; i < 10; i++) {
            ID result = atom_swap(counter, inc_func, args, 0);
            RELEASE(result);
        }
        
        // Verify final value
        ID value = atom_deref(counter);
        TEST_ASSERT_NOT_NULL(value);
        TEST_ASSERT_EQUAL(10, as_fixnum((CljValue)value));
        RELEASE(value);
    }
    
    RELEASE(counter);
    // Don't free st - it's the global test evalState
}

