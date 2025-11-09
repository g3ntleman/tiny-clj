/*
 * Unity Tests for Atom Implementation in Tiny-CLJ
 * 
 * Test-First: Tests for atom, deref, reset!, swap! functionality
 */

#include "tests_common.h"
#include "../tiny_clj.h"
#include "../memory.h"
#include "../clj_strings.h"
#include "../object.h"
#include "../value.h"
#include "../runtime.h"
#include "../atom.h"
#include "../namespace.h"
#include "../symbol.h"
#include "../map.h"
#include "../kv_macros.h"
#include "../reader.h"
#include "../function_call.h"
#include "../list.h"
#include "../builtins.h"

// Forward declaration for clojure_core_code
extern const char *clojure_core_code;

// Get the global test evalState (with inc available)
extern EvalState* test_get_eval_state(void);

// Helper function to get evalState for tests (avoids shadowing)
static EvalState* get_test_eval_state(void) {
    return test_get_eval_state();
}

// ============================================================================
// TEST: Atom Creation
// ============================================================================

TEST(test_atom_create_with_value) {
    CljAtom *atom = make_atom(fixnum(42));
    TEST_ASSERT_NOT_NULL(atom);
    TEST_ASSERT_EQUAL(CLJ_ATOM, TYPE((CljObject*)atom));
    TEST_ASSERT_EQUAL(42, as_fixnum((CljValue)atom->value));
    RELEASE((CljObject*)atom);
}

TEST(test_atom_create_with_nil) {
    CljAtom *atom = make_atom(NULL);
    TEST_ASSERT_NOT_NULL(atom);
    TEST_ASSERT_EQUAL(CLJ_ATOM, TYPE((CljObject*)atom));
    TEST_ASSERT_NULL(atom->value);
    RELEASE((CljObject*)atom);
}

TEST(test_atom_create_with_string) {
    CljObject *str = make_string("hello");
    CljAtom *atom = make_atom((ID)str);
    TEST_ASSERT_NOT_NULL(atom);
    TEST_ASSERT_NOT_NULL(atom->value);
    RELEASE((CljObject*)atom);
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
    RELEASE((CljObject*)atom);
}

TEST(test_atom_deref_nil) {
    CljAtom *atom = make_atom(NULL);
    ID value = atom_deref(atom);
    TEST_ASSERT_NULL(value);
    RELEASE((CljObject*)atom);
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
    RELEASE((CljObject*)atom);
}

TEST(test_atom_reset_to_nil) {
    CljAtom *atom = make_atom(fixnum(42));
    ID new_value = atom_reset(atom, NULL);
    TEST_ASSERT_NULL(new_value);
    TEST_ASSERT_NULL(atom->value);
    RELEASE((CljObject*)atom);
}

// ============================================================================
// TEST: Atom Swap (swap!)
// ============================================================================

TEST(test_atom_swap_simple) {
    CljAtom *atom = make_atom(fixnum(42));
    
    // Get 'inc' function from clojure.core using global test evalState
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    CljObject *inc_sym = intern_symbol_global("inc");
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
    
    RELEASE((CljObject*)atom);
    // Don't free st - it's the global test evalState
}

TEST(test_atom_swap_with_args) {
    CljAtom *atom = make_atom(fixnum(10));
    
    // Get '+' function from clojure.core using global test evalState
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    CljObject *plus_sym = intern_symbol_global("+");
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
    
    RELEASE((CljObject*)atom);
    // Don't free st - it's the global test evalState
}

TEST(test_atom_swap_persists) {
    CljAtom *atom = make_atom(fixnum(42));
    
    // Get 'inc' function using global test evalState
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    CljObject *inc_sym = intern_symbol_global("inc");
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
    
    RELEASE((CljObject*)atom);
    // Don't free st - it's the global test evalState
}

TEST(test_atom_swap_multiple_times) {
    CljAtom *atom = make_atom(fixnum(0));
    
    // Get 'inc' function using global test evalState
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    CljObject *inc_sym = intern_symbol_global("inc");
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
    
    RELEASE((CljObject*)atom);
    // Don't free st - it's the global test evalState
}

// ============================================================================
// TEST: Built-in Functions (atom, deref, reset!, swap!)
// ============================================================================

TEST(test_atom_builtin_creates_atom) {
    ID args[] = {fixnum(42)};
    ID result = native_atom(args, 1);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_type(result, CLJ_ATOM));
    RELEASE(result);
}

TEST(test_atom_builtin_deref) {
    CljAtom *atom = make_atom(fixnum(42));
    ID args[] = {(ID)atom};
    ID result = native_deref(args, 1);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL(42, as_fixnum((CljValue)result));
    RELEASE(result);
    RELEASE((CljObject*)atom);
}

TEST(test_atom_builtin_reset_bang) {
    CljAtom *atom = make_atom(fixnum(42));
    ID args[] = {(ID)atom, fixnum(100)};
    ID result = native_reset_bang(args, 2);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL(100, as_fixnum((CljValue)result));
    TEST_ASSERT_EQUAL(100, as_fixnum((CljValue)atom->value));
    RELEASE(result);
    RELEASE((CljObject*)atom);
}

TEST(test_atom_builtin_swap_bang) {
    CljAtom *atom = make_atom(fixnum(42));
    
    // Get 'inc' function using global test evalState
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    CljObject *inc_sym = intern_symbol_global("inc");
    ID inc_func = ns_resolve(g_test_eval_state, inc_sym);
    
    if (inc_func) {
        ID args[] = {(ID)atom, inc_func};
        ID result = native_swap_bang(args, 2);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_EQUAL(43, as_fixnum((CljValue)result));
        RELEASE(result);
    }
    
    RELEASE((CljObject*)atom);
    // Don't free st - it's the global test evalState
}

// ============================================================================
// TEST: Memory Management
// ============================================================================

TEST(test_atom_memory_management) {
    CljObject *str = make_string("test");
    CljAtom *atom = make_atom((ID)str);
    
    // Atom should retain the string (rc=1 from make_string, +1 from make_atom)
    TEST_ASSERT_EQUAL(2, ((CljObject*)str)->rc);
    
    RELEASE((CljObject*)atom);
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
    
    RELEASE((CljObject*)atom1);
    RELEASE((CljObject*)atom2);
}

// ============================================================================
// TEST: Print Representation
// ============================================================================

TEST(test_atom_print_representation) {
    CljAtom *atom = make_atom(fixnum(42));
    const char *str = pr_str((CljObject*)atom);
    TEST_ASSERT_NOT_NULL(str);
    TEST_ASSERT_NOT_NULL(strstr(str, "Atom"));
    free((void*)str);
    RELEASE((CljObject*)atom);
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
    CljObject *inc_sym = intern_symbol_global("inc");
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
    CljObject *inc_sym = intern_symbol_global("inc");
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
    
    RELEASE((CljObject*)counter);
    // Don't free st - it's the global test evalState
}

// ============================================================================
// TEST: clojure.core Loading Debugging Tests
// ============================================================================

// Test: Verify that clojure.core cache is set
TEST(test_atom_clojure_core_cache_set) {
    TEST_ASSERT_NOT_NULL_MESSAGE(g_runtime.clojure_core_cache, 
                                  "clojure.core cache should be set after setUp()");
}

// Test: Verify that ns_resolve can find 'inc' in clojure.core
TEST(test_atom_ns_resolve_inc_in_clojure_core) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Ensure clojure.core cache is set
    TEST_ASSERT_NOT_NULL_MESSAGE(g_runtime.clojure_core_cache, 
                                  "clojure.core cache should be set");
    
    // Get 'inc' symbol
    CljObject *inc_sym = intern_symbol_global("inc");
    TEST_ASSERT_NOT_NULL(inc_sym);
    
    // Debug: Check if mappings exist
    CljNamespace *clojure_core = (CljNamespace*)g_runtime.clojure_core_cache;
    TEST_ASSERT_NOT_NULL_MESSAGE(clojure_core->mappings, 
                                  "clojure.core mappings should exist");
    
    // Debug: Check if mappings are empty or if symbols exist
    CljMap *map = (CljMap*)clojure_core->mappings;
    TEST_ASSERT_NOT_NULL_MESSAGE(map, "clojure.core mappings should exist");
    
    // Debug: Check all stored symbols to see what's actually there
    bool found_inc = false;
    int symbol_count = 0;
    const char *first_symbol_name = NULL;
    for (int i = 0; i < map->count; i++) {
        CljObject *stored_key = KV_KEY(map->data, i);
        if (stored_key && is_type(stored_key, CLJ_SYMBOL)) {
            symbol_count++;
            CljSymbol *stored_sym = as_symbol(stored_key);
            if (stored_sym && stored_sym->name) {
                // Store first symbol name for debugging
                if (!first_symbol_name) {
                    first_symbol_name = stored_sym->name;
                }
                if (strcmp(stored_sym->name, "inc") == 0) {
                    found_inc = true;
                    // Check if this symbol has a namespace
                    if (stored_sym->ns) {
                        // Symbol has namespace - this might be the issue
                        CljSymbol *ns_sym = as_symbol(stored_sym->ns->name);
                        if (ns_sym && ns_sym->name) {
                            // Symbol has namespace name, check if it's clojure.core
                        }
                    }
                    break;
                }
            }
        }
    }
    
    // Debug: Try direct map_get to see if symbol is found
    ID direct_lookup = map_get((CljMap*)clojure_core->mappings, (CljValue)inc_sym);
    if (!direct_lookup) {
        if (symbol_count == 0) {
            TEST_FAIL_MESSAGE("clojure.core mappings are empty - core functions were not loaded");
        } else if (!found_inc) {
            // inc not found - check if eval_core_source actually loaded it
            // This might mean that (def inc ...) failed to evaluate or wasn't parsed correctly
            char msg[256];
            snprintf(msg, sizeof(msg), 
                    "'inc' symbol not found in clojure.core mappings (but %d other symbols exist, first: %s)",
                    symbol_count, first_symbol_name ? first_symbol_name : "unknown");
            TEST_FAIL_MESSAGE(msg);
        } else {
            // Symbol found but map_get didn't find it - this is a structural comparison issue
            TEST_FAIL_MESSAGE("'inc' symbol found in mappings but map_get didn't find it (structural comparison issue)");
        }
    }
    
    // Resolve 'inc' symbol
    ID resolved = ns_resolve(g_test_eval_state, inc_sym);
    TEST_ASSERT_NOT_NULL_MESSAGE(resolved, 
                                 "ns_resolve should find 'inc' in clojure.core");
    
    // Verify that resolved value is a function (CLJ_CLOSURE from clojure.core.clj)
    TEST_ASSERT_TRUE_MESSAGE(is_type(resolved, CLJ_CLOSURE) || is_type(resolved, CLJ_FUNC),
                             "resolved 'inc' should be a function");
    
    // Don't free st - it's the global test evalState
}

// Test: Verify that atom_swap can resolve 'inc' symbol
TEST(test_atom_swap_resolve_inc_symbol) {
    CljAtom *atom = make_atom(fixnum(42));
    
    // Get 'inc' symbol (not resolved yet)
    CljObject *inc_sym = intern_symbol_global("inc");
    TEST_ASSERT_NOT_NULL(inc_sym);
    
    // Try swap! with symbol (should resolve internally)
    ID args[] = {};
    ID result = atom_swap(atom, (ID)inc_sym, args, 0);
    
    if (result) {
        TEST_ASSERT_TRUE(is_fixnum((CljValue)result));
        TEST_ASSERT_EQUAL(43, as_fixnum((CljValue)result));
        RELEASE(result);
    } else {
        TEST_FAIL_MESSAGE("atom_swap should resolve 'inc' symbol and execute it");
    }
    
    RELEASE((CljObject*)atom);
}

// Test: Verify that atom_swap works with already-resolved function
TEST(test_atom_swap_with_resolved_function) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Get 'inc' symbol and resolve it
    CljObject *inc_sym = intern_symbol_global("inc");
    TEST_ASSERT_NOT_NULL(inc_sym);
    
    ID inc_func = ns_resolve(g_test_eval_state, inc_sym);
    TEST_ASSERT_NOT_NULL_MESSAGE(inc_func, 
                                 "should resolve 'inc' to a function");
    
    // Create an atom
    CljAtom *atom = make_atom(fixnum(42));
    TEST_ASSERT_NOT_NULL(atom);
    
    // Swap with resolved function (not symbol)
    ID args[] = {};
    ID result = atom_swap(atom, inc_func, args, 0);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)result));
    TEST_ASSERT_EQUAL(43, as_fixnum((CljValue)result));
    
    RELEASE(result);
    RELEASE((CljObject*)atom);
    // Don't free st - it's the global test evalState
}

// Test: Verify that def is recognized as special form
TEST(test_atom_def_symbol_recognized) {
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        
        // Get 'def' symbol from parser
        Reader reader;
        reader_init(&reader, "(def inc (fn [x] (+ x 1)))");
        ID form = value_by_parsing_expr(&reader, g_test_eval_state);
        TEST_ASSERT_NOT_NULL(form);
        
        // Extract the 'def' symbol from the list
        CljList *list = as_list(form);
        TEST_ASSERT_NOT_NULL(list);
        CljObject *def_sym = LIST_FIRST(list);
        TEST_ASSERT_NOT_NULL(def_sym);
        TEST_ASSERT_TRUE_MESSAGE(is_type(def_sym, CLJ_SYMBOL), 
                                 "first element should be a symbol");
        
        // Check if def_sym matches SYM_DEF
        extern CljObject *SYM_DEF;
        TEST_ASSERT_NOT_NULL(SYM_DEF);
        
        // Check pointer equality
        bool pointer_match = (def_sym == SYM_DEF);
        
        // If pointer doesn't match, check if they're the same symbol via symbol table
        if (!pointer_match) {
            CljSymbol *parsed_sym = as_symbol(def_sym);
            CljSymbol *special_sym = as_symbol(SYM_DEF);
            if (parsed_sym && special_sym && parsed_sym->name && special_sym->name) {
                (void)(strcmp(parsed_sym->name, special_sym->name) == 0);
                TEST_FAIL_MESSAGE("def symbol pointer mismatch - parsed symbol has different pointer than SYM_DEF (symbol interning issue)");
            } else {
                TEST_FAIL_MESSAGE("def symbol pointer mismatch and cannot compare names");
            }
        }
        
        // Don't RELEASE form - value_by_parsing_expr returns autoreleased object
        // Don't free st - it's the global test evalState
}

// Test: Verify that (def inc ...) is parsed correctly
TEST(test_atom_def_inc_parsed) {
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        
        // Parse (def inc (fn [x] (+ x 1)))
        Reader reader;
        reader_init(&reader, "(def inc (fn [x] (+ x 1)))");
        ID form = value_by_parsing_expr(&reader, g_test_eval_state);
        TEST_ASSERT_NOT_NULL_MESSAGE(form, "should parse (def inc ...)");
        
        // Verify it's a list
        CljList *list = as_list(form);
        TEST_ASSERT_NOT_NULL_MESSAGE(list, "parsed form should be a list");
        
        // Verify first element is 'def'
        CljObject *def_sym = LIST_FIRST(list);
        TEST_ASSERT_NOT_NULL_MESSAGE(def_sym, "first element should be 'def' symbol");
        TEST_ASSERT_TRUE_MESSAGE(is_type(def_sym, CLJ_SYMBOL), 
                                "first element should be a symbol");
        
        // Verify second element is 'inc'
        CljList *rest = as_list((ID)list->rest);
        CljObject *inc_sym = rest ? LIST_FIRST(rest) : NULL;
        TEST_ASSERT_NOT_NULL_MESSAGE(inc_sym, "second element should be 'inc' symbol");
        TEST_ASSERT_TRUE_MESSAGE(is_type(inc_sym, CLJ_SYMBOL), 
                                "second element should be a symbol");
        
        CljSymbol *inc = as_symbol(inc_sym);
        TEST_ASSERT_NOT_NULL(inc);
        TEST_ASSERT_NOT_NULL(inc->name);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("inc", inc->name, 
                                        "second element should be 'inc' symbol");
        
        // Don't RELEASE form - value_by_parsing_expr returns autoreleased object
        // Don't free st - it's the global test evalState
}

// Test: Verify that eval_def is called when (def inc ...) is evaluated
TEST(test_atom_def_inc_evaluated) {
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        
        // Parse and evaluate (def inc (fn [x] (+ x 1)))
        Reader reader;
        reader_init(&reader, "(def inc (fn [x] (+ x 1)))");
        ID form = value_by_parsing_expr(&reader, g_test_eval_state);
        TEST_ASSERT_NOT_NULL(form);
        
        // Evaluate the form
        CljMap *env = g_test_eval_state->current_ns ? (CljMap*)g_test_eval_state->current_ns->mappings : NULL;
        ID result = eval_list(as_list(form), env, g_test_eval_state);
        
        // Should return the symbol 'inc'
        TEST_ASSERT_NOT_NULL_MESSAGE(result, "eval_def should return the symbol");
        TEST_ASSERT_TRUE_MESSAGE(is_type(result, CLJ_SYMBOL), 
                                "eval_def should return a symbol");
        
        // Verify 'inc' is now in the namespace mappings
        CljNamespace *ns = g_test_eval_state->current_ns;
        TEST_ASSERT_NOT_NULL(ns);
        TEST_ASSERT_NOT_NULL_MESSAGE(ns->mappings, "namespace should have mappings");
        
        CljObject *inc_sym = intern_symbol_global("inc");
        ID inc_value = map_get((CljMap*)ns->mappings, (CljValue)inc_sym);
        TEST_ASSERT_NOT_NULL_MESSAGE(inc_value, 
                                     "'inc' should be in namespace mappings after eval_def");
        
        // Don't RELEASE form - value_by_parsing_expr returns autoreleased object
        // Don't free g_test_eval_state - it's the global test evalState
}

// Test: Verify that clojure.core actually contains (def inc ...) in source
TEST(test_atom_clojure_core_contains_inc) {
    // Check if clojure.core source contains (def inc ...)
    extern const char *clojure_core_code;
    TEST_ASSERT_NOT_NULL_MESSAGE(clojure_core_code, "clojure.core source should exist");
    
    // Search for "def inc" in source
    const char *found = strstr(clojure_core_code, "def inc");
    TEST_ASSERT_NOT_NULL_MESSAGE(found, "clojure.core source should contain 'def inc'");
    
    // Search for "(def inc" to be more specific
    found = strstr(clojure_core_code, "(def inc");
    TEST_ASSERT_NOT_NULL_MESSAGE(found, "clojure.core source should contain '(def inc'");
}

// Test: Verify that eval_core_source processes all expressions
TEST(test_atom_eval_core_source_processes_inc) {
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        
        // Test with just (def inc ...) expression
        const char *test_source = "(def inc (fn [x] (+ x 1)))";
        
        Reader reader;
        reader_init(&reader, test_source);
        
        // Parse the expression
        ID form = value_by_parsing_expr(&reader, g_test_eval_state);
        TEST_ASSERT_NOT_NULL_MESSAGE(form, "should parse (def inc ...)");
        
        // Evaluate it
        ID result = eval_parsed((CljObject*)form, g_test_eval_state, NULL);
        
        // Should succeed
        TEST_ASSERT_NOT_NULL_MESSAGE(result, "should evaluate (def inc ...) successfully");
        
        // Verify inc is in mappings
        CljObject *inc_sym = intern_symbol_global("inc");
        ID inc_value = map_get((CljMap*)g_test_eval_state->current_ns->mappings, (CljValue)inc_sym);
        TEST_ASSERT_NOT_NULL_MESSAGE(inc_value, 
                                     "'inc' should be in namespace mappings after evaluation");
        
        // Don't RELEASE form - value_by_parsing_expr returns autoreleased object
        // Don't free g_test_eval_state - it's the global test evalState
}
