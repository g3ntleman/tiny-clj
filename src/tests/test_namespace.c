#include "unity.h"
#include "tiny_clj.h"
#include "function_call.h"
#include "reader.h"
#include "object.h"
#include "namespace.h"
#include "memory.h"

// Forward declaration for load_clojure_core
int load_clojure_core(EvalState *st);

// Test namespace lookup for core functions
void test_namespace_lookup_core_functions(void) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);
    
    // Load clojure.core functions
    load_clojure_core(st);
    
    // Test that map symbol exists in clojure.core namespace
    CljObject *map_sym = intern_symbol_global("map");
    TEST_ASSERT_NOT_NULL(map_sym);
    
    // Switch to clojure.core namespace
    evalstate_set_ns(st, "clojure.core");
    
    // Resolve map symbol in clojure.core namespace
    CljObject *resolved = ns_resolve(st, map_sym);
    TEST_ASSERT_NOT_NULL(resolved);
    TEST_ASSERT_TRUE(is_type(resolved, CLJ_CLOSURE));
    
    // Cleanup
    RELEASE((CljObject*)resolved);
    RELEASE((CljObject*)map_sym);
    evalstate_free(st);
}

// Test namespace lookup for user namespace
void test_namespace_lookup_user_namespace(void) {
    // Test that symbols are resolved in user namespace by default
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);
    
    // Test direct namespace storage and retrieval
    CljObject *test_sym = intern_symbol_global("test-var");
    CljObject *value = fixnum(42);
    
    // Store variable directly in namespace
    ns_define(st, test_sym, value);
    
    // Now resolve test-var in user namespace
    CljObject *resolved = ns_resolve(st, test_sym);
    TEST_ASSERT_NOT_NULL(resolved);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)resolved));
    TEST_ASSERT_EQUAL(42, as_fixnum((CljValue)resolved));
    
    // Cleanup
    RELEASE((CljObject*)resolved);
    RELEASE((CljObject*)test_sym);
    RELEASE((CljObject*)value);
    evalstate_free(st);
}

// Test cross-namespace symbol resolution
void test_namespace_lookup_cross_namespace(void) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);
    
    // Load clojure.core functions
    load_clojure_core(st);
    
    // Switch to user namespace
    evalstate_set_ns(st, "user");
    
    // Try to resolve map symbol (should find it in clojure.core)
    CljObject *map_sym = intern_symbol_global("map");
    CljObject *resolved = ns_resolve(st, map_sym);
    
    // Should resolve to clojure.core/map
    TEST_ASSERT_NOT_NULL(resolved);
    TEST_ASSERT_TRUE(is_type(resolved, CLJ_CLOSURE));
    
    // Cleanup
    RELEASE((CljObject*)resolved);
    RELEASE((CljObject*)map_sym);
    evalstate_free(st);
}

// Test symbol interning - same symbol should return same pointer
void test_symbol_interning_consistency(void) {
    // Test that intern_symbol_global returns the same pointer for the same name
    CljObject *sym1 = intern_symbol_global("test-symbol");
    CljObject *sym2 = intern_symbol_global("test-symbol");
    
    TEST_ASSERT_NOT_NULL(sym1);
    TEST_ASSERT_NOT_NULL(sym2);
    TEST_ASSERT_EQUAL_PTR(sym1, sym2); // Should be the same pointer
    
    // Test different symbols return different pointers
    CljObject *sym3 = intern_symbol_global("different-symbol");
    TEST_ASSERT_NOT_NULL(sym3);
    TEST_ASSERT_TRUE(sym1 != sym3);
    
    // Cleanup
    RELEASE((CljObject*)sym1);
    RELEASE((CljObject*)sym2);
    RELEASE((CljObject*)sym3);
}

// Test symbol interning with namespace
void test_symbol_interning_with_namespace(void) {
    // Test that intern_symbol with namespace works correctly
    CljObject *sym1 = intern_symbol("user", "test-symbol");
    CljObject *sym2 = intern_symbol("user", "test-symbol");
    
    TEST_ASSERT_NOT_NULL(sym1);
    TEST_ASSERT_NOT_NULL(sym2);
    TEST_ASSERT_EQUAL_PTR(sym1, sym2); // Should be the same pointer
    
    // Test different namespace returns different symbol
    CljObject *sym3 = intern_symbol("clojure.core", "test-symbol");
    TEST_ASSERT_NOT_NULL(sym3);
    TEST_ASSERT_TRUE(sym1 != sym3);
    
    // Cleanup
    RELEASE((CljObject*)sym1);
    RELEASE((CljObject*)sym2);
    RELEASE((CljObject*)sym3);
}

// Test symbol interning with NULL namespace (global)
void test_symbol_interning_global(void) {
    // Test that intern_symbol_global is equivalent to intern_symbol(NULL, name)
    CljObject *sym1 = intern_symbol_global("global-symbol");
    CljObject *sym2 = intern_symbol(NULL, "global-symbol");
    
    TEST_ASSERT_NOT_NULL(sym1);
    TEST_ASSERT_NOT_NULL(sym2);
    TEST_ASSERT_EQUAL_PTR(sym1, sym2); // Should be the same pointer
    
    // Cleanup
    RELEASE((CljObject*)sym1);
    RELEASE((CljObject*)sym2);
}

// Test symbol table functionality
void test_symbol_table_operations(void) {
    // Test that symbol table correctly stores and retrieves symbols
    const char *test_name = "table-test-symbol";
    
    // First call should create new symbol
    CljObject *sym1 = intern_symbol_global(test_name);
    TEST_ASSERT_NOT_NULL(sym1);
    
    // Second call should return same symbol
    CljObject *sym2 = intern_symbol_global(test_name);
    TEST_ASSERT_NOT_NULL(sym2);
    TEST_ASSERT_EQUAL_PTR(sym1, sym2);
    
    // Test symbol count
    int count = symbol_count();
    TEST_ASSERT_TRUE(count > 0);
    
    // Cleanup
    RELEASE((CljObject*)sym1);
    RELEASE((CljObject*)sym2);
}

// Test namespace creation and switching
void test_namespace_creation_and_switching(void) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);
    
    // Test initial namespace is user
    TEST_ASSERT_NOT_NULL(st->current_ns);
    TEST_ASSERT_EQUAL_STRING("user", as_symbol(st->current_ns->name)->name);
    
    // Test switching to new namespace
    evalstate_set_ns(st, "test-namespace");
    TEST_ASSERT_NOT_NULL(st->current_ns);
    TEST_ASSERT_EQUAL_STRING("test-namespace", as_symbol(st->current_ns->name)->name);
    
    // Test switching back to user
    evalstate_set_ns(st, "user");
    TEST_ASSERT_EQUAL_STRING("user", as_symbol(st->current_ns->name)->name);
    
    evalstate_free(st);
}

// Test namespace variable storage and retrieval
void test_namespace_variable_storage(void) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);
    
    // Create symbols
    CljObject *var_sym = intern_symbol_global("test-variable");
    CljObject *value = fixnum(123);
    
    // Store variable in namespace
    ns_define(st, var_sym, value);
    
    // Retrieve variable from namespace
    CljObject *retrieved = ns_resolve(st, var_sym);
    TEST_ASSERT_NOT_NULL(retrieved);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)retrieved));
    TEST_ASSERT_EQUAL(123, as_fixnum((CljValue)retrieved));
    
    // Cleanup
    RELEASE((CljObject*)retrieved);
    RELEASE((CljObject*)var_sym);
    RELEASE((CljObject*)value);
    evalstate_free(st);
}

// Test namespace with multiple variables
void test_namespace_multiple_variables(void) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);
    
    // Create multiple variables
    CljObject *var1_sym = intern_symbol_global("var1");
    CljObject *var2_sym = intern_symbol_global("var2");
    CljObject *value1 = fixnum(100);
    CljObject *value2 = fixnum(200);
    
    // Store variables
    ns_define(st, var1_sym, value1);
    ns_define(st, var2_sym, value2);
    
    // Retrieve and verify
    CljObject *retrieved1 = ns_resolve(st, var1_sym);
    CljObject *retrieved2 = ns_resolve(st, var2_sym);
    
    TEST_ASSERT_NOT_NULL(retrieved1);
    TEST_ASSERT_NOT_NULL(retrieved2);
    TEST_ASSERT_EQUAL(100, as_fixnum((CljValue)retrieved1));
    TEST_ASSERT_EQUAL(200, as_fixnum((CljValue)retrieved2));
    
    // Cleanup
    RELEASE((CljObject*)retrieved1);
    RELEASE((CljObject*)retrieved2);
    RELEASE((CljObject*)var1_sym);
    RELEASE((CljObject*)var2_sym);
    RELEASE((CljObject*)value1);
    RELEASE((CljObject*)value2);
    evalstate_free(st);
}

// Test symbol resolution with fallback to global namespace
void test_symbol_resolution_fallback(void) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);
    
    // Test that built-in functions are resolved via eval_symbol fallback
    CljObject *plus_sym = intern_symbol_global("+");
    CljObject *resolved = eval_symbol(plus_sym, st);
    
    TEST_ASSERT_NOT_NULL(resolved);
    TEST_ASSERT_TRUE(is_type(resolved, CLJ_FUNC)); // Should be a native function
    
    // Cleanup
    RELEASE((CljObject*)resolved);
    RELEASE((CljObject*)plus_sym);
    evalstate_free(st);
}

// Test namespace with special characters in names
void test_namespace_special_characters(void) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);
    
    // Test symbols with special characters
    CljObject *special_sym = intern_symbol_global("test-var?");
    CljObject *value = fixnum(42);
    
    // Store and retrieve
    ns_define(st, special_sym, value);
    CljObject *retrieved = ns_resolve(st, special_sym);
    
    TEST_ASSERT_NOT_NULL(retrieved);
    TEST_ASSERT_EQUAL(42, as_fixnum((CljValue)retrieved));
    
    // Cleanup
    RELEASE((CljObject*)retrieved);
    RELEASE((CljObject*)special_sym);
    RELEASE((CljObject*)value);
    evalstate_free(st);
}

// Test namespace error handling
void test_namespace_error_handling(void) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);
    
    // Test resolving non-existent symbol
    CljObject *non_existent = intern_symbol_global("non-existent-var");
    CljObject *resolved = ns_resolve(st, non_existent);
    
    TEST_ASSERT_NULL(resolved); // Should return NULL for non-existent symbol
    
    // Test with NULL parameters
    CljObject *result1 = ns_resolve(NULL, non_existent);
    CljObject *result2 = ns_resolve(st, NULL);
    
    TEST_ASSERT_NULL(result1);
    TEST_ASSERT_NULL(result2);
    
    // Cleanup
    RELEASE((CljObject*)non_existent);
    evalstate_free(st);
}

// Test namespace memory management
void test_namespace_memory_management(void) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);
    
    // Create and store many variables
    for (int i = 0; i < 10; i++) {
        char name[32];
        snprintf(name, sizeof(name), "var%d", i);
        
        CljObject *sym = intern_symbol_global(name);
        CljObject *value = fixnum(i * 10);
        
        ns_define(st, sym, value);
        
        // Verify it was stored
        CljObject *retrieved = ns_resolve(st, sym);
        TEST_ASSERT_NOT_NULL(retrieved);
        TEST_ASSERT_EQUAL(i * 10, as_fixnum((CljValue)retrieved));
        
        RELEASE((CljObject*)retrieved);
        RELEASE((CljObject*)sym);
        RELEASE((CljObject*)value);
    }
    
    evalstate_free(st);
}
