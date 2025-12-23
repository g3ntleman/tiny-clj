#include "tests_common.h"
#include "../to_string.h"

// Test: Symbol can be created even if namespace does not exist
TEST(test_symbol_creation_without_namespace) {
    // Create a symbol with a namespace that doesn't exist yet
    // intern_symbol expects CljSymbol* for ns_name, so we need to create the namespace name symbol first
    CljSymbol *ns_name_sym = intern_symbol_global("non-existent-ns");
    CljSymbol *sym = intern_symbol(ns_name_sym, "test-symbol");

    TEST_ASSERT_NOT_NULL_MESSAGE(sym, "Symbol should be created even if namespace doesn't exist");
    TEST_ASSERT_NOT_NULL_MESSAGE(sym->cname, "Symbol should have a name");
    TEST_ASSERT_EQUAL_STRING("test-symbol", sym->cname);

    // Namespace should be a symbol (namespace name), not a namespace object
    TEST_ASSERT_NOT_NULL_MESSAGE(sym->ns_name, "Symbol should have namespace name symbol");
    TEST_ASSERT_EQUAL_INT(CLJ_SYMBOL, TAG(sym->ns_name));

    RELEASE(sym);
    RELEASE(ns_name_sym);
}

// Test: Symbol->ns is CljSymbol* (namespace name), not CljNamespace*
TEST(test_symbol_ns_is_symbol_not_namespace) {
    // Create a symbol with namespace
    // intern_symbol expects CljSymbol* for ns_name, so we need to create the namespace name symbol first
    CljSymbol *ns_name_sym = intern_symbol_global("my-ns");
    CljSymbol *sym = intern_symbol(ns_name_sym, "my-var");

    TEST_ASSERT_NOT_NULL_MESSAGE(sym, "Symbol should be created");

    // sym->ns_name should be CljSymbol* (namespace name symbol)
    TEST_ASSERT_NOT_NULL_MESSAGE(sym->ns_name, "Symbol should have namespace name symbol");
    TEST_ASSERT_EQUAL_INT(CLJ_SYMBOL, TAG(sym->ns_name));
    TEST_ASSERT_EQUAL_STRING("my-ns", sym->ns_name->cname);

    RELEASE(sym);
    RELEASE(ns_name_sym);
}

// Test: Equality comparison works with Symbol->ns as CljSymbol*
// Symbols are interned, so equality is based on pointer comparison
TEST(test_symbol_equality_with_symbol_ns) {
    // Create two symbols with same namespace name using intern_symbol
    // intern_symbol ensures symbols are interned (same pointer for same name+namespace)
    CljSymbol *ns_name_sym = intern_symbol_global("my-ns");
    CljSymbol *sym1 = intern_symbol(ns_name_sym, "test");
    CljSymbol *sym2 = intern_symbol(ns_name_sym, "test");

    TEST_ASSERT_NOT_NULL_MESSAGE(sym1, "First symbol should be created");
    TEST_ASSERT_NOT_NULL_MESSAGE(sym2, "Second symbol should be created");

    // Symbols should be interned (same pointer)
    TEST_ASSERT_EQUAL_PTR_MESSAGE(sym1, sym2, "Interned symbols with same name and namespace should be the same pointer");

    // Equality should work with Symbol->ns as CljSymbol*
    // Since symbols are interned, pointer comparison (clj_equal) should return true
    TEST_ASSERT_TRUE_MESSAGE(clj_equal((CljObject*)sym1, (CljObject*)sym2),
                             "Interned symbols with same name and namespace should be equal (pointer comparison)");

    RELEASE(sym1);
    RELEASE(sym2);
    RELEASE(ns_name_sym);
}

// Test: String representation works with Symbol->ns as CljSymbol*
TEST(test_symbol_string_representation) {
    // Create a namespace-qualified symbol with unique names to avoid conflicts
    CljSymbol *ns_name_sym = intern_symbol_global("test-repr-ns");
    CljSymbol *sym = intern_symbol(ns_name_sym, "test-repr-var");

    TEST_ASSERT_NOT_NULL_MESSAGE(sym, "Symbol should be created");
    TEST_ASSERT_NOT_NULL_MESSAGE(sym->ns_name, "Symbol should have ns_name");
    TEST_ASSERT_EQUAL_INT_MESSAGE(CLJ_SYMBOL, TAG(sym->ns_name), "ns_name should be a symbol");

    // String representation should work
    CljString *str = to_string(sym);
    TEST_ASSERT_NOT_NULL_MESSAGE(str, "String representation should work");
    
    const char *str_data = clj_string_data(str);
    TEST_ASSERT_NOT_NULL_MESSAGE(str_data, "String data should not be NULL");
    
    // Expected: "test-repr-ns/test-repr-var"
    TEST_ASSERT_TRUE_MESSAGE(strstr(str_data, "test-repr-ns") != NULL,
                             "String representation should contain namespace");
    TEST_ASSERT_TRUE_MESSAGE(strstr(str_data, "test-repr-var") != NULL,
                             "String representation should contain name");

    RELEASE(sym);
    RELEASE(ns_name_sym);
}

// Test: Namespace lookup over Symbol->ns->cname
TEST(test_namespace_lookup_from_symbol) {
    // Create namespace first
    CljNamespace *ns = ns_get_or_create("test-ns", NULL);
    TEST_ASSERT_NOT_NULL_MESSAGE(ns, "Namespace should be created");

    // Create symbol with that namespace
    // intern_symbol expects CljSymbol* for ns_name, so we need to create the namespace name symbol first
    CljSymbol *ns_name_sym = intern_symbol_global("test-ns");
    CljSymbol *sym = intern_symbol(ns_name_sym, "test-var");
    TEST_ASSERT_NOT_NULL_MESSAGE(sym, "Symbol should be created");

    // Should be able to lookup namespace via sym->ns_name->cname
    CljNamespace *found_ns = symbol_get_namespace(sym);
    TEST_ASSERT_EQUAL_PTR_MESSAGE(ns, found_ns, "Should find namespace via symbol's namespace name");

    RELEASE(sym);
    RELEASE(ns_name_sym);
}

// Test: Symbol without namespace (ns = NULL)
TEST(test_symbol_without_namespace) {
    CljSymbol *sym = intern_symbol_global("unqualified");

    TEST_ASSERT_NOT_NULL_MESSAGE(sym, "Symbol should be created");
    TEST_ASSERT_NULL_MESSAGE(sym->ns_name, "Symbol without namespace should have ns = NULL");
    TEST_ASSERT_EQUAL_STRING("unqualified", sym->cname);

    RELEASE(sym);
}

