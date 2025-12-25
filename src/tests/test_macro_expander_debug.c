/*
 * Isolated test for macro expansion debugging
 * 
 * This test runs WITHOUT clojure.core to isolate macro expansion issues.
 * Uses print_ast to debug AST structures during macro expansion.
 */

#include "tests_common.h"
#include "debug.h"
#include "parser.h"
#include "ast_canon.h"
#include "eval.h"
#include "macro.h"
#include "function.h"
#include "list.h"
#include "symbol.h"
#include "namespace.h"

// Forward declarations
extern void register_builtins(void);
extern int load_clojure_core(EvalState *st);

// Test macro expansion without clojure.core
TEST(test_macro_expander_debug_isolated) {
    // Initialize runtime without loading clojure.core
    runtime_reset(&g_runtime);
    runtime_init(&g_runtime);
    
    // Register only essential builtins (eval, read-string, etc.)
    // These are needed for basic functionality but don't require clojure.core
    if (!g_runtime.builtins_registered) {
        meta_registry_init();
        register_builtins();
        g_runtime.builtins_registered = true;
    }
    
    // Create eval state WITHOUT loading clojure.core
    EvalState *st = get_global_eval_state();
    evalstate_set_ns(st, "user");
    
    // Manually define minimal functions needed for macro testing
    // We need: defmacro, list, quote, eval
    
    // First, define 'list' function manually
    // (defn list [& args] args) - but we'll use a simpler approach
    // Actually, we can use native_list from builtins
    
    // Define a simple macro using eval_string
    // We need to manually build the macro definition since we don't have defmacro yet
    // For this test, we'll directly test the macro expansion mechanism
    
    // Parse a simple macro call form: (double 5)
    // where double is defined as: (defmacro double [x] (list '* x 2))
    
    // First, manually register a macro function
    CljSymbol *double_sym = intern_symbol_global("double");
    CljNamespace *user_ns = st->current_ns;
    
    // Create a macro function that expands (double x) to (* x 2)
    // We'll use eval_string to create the macro, but we need list and quote first
    
    // Actually, let's test the canonicalization directly with a manually created macro
    // Parse: (double 5)
    const char *macro_call = "(double 5)";
    ID parsed = parse(macro_call, st);
    TEST_ASSERT_NOT_NULL(parsed);
    
#ifdef DEBUG
    fprintf(stderr, "\n=== MACRO EXPANSION DEBUG TEST (isolated) ===\n");
    const char *parsed_ast = print_ast((CljObject*)parsed);
    fprintf(stderr, "[DEBUG] Parsed form: %s\n", parsed_ast);
    free((void*)parsed_ast);
#endif
    
    // Canonicalize - this should trigger macro expansion if macro is registered
    ID canonicalized = canonicalize_ast(parsed, st);
    TEST_ASSERT_NOT_NULL(canonicalized);
    
#ifdef DEBUG
    const char *canon_ast = print_ast((CljObject*)canonicalized);
    fprintf(stderr, "[DEBUG] Canonicalized form: %s\n", canon_ast);
    free((void*)canon_ast);
    
    // Check if first element is a symbol or immediate
    if (canonicalized && list_type_matches(TAG(canonicalized))) {
        CljList *list = as_list(canonicalized);
        if (list && list->first) {
            unsigned char first_tag = TAG(list->first);
            bool is_immediate = IS_IMMEDIATE(list->first);
            fprintf(stderr, "[DEBUG] First element tag: %d, is_immediate: %d\n", 
                    first_tag, is_immediate);
            
            if (is_immediate && first_tag != CLJ_SYMBOL) {
                fprintf(stderr, "[DEBUG] ERROR: First element is immediate value, not a symbol!\n");
                const char *first_ast = print_ast((CljObject*)list->first);
                fprintf(stderr, "[DEBUG] First element AST: %s\n", first_ast);
                free((void*)first_ast);
            }
        }
    }
    fprintf(stderr, "=== END DEBUG TEST ===\n\n");
#endif
    
    // Cleanup
    RELEASE(parsed);
    RELEASE(canonicalized);
    RELEASE(double_sym);
}

// Test with a manually created macro function
TEST(test_macro_expander_debug_with_macro) {
    // Initialize runtime
    runtime_reset(&g_runtime);
    runtime_init(&g_runtime);
    
    if (!g_runtime.builtins_registered) {
        meta_registry_init();
        register_builtins();
        g_runtime.builtins_registered = true;
    }
    
    EvalState *st = get_global_eval_state();
    evalstate_set_ns(st, "user");
    
    // Manually create a macro function
    // Macro: (defmacro double [x] (list '* x 2))
    // We'll create this using eval_string, but we need list and quote
    
    // For now, let's test the expansion mechanism by directly calling
    // the macro expansion code path
    
    // Parse a form that should trigger macro expansion
    const char *form = "(test-macro 42)";
    ID parsed = parse(form, st);
    TEST_ASSERT_NOT_NULL(parsed);
    
#ifdef DEBUG
    fprintf(stderr, "\n=== MACRO EXPANSION WITH MACRO FUNCTION ===\n");
    const char *parsed_ast = print_ast((CljObject*)parsed);
    fprintf(stderr, "[DEBUG] Original parsed form: %s\n", parsed_ast);
    free((void*)parsed_ast);
#endif
    
    // Try to canonicalize - this will check for macro
    ID canonicalized = canonicalize_ast(parsed, st);
    
#ifdef DEBUG
    if (canonicalized) {
        const char *canon_ast = print_ast((CljObject*)canonicalized);
        fprintf(stderr, "[DEBUG] After canonicalization: %s\n", canon_ast);
        free((void*)canon_ast);
    } else {
        fprintf(stderr, "[DEBUG] Canonicalization returned NULL\n");
    }
    fprintf(stderr, "=== END MACRO FUNCTION TEST ===\n\n");
#endif
    
    // Cleanup
    RELEASE(parsed);
    if (canonicalized) {
        RELEASE(canonicalized);
    }
}

