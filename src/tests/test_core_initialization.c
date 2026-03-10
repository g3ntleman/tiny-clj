#include "tests_common.h"
#include "symbol.h"
#include "namespace.h"
#include "runtime.h"
#include "map.h"
#include "kv_macros.h"
#include "reader.h"
#include "eval.h"
#include "../ast.h"
#include "list.h"
#include "vector.h"

// Forward declaration for load_clojure_core
int load_clojure_core(EvalState *st);
// Forward declaration for value_by_parsing_expr
extern CljValue value_by_parsing_expr(Reader *reader, EvalState *st);

// Test: Verify that inc is loaded during test initialization
TEST(test_core_initialization_inc_loaded) {
    // This test runs AFTER setUp(), so clojure.core should already be loaded
    CljNamespace *clojure_core = ns_find_by_symbol(SYM_CLOJURE_CORE);
    TEST_ASSERT_NOT_NULL_MESSAGE(clojure_core, 
                                 "clojure.core should be found in registry after setUp()");
    TEST_ASSERT_NOT_NULL_MESSAGE(clojure_core, "clojure.core namespace should exist");
    TEST_ASSERT_NOT_NULL_MESSAGE(clojure_core->mappings, "clojure.core mappings should exist");
    
    // Get inc symbol
    CljSymbol *inc_sym = intern_symbol_global("inc");
    TEST_ASSERT_NOT_NULL(inc_sym);
    
    // Check if inc is in clojure.core mappings
    CljPersistentMap *map = clojure_core->mappings;
    TEST_ASSERT_NOT_NULL(map);
    
    int symbol_count = 0;
    const char *first_symbol_name = NULL;
    bool found_inc = false;
    
    MAP_FOR_EACH(map, key, value) {
        (void)value;  // unused
        if (key && TAG(key) == CLJ_SYMBOL) {
            CljSymbol *sym = as_symbol(key);
            symbol_count++;
            if (!first_symbol_name && sym->cname) {
                first_symbol_name = sym->cname;
            }
            if (sym->cname && strcmp(sym->cname, "inc") == 0) {
                found_inc = true;
            }
        }
    }
    
    // Try direct map_get
    CljObject *inc_value = map_get_sentinel(map, inc_sym, NULL);
    
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
    CljNamespace *clojure_core = ns_find_by_symbol(SYM_CLOJURE_CORE);
    TEST_ASSERT_NOT_NULL(clojure_core);
    
    const char *functions[] = {"inc", "dec"};
    int num_functions = sizeof(functions) / sizeof(functions[0]);
    int missing_count = 0;
    char missing_names[256] = "";
    
    for (int i = 0; i < num_functions; i++) {
        CljSymbol *sym = intern_symbol_global(functions[i]);
        CljObject *value = map_get_sentinel(clojure_core->mappings, sym, NULL);
        
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
    CljNamespace *clojure_core = ns_find_by_symbol(SYM_CLOJURE_CORE);
    TEST_ASSERT_NOT_NULL(clojure_core);
    
    // Get + symbol
    CljSymbol *plus_sym = intern_symbol_global("+");
    TEST_ASSERT_NOT_NULL(plus_sym);
    
    // Check if + is in clojure.core mappings (should be registered by register_builtins)
    CljObject *plus_value = map_get_sentinel(clojure_core->mappings, plus_sym, NULL);
    TEST_ASSERT_NOT_NULL_MESSAGE(plus_value, 
                                 "+ should be in clojure.core mappings (registered by register_builtins)");
}

TEST(test_evalstate_new_load_core_bootstraps_builtins) {
    runtime_reset(&g_runtime);
    WITH_AUTORELEASE_POOL({
        runtime_init(&g_runtime);
    });

    TEST_ASSERT_FALSE_MESSAGE(g_runtime.builtins_registered,
                              "runtime_init should not pre-register builtins");

    EvalState *st = NULL;
    TRY {
        st = evalstate_new(true);
    } CATCH(ex) {
        char msg[256];
        test_snprintf(msg, sizeof(msg),
                      "evalstate_new(true) should bootstrap builtins before core load, but threw: %s - %s",
                      ex ? ex->type : "unknown",
                      ex ? ex->message : "no message");
        TEST_FAIL_MESSAGE(msg);
    } END_TRY

    TEST_ASSERT_NOT_NULL_MESSAGE(st, "evalstate_new(true) should return an eval state");
    TEST_ASSERT_TRUE_MESSAGE(g_runtime.builtins_registered,
                             "evalstate_new(true) should mark builtins as registered");

    evalstate_set_ns(st, "user");
    ID result = eval_string("(inc 1)", st);
    TEST_ASSERT_TRUE_MESSAGE(is_fixnum(result), "(inc 1) should return a number after bootstrap");
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(result));
}

// ============================================================================
// Tests for clojure.core loading
// ============================================================================

// Test: Verify that inc is loaded correctly when loading clojure.core
TEST(test_clojure_core_loads_inc) {
    // Get inc symbol before loading
    CljSymbol *inc_sym = intern_symbol_global("inc");
    TEST_ASSERT_NOT_NULL(inc_sym);
    
    // Check if clojure.core is already loaded (from setUp)
    CljNamespace *clojure_core_before = ns_find_by_symbol(SYM_CLOJURE_CORE);
    bool already_loaded = (clojure_core_before != NULL && 
                          clojure_core_before->mappings != NULL &&
                          map_get_sentinel(clojure_core_before->mappings, inc_sym, NULL) != NULL);
    
    // Only load clojure.core if it's not already loaded
    // (setUp() already loads it, so this avoids double-loading)
    if (!already_loaded) {
    evalstate_set_ns(g_test_eval_state, "clojure.core");
    load_clojure_core(g_test_eval_state);
    }
    
    // Check if inc is in clojure.core mappings
    CljNamespace *clojure_core = ns_find_by_symbol(SYM_CLOJURE_CORE);
    TEST_ASSERT_NOT_NULL_MESSAGE(clojure_core, "clojure.core cache should be set");
    
    if (clojure_core && clojure_core->mappings) {
        CljObject *inc_value = map_get_sentinel(clojure_core->mappings, inc_sym, NULL);
        
        if (!inc_value) {
            CljPersistentMap *map = clojure_core->mappings;
            int symbol_count = 0;
            const char *first_symbol = NULL;
            const char *inc_symbol_found = NULL;
            CljObject *inc_symbol_ptr = NULL;
            
            MAP_FOR_EACH(map, key, value) {
                (void)value;  // unused
                if (key && TAG(key) == CLJ_SYMBOL) {
                    CljSymbol *sym = as_symbol(key);
                    symbol_count++;
                    if (!first_symbol && sym->cname) {
                        first_symbol = sym->cname;
                    }
                    // Check if this is inc by name
                    if (sym->cname && strcmp(sym->cname, "inc") == 0) {
                        inc_symbol_found = sym->cname;
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
    // Check if clojure.core is already loaded (from setUp)
    CljNamespace *clojure_core_before = ns_find_by_symbol(SYM_CLOJURE_CORE);
    CljSymbol *inc_sym = intern_symbol_global("inc");
    bool already_loaded = (clojure_core_before != NULL && 
                          clojure_core_before->mappings != NULL &&
                          inc_sym != NULL &&
                          map_get_sentinel(clojure_core_before->mappings, inc_sym, NULL) != NULL);
    
    // Only load clojure.core if it's not already loaded
    // (setUp() already loads it, so this avoids double-loading)
    if (!already_loaded) {
    evalstate_set_ns(g_test_eval_state, "clojure.core");
    load_clojure_core(g_test_eval_state);
    }
    
    // Check if key functions are loaded
    CljNamespace *clojure_core = ns_find_by_symbol(SYM_CLOJURE_CORE);
    TEST_ASSERT_NOT_NULL_MESSAGE(clojure_core, "clojure.core cache should be set");
    
    if (clojure_core && clojure_core->mappings) {
        // Check for inc, dec
        const char *functions[] = {"inc", "dec"};
        int num_functions = sizeof(functions) / sizeof(functions[0]);
        int missing_count = 0;
        char missing_names[256] = "";
        
        for (int i = 0; i < num_functions; i++) {
            CljSymbol *sym = intern_symbol_global(functions[i]);
            CljObject *value = map_get_sentinel(clojure_core->mappings, sym, NULL);
            
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

    ID canonical_form = canonicalize_ast(form, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(canonical_form);
    
    // Extract the symbol from the parsed form
    CljObject *inc_sym_obj = NULL;
    CljObject *fn_expr = NULL;
    CljObject *op_sym = NULL;
    if (canonical_form && TAG(canonical_form) == CLJ_AST_CALL) {
        CljASTCall *call = as_ast_call(canonical_form);
        op_sym = call ? call->op : NULL;
        if (call && call->args && vector_count(call->args) > 0) {
            inc_sym_obj = vector_nth(call->args, 0);
        }
        if (call && call->args && vector_count(call->args) > 1) {
            fn_expr = vector_nth(call->args, 1);
        }
    } else if (canonical_form && is_list_type(TAG(canonical_form))) {
        CljList *list = as_list(canonical_form);
        op_sym = list ? LIST_FIRST(list) : NULL;
        inc_sym_obj = list_nth(list, 1);
        fn_expr = (CljObject*)list_nth(list, 2);
    }

    TEST_ASSERT_NOT_NULL(inc_sym_obj);
    TEST_ASSERT_TRUE(inc_sym_obj && TAG(inc_sym_obj) == CLJ_SYMBOL);
    TEST_ASSERT_NOT_NULL(fn_expr);
    TEST_ASSERT_NOT_NULL(op_sym);
    TEST_ASSERT_TRUE(TAG(op_sym) == CLJ_SYMBOL);
    TEST_ASSERT_EQUAL_PTR_MESSAGE(SYM_DEF, op_sym, "form should be a def");

    CljSymbol *inc_sym = as_symbol(inc_sym_obj);
    TEST_ASSERT_NOT_NULL(inc_sym);

    // Verify that inc_sym is the same as intern_symbol_global("inc")
    CljSymbol *inc_sym_interned = intern_symbol_global("inc");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(inc_sym, inc_sym_interned,
                                  "inc symbol should be interned");

    // Evaluate the def expression
    CljPersistentMap *env = g_test_eval_state->current_ns ? g_test_eval_state->current_ns->mappings : NULL;
    TEST_ASSERT_NOT_NULL(env);

    TRY {
        (void)eval_body(canonical_form, env, g_test_eval_state, NULL);

        // CRITICAL: ns_define stores qualified symbols in mappings
        // Get the qualified symbol from the symbol table for lookup
        CljSymbol *qualified_inc_sym = NULL;
        if (g_test_eval_state->current_ns->name && g_test_eval_state->current_ns->name->cname) {
            qualified_inc_sym = intern_symbol(g_test_eval_state->current_ns->name, "inc");
        }
        CljObject *inc_value = qualified_inc_sym ? map_get_sentinel(g_test_eval_state->current_ns->mappings, qualified_inc_sym, NULL) : NULL;

        if (!inc_value) {
            CljPersistentMap *map = g_test_eval_state->current_ns->mappings;
            int symbol_count = 0;
            const char *first_symbol = NULL;
            MAP_FOR_EACH(map, key, value) {
                (void)value;  // unused
                if (key && TAG(key) == CLJ_SYMBOL) {
                    CljSymbol *sym = as_symbol(key);
                    symbol_count++;
                    if (!first_symbol && sym->cname) {
                        first_symbol = sym->cname;
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

        // Don't RELEASE result - eval_body returns autoreleased object
    } CATCH(ex) {
        char msg[256];
        snprintf(msg, sizeof(msg),
                "Exception during def inc evaluation: %s",
                ex && ex->message[0] ? ex->message : "unknown");
        TEST_FAIL_MESSAGE(msg);
    } END_TRY
    
    // Don't RELEASE form - value_by_parsing_expr returns autoreleased object
}

// Test: Verify that + is available when evaluating (fn [x] (+ x 1))
TEST(test_plus_available_during_fn_evaluation) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    
    // Ensure + is registered (should be done by register_builtins)
    CljSymbol *plus_sym = intern_symbol_global("+");
    TEST_ASSERT_NOT_NULL(plus_sym);
    
    // Check if + is in clojure.core mappings
    CljNamespace *clojure_core = ns_find_by_symbol(SYM_CLOJURE_CORE);
    TEST_ASSERT_NOT_NULL_MESSAGE(clojure_core, "clojure.core should exist");
    
    if (clojure_core && clojure_core->mappings) {
        CljObject *plus_value = map_get_sentinel(clojure_core->mappings, plus_sym, NULL);
        TEST_ASSERT_NOT_NULL_MESSAGE(plus_value, 
                                    "+ should be in clojure.core mappings");
    }
    
    // Parse "(fn [x] (+ x 1))"
    Reader reader;
    reader_init(&reader, "(fn [x] (+ x 1))");
    CljValue form = value_by_parsing_expr(&reader, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(form);

    ID canonical_form = canonicalize_ast(form, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(canonical_form);
    
    // Evaluate the fn expression
    CljPersistentMap *env = g_test_eval_state->current_ns ? g_test_eval_state->current_ns->mappings : NULL;
    TEST_ASSERT_NOT_NULL(env);
    
    TRY {
        CljValue result = eval_body(canonical_form, env, g_test_eval_state, NULL);
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

    ID canonical_form = canonicalize_ast(form, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(canonical_form);
    
    // Evaluate the def expression
    CljPersistentMap *env = g_test_eval_state->current_ns ? g_test_eval_state->current_ns->mappings : NULL;
    TEST_ASSERT_NOT_NULL(env);
    
    TRY {
        (void)eval_body(canonical_form, env, g_test_eval_state, NULL);
        
        // CRITICAL: ns_define stores qualified symbols in mappings
        // Get the qualified symbol from the symbol table for lookup
        CljSymbol *test_var_sym = intern_symbol_global("test-var");
        TEST_ASSERT_NOT_NULL(test_var_sym);
        
        CljSymbol *qualified_test_var_sym = NULL;
        if (g_test_eval_state->current_ns->name && g_test_eval_state->current_ns->name->cname) {
            qualified_test_var_sym = intern_symbol(g_test_eval_state->current_ns->name, "test-var");
        }
        TEST_ASSERT_NOT_NULL_MESSAGE(qualified_test_var_sym, "Should be able to create qualified symbol");
        
        CljObject *test_var_value_check = map_get(g_test_eval_state->current_ns->mappings,
                              qualified_test_var_sym);
        bool found_key = (test_var_value_check != NOT_FOUND);
        
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
