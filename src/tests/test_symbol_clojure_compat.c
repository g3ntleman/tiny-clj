#include "tests_common.h"

// Test: Symbol can be created even if namespace does not exist
TEST(test_symbol_creation_without_namespace) {
    // Create a symbol with a namespace that doesn't exist yet
    CljSymbol *sym = make_symbol("test-symbol", "non-existent-ns");
    
    TEST_ASSERT_NOT_NULL_MESSAGE(sym, "Symbol should be created even if namespace doesn't exist");
    TEST_ASSERT_NOT_NULL_MESSAGE(sym->name, "Symbol should have a name");
    TEST_ASSERT_EQUAL_STRING("test-symbol", sym->name);
    
    // Namespace should be a symbol (namespace name), not a namespace object
    TEST_ASSERT_NOT_NULL_MESSAGE(sym->ns, "Symbol should have namespace name symbol");
    TEST_ASSERT_EQUAL_INT(CLJ_SYMBOL, TAG(sym->ns));
    
    RELEASE((CljObject*)sym);
}

// Test: Symbol->ns is CljSymbol* (namespace name), not CljNamespace*
TEST(test_symbol_ns_is_symbol_not_namespace) {
    // Create a symbol with namespace
    CljSymbol *sym = make_symbol("my-var", "my-ns");
    
    TEST_ASSERT_NOT_NULL_MESSAGE(sym, "Symbol should be created");
    
    // sym->ns should be CljSymbol* (namespace name symbol)
    TEST_ASSERT_NOT_NULL_MESSAGE(sym->ns, "Symbol should have namespace name symbol");
    TEST_ASSERT_EQUAL_INT(CLJ_SYMBOL, TAG(sym->ns));
    TEST_ASSERT_EQUAL_STRING("my-ns", sym->ns->name);
    
    RELEASE((CljObject*)sym);
}

// Test: Equality comparison works with Symbol->ns as CljSymbol*
TEST(test_symbol_equality_with_symbol_ns) {
    // Create two symbols with same namespace name
    CljSymbol *sym1 = make_symbol("test", "my-ns");
    CljSymbol *sym2 = make_symbol("test", "my-ns");
    
    TEST_ASSERT_NOT_NULL_MESSAGE(sym1, "First symbol should be created");
    TEST_ASSERT_NOT_NULL_MESSAGE(sym2, "Second symbol should be created");
    
    // Equality should work with Symbol->ns as CljSymbol*
    // Symbols with same name and namespace should be equal
    TEST_ASSERT_TRUE_MESSAGE(clj_equal((CljObject*)sym1, (CljObject*)sym2),
                             "Symbols with same name and namespace should be equal");
    
    RELEASE((CljObject*)sym1);
    RELEASE((CljObject*)sym2);
}

// Test: String representation works with Symbol->ns as CljSymbol*
TEST(test_symbol_string_representation) {
    // Create a namespace-qualified symbol
    CljSymbol *sym = make_symbol("my-var", "my-ns");
    
    TEST_ASSERT_NOT_NULL_MESSAGE(sym, "Symbol should be created");
    
    // String representation should work
    const char *str = to_cstring((CljObject*)sym);
    TEST_ASSERT_NOT_NULL_MESSAGE(str, "String representation should work");
    TEST_ASSERT_TRUE_MESSAGE(strstr(str, "my-ns") != NULL && strstr(str, "my-var") != NULL,
                             "String representation should contain namespace and name");
    free((void*)str);
    
    RELEASE((CljObject*)sym);
}

// Test: Namespace lookup over Symbol->ns->name
TEST(test_namespace_lookup_from_symbol) {
    // Create namespace first
    CljNamespace *ns = ns_get_or_create("test-ns", NULL);
    TEST_ASSERT_NOT_NULL_MESSAGE(ns, "Namespace should be created");
    
    // Create symbol with that namespace
    CljSymbol *sym = make_symbol("test-var", "test-ns");
    TEST_ASSERT_NOT_NULL_MESSAGE(sym, "Symbol should be created");
    
    // Should be able to lookup namespace via sym->ns->name
    CljNamespace *found_ns = symbol_get_namespace(sym);
    TEST_ASSERT_EQUAL_PTR_MESSAGE(ns, found_ns, "Should find namespace via symbol's namespace name");
    
    RELEASE((CljObject*)sym);
}

// Test: Symbol without namespace (ns = NULL)
TEST(test_symbol_without_namespace) {
    CljSymbol *sym = make_symbol("unqualified", NULL);
    
    TEST_ASSERT_NOT_NULL_MESSAGE(sym, "Symbol should be created");
    TEST_ASSERT_NULL_MESSAGE(sym->ns, "Symbol without namespace should have ns = NULL");
    TEST_ASSERT_EQUAL_STRING("unqualified", sym->name);
    
    RELEASE((CljObject*)sym);
}

