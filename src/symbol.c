#include "symbol.h"
#include "object.h"
#include "runtime.h"
#include "value.h"
#include "exception.h"
#include "namespace.h"
#include "types.h"  // For SINGLETON_RC
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

// Global symbol for clojure.core namespace name (for fast comparison)
CljSymbol *SYM_CLOJURE_CORE = NULL;

// Additional symbols for hot path optimization
CljSymbol *SYM_NS_STAR = NULL;

// Static symbol structs for special forms (compile-time initialization)
// These symbols have rc = SINGLETON_RC and use string literals (no strdup needed)
static struct { CljSymbol sym; } sym_try_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "try" }
};
static struct { CljSymbol sym; } sym_catch_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "catch" }
};
static struct { CljSymbol sym; } sym_if_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "if" }
};
static struct { CljSymbol sym; } sym_cond_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "cond" }
};
static struct { CljSymbol sym; } sym_when_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "when" }
};
static struct { CljSymbol sym; } sym_while_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "while" }
};
static struct { CljSymbol sym; } sym_let_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "let" }
};
static struct { CljSymbol sym; } sym_fn_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "fn" }
};
static struct { CljSymbol sym; } sym_def_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "def" }
};
static struct { CljSymbol sym; } sym_defn_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "defn" }
};
static struct { CljSymbol sym; } sym_deref_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "deref" }
};
static struct { CljSymbol sym; } sym_nil_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "nil" }
};
static struct { CljSymbol sym; } sym_quote_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "quote" }
};
static struct { CljSymbol sym; } sym_quasiquote_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "quasiquote" }
};
static struct { CljSymbol sym; } sym_unquote_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "unquote" }
};
static struct { CljSymbol sym; } sym_splice_unquote_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "splice-unquote" }
};
static struct { CljSymbol sym; } sym_do_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "do" }
};
static struct { CljSymbol sym; } sym_loop_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "loop" }
};
static struct { CljSymbol sym; } sym_recur_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "recur" }
};
static struct { CljSymbol sym; } sym_throw_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "throw" }
};
static struct { CljSymbol sym; } sym_finally_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "finally" }
};
static struct { CljSymbol sym; } sym_var_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "var" }
};
static struct { CljSymbol sym; } sym_ns_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "ns" }
};
static struct { CljSymbol sym; } sym_time_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "time" }
};
static struct { CljSymbol sym; } sym_go_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "go" }
};

// Static symbol structs for built-in functions (compile-time initialization)
static struct { CljSymbol sym; } sym_plus_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "+" }
};
static struct { CljSymbol sym; } sym_minus_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "-" }
};
static struct { CljSymbol sym; } sym_multiply_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "*" }
};
static struct { CljSymbol sym; } sym_divide_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "/" }
};
static struct { CljSymbol sym; } sym_equals_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "=" }
};
static struct { CljSymbol sym; } sym_lt_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "<" }
};
static struct { CljSymbol sym; } sym_gt_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = ">" }
};
static struct { CljSymbol sym; } sym_le_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "<=" }
};
static struct { CljSymbol sym; } sym_ge_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = ">=" }
};
static struct { CljSymbol sym; } sym_println_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "println" }
};
static struct { CljSymbol sym; } sym_print_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "print" }
};
static struct { CljSymbol sym; } sym_str_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "str" }
};
static struct { CljSymbol sym; } sym_conj_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "conj" }
};
static struct { CljSymbol sym; } sym_nth_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "nth" }
};
static struct { CljSymbol sym; } sym_first_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "first" }
};
static struct { CljSymbol sym; } sym_rest_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "rest" }
};
static struct { CljSymbol sym; } sym_count_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "count" }
};

// Static symbol structs for additional symbols (compile-time initialization)
static struct { CljSymbol sym; } sym_cons_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "cons" }
};
static struct { CljSymbol sym; } sym_seq_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "seq" }
};
static struct { CljSymbol sym; } sym_next_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "next" }
};
static struct { CljSymbol sym; } sym_list_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "list" }
};
static struct { CljSymbol sym; } sym_and_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "and" }
};
static struct { CljSymbol sym; } sym_or_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "or" }
};
static struct { CljSymbol sym; } sym_for_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "for" }
};
static struct { CljSymbol sym; } sym_doseq_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "doseq" }
};
static struct { CljSymbol sym; } sym_dotimes_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "dotimes" }
};

// Static symbol structs for keywords (compile-time initialization)
static struct { CljSymbol sym; } sym_kw_line_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = ":line" }
};
static struct { CljSymbol sym; } sym_kw_file_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = ":file" }
};
static struct { CljSymbol sym; } sym_kw_doc_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = ":doc" }
};
static struct { CljSymbol sym; } sym_kw_error_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = ":error" }
};
static struct { CljSymbol sym; } sym_kw_stack_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = ":stack" }
};
static struct { CljSymbol sym; } sym_kw_ns_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = ":ns" }
};

// Additional symbols for optimization (used in hot path)
static struct { CljSymbol sym; } sym_ns_star_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns = NULL, .name = "*ns*" }
};

// Initialisierung der globalen Symbole
void init_special_symbols() {
    // Special forms - static structs with symbol table registration
    // Names are already set to string literals in static initialization (no strdup needed)
    SYM_TRY = &sym_try_data.sym;
    symbol_table_add(NULL, "try", SYM_TRY);
    
    SYM_CATCH = &sym_catch_data.sym;
    symbol_table_add(NULL, "catch", SYM_CATCH);
    
    SYM_IF = &sym_if_data.sym;
    symbol_table_add(NULL, "if", SYM_IF);
    
    SYM_COND = &sym_cond_data.sym;
    symbol_table_add(NULL, "cond", SYM_COND);
    
    SYM_WHEN = &sym_when_data.sym;
    symbol_table_add(NULL, "when", SYM_WHEN);
    
    SYM_WHILE = &sym_while_data.sym;
    symbol_table_add(NULL, "while", SYM_WHILE);
    
    SYM_LET = &sym_let_data.sym;
    symbol_table_add(NULL, "let", SYM_LET);
    
    SYM_FN = &sym_fn_data.sym;
    symbol_table_add(NULL, "fn", SYM_FN);
    
    SYM_QUOTE = &sym_quote_data.sym;
    symbol_table_add(NULL, "quote", SYM_QUOTE);
    
    SYM_QUASIQUOTE = &sym_quasiquote_data.sym;
    symbol_table_add(NULL, "quasiquote", SYM_QUASIQUOTE);
    
    SYM_UNQUOTE = &sym_unquote_data.sym;
    symbol_table_add(NULL, "unquote", SYM_UNQUOTE);
    
    SYM_SPLICE_UNQUOTE = &sym_splice_unquote_data.sym;
    symbol_table_add(NULL, "splice-unquote", SYM_SPLICE_UNQUOTE);
    
    SYM_DO = &sym_do_data.sym;
    symbol_table_add(NULL, "do", SYM_DO);
    
    SYM_LOOP = &sym_loop_data.sym;
    symbol_table_add(NULL, "loop", SYM_LOOP);
    
    SYM_RECUR = &sym_recur_data.sym;
    symbol_table_add(NULL, "recur", SYM_RECUR);
    
    SYM_THROW = &sym_throw_data.sym;
    symbol_table_add(NULL, "throw", SYM_THROW);
    
    SYM_FINALLY = &sym_finally_data.sym;
    symbol_table_add(NULL, "finally", SYM_FINALLY);
    
    SYM_DEFN = &sym_defn_data.sym;
    symbol_table_add(NULL, "defn", SYM_DEFN);
    
    SYM_DEREF = &sym_deref_data.sym;
    symbol_table_add(NULL, "deref", SYM_DEREF);
    
    SYM_NIL = &sym_nil_data.sym;
    symbol_table_add(NULL, "nil", SYM_NIL);
    
    SYM_VAR = &sym_var_data.sym;
    symbol_table_add(NULL, "var", SYM_VAR);
    
    // Built-in functions - static structs with symbol table registration
    SYM_DEF = &sym_def_data.sym;
    symbol_table_add(NULL, "def", SYM_DEF);
    
    SYM_NS = &sym_ns_data.sym;
    symbol_table_add(NULL, "ns", SYM_NS);
    
    SYM_TIME = &sym_time_data.sym;
    symbol_table_add(NULL, "time", SYM_TIME);
    
    SYM_GO = &sym_go_data.sym;
    symbol_table_add(NULL, "go", SYM_GO);
    
    SYM_PLUS = &sym_plus_data.sym;
    symbol_table_add(NULL, "+", SYM_PLUS);
    
    SYM_MINUS = &sym_minus_data.sym;
    symbol_table_add(NULL, "-", SYM_MINUS);
    
    SYM_MULTIPLY = &sym_multiply_data.sym;
    symbol_table_add(NULL, "*", SYM_MULTIPLY);
    
    SYM_DIVIDE = &sym_divide_data.sym;
    symbol_table_add(NULL, "/", SYM_DIVIDE);
    
    SYM_EQUALS = &sym_equals_data.sym;
    symbol_table_add(NULL, "=", SYM_EQUALS);
    
    SYM_LT = &sym_lt_data.sym;
    symbol_table_add(NULL, "<", SYM_LT);
    
    SYM_GT = &sym_gt_data.sym;
    symbol_table_add(NULL, ">", SYM_GT);
    
    SYM_LE = &sym_le_data.sym;
    symbol_table_add(NULL, "<=", SYM_LE);
    
    SYM_GE = &sym_ge_data.sym;
    symbol_table_add(NULL, ">=", SYM_GE);
    
    SYM_PRINTLN = &sym_println_data.sym;
    symbol_table_add(NULL, "println", SYM_PRINTLN);
    
    SYM_PRINT = &sym_print_data.sym;
    symbol_table_add(NULL, "print", SYM_PRINT);
    
    SYM_STR = &sym_str_data.sym;
    symbol_table_add(NULL, "str", SYM_STR);
    
    SYM_CONJ = &sym_conj_data.sym;
    symbol_table_add(NULL, "conj", SYM_CONJ);
    
    SYM_NTH = &sym_nth_data.sym;
    symbol_table_add(NULL, "nth", SYM_NTH);
    
    SYM_FIRST = &sym_first_data.sym;
    symbol_table_add(NULL, "first", SYM_FIRST);
    
    SYM_REST = &sym_rest_data.sym;
    symbol_table_add(NULL, "rest", SYM_REST);
    
    SYM_COUNT = &sym_count_data.sym;
    symbol_table_add(NULL, "count", SYM_COUNT);
    
    // Additional symbols - static structs with symbol table registration
    SYM_CONS = &sym_cons_data.sym;
    symbol_table_add(NULL, "cons", SYM_CONS);
    
    SYM_SEQ = &sym_seq_data.sym;
    symbol_table_add(NULL, "seq", SYM_SEQ);
    
    SYM_NEXT = &sym_next_data.sym;
    symbol_table_add(NULL, "next", SYM_NEXT);
    
    SYM_LIST = &sym_list_data.sym;
    symbol_table_add(NULL, "list", SYM_LIST);
    
    SYM_AND = &sym_and_data.sym;
    symbol_table_add(NULL, "and", SYM_AND);
    
    SYM_OR = &sym_or_data.sym;
    symbol_table_add(NULL, "or", SYM_OR);
    
    SYM_FOR = &sym_for_data.sym;
    symbol_table_add(NULL, "for", SYM_FOR);
    
    SYM_DOSEQ = &sym_doseq_data.sym;
    symbol_table_add(NULL, "doseq", SYM_DOSEQ);
    
    SYM_DOTIMES = &sym_dotimes_data.sym;
    symbol_table_add(NULL, "dotimes", SYM_DOTIMES);
    
    // Keywords - static structs with symbol table registration
    SYM_KW_LINE = &sym_kw_line_data.sym;
    symbol_table_add(NULL, ":line", SYM_KW_LINE);
    
    SYM_KW_FILE = &sym_kw_file_data.sym;
    symbol_table_add(NULL, ":file", SYM_KW_FILE);
    
    SYM_KW_DOC = &sym_kw_doc_data.sym;
    symbol_table_add(NULL, ":doc", SYM_KW_DOC);
    
    SYM_KW_ERROR = &sym_kw_error_data.sym;
    symbol_table_add(NULL, ":error", SYM_KW_ERROR);
    
    SYM_KW_STACK = &sym_kw_stack_data.sym;
    symbol_table_add(NULL, ":stack", SYM_KW_STACK);
    
    SYM_KW_NS = &sym_kw_ns_data.sym;
    symbol_table_add(NULL, ":ns", SYM_KW_NS);
    
    // Additional symbols for hot path optimization
    SYM_NS_STAR = &sym_ns_star_data.sym;
    symbol_table_add(NULL, "*ns*", SYM_NS_STAR);
    
    // Global symbol for clojure.core namespace name (for fast comparison)
    // Use intern_symbol_global to ensure same symbol is returned by intern_symbol
    SYM_CLOJURE_CORE = intern_symbol_global("clojure.core");
}

// Helper function to find symbol in vector by comparing name and namespace
static CljSymbol* vector_find_symbol(CljVector *vec, const char *ns, const char *name) {
    if (!vec || !name) return NULL;
    
    // Create temporary symbol structure for comparison (not heap-allocated)
    CljSymbol temp_sym = {
        .base = { .type = CLJ_SYMBOL, .rc = 0 },  // rc=0: not heap-allocated
        .name = name,
        .ns = ns ? intern_symbol_global(ns) : NULL
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
static CljSymbol* symbol_table_find(const char *ns, const char *name) {
    if (name && g_runtime.symbol_table) {
        return vector_find_symbol(g_runtime.symbol_table, ns, name);  // Happy path
    }
    return NULL;
}

// Add symbol to the table
void symbol_table_add(const char *ns, const char *name, CljSymbol *symbol) {
    if (!symbol || !name) return;
    
    if (!g_runtime.symbol_table) {
        g_runtime.symbol_table = make_vector(16, CLJ_VECTOR);
    }
    
    CljSymbol *existing = vector_find_symbol(g_runtime.symbol_table, ns, name);
    if (existing) {
        return;  // Already exists
    }
    
    CljVector *new_table = vector_conj(g_runtime.symbol_table, (ID)symbol);
    if (new_table) {
        if (new_table != g_runtime.symbol_table) {
            RELEASE(g_runtime.symbol_table);
            g_runtime.symbol_table = new_table;
        }
        RETAIN((ID)symbol);
    }
}

/**
 * @brief Create a symbol value
 * @param name Symbol name
 * @param ns Namespace (can be NULL)
 * @return CljSymbol symbol object
 */
CljSymbol* make_symbol(const char *name, const char *ns) {
    if (!name) {
        throw_exception_formatted("ArgumentError", __FILE__, __LINE__, 0,
                "make_symbol: name cannot be NULL");
        return NULL;
    }
    
    // Range check for name length (keep for safety)
    if (strlen(name) >= SYMBOL_NAME_MAX_LEN) {
        throw_exception_formatted("ArgumentError", __FILE__, __LINE__, 0,
                "Symbol name '%s' exceeds maximum length of %d characters", 
                name, SYMBOL_NAME_MAX_LEN - 1);
        return NULL;
    }
    
    // Use malloc directly instead of ALLOC macro
    CljSymbol *sym = (CljSymbol*)malloc(sizeof(CljSymbol));
    if (!sym) {
        throw_exception_formatted("OutOfMemoryError", __FILE__, __LINE__, 0,
                "Failed to allocate memory for symbol '%s'", name);
        return NULL;
    }
    
    sym->base.type = CLJ_SYMBOL;
    sym->base.rc = 1;  // Heap-allocated symbols start with rc=1
    
    // Store strdup'd name for heap-allocated symbols
    sym->name = strdup(name);
    if (!sym->name) {
        free(sym);
        throw_exception_formatted("OutOfMemoryError", __FILE__, __LINE__, 0,
                "Failed to duplicate string for symbol '%s'", name);
        return NULL;
    }
    
    // Enforce invariant: symbols always have a name
    CLJ_ASSERT(sym->name != NULL && "Symbol must have a name after creation");
    
    // Get interned symbol for namespace name (Clojure-compatible: Symbol->ns is a Symbol, not Namespace object)
    if (ns) {
        sym->ns = intern_symbol_global(ns);
        if (!sym->ns) {
            free(sym);
            throw_exception_formatted("OutOfMemoryError", __FILE__, __LINE__, 0,
                    "Failed to intern namespace name symbol '%s' for symbol '%s'", ns, name);
            return NULL;
        }
    } else {
        sym->ns = NULL;
    }
    
    return sym;
}

// Actual symbol interning
CljSymbol* intern_symbol(const char *ns, const char *name) {
    if (!name) return NULL;
    
    CljSymbol *existing = symbol_table_find(ns, name);
    if (existing) {
        return existing;
    }
    
    CljSymbol *symbol = make_symbol(name, ns);
    if (!symbol) return NULL;
    
    symbol_table_add(ns, name, symbol);
    
    return symbol;
}

// Global symbols (without namespace)
CljSymbol* intern_symbol_global(const char *name) {
    return intern_symbol(NULL, name);
}

// Helper: Get namespace object from symbol's namespace name (DRY principle)
// Returns NULL if namespace doesn't exist
struct CljNamespace* symbol_get_namespace(CljSymbol *sym) {
    if (!sym || !sym->ns || !sym->ns->name) return NULL;
    return ns_find(sym->ns->name);
}

// Helper: Get namespace name string from symbol (DRY principle)
// Returns NULL if symbol has no namespace
const char* symbol_get_namespace_name(CljSymbol *sym) {
    if (!sym || !sym->ns) return NULL;
    return sym->ns->name;
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

