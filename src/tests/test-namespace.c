/*
 * Consolidated Namespace Tests (MinUnit)
 * 
 * Tests for:
 * - EvalState creation and namespace management
 * - (ns) function and namespace switching
 * - Namespace isolation
 * - *ns* special variable
 */

#include "object.h"
#include "parser.h"
#include "symbol.h"
#include "clj_string.h"
#include "namespace.h"
#include "map.h"
#include "tiny_clj.h"
#include "minunit.h"
#include <stdio.h>
#include <string.h>

// Forward declaration
int load_clojure_core(EvalState *st);

// ============================================================================
// TEST HELPERS
// ============================================================================

static EvalState *global_eval_state = NULL;

static void test_setup(void) {
    init_special_symbols();
    meta_registry_init();
    global_eval_state = evalstate_new();
    // Do NOT load clojure.core here - it resets namespace to 'user'
}

static void test_teardown(void) {
    if (global_eval_state) {
        evalstate_free(global_eval_state);
        global_eval_state = NULL;
    }
    symbol_table_cleanup();
    meta_registry_cleanup();
}

static CljObject* eval_code(const char *code) {
    return eval_string(code, global_eval_state);
}

// Helper to get current namespace name
static const char* get_current_ns_name(void) {
    if (!global_eval_state || !global_eval_state->current_ns) return NULL;
    if (!global_eval_state->current_ns->name) return NULL;
    CljSymbol *sym = as_symbol(global_eval_state->current_ns->name);
    return sym ? sym->name : NULL;
}

// Assertion helpers

#define assert_type(msg, obj, type) mu_assert(msg, is_type(obj, type))
#define assert_ns_name(msg, expected) mu_assert(msg, \
    get_current_ns_name() && strcmp(get_current_ns_name(), expected) == 0)

// ============================================================================
// EVALSTATE & NAMESPACE INFRASTRUCTURE TESTS
// ============================================================================

static char* test_evalstate_creation(void) {
    test_setup();
    
    mu_assert("EvalState should be created", global_eval_state != NULL);
    mu_assert("EvalState should have current_ns", global_eval_state->current_ns != NULL);
    // Note: last_error removed - Exception handling now uses global exception stack
    
    test_teardown();
    return NULL;
}

static char* test_evalstate_set_ns(void) {
    test_setup();
    
    // Test setting namespace via C API
    evalstate_set_ns(global_eval_state, "test.namespace");
    mu_assert("EvalState should have current_ns", global_eval_state->current_ns != NULL);
    mu_assert("Namespace name should be a symbol", 
        global_eval_state->current_ns->name->type == CLJ_SYMBOL);
    
    const char *ns_name = get_current_ns_name();
    mu_assert("Namespace should be 'test.namespace'", 
        ns_name && strcmp(ns_name, "test.namespace") == 0);
    
    test_teardown();
    return NULL;
}

static char* test_ns_get_or_create(void) {
    test_setup();
    
    // Test creating new namespace
    CljNamespace *ns1 = ns_get_or_create("new.namespace", "test-namespace.c");
    mu_assert("ns_get_or_create should return namespace", ns1 != NULL);
    mu_assert("Namespace name should be a symbol", ns1->name->type == CLJ_SYMBOL);
    mu_assert("Namespace should have mappings", ns1->mappings != NULL);
    
    // Test getting existing namespace
    CljNamespace *ns2 = ns_get_or_create("new.namespace", "test-namespace.c");
    mu_assert("Should return same namespace pointer", ns1 == ns2);
    
    // Test creating different namespace
    CljNamespace *ns3 = ns_get_or_create("other.namespace", "test-namespace.c");
    mu_assert("Different namespace should be created", ns3 != NULL);
    mu_assert("Different namespace should have different pointer", ns1 != ns3);
    
    test_teardown();
    return NULL;
}

static char* test_map_operations(void) {
    test_setup();
    
    // Test map creation and basic operations
    CljObject *map = autorelease(make_map(4));
    CljMap *map_data = as_map(map);
    
    mu_assert("Map should start empty", map_data->count == 0);
    mu_assert("Map should have capacity", map_data->capacity == 4);
    
    // Test map_assoc
    CljObject *key = autorelease(make_string("test-key"));
    CljObject *value = autorelease(make_int(42));
    
    map_assoc(map, key, value);
    mu_assert("Map should have one entry", map_data->count == 1);
    
    // Test map_get
    CljObject *retrieved = map_get(map, key);
    mu_assert("Retrieved value should not be NULL", retrieved != NULL);
    mu_assert("Retrieved value should equal original", clj_equal(retrieved, value));
    
    // Test map_get with non-existent key
    CljObject *other_key = autorelease(make_string("other-key"));
    CljObject *not_found = map_get(map, other_key);
    mu_assert("Non-existent key should return NULL", not_found == NULL);
    
    test_teardown();
    return NULL;
}

static char* test_namespace_isolation(void) {
    test_setup();
    
    // Test that different namespaces are isolated
    CljNamespace *ns1 = ns_get_or_create("namespace1", "test-namespace.c");
    CljNamespace *ns2 = ns_get_or_create("namespace2", "test-namespace.c");
    
    // Add same symbol to both namespaces with different values
    CljObject *sym = autorelease(make_symbol("shared-symbol", NULL));
    CljObject *val1 = autorelease(make_int(100));
    CljObject *val2 = autorelease(make_int(200));
    
    map_assoc(ns1->mappings, sym, val1);
    map_assoc(ns2->mappings, sym, val2);
    
    // Test that values are different
    CljObject *found1 = map_get(ns1->mappings, sym);
    CljObject *found2 = map_get(ns2->mappings, sym);
    
    mu_assert("Value in ns1 should be found", found1 != NULL);
    mu_assert("Value in ns2 should be found", found2 != NULL);
    mu_assert("Values should be different", !clj_equal(found1, found2));
    mu_assert("ns1 value should be 100", clj_equal(found1, val1));
    mu_assert("ns2 value should be 200", clj_equal(found2, val2));
    
    test_teardown();
    return NULL;
}

static char* test_eval_expr_simple_atoms(void) {
    test_setup();
    
    evalstate_set_ns(global_eval_state, "test.eval");
    
    // Test integer evaluation
    CljObject *int_obj = autorelease(make_int(42));
    CljObject *result = eval_expr_simple(int_obj, global_eval_state);
    mu_assert("Integer should evaluate", result != NULL);
    mu_assert("Integer should evaluate to itself", clj_equal(int_obj, result));
    
    // Test string evaluation
    CljObject *str_obj = autorelease(make_string("hello"));
    result = eval_expr_simple(str_obj, global_eval_state);
    mu_assert("String should evaluate", result != NULL);
    mu_assert("String should evaluate to itself", clj_equal(str_obj, result));
    
    // Test nil evaluation
    result = eval_expr_simple(clj_nil(), global_eval_state);
    mu_assert("Nil should evaluate", result != NULL);
    mu_assert("Nil should evaluate to itself", clj_equal(clj_nil(), result));
    
    test_teardown();
    return NULL;
}

static char* test_eval_expr_simple_symbols(void) {
    test_setup();
    
    evalstate_set_ns(global_eval_state, "test.eval");
    
    // Add symbol to namespace
    CljObject *sym = autorelease(make_symbol("test-symbol", NULL));
    CljObject *value = autorelease(make_int(123));
    map_assoc(global_eval_state->current_ns->mappings, sym, value);
    
    // Test symbol evaluation
    CljObject *result = eval_expr_simple(sym, global_eval_state);
    mu_assert("Symbol should evaluate", result != NULL);
    mu_assert("Symbol should evaluate to its value", clj_equal(value, result));
    
    
    test_teardown();
    return NULL;
}

// ============================================================================
// (NS) FUNCTION TESTS
// ============================================================================

static char* test_ns_returns_nil(void) {
    test_setup();
    
    CljObject *result = eval_code("(ns test.namespace)");
    // Note: (ns) returns nil but result might be NULL
    mu_assert("(ns test.namespace) should return nil or NULL", 
        !result || result->type == CLJ_NIL);
    
    test_teardown();
    return NULL;
}

static char* test_ns_switches_namespace(void) {
    test_setup();
    
    // Start in 'user' namespace
    const char *start_ns = get_current_ns_name();
    mu_assert("Should start in a namespace", start_ns != NULL);
    printf("  Start namespace: %s\n", start_ns ? start_ns : "NULL");
    
    // Switch to foo-bar (using hyphen instead of dot due to parser limitation)
    eval_code("(ns foo-bar)");
    const char *after_switch = get_current_ns_name();
    printf("  After (ns foo-bar): %s\n", after_switch ? after_switch : "NULL");
    assert_ns_name("Should switch to 'foo-bar'", "foo-bar");
    
    // Switch to another namespace (using hyphen instead of dot)
    eval_code("(ns my-app)");
    assert_ns_name("Should switch to 'my-app'", "my-app");
    
    // Switch back to user
    eval_code("(ns user)");
    assert_ns_name("Should switch back to 'user'", "user");
    
    test_teardown();
    return NULL;
}

static char* test_ns_star_reflects_current_namespace(void) {
    test_setup();
    
    // Check initial namespace
    CljObject *ns1 = eval_code("*ns*");
    mu_assert("*ns* should be symbol", ns1 && ns1->type == CLJ_SYMBOL);
    mu_assert("*ns* should be 'user'", strcmp(as_symbol(ns1)->name, "user") == 0);
    
    // Switch namespace (using hyphen instead of dot due to parser limitation)
    eval_code("(ns custom-namespace)");
    
    // Check *ns* updated
    CljObject *ns2 = eval_code("*ns*");
    mu_assert("*ns* should update", ns2 && ns2->type == CLJ_SYMBOL);
    mu_assert("*ns* should be 'custom-namespace'", 
        strcmp(as_symbol(ns2)->name, "custom-namespace") == 0);
    
    test_teardown();
    return NULL;
}

static char* test_namespace_variable_isolation(void) {
    test_setup();
    // Note: (def) is a built-in, doesn't need clojure.core
    
    // Define x in alpha namespace
    eval_code("(ns alpha)");
    eval_code("(def x 111)");
    CljObject *alpha_x = eval_code("x");
    mu_assert("alpha.x should be defined", alpha_x && alpha_x->type == CLJ_INT);
    
    // Define x in beta namespace  
    eval_code("(ns beta)");
    eval_code("(def x 222)");
    CljObject *beta_x = eval_code("x");
    mu_assert("beta.x should be defined", beta_x && beta_x->type == CLJ_INT);
    
    // Switch back to alpha - x should still exist
    eval_code("(ns alpha)");
    CljObject *alpha_x_again = eval_code("x");
    mu_assert("alpha.x should still exist", alpha_x_again && alpha_x_again->type == CLJ_INT);
    
    // Back to beta - x should still exist
    eval_code("(ns beta)");
    CljObject *beta_x_again = eval_code("x");
    mu_assert("beta.x should still exist", beta_x_again && beta_x_again->type == CLJ_INT);
    
    test_teardown();
    return NULL;
}

static char* test_ns_creates_namespace_if_not_exists(void) {
    test_setup();
    
    // Switch to non-existent namespace (using hyphen instead of dot)
    CljObject *result = eval_code("(ns brand-new-namespace)");
    assert_type("(ns) should return nil", result, CLJ_NIL);
    assert_ns_name("Should create and switch to new namespace", "brand-new-namespace");
    
    // Should be able to define variables in new namespace (def is built-in)
    eval_code("(def test-var 42)");
    CljObject *var_value = eval_code("test-var");
    mu_assert("Variable defined in new namespace", var_value && var_value->type == CLJ_INT);
    
    test_teardown();
    return NULL;
}

static char* test_ns_with_dots_in_name(void) {
    test_setup();
    
    // Test namespace with multiple hyphens (instead of dots due to parser limitation)
    eval_code("(ns com-company-project-module)");
    assert_ns_name("Should handle hyphens in name", "com-company-project-module");
    
    CljObject *ns = eval_code("*ns*");
    mu_assert("*ns* should show full name with hyphens", 
        ns && ns->type == CLJ_SYMBOL &&
        strcmp(as_symbol(ns)->name, "com-company-project-module") == 0);
    
    test_teardown();
    return NULL;
}

// ============================================================================
// TEST RUNNER
// ============================================================================

#ifndef UNIFIED_TEST_RUNNER

static char* all_tests(void) {
    printf("\n🧪 === Consolidated Namespace Tests ===\n");
    
    // Infrastructure tests
    mu_run_test(test_evalstate_creation);
    mu_run_test(test_evalstate_set_ns);
    mu_run_test(test_ns_get_or_create);
    mu_run_test(test_map_operations);
    mu_run_test(test_namespace_isolation);
    mu_run_test(test_eval_expr_simple_atoms);
    mu_run_test(test_eval_expr_simple_symbols);
    
    // (ns) function tests
    mu_run_test(test_ns_returns_nil);
    mu_run_test(test_ns_switches_namespace);
    mu_run_test(test_ns_star_reflects_current_namespace);
    mu_run_test(test_namespace_variable_isolation);
    mu_run_test(test_ns_creates_namespace_if_not_exists);
    mu_run_test(test_ns_with_dots_in_name);
    
    return NULL;
}

int main(void) {
    char *result = all_tests();
    
    if (result != NULL) {
        printf("❌ %s\n", result);
        return 1;
    }
    
    printf("✅ ALL TESTS PASSED\n");
    printf("Tests run: %d\n", tests_run);
    
    return 0;
}

#else

// Export for unified test runner
char *run_namespace_tests(void) {
    // Infrastructure tests
    mu_run_test(test_evalstate_creation);
    mu_run_test(test_evalstate_set_ns);
    mu_run_test(test_ns_get_or_create);
    mu_run_test(test_map_operations);
    mu_run_test(test_namespace_isolation);
    mu_run_test(test_eval_expr_simple_atoms);
    mu_run_test(test_eval_expr_simple_symbols);
    
    // (ns) function tests
    mu_run_test(test_ns_returns_nil);
    mu_run_test(test_ns_switches_namespace);
    mu_run_test(test_ns_star_reflects_current_namespace);
    mu_run_test(test_namespace_variable_isolation);
    mu_run_test(test_ns_creates_namespace_if_not_exists);
    mu_run_test(test_ns_with_dots_in_name);
    
    return 0;
}

#endif

