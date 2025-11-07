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

// Test: Verify that (def inc (fn [x] (+ x 1))) is evaluated correctly
TEST(test_def_inc_evaluation_during_load) {
    TEST_ASSERT_NOT_NULL(st);
    
    
    // Parse "(def inc (fn [x] (+ x 1)))"
    Reader reader;
    reader_init(&reader, "(def inc (fn [x] (+ x 1)))");
    CljValue form = value_by_parsing_expr(&reader, st);
    TEST_ASSERT_NOT_NULL(form);
    
    // Extract the symbol from the parsed form
    if (is_type(form, CLJ_LIST)) {
        CljList *list = as_list(form);
        CljSymbol *inc_sym = as_symbol(list_nth(list, 1));
        CljObject *fn_expr = (CljObject*)list_nth(list, 2);
        
        TEST_ASSERT_NOT_NULL(inc_sym);
        TEST_ASSERT_TRUE(is_type(inc_sym, CLJ_SYMBOL));
        TEST_ASSERT_NOT_NULL(fn_expr);
        
        // Verify that inc_sym is the same as intern_symbol_global("inc")
        CljObject *inc_sym_interned = intern_symbol_global("inc");
        TEST_ASSERT_EQUAL_PTR_MESSAGE(inc_sym, inc_sym_interned,
                                      "inc symbol should be interned");
        
        // Evaluate the def expression
        CljMap *env = st->current_ns ? (CljMap*)st->current_ns->mappings : NULL;
        TEST_ASSERT_NOT_NULL(env);
        
        TRY {
            CljValue result = eval_list(list, env, st);
            
            // Check if inc is now in the mappings
            CljObject *inc_value = (CljObject*)map_get((CljValue)st->current_ns->mappings, (CljValue)inc_sym_interned);
            
            if (!inc_value) {
                // Debug: Check what symbols ARE in the mappings
                CljMap *map = (CljMap*)st->current_ns->mappings;
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
            TEST_ASSERT_TRUE_MESSAGE(is_type(inc_value, CLJ_FUNC) || is_type(inc_value, CLJ_CLOSURE),
                                    "inc should be a function");
            
            RELEASE((CljObject*)result);
        } CATCH(ex) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                    "Exception during def inc evaluation: %s",
                    ex && ex->message[0] ? ex->message : "unknown");
            TEST_FAIL_MESSAGE(msg);
        } END_TRY
    }
    
    RELEASE((CljObject*)form);
}

// Test: Verify that + is available when evaluating (fn [x] (+ x 1))
TEST(test_plus_available_during_fn_evaluation) {
    TEST_ASSERT_NOT_NULL(st);
    
    
    // Ensure + is registered (should be done by register_builtins)
    CljObject *plus_sym = intern_symbol_global("+");
    TEST_ASSERT_NOT_NULL(plus_sym);
    
    // Check if + is in clojure.core mappings
    CljNamespace *clojure_core = (CljNamespace*)g_runtime.clojure_core_cache;
    TEST_ASSERT_NOT_NULL_MESSAGE(clojure_core, "clojure.core should exist");
    
    if (clojure_core && clojure_core->mappings) {
        CljObject *plus_value = (CljObject*)map_get((CljValue)clojure_core->mappings, (CljValue)plus_sym);
        TEST_ASSERT_NOT_NULL_MESSAGE(plus_value, 
                                    "+ should be in clojure.core mappings");
    }
    
    // Parse "(fn [x] (+ x 1))"
    Reader reader;
    reader_init(&reader, "(fn [x] (+ x 1))");
    CljValue form = value_by_parsing_expr(&reader, st);
    TEST_ASSERT_NOT_NULL(form);
    
    // Evaluate the fn expression
    CljMap *env = st->current_ns ? (CljMap*)st->current_ns->mappings : NULL;
    TEST_ASSERT_NOT_NULL(env);
    
    TRY {
        CljValue result = eval_list(as_list(form), env, st);
        TEST_ASSERT_NOT_NULL_MESSAGE(result, 
                                    "fn expression should evaluate to a function");
        TEST_ASSERT_TRUE_MESSAGE(is_type(result, CLJ_FUNC) || is_type(result, CLJ_CLOSURE),
                                "fn expression should return a function");
        
        RELEASE((CljObject*)result);
    } CATCH(ex) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                    "Exception during fn evaluation: %s",
                    ex && ex->message[0] ? ex->message : "unknown");
        TEST_FAIL_MESSAGE(msg);
    } END_TRY
    
    RELEASE((CljObject*)form);
}

// Test: Verify that eval_def stores the symbol even if value evaluation returns NULL
TEST(test_def_stores_symbol_even_if_value_null) {
    TEST_ASSERT_NOT_NULL(st);
    
    evalstate_set_ns(st, "user");
    
    // Parse "(def test-var nil)"
    Reader reader;
    reader_init(&reader, "(def test-var nil)");
    CljValue form = value_by_parsing_expr(&reader, st);
    TEST_ASSERT_NOT_NULL(form);
    
    // Evaluate the def expression
    CljMap *env = st->current_ns ? (CljMap*)st->current_ns->mappings : NULL;
    TEST_ASSERT_NOT_NULL(env);
    
    TRY {
        CljValue result = eval_list(as_list(form), env, st);
        
        // Check if test-var is in the mappings (even if value is nil/NULL)
        CljObject *test_var_sym = intern_symbol_global("test-var");
        TEST_ASSERT_NOT_NULL(test_var_sym);
        
        // test_var_value can be NULL if nil was stored
        (void)map_get((CljValue)st->current_ns->mappings, (CljValue)test_var_sym);
        // But the key should be in the map
        // Let's check if the key exists by iterating
        CljMap *map = (CljMap*)st->current_ns->mappings;
        bool found_key = false;
        for (int i = 0; i < map->count; i++) {
            CljObject *key = KV_KEY(map->data, i);
            if (key == test_var_sym) {
                found_key = true;
                break;
            }
        }
        
        TEST_ASSERT_TRUE_MESSAGE(found_key, 
                                "test-var should be in mappings after def");
        
        RELEASE((CljObject*)result);
    } CATCH(ex) {
        char msg[256];
        snprintf(msg, sizeof(msg),
                "Exception during def test-var evaluation: %s",
                ex && ex->message[0] ? ex->message : "unknown");
        TEST_FAIL_MESSAGE(msg);
    } END_TRY
    
    RELEASE((CljObject*)form);
}

