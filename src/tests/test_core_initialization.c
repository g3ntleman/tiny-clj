#include "tests_common.h"
#include "symbol.h"
#include "namespace.h"
#include "runtime.h"
#include "map.h"
#include "kv_macros.h"

// Test: Verify that inc is loaded during test initialization
TEST(test_core_initialization_inc_loaded) {
    // This test runs AFTER setUp(), so clojure.core should already be loaded
    TEST_ASSERT_NOT_NULL_MESSAGE(g_runtime.clojure_core_cache, 
                                 "clojure.core cache should be set after setUp()");
    
    CljNamespace *clojure_core = (CljNamespace*)g_runtime.clojure_core_cache;
    TEST_ASSERT_NOT_NULL_MESSAGE(clojure_core, "clojure.core namespace should exist");
    TEST_ASSERT_NOT_NULL_MESSAGE(clojure_core->mappings, "clojure.core mappings should exist");
    
    // Get inc symbol
    CljObject *inc_sym = intern_symbol_global("inc");
    TEST_ASSERT_NOT_NULL(inc_sym);
    
    // Check if inc is in clojure.core mappings
    CljMap *map = (CljMap*)clojure_core->mappings;
    TEST_ASSERT_NOT_NULL(map);
    
    // Debug: Count all symbols in mappings
    int symbol_count = 0;
    const char *first_symbol_name = NULL;
    bool found_inc = false;
    
    for (int i = 0; i < map->count; i++) {
        CljObject *key = KV_KEY(map->data, i);
        if (key && is_type(key, CLJ_SYMBOL)) {
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
    CljObject *inc_value = (CljObject*)map_get((CljValue)map, (CljValue)inc_sym);
    
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
    TEST_ASSERT_TRUE_MESSAGE(is_type(inc_value, CLJ_FUNC) || is_type(inc_value, CLJ_CLOSURE),
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
    CljObject *plus_sym = intern_symbol_global("+");
    TEST_ASSERT_NOT_NULL(plus_sym);
    
    // Check if + is in clojure.core mappings (should be registered by register_builtins)
    CljObject *plus_value = (CljObject*)map_get((CljValue)clojure_core->mappings, (CljValue)plus_sym);
    TEST_ASSERT_NOT_NULL_MESSAGE(plus_value, 
                                 "+ should be in clojure.core mappings (registered by register_builtins)");
}

