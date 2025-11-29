#include "symbol.h"
#include "object.h"
#include "runtime.h"
#include "value.h"
#include "exception.h"
#include "namespace.h"
#include "types.h"  // For SINGLETON_RC
#include "memory.h" // For ASSIGN
#include "memory.h"
#include "vector.h"  // For vector operations
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// Globale Symbol-Pointer Definitionen
CljSymbol *SYM_TRY = NULL;
CljSymbol *SYM_CATCH = NULL;
CljSymbol *SYM_IF = NULL;
CljSymbol *SYM_COND = NULL;
CljSymbol *SYM_WHEN = NULL;
CljSymbol *SYM_WHILE = NULL;
CljSymbol *SYM_LET = NULL;
CljSymbol *SYM_FN = NULL;
CljSymbol *SYM_DEF = NULL;
CljSymbol *SYM_DEFN = NULL;
CljSymbol *SYM_QUOTE = NULL;
CljSymbol *SYM_QUASIQUOTE = NULL;
CljSymbol *SYM_UNQUOTE = NULL;
CljSymbol *SYM_SPLICE_UNQUOTE = NULL;
CljSymbol *SYM_DO = NULL;
CljSymbol *SYM_LOOP = NULL;
CljSymbol *SYM_RECUR = NULL;
CljSymbol *SYM_THROW = NULL;
CljSymbol *SYM_FINALLY = NULL;
CljSymbol *SYM_VAR = NULL;
CljSymbol *SYM_NS = NULL;
CljSymbol *SYM_TIME = NULL;
CljSymbol *SYM_GO = NULL;
CljSymbol *SYM_DEREF = NULL;
CljSymbol *SYM_NIL = NULL;

// Builtin-Funktionen
CljSymbol *SYM_PLUS = NULL;
CljSymbol *SYM_MINUS = NULL;
CljSymbol *SYM_MULTIPLY = NULL;
CljSymbol *SYM_DIVIDE = NULL;
CljSymbol *SYM_EQUALS = NULL;
CljSymbol *SYM_LT = NULL;
CljSymbol *SYM_GT = NULL;
CljSymbol *SYM_LE = NULL;
CljSymbol *SYM_GE = NULL;
CljSymbol *SYM_PRINTLN = NULL;
CljSymbol *SYM_PRINT = NULL;
CljSymbol *SYM_STR = NULL;
CljSymbol *SYM_CONJ = NULL;
CljSymbol *SYM_NTH = NULL;
CljSymbol *SYM_TRIM = NULL;
CljSymbol *SYM_UPPER_CASE = NULL;
CljSymbol *SYM_LOWER_CASE = NULL;
CljSymbol *SYM_LAST_INDEX_OF = NULL;
CljSymbol *SYM_STRING_REVERSE = NULL;
CljSymbol *SYM_FIRST = NULL;
CljSymbol *SYM_REST = NULL;
CljSymbol *SYM_COUNT = NULL;

// Additional symbols for optimization
CljSymbol *SYM_CONS = NULL;
CljSymbol *SYM_SEQ = NULL;
CljSymbol *SYM_NEXT = NULL;
CljSymbol *SYM_LIST = NULL;
CljSymbol *SYM_AND = NULL;
CljSymbol *SYM_OR = NULL;
CljSymbol *SYM_FOR = NULL;
CljSymbol *SYM_DOSEQ = NULL;
CljSymbol *SYM_DOTIMES = NULL;

// Keywords
CljSymbol *SYM_KW_LINE = NULL;
CljSymbol *SYM_KW_FILE = NULL;
CljSymbol *SYM_KW_DOC = NULL;
CljSymbol *SYM_KW_ERROR = NULL;
CljSymbol *SYM_KW_STACK = NULL;
CljSymbol *SYM_KW_NS = NULL;
CljSymbol *SYM_KW_NAME = NULL;
CljSymbol *SYM_KW_NATIVE = NULL;

// Global symbols for namespace names (for fast comparison)
CljSymbol *SYM_CLOJURE_CORE = NULL;
CljSymbol *SYM_CLOJURE_STRING = NULL;
CljSymbol *SYM_CLOJURE_LANG = NULL;

// Additional symbols for hot path optimization
CljSymbol *SYM_NS_STAR = NULL;

// Macro to reduce boilerplate for static symbol declarations (DRY principle)
#define DEFINE_STATIC_SYMBOL(var_name, symbol_name) \
    static struct { CljSymbol sym; } var_name = { \
        .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .cname = symbol_name } \
    }

// Static symbol structs for special forms (compile-time initialization)
// These symbols have rc = SINGLETON_RC and use string literals (no strdup needed)
DEFINE_STATIC_SYMBOL(sym_try_data, "try");
DEFINE_STATIC_SYMBOL(sym_catch_data, "catch");
DEFINE_STATIC_SYMBOL(sym_if_data, "if");
DEFINE_STATIC_SYMBOL(sym_cond_data, "cond");
DEFINE_STATIC_SYMBOL(sym_when_data, "when");
DEFINE_STATIC_SYMBOL(sym_while_data, "while");
DEFINE_STATIC_SYMBOL(sym_let_data, "let");
DEFINE_STATIC_SYMBOL(sym_fn_data, "fn");
DEFINE_STATIC_SYMBOL(sym_def_data, "def");
DEFINE_STATIC_SYMBOL(sym_defn_data, "defn");
DEFINE_STATIC_SYMBOL(sym_deref_data, "deref");
DEFINE_STATIC_SYMBOL(sym_nil_data, "nil");
DEFINE_STATIC_SYMBOL(sym_quote_data, "quote");
DEFINE_STATIC_SYMBOL(sym_quasiquote_data, "quasiquote");
DEFINE_STATIC_SYMBOL(sym_unquote_data, "unquote");
DEFINE_STATIC_SYMBOL(sym_splice_unquote_data, "splice-unquote");
DEFINE_STATIC_SYMBOL(sym_do_data, "do");
DEFINE_STATIC_SYMBOL(sym_loop_data, "loop");
DEFINE_STATIC_SYMBOL(sym_recur_data, "recur");
DEFINE_STATIC_SYMBOL(sym_throw_data, "throw");
DEFINE_STATIC_SYMBOL(sym_finally_data, "finally");
DEFINE_STATIC_SYMBOL(sym_var_data, "var");
DEFINE_STATIC_SYMBOL(sym_ns_data, "ns");
DEFINE_STATIC_SYMBOL(sym_time_data, "time");
DEFINE_STATIC_SYMBOL(sym_go_data, "go");

// Static symbol structs for built-in functions (compile-time initialization)
DEFINE_STATIC_SYMBOL(sym_plus_data, "+");
DEFINE_STATIC_SYMBOL(sym_minus_data, "-");
DEFINE_STATIC_SYMBOL(sym_multiply_data, "*");
DEFINE_STATIC_SYMBOL(sym_divide_data, "/");
DEFINE_STATIC_SYMBOL(sym_equals_data, "=");
DEFINE_STATIC_SYMBOL(sym_lt_data, "<");
DEFINE_STATIC_SYMBOL(sym_gt_data, ">");
DEFINE_STATIC_SYMBOL(sym_le_data, "<=");
DEFINE_STATIC_SYMBOL(sym_ge_data, ">=");
DEFINE_STATIC_SYMBOL(sym_println_data, "println");
DEFINE_STATIC_SYMBOL(sym_print_data, "print");
DEFINE_STATIC_SYMBOL(sym_str_data, "str");
DEFINE_STATIC_SYMBOL(sym_conj_data, "conj");
DEFINE_STATIC_SYMBOL(sym_nth_data, "nth");
DEFINE_STATIC_SYMBOL(sym_first_data, "first");
DEFINE_STATIC_SYMBOL(sym_rest_data, "rest");
DEFINE_STATIC_SYMBOL(sym_count_data, "count");

// Static symbol structs for additional symbols (compile-time initialization)
DEFINE_STATIC_SYMBOL(sym_cons_data, "cons");
DEFINE_STATIC_SYMBOL(sym_seq_data, "seq");
DEFINE_STATIC_SYMBOL(sym_next_data, "next");
DEFINE_STATIC_SYMBOL(sym_list_data, "list");
DEFINE_STATIC_SYMBOL(sym_and_data, "and");
DEFINE_STATIC_SYMBOL(sym_or_data, "or");
DEFINE_STATIC_SYMBOL(sym_for_data, "for");
DEFINE_STATIC_SYMBOL(sym_doseq_data, "doseq");
DEFINE_STATIC_SYMBOL(sym_dotimes_data, "dotimes");

// Non-static symbol structs for clojure.string native functions (compile-time initialization)
// These are non-static so they can be used in builtins.c's native function table
StaticSymbolData sym_trim_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .cname = "trim" }
};
StaticSymbolData sym_upper_case_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .cname = "upper-case" }
};
StaticSymbolData sym_lower_case_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .cname = "lower-case" }
};
StaticSymbolData sym_last_index_of_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .cname = "last-index-of" }
};
StaticSymbolData sym_string_reverse_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .cname = "reverse" }
};

// Static symbol structs for keywords (compile-time initialization)
DEFINE_STATIC_SYMBOL(sym_kw_line_data, ":line");
DEFINE_STATIC_SYMBOL(sym_kw_file_data, ":file");
DEFINE_STATIC_SYMBOL(sym_kw_doc_data, ":doc");
DEFINE_STATIC_SYMBOL(sym_kw_error_data, ":error");
DEFINE_STATIC_SYMBOL(sym_kw_stack_data, ":stack");
DEFINE_STATIC_SYMBOL(sym_kw_ns_data, ":ns");
DEFINE_STATIC_SYMBOL(sym_kw_name_data, ":name");
DEFINE_STATIC_SYMBOL(sym_kw_native_data, ":native");

// Additional symbols for optimization (used in hot path)
DEFINE_STATIC_SYMBOL(sym_ns_star_data, "*ns*");

// Undef macro to avoid namespace pollution
#undef DEFINE_STATIC_SYMBOL

// Macro to reduce initialization boilerplate (DRY principle)
#define INIT_SYMBOL(sym_var, data_var) \
    do { \
        sym_var = &data_var.sym; \
        symbol_table_add(sym_var); \
    } while(0)

// Macro for symbols with namespace (sets ns_name at runtime)
#define INIT_SYMBOL_NS(sym_var, data_var, ns_sym) \
    do { \
        sym_var = &data_var.sym; \
        sym_var->ns_name = ns_sym; \
        symbol_table_add(sym_var); \
    } while(0)

// Initialisierung der globalen Symbole
void init_special_symbols() {
    // Special forms - static structs with symbol table registration
    // Names are already set to string literals in static initialization (no strdup needed)
    INIT_SYMBOL(SYM_TRY, sym_try_data);
    INIT_SYMBOL(SYM_CATCH, sym_catch_data);
    INIT_SYMBOL(SYM_IF, sym_if_data);
    INIT_SYMBOL(SYM_COND, sym_cond_data);
    INIT_SYMBOL(SYM_WHEN, sym_when_data);
    INIT_SYMBOL(SYM_WHILE, sym_while_data);
    INIT_SYMBOL(SYM_LET, sym_let_data);
    INIT_SYMBOL(SYM_FN, sym_fn_data);
    INIT_SYMBOL(SYM_QUOTE, sym_quote_data);
    INIT_SYMBOL(SYM_QUASIQUOTE, sym_quasiquote_data);
    INIT_SYMBOL(SYM_UNQUOTE, sym_unquote_data);
    INIT_SYMBOL(SYM_SPLICE_UNQUOTE, sym_splice_unquote_data);
    INIT_SYMBOL(SYM_DO, sym_do_data);
    INIT_SYMBOL(SYM_LOOP, sym_loop_data);
    INIT_SYMBOL(SYM_RECUR, sym_recur_data);
    INIT_SYMBOL(SYM_THROW, sym_throw_data);

    INIT_SYMBOL(SYM_FINALLY, sym_finally_data);

    INIT_SYMBOL(SYM_DEFN, sym_defn_data);

    INIT_SYMBOL(SYM_DEREF, sym_deref_data);

    INIT_SYMBOL(SYM_NIL, sym_nil_data);

    INIT_SYMBOL(SYM_VAR, sym_var_data);

    // Built-in functions - static structs with symbol table registration
    INIT_SYMBOL(SYM_DEF, sym_def_data);

    INIT_SYMBOL(SYM_NS, sym_ns_data);

    INIT_SYMBOL(SYM_TIME, sym_time_data);

    INIT_SYMBOL(SYM_GO, sym_go_data);

    INIT_SYMBOL(SYM_PLUS, sym_plus_data);

    INIT_SYMBOL(SYM_MINUS, sym_minus_data);

    INIT_SYMBOL(SYM_MULTIPLY, sym_multiply_data);

    INIT_SYMBOL(SYM_DIVIDE, sym_divide_data);

    INIT_SYMBOL(SYM_EQUALS, sym_equals_data);

    INIT_SYMBOL(SYM_LT, sym_lt_data);

    INIT_SYMBOL(SYM_GT, sym_gt_data);

    INIT_SYMBOL(SYM_LE, sym_le_data);

    INIT_SYMBOL(SYM_GE, sym_ge_data);

    INIT_SYMBOL(SYM_PRINTLN, sym_println_data);

    INIT_SYMBOL(SYM_PRINT, sym_print_data);

    INIT_SYMBOL(SYM_STR, sym_str_data);

    INIT_SYMBOL(SYM_CONJ, sym_conj_data);

    INIT_SYMBOL(SYM_NTH, sym_nth_data);

    INIT_SYMBOL(SYM_FIRST, sym_first_data);

    INIT_SYMBOL(SYM_REST, sym_rest_data);

    INIT_SYMBOL(SYM_COUNT, sym_count_data);

    // Initialize clojure.string namespace symbol first (needed for clojure.string functions)
    SYM_CLOJURE_STRING = intern_symbol_global("clojure.string");

    // clojure.string native function symbols - with namespace
    INIT_SYMBOL_NS(SYM_TRIM, sym_trim_data, SYM_CLOJURE_STRING);

    INIT_SYMBOL_NS(SYM_UPPER_CASE, sym_upper_case_data, SYM_CLOJURE_STRING);

    INIT_SYMBOL_NS(SYM_LOWER_CASE, sym_lower_case_data, SYM_CLOJURE_STRING);

    INIT_SYMBOL_NS(SYM_LAST_INDEX_OF, sym_last_index_of_data, SYM_CLOJURE_STRING);

    INIT_SYMBOL_NS(SYM_STRING_REVERSE, sym_string_reverse_data, SYM_CLOJURE_STRING);

    // Additional symbols - static structs with symbol table registration
    INIT_SYMBOL(SYM_CONS, sym_cons_data);

    INIT_SYMBOL(SYM_SEQ, sym_seq_data);

    INIT_SYMBOL(SYM_NEXT, sym_next_data);

    INIT_SYMBOL(SYM_LIST, sym_list_data);

    INIT_SYMBOL(SYM_AND, sym_and_data);

    INIT_SYMBOL(SYM_OR, sym_or_data);

    INIT_SYMBOL(SYM_FOR, sym_for_data);

    INIT_SYMBOL(SYM_DOSEQ, sym_doseq_data);

    INIT_SYMBOL(SYM_DOTIMES, sym_dotimes_data);

    // Keywords - static structs with symbol table registration
    INIT_SYMBOL(SYM_KW_LINE, sym_kw_line_data);

    INIT_SYMBOL(SYM_KW_FILE, sym_kw_file_data);

    INIT_SYMBOL(SYM_KW_DOC, sym_kw_doc_data);

    INIT_SYMBOL(SYM_KW_ERROR, sym_kw_error_data);

    INIT_SYMBOL(SYM_KW_STACK, sym_kw_stack_data);

    INIT_SYMBOL(SYM_KW_NS, sym_kw_ns_data);

    INIT_SYMBOL(SYM_KW_NAME, sym_kw_name_data);

    INIT_SYMBOL(SYM_KW_NATIVE, sym_kw_native_data);

    // Additional symbols for hot path optimization
    INIT_SYMBOL(SYM_NS_STAR, sym_ns_star_data);

    // Global symbols for namespace names (for fast comparison)
    // Use intern_symbol_global to ensure same symbol is returned by intern_symbol
    SYM_CLOJURE_CORE = intern_symbol_global("clojure.core");
    SYM_CLOJURE_LANG = intern_symbol_global("clojure.lang");
    
    // Clean up macros to avoid namespace pollution
    #undef INIT_SYMBOL
    #undef INIT_SYMBOL_NS
}

// Helper function to find symbol in vector by comparing name and namespace
static CljSymbol* find_symbol(CljVector *vec, CljSymbol *ns_name, const char *cname) {
    if (!vec || !cname) return NULL;

    // Create temporary symbol structure for comparison (not heap-allocated)
    CljSymbol temp_sym = {
        .base = { .type = CLJ_SYMBOL, .rc = 0 },  // rc=0: not heap-allocated
        .cname = cname,
        .ns_name = ns_name  // Use ns_name directly (already a CljSymbol*)
    };

    int index = vector_index_of(vec, (ID)&temp_sym);
    if (index != INDEX_NOT_FOUND) {
        ID elem = vector_nth(vec, (unsigned int)index);
        CLJ_ASSERT(elem && TAG(elem) == CLJ_SYMBOL && "vector_index_of should only return indices of symbols when searching for a symbol");
        return as_symbol(elem);
    }

    return NULL;
}

// Find symbol in the table
static CljSymbol* symbol_table_find(CljSymbol *ns_name, const char *cname) {
    if (cname && g_runtime.symbol_table) {
        return find_symbol(g_runtime.symbol_table, ns_name, cname);  // Happy path
    }
    return NULL;
}

// Add symbol to the table
void symbol_table_add(CljSymbol *symbol) {
    if (!symbol || !symbol->cname) return;

    // Extract namespace and name from symbol
    CljSymbol *ns_name = symbol->ns_name;  // Already a CljSymbol*
    const char *cname = symbol->cname;

    if (!g_runtime.symbol_table) {
        g_runtime.symbol_table = make_vector(16, CLJ_VECTOR);
    }

    CljSymbol *existing = find_symbol(g_runtime.symbol_table, ns_name, cname);
    if (existing) {
        return;  // Already exists
    }

    ASSIGN(g_runtime.symbol_table, vector_conj(g_runtime.symbol_table, (ID)symbol));
}

/**
 * @brief Create a symbol value
 * @param cname Symbol name
 * @param ns_name Namespace name symbol (can be NULL)
 * @return CljSymbol symbol object
 */
CljSymbol* make_symbol(const char *cname, CljSymbol *ns_name) {
    if (!cname) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                "make_symbol: name cannot be NULL");
        return NULL;
    }

    // Range check for name length (keep for safety)
    if (strlen(cname) >= SYMBOL_NAME_MAX_LEN) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                "Symbol name '%s' exceeds maximum length of %d characters",
                cname, SYMBOL_NAME_MAX_LEN - 1);
        return NULL;
    }

    // Use malloc directly instead of ALLOC macro
    CljSymbol *sym = (CljSymbol*)malloc(sizeof(CljSymbol));
    if (!sym) {
        throw_exception_formatted(EXCEPTION_OUT_OF_MEMORY, __FILE__, __LINE__, 0,
                "Failed to allocate memory for symbol '%s'", cname);
        return NULL;
    }

    sym->base.type = CLJ_SYMBOL;
    sym->base.rc = 1;  // Heap-allocated symbols start with rc=1

    // Store strdup'd name for heap-allocated symbols
    sym->cname = strdup(cname);
    if (!sym->cname) {
        free(sym);
        throw_exception_formatted(EXCEPTION_OUT_OF_MEMORY, __FILE__, __LINE__, 0,
                "Failed to duplicate string for symbol '%s'", cname);
        return NULL;
    }

    // Enforce invariant: symbols always have a name
    CLJ_ASSERT(sym->cname != NULL && "Symbol must have a name after creation");

    // Use ns_name directly (already a CljSymbol*)
    sym->ns_name = ns_name;

    return sym;
}

// Actual symbol interning
CljSymbol* intern_symbol(CljSymbol *ns_name, const char *cname) {
    if (!cname) return NULL;

    CljSymbol *existing = symbol_table_find(ns_name, cname);
    if (existing) {
        return existing;
    }

    CljSymbol *symbol = make_symbol(cname, ns_name);
    if (!symbol) return NULL;

    symbol_table_add(symbol);

    return symbol;
}

// Global symbols (without namespace)
CljSymbol* intern_symbol_global(const char *cname) {
    return intern_symbol(NULL, cname);
}

// Helper: Get namespace object from symbol's namespace name (DRY principle)
// Returns clojure.core namespace if symbol has no explicit namespace (ns_name == NULL)
// Returns NULL if namespace doesn't exist
struct CljNamespace* symbol_get_namespace(CljSymbol *sym) {
    if (!sym) return NULL;
    
    // NULL namespace means clojure.core (default for core symbols)
    if (!sym->ns_name) {
        return ns_find("clojure.core");
    }
    
    // Safety check: ensure ns_name is a valid symbol before accessing cname
    if (TAG(sym->ns_name) != CLJ_SYMBOL) return NULL;
    CljSymbol *ns_sym = as_symbol(sym->ns_name);
    if (!ns_sym || !ns_sym->cname) return NULL;
    // Use fast symbol-based lookup (avoids redundant intern_symbol call)
    return ns_find_by_symbol(sym->ns_name);
}

// Helper: Get namespace name string from symbol (DRY principle)
// Returns "clojure.core" if symbol has no explicit namespace (ns_name == NULL)
const char* symbol_get_namespace_name(CljSymbol *sym) {
    if (!sym) return NULL;
    
    // NULL namespace means clojure.core (default for core symbols)
    if (!sym->ns_name) {
        return "clojure.core";
    }
    
    // Safety check: ensure ns_name is a valid symbol before accessing cname
    if (TAG(sym->ns_name) != CLJ_SYMBOL) return NULL;
    CljSymbol *ns_sym = as_symbol(sym->ns_name);
    if (!ns_sym || !ns_sym->cname) return NULL;
    return ns_sym->cname;
}

// Clean up symbol table (ONLY for test cleanup, not regular symbols)
// This function will be eliminated by dead-code-elimination in production builds
// since it's only called from test files
void symbol_table_cleanup() {
    if (g_runtime.symbol_table) {
        RELEASE(g_runtime.symbol_table);
        g_runtime.symbol_table = NULL;
    }
}

