#include "tests_common.h"
#include "symbol.h"
#include "namespace.h"
#include "runtime.h"
#include "map.h"
#include "kv_macros.h"
#include "reader.h"
#include "function_call.h"
#include "list.h"

// Forward declaration for load_clojure_core
int load_clojure_core(EvalState *st);
// Forward declaration for value_by_parsing_expr
extern CljValue value_by_parsing_expr(Reader *reader, EvalState *st);

// Test: Verify that inc is loaded during test initialization
TEST(test_core_initialization_inc_loaded) {
    // This test runs AFTER setUp(), so clojure.core should already be loaded
    TEST_ASSERT_NOT_NULL_MESSAGE(g_runtime.clojure_core_cache, 
                                 "clojure.core cache should be set after setUp()");
    
    CljNamespace *clojure_core = (CljNamespace*)g_runtime.clojure_core_cache;
    TEST_ASSERT_NOT_NULL_MESSAGE(clojure_core, "clojure.core namespace should exist");
    TEST_ASSERT_NOT_NULL_MESSAGE(clojure_core->mappings, "clojure.core mappings should exist");
    
    // Get inc symbol
    CljSymbol *inc_sym = intern_symbol_global("inc");
    TEST_ASSERT_NOT_NULL(inc_sym);
    
    // Check if inc is in clojure.core mappings
    CljMap *map = clojure_core->mappings;
    TEST_ASSERT_NOT_NULL(map);
    
    int symbol_count = 0;
    const char *first_symbol_name = NULL;
    bool found_inc = false;
    
    MAP_FOR_EACH(map, key, value) {
        (void)value;  // unused
        if (key && TAG(key) == CLJ_SYMBOL) {
            CljSymbol *sym = as_symbol(key);
            symbol_count++;
            if (!first_symbol_name && sym->name) {
                first_symbol_name = sym->name;
            }
            if (sym->name && strcmp(sym->name, "inc") == 0) {
                found_inc = true;
            }
        }
    }
    
    // Try direct map_get
    CljObject *inc_value = map_get(map, inc_sym, NULL);
    
    if (!inc_value && !found_inc) {
        char msg[512];
        snprintf(msg, sizeof(msg),
                "'inc' not found in clojure.core mappings after initialization. "
                "Symbol count: %d, first: %s. "
                "inc_sym pointer: %p. "
                "This suggests that (def inc ...) failed to evaluate during load_clojure_core()",
                symbol_count, 
                first_symbol_name ? first_symbol_name : "unknown",
                inc_sym);
        TEST_FAIL_MESSAGE(msg);
    }
    
    TEST_ASSERT_NOT_NULL_MESSAGE(inc_value, 
                                "inc should be in clojure.core mappings after initialization");
    TEST_ASSERT_TRUE_MESSAGE(inc_value && TAG(inc_value) == CLJ_FUNC || inc_value && TAG(inc_value) == CLJ_CLOSURE,
                             "inc should be a function");
}

// Test: Verify that all arithmetic functions are loaded
TEST(test_core_initialization_arithmetic_functions) {
    CljNamespace *clojure_core = (CljNamespace*)g_runtime.clojure_core_cache;
    TEST_ASSERT_NOT_NULL(clojure_core);
    
    const char *functions[] = {"add", "sub", "mul", "div", "inc", "dec", "square"};
    int missing_count = 0;
    char missing_names[256] = "";
    
    for (int i = 0; i < 7; i++) {
        CljSymbol *sym = intern_symbol_global(functions[i]);
        CljObject *value = map_get(clojure_core->mappings, sym, NULL);
        
        if (!value) {
            missing_count++;
            if (strlen(missing_names) < sizeof(missing_names) - 10) {
                strcat(missing_names, functions[i]);
                strcat(missing_names, " ");
            }
        }
    }
    
    if (missing_count > 0) {
        char msg[512];
        snprintf(msg, sizeof(msg),
                "%d arithmetic functions missing from clojure.core after initialization: %s",
                missing_count, missing_names);
        TEST_FAIL_MESSAGE(msg);
    }
}

// Test: Verify that + is available (builtin) before loading clojure.core
TEST(test_core_initialization_plus_available) {
    CljNamespace *clojure_core = (CljNamespace*)g_runtime.clojure_core_cache;
    TEST_ASSERT_NOT_NULL(clojure_core);
    
    // Get + symbol
    CljSymbol *plus_sym = intern_symbol_global("+");
    TEST_ASSERT_NOT_NULL(plus_sym);
    
    // Check if + is in clojure.core mappings (should be registered by register_builtins)
    CljObject *plus_value = map_get(clojure_core->mappings, plus_sym, NULL);
    TEST_ASSERT_NOT_NULL_MESSAGE(plus_value, 
                                 "+ should be in clojure.core mappings (registered by register_builtins)");
}

// ============================================================================
// Tests for clojure.core loading
// ============================================================================

// Test: Verify that inc is loaded correctly when loading clojure.core
TEST(test_clojure_core_loads_inc) {
    // Get inc symbol before loading
    CljSymbol *inc_sym = intern_symbol_global("inc");
    TEST_ASSERT_NOT_NULL(inc_sym);
    
    // Load clojure.core (use global st from setUp)
    evalstate_set_ns(g_test_eval_state, "clojure.core");
    load_clojure_core(g_test_eval_state);
    
    // Check if inc is in clojure.core mappings
    CljNamespace *clojure_core = (CljNamespace*)g_runtime.clojure_core_cache;
    TEST_ASSERT_NOT_NULL_MESSAGE(clojure_core, "clojure.core cache should be set");
    
    if (clojure_core && clojure_core->mappings) {
        CljObject *inc_value = map_get(clojure_core->mappings, inc_sym, NULL);
        
        if (!inc_value) {
            CljMap *map = clojure_core->mappings;
            int symbol_count = 0;
            const char *first_symbol = NULL;
            const char *inc_symbol_found = NULL;
            CljObject *inc_symbol_ptr = NULL;
            
            MAP_FOR_EACH(map, key, value) {
                (void)value;  // unused
                if (key && TAG(key) == CLJ_SYMBOL) {
                    CljSymbol *sym = as_symbol(key);
                    symbol_count++;
                    if (!first_symbol && sym->name) {
                        first_symbol = sym->name;
                    }
                    // Check if this is inc by name
                    if (sym->name && strcmp(sym->name, "inc") == 0) {
                        inc_symbol_found = sym->name;
                        inc_symbol_ptr = key;
                    }
                }
            }
            
            char msg[512];
            snprintf(msg, sizeof(msg),
                    "'inc' not found in clojure.core mappings after load. "
                    "Symbol count: %d, first: %s. "
                    "inc_sym pointer: %p. "
                    "Found inc by name: %s (ptr: %p). "
                    "Pointers equal: %d",
                    symbol_count, 
                    first_symbol ? first_symbol : "unknown",
                    inc_sym,
                    inc_symbol_found ? inc_symbol_found : "no",
                    inc_symbol_ptr,
                    (inc_sym == (CljSymbol*)inc_symbol_ptr) ? 1 : 0);
            TEST_FAIL_MESSAGE(msg);
        }
        
        TEST_ASSERT_NOT_NULL_MESSAGE(inc_value, 
                                    "inc should be in clojure.core mappings after load");
        TEST_ASSERT_TRUE_MESSAGE(inc_value && TAG(inc_value) == CLJ_FUNC || inc_value && TAG(inc_value) == CLJ_CLOSURE,
                                "inc should be a function");
    }
}

// Test: Verify that all core functions are loaded correctly
TEST(test_clojure_core_loads_all_functions) {
    // Load clojure.core (use global st from setUp)
    evalstate_set_ns(g_test_eval_state, "clojure.core");
    load_clojure_core(g_test_eval_state);
    
    // Check if key functions are loaded
    CljNamespace *clojure_core = (CljNamespace*)g_runtime.clojure_core_cache;
    TEST_ASSERT_NOT_NULL_MESSAGE(clojure_core, "clojure.core cache should be set");
    
    if (clojure_core && clojure_core->mappings) {
        // Check for add, sub, mul, div, inc, dec, square
        const char *functions[] = {"add", "sub", "mul", "div", "inc", "dec", "square"};
        int missing_count = 0;
        char missing_names[256] = "";
        
        for (int i = 0; i < 7; i++) {
            CljSymbol *sym = intern_symbol_global(functions[i]);
            CljObject *value = map_get(clojure_core->mappings, sym, NULL);
            
            if (!value) {
                missing_count++;
                if (strlen(missing_names) < sizeof(missing_names) - 10) {
                    strcat(missing_names, functions[i]);
                    strcat(missing_names, " ");
                }
            }
        }
        
        if (missing_count > 0) {
            char msg[512];
            snprintf(msg, sizeof(msg),
                    "%d functions missing from clojure.core: %s",
                    missing_count, missing_names);
            TEST_FAIL_MESSAGE(msg);
        }
    }
}

// ============================================================================
// Tests for def/inc evaluation during load
// ============================================================================

// Test: Verify that (def inc (fn [x] (+ x 1))) is evaluated correctly
TEST(test_def_inc_evaluation_during_load) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    
    // Parse "(def inc (fn [x] (+ x 1)))"
    Reader reader;
    reader_init(&reader, "(def inc (fn [x] (+ x 1)))");
    CljValue form = value_by_parsing_expr(&reader, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(form);
    
    // Extract the symbol from the parsed form
    if (form && TAG(form) == CLJ_LIST) {
        CljList *list = as_list(form);
        CljSymbol *inc_sym = as_symbol(list_nth(list, 1));
        CljObject *fn_expr = (CljObject*)list_nth(list, 2);
        
        TEST_ASSERT_NOT_NULL(inc_sym);
        TEST_ASSERT_TRUE(inc_sym && TAG(inc_sym) == CLJ_SYMBOL);
        TEST_ASSERT_NOT_NULL(fn_expr);
        
        // Verify that inc_sym is the same as intern_symbol_global("inc")
        CljSymbol *inc_sym_interned = intern_symbol_global("inc");
        TEST_ASSERT_EQUAL_PTR_MESSAGE(inc_sym, inc_sym_interned,
                                      "inc symbol should be interned");
        
        // Evaluate the def expression
        CljMap *env = g_test_eval_state->current_ns ? g_test_eval_state->current_ns->mappings : NULL;
        TEST_ASSERT_NOT_NULL(env);
        
        TRY {
            (void)eval_list(list, env, g_test_eval_state, NULL);
            
            // Check if inc is now in the mappings
            CljObject *inc_value = map_get(g_test_eval_state->current_ns->mappings, inc_sym_interned, NULL);
            
            if (!inc_value) {
                CljMap *map = g_test_eval_state->current_ns->mappings;
                int symbol_count = 0;
                const char *first_symbol = NULL;
                MAP_FOR_EACH(map, key, value) {
                    (void)value;  // unused
                    if (key && TAG(key) == CLJ_SYMBOL) {
                        CljSymbol *sym = as_symbol(key);
                        symbol_count++;
                        if (!first_symbol && sym->name) {
                            first_symbol = sym->name;
                        }
                    }
                }
                
                char msg[256];
                snprintf(msg, sizeof(msg),
                        "'inc' not found in mappings after def evaluation "
                        "(but %d other symbols exist, first: %s)",
                        symbol_count, first_symbol ? first_symbol : "unknown");
                TEST_FAIL_MESSAGE(msg);
            }
            
            TEST_ASSERT_NOT_NULL_MESSAGE(inc_value, 
                                        "inc should be in mappings after def evaluation");
            
            // Verify that inc_value is a function
            TEST_ASSERT_TRUE_MESSAGE(inc_value && TAG(inc_value) == CLJ_FUNC || inc_value && TAG(inc_value) == CLJ_CLOSURE,
                                    "inc should be a function");
            
            // Don't RELEASE result - eval_string returns autoreleased object
        } CATCH(ex) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                    "Exception during def inc evaluation: %s",
                    ex && ex->message[0] ? ex->message : "unknown");
            TEST_FAIL_MESSAGE(msg);
        } END_TRY
    }
    
    // Don't RELEASE form - value_by_parsing_expr returns autoreleased object
}

// Test: Verify that + is available when evaluating (fn [x] (+ x 1))
TEST(test_plus_available_during_fn_evaluation) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    
    // Ensure + is registered (should be done by register_builtins)
    CljSymbol *plus_sym = intern_symbol_global("+");
    TEST_ASSERT_NOT_NULL(plus_sym);
    
    // Check if + is in clojure.core mappings
    CljNamespace *clojure_core = (CljNamespace*)g_runtime.clojure_core_cache;
    TEST_ASSERT_NOT_NULL_MESSAGE(clojure_core, "clojure.core should exist");
    
    if (clojure_core && clojure_core->mappings) {
        CljObject *plus_value = map_get(clojure_core->mappings, plus_sym, NULL);
        TEST_ASSERT_NOT_NULL_MESSAGE(plus_value, 
                                    "+ should be in clojure.core mappings");
    }
    
    // Parse "(fn [x] (+ x 1))"
    Reader reader;
    reader_init(&reader, "(fn [x] (+ x 1))");
    CljValue form = value_by_parsing_expr(&reader, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(form);
    
    // Evaluate the fn expression
    CljMap *env = g_test_eval_state->current_ns ? g_test_eval_state->current_ns->mappings : NULL;
    TEST_ASSERT_NOT_NULL(env);
    
    TRY {
        CljValue result = eval_list(as_list(form), env, g_test_eval_state, NULL);
        TEST_ASSERT_NOT_NULL_MESSAGE(result, 
                                    "fn expression should evaluate to a function");
        TEST_ASSERT_TRUE_MESSAGE(result && TAG(result) == CLJ_FUNC || result && TAG(result) == CLJ_CLOSURE,
                                "fn expression should return a function");
        
        // Don't RELEASE result - eval_string returns autoreleased object
    } CATCH(ex) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                    "Exception during fn evaluation: %s",
                    ex && ex->message[0] ? ex->message : "unknown");
        TEST_FAIL_MESSAGE(msg);
    } END_TRY
    
    // Don't RELEASE form - value_by_parsing_expr returns autoreleased object
}

// Test: Verify that eval_def stores the symbol even if value evaluation returns NULL
TEST(test_def_stores_symbol_even_if_value_null) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    evalstate_set_ns(g_test_eval_state, "user");
    
    // Parse "(def test-var nil)"
    Reader reader;
    reader_init(&reader, "(def test-var nil)");
    CljValue form = value_by_parsing_expr(&reader, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(form);
    
    // Evaluate the def expression
    CljMap *env = g_test_eval_state->current_ns ? g_test_eval_state->current_ns->mappings : NULL;
    TEST_ASSERT_NOT_NULL(env);
    
    TRY {
        (void)eval_list(as_list(form), env, g_test_eval_state, NULL);
        
        // Check if test-var is in the mappings (even if value is nil/NULL)
        CljSymbol *test_var_sym = intern_symbol_global("test-var");
        TEST_ASSERT_NOT_NULL(test_var_sym);
        
        // test_var_value can be NULL if nil was stored
        (void)map_get(g_test_eval_state->current_ns->mappings, test_var_sym, NULL);
        // But the key should be in the map
        // Let's check if the key exists by iterating
        CljMap *map = g_test_eval_state->current_ns->mappings;
        bool found_key = false;
        MAP_FOR_EACH(map, key, value) {
            (void)value;  // unused
            if (key && TAG(key) == CLJ_SYMBOL && (CljSymbol*)key == test_var_sym) {
                found_key = true;
                break;
            }
        }
        
        TEST_ASSERT_TRUE_MESSAGE(found_key, 
                                "test-var should be in mappings after def");
        
        // Don't RELEASE result - eval_string returns autoreleased object
    } CATCH(ex) {
        char msg[256];
        snprintf(msg, sizeof(msg),
                "Exception during def test-var evaluation: %s",
                ex && ex->message[0] ? ex->message : "unknown");
        TEST_FAIL_MESSAGE(msg);
    } END_TRY
    
    // Don't RELEASE form - value_by_parsing_expr returns autoreleased object
}

