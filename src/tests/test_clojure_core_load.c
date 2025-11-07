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

// Test: Verify that inc is loaded correctly when loading clojure.core
TEST(test_clojure_core_loads_inc) {
    // Get inc symbol before loading
    CljObject *inc_sym = intern_symbol_global("inc");
    TEST_ASSERT_NOT_NULL(inc_sym);
    
    // Load clojure.core (use global st from setUp)
    evalstate_set_ns(st, "clojure.core");
    load_clojure_core(st);
    
    // Check if inc is in clojure.core mappings
    CljNamespace *clojure_core = (CljNamespace*)g_runtime.clojure_core_cache;
    TEST_ASSERT_NOT_NULL_MESSAGE(clojure_core, "clojure.core cache should be set");
    
    if (clojure_core && clojure_core->mappings) {
        CljObject *inc_value = (CljObject*)map_get((CljValue)clojure_core->mappings, (CljValue)inc_sym);
        
        if (!inc_value) {
            // Debug: Check what symbols ARE in the mappings
            CljMap *map = (CljMap*)clojure_core->mappings;
            int symbol_count = 0;
            const char *first_symbol = NULL;
            const char *inc_symbol_found = NULL;
            CljObject *inc_symbol_ptr = NULL;
            
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
                    (inc_sym == inc_symbol_ptr) ? 1 : 0);
            TEST_FAIL_MESSAGE(msg);
        }
        
        TEST_ASSERT_NOT_NULL_MESSAGE(inc_value, 
                                    "inc should be in clojure.core mappings after load");
        TEST_ASSERT_TRUE_MESSAGE(is_type(inc_value, CLJ_FUNC) || is_type(inc_value, CLJ_CLOSURE),
                                "inc should be a function");
    }
}

// Test: Verify that all core functions are loaded correctly
TEST(test_clojure_core_loads_all_functions) {
    // Load clojure.core (use global st from setUp)
    evalstate_set_ns(st, "clojure.core");
    load_clojure_core(st);
    
    // Check if key functions are loaded
    CljNamespace *clojure_core = (CljNamespace*)g_runtime.clojure_core_cache;
    TEST_ASSERT_NOT_NULL_MESSAGE(clojure_core, "clojure.core cache should be set");
    
    if (clojure_core && clojure_core->mappings) {
        // Check for add, sub, mul, div, inc, dec
        const char *functions[] = {"add", "sub", "mul", "div", "inc", "dec", "square"};
        int missing_count = 0;
        char missing_names[256] = "";
        
        for (int i = 0; i < 7; i++) {
            CljObject *sym = intern_symbol_global(functions[i]);
            CljObject *value = (CljObject*)map_get((CljValue)clojure_core->mappings, (CljValue)sym);
            
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

