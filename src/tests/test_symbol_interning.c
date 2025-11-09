#include "tests_common.h"
#include "symbol.h"
#include "namespace.h"
#include "runtime.h"
#include "reader.h"
#include "function_call.h"
#include "list.h"
#include "map.h"
#include "kv_macros.h"

// Forward declaration for load_clojure_core
int load_clojure_core(EvalState *st);
// Forward declaration for value_by_parsing_expr
extern CljValue value_by_parsing_expr(Reader *reader, EvalState *st);

// Test: Verify that inc symbol is interned correctly when loading clojure.core
TEST(test_inc_symbol_interning_during_load) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Get inc symbol BEFORE loading clojure.core
    CljSymbol *inc_sym_before = intern_symbol_global("inc");
    TEST_ASSERT_NOT_NULL(inc_sym_before);
    
    // Load clojure.core
    
    // Get inc symbol AFTER loading clojure.core
    CljSymbol *inc_sym_after = intern_symbol_global("inc");
    TEST_ASSERT_NOT_NULL(inc_sym_after);
    
    // They should be the SAME pointer (same interned symbol)
    TEST_ASSERT_EQUAL_PTR_MESSAGE(inc_sym_before, inc_sym_after,
                                  "inc symbol should be the same before and after loading");
    
    // Verify that inc is in clojure.core mappings
    CljNamespace *clojure_core = (CljNamespace*)g_runtime.clojure_core_cache;
    TEST_ASSERT_NOT_NULL_MESSAGE(clojure_core, "clojure.core cache should be set");
    
    if (clojure_core && clojure_core->mappings) {
        // Check if inc_sym_after is in the mappings
        CljObject *inc_value = (CljObject*)map_get((CljMap*)clojure_core->mappings, (CljValue)inc_sym_after);
        
        if (!inc_value) {
            // Debug: Check what symbols ARE in the mappings
            CljMap *map = (CljMap*)clojure_core->mappings;
            int symbol_count = 0;
            const char *first_symbol = NULL;
            for (int i = 0; i < map->count; i++) {
                CljObject *key = KV_KEY(map->data, i);
                if (key && is_type(key, CLJ_SYMBOL)) {
                    CljSymbol *sym = as_symbol(key);
                    symbol_count++;
                    if (!first_symbol && sym->name) {
                        first_symbol = sym->name;
                    }
                    // Check if this is inc by name
                    if (sym->name && strcmp(sym->name, "inc") == 0) {
                        // Found inc by name - check if it's the same pointer
                        if (key != inc_sym_after) {
                            char msg[256];
                            snprintf(msg, sizeof(msg),
                                    "Found 'inc' in mappings but with different symbol pointer! "
                                    "Stored: %p, Lookup: %p",
                                    key, inc_sym_after);
                            TEST_FAIL_MESSAGE(msg);
                        }
                    }
                }
            }
            
            char msg[256];
            snprintf(msg, sizeof(msg),
                    "'inc' not found in clojure.core mappings using symbol pointer %p "
                    "(but %d other symbols exist, first: %s)",
                    inc_sym_after, symbol_count, first_symbol ? first_symbol : "unknown");
            TEST_FAIL_MESSAGE(msg);
        }
        
        TEST_ASSERT_NOT_NULL_MESSAGE(inc_value, "inc should be in clojure.core mappings");
    }
    
}

// Test: Verify that symbols used during parsing are the same as interned symbols
TEST(test_inc_symbol_pointer_consistency) {
    // This test verifies that when we parse "(def inc ...)", the symbol "inc"
    // used in the parsed form is the same as when we later look it up
    
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    
    // Get inc symbol before parsing
    CljSymbol *inc_sym_before = intern_symbol_global("inc");
    TEST_ASSERT_NOT_NULL(inc_sym_before);
    
    // Parse "(def inc (fn [x] (+ x 1)))"
    Reader reader;
    reader_init(&reader, "(def inc (fn [x] (+ x 1)))");
    CljValue form = value_by_parsing_expr(&reader, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(form);
    
    // Extract the symbol from the parsed form
    if (is_type(form, CLJ_LIST)) {
        CljList *list = as_list(form);
        CljObject *inc_sym_in_form = (CljObject*)list_nth(list, 1);
        
        TEST_ASSERT_NOT_NULL(inc_sym_in_form);
        TEST_ASSERT_TRUE(is_type(inc_sym_in_form, CLJ_SYMBOL));
        
        // Get inc symbol after parsing
        CljSymbol *inc_sym_after = intern_symbol_global("inc");
        TEST_ASSERT_NOT_NULL(inc_sym_after);
        
        // The symbol in the parsed form should be the same as the interned symbol
        TEST_ASSERT_EQUAL_PTR_MESSAGE(inc_sym_before, inc_sym_in_form,
                                     "Symbol in parsed form should be same as interned symbol");
        TEST_ASSERT_EQUAL_PTR_MESSAGE(inc_sym_after, inc_sym_in_form,
                                     "Symbol after parsing should be same as symbol in form");
        
        // Now evaluate the def
        CljMap *env = g_test_eval_state->current_ns ? (CljMap*)g_test_eval_state->current_ns->mappings : NULL;
        CljValue result = eval_list(list, env, g_test_eval_state);
        
        // Check if inc is now in the mappings with the same symbol pointer
        if (g_test_eval_state->current_ns && g_test_eval_state->current_ns->mappings) {
            CljObject *inc_value = (CljObject*)map_get((CljMap*)g_test_eval_state->current_ns->mappings, (CljValue)inc_sym_after);
            
            if (!inc_value) {
                // Debug: try with the symbol from the form
                inc_value = (CljObject*)map_get((CljMap*)g_test_eval_state->current_ns->mappings, (CljValue)inc_sym_in_form);
                
                if (!inc_value) {
                    char msg[256];
                    snprintf(msg, sizeof(msg),
                            "inc not found in mappings after def. "
                            "Form symbol: %p, Interned symbol: %p, Equal: %d",
                            inc_sym_in_form, inc_sym_after,
                            (inc_sym_in_form == inc_sym_after) ? 1 : 0);
                    TEST_FAIL_MESSAGE(msg);
                }
            }
            
            TEST_ASSERT_NOT_NULL_MESSAGE(inc_value, "inc should be in mappings after def");
        }
        
        // Don't RELEASE result - eval_list returns autoreleased object
    }
    
    // Don't RELEASE form - value_by_parsing_expr returns autoreleased object
}

// Test: Verify symbol interning consistency across multiple calls
TEST(test_inc_symbol_interning_consistency) {
    // Test that intern_symbol_global("inc") always returns the same pointer
    CljSymbol *sym1 = intern_symbol_global("inc");
    CljSymbol *sym2 = intern_symbol_global("inc");
    CljSymbol *sym3 = intern_symbol_global("inc");
    
    TEST_ASSERT_NOT_NULL(sym1);
    TEST_ASSERT_NOT_NULL(sym2);
    TEST_ASSERT_NOT_NULL(sym3);
    
    TEST_ASSERT_EQUAL_PTR_MESSAGE(sym1, sym2, "First and second call should return same pointer");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(sym2, sym3, "Second and third call should return same pointer");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(sym1, sym3, "First and third call should return same pointer");
}

// Test: Verify that map_get uses pointer equality for interned symbols
TEST(test_map_get_with_interned_symbols) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    evalstate_set_ns(g_test_eval_state, "user");
    
    // Create a test symbol
    CljSymbol *test_sym = intern_symbol_global("test-var");
    TEST_ASSERT_NOT_NULL(test_sym);
    
    // Store a value using test_sym
    CljObject *value = fixnum(42);
    ns_define(g_test_eval_state->current_ns, test_sym, value);
    
    // Get the symbol again (should be same pointer)
    CljSymbol *test_sym2 = intern_symbol_global("test-var");
    TEST_ASSERT_NOT_NULL(test_sym2);
    TEST_ASSERT_EQUAL_PTR_MESSAGE(test_sym, test_sym2, 
                                  "Should get same symbol pointer");
    
    // Try to retrieve using the second symbol pointer
    CljObject *retrieved = (CljObject*)map_get((CljMap*)g_test_eval_state->current_ns->mappings, (CljValue)test_sym2);
    TEST_ASSERT_NOT_NULL_MESSAGE(retrieved, 
                                 "Should retrieve value using interned symbol pointer");
    
    if (retrieved) {
        TEST_ASSERT_TRUE(is_fixnum((CljValue)retrieved));
        TEST_ASSERT_EQUAL(42, as_fixnum((CljValue)retrieved));
    }
    
}

