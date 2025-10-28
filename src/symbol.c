#include "symbol.h"
#include "object.h"
#include <stdbool.h>

// Globale Symbol-Pointer Definitionen
CljObject *SYM_TRY = NULL;
CljObject *SYM_CATCH = NULL;
CljObject *SYM_IF = NULL;
CljObject *SYM_LET = NULL;
CljObject *SYM_FN = NULL;
CljObject *SYM_DEF = NULL;
CljObject *SYM_DEFN = NULL;
CljObject *SYM_QUOTE = NULL;
CljObject *SYM_QUASIQUOTE = NULL;
CljObject *SYM_UNQUOTE = NULL;
CljObject *SYM_SPLICE_UNQUOTE = NULL;
CljObject *SYM_DO = NULL;
CljObject *SYM_LOOP = NULL;
CljObject *SYM_RECUR = NULL;
CljObject *SYM_THROW = NULL;
CljObject *SYM_FINALLY = NULL;
CljObject *SYM_VAR = NULL;
CljObject *SYM_NS = NULL;

// Builtin-Funktionen
CljObject *SYM_PLUS = NULL;
CljObject *SYM_MINUS = NULL;
CljObject *SYM_MULTIPLY = NULL;
CljObject *SYM_DIVIDE = NULL;
CljObject *SYM_EQUALS = NULL;
CljObject *SYM_EQUAL = NULL;
CljObject *SYM_LT = NULL;
CljObject *SYM_GT = NULL;
CljObject *SYM_LE = NULL;
CljObject *SYM_GE = NULL;
CljObject *SYM_PRINTLN = NULL;
CljObject *SYM_PRINT = NULL;
CljObject *SYM_STR = NULL;
CljObject *SYM_CONJ = NULL;
CljObject *SYM_NTH = NULL;
CljObject *SYM_FIRST = NULL;
CljObject *SYM_REST = NULL;
CljObject *SYM_COUNT = NULL;

// Additional symbols for optimization
CljObject *SYM_CONS = NULL;
CljObject *SYM_SEQ = NULL;
CljObject *SYM_NEXT = NULL;
CljObject *SYM_LIST = NULL;
CljObject *SYM_AND = NULL;
CljObject *SYM_OR = NULL;
CljObject *SYM_FOR = NULL;
CljObject *SYM_DOSEQ = NULL;
CljObject *SYM_DOTIMES = NULL;

// Keywords
CljObject *SYM_KW_LINE = NULL;
CljObject *SYM_KW_FILE = NULL;
CljObject *SYM_KW_DOC = NULL;
CljObject *SYM_KW_ERROR = NULL;
CljObject *SYM_KW_STACK = NULL;

// Static symbol structs for special forms (compile-time initialization)
// These symbols have rc = 1 and use string literals (no strdup needed)
static struct { CljSymbol sym; } sym_try_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "try" }
};
static struct { CljSymbol sym; } sym_catch_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "catch" }
};
static struct { CljSymbol sym; } sym_if_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "if" }
};
static struct { CljSymbol sym; } sym_let_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "let" }
};
static struct { CljSymbol sym; } sym_fn_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "fn" }
};
static struct { CljSymbol sym; } sym_def_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "def" }
};
static struct { CljSymbol sym; } sym_defn_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "defn" }
};
static struct { CljSymbol sym; } sym_quote_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "quote" }
};
static struct { CljSymbol sym; } sym_quasiquote_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "quasiquote" }
};
static struct { CljSymbol sym; } sym_unquote_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "unquote" }
};
static struct { CljSymbol sym; } sym_splice_unquote_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "splice-unquote" }
};
static struct { CljSymbol sym; } sym_do_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "do" }
};
static struct { CljSymbol sym; } sym_loop_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "loop" }
};
static struct { CljSymbol sym; } sym_recur_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "recur" }
};
static struct { CljSymbol sym; } sym_throw_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "throw" }
};
static struct { CljSymbol sym; } sym_finally_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "finally" }
};
static struct { CljSymbol sym; } sym_var_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "var" }
};
static struct { CljSymbol sym; } sym_ns_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "ns" }
};

// Static symbol structs for built-in functions (compile-time initialization)
static struct { CljSymbol sym; } sym_plus_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "+" }
};
static struct { CljSymbol sym; } sym_minus_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "-" }
};
static struct { CljSymbol sym; } sym_multiply_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "*" }
};
static struct { CljSymbol sym; } sym_divide_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "/" }
};
static struct { CljSymbol sym; } sym_equals_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "=" }
};
static struct { CljSymbol sym; } sym_equal_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "equal" }
};
static struct { CljSymbol sym; } sym_lt_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "<" }
};
static struct { CljSymbol sym; } sym_gt_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = ">" }
};
static struct { CljSymbol sym; } sym_le_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "<=" }
};
static struct { CljSymbol sym; } sym_ge_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = ">=" }
};
static struct { CljSymbol sym; } sym_println_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "println" }
};
static struct { CljSymbol sym; } sym_print_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "print" }
};
static struct { CljSymbol sym; } sym_str_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "str" }
};
static struct { CljSymbol sym; } sym_conj_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "conj" }
};
static struct { CljSymbol sym; } sym_nth_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "nth" }
};
static struct { CljSymbol sym; } sym_first_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "first" }
};
static struct { CljSymbol sym; } sym_rest_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "rest" }
};
static struct { CljSymbol sym; } sym_count_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "count" }
};

// Static symbol structs for additional symbols (compile-time initialization)
static struct { CljSymbol sym; } sym_cons_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "cons" }
};
static struct { CljSymbol sym; } sym_seq_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "seq" }
};
static struct { CljSymbol sym; } sym_next_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "next" }
};
static struct { CljSymbol sym; } sym_list_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "list" }
};
static struct { CljSymbol sym; } sym_and_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "and" }
};
static struct { CljSymbol sym; } sym_or_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "or" }
};
static struct { CljSymbol sym; } sym_for_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = "for" }
};
static struct { CljSymbol sym; } sym_doseq_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = NULL }
};
static struct { CljSymbol sym; } sym_dotimes_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = NULL }
};

// Static symbol structs for keywords (compile-time initialization)
static struct { CljSymbol sym; } sym_kw_line_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = ":line" }
};
static struct { CljSymbol sym; } sym_kw_file_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = ":file" }
};
static struct { CljSymbol sym; } sym_kw_doc_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = ":doc" }
};
static struct { CljSymbol sym; } sym_kw_error_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = ":error" }
};
static struct { CljSymbol sym; } sym_kw_stack_data = {
    .sym = { .base = { .type = CLJ_SYMBOL, .rc = 1 }, .ns = NULL, .name = ":stack" }
};

// Initialisierung der globalen Symbole
void init_special_symbols() {
    // Special forms - static structs with symbol table registration
    // Initialize names with strdup to avoid string literal issues
    sym_try_data.sym.name = strdup("try");
    SYM_TRY = (CljObject*)&sym_try_data;
    symbol_table_add(NULL, "try", SYM_TRY);
    
    SYM_CATCH = (CljObject*)&sym_catch_data;
    symbol_table_add(NULL, "catch", SYM_CATCH);
    
    SYM_IF = (CljObject*)&sym_if_data;
    symbol_table_add(NULL, "if", SYM_IF);
    
    SYM_LET = (CljObject*)&sym_let_data;
    symbol_table_add(NULL, "let", SYM_LET);
    
    SYM_FN = (CljObject*)&sym_fn_data;
    symbol_table_add(NULL, "fn", SYM_FN);
    
    SYM_QUOTE = (CljObject*)&sym_quote_data;
    symbol_table_add(NULL, "quote", SYM_QUOTE);
    
    SYM_QUASIQUOTE = (CljObject*)&sym_quasiquote_data;
    symbol_table_add(NULL, "quasiquote", SYM_QUASIQUOTE);
    
    SYM_UNQUOTE = (CljObject*)&sym_unquote_data;
    symbol_table_add(NULL, "unquote", SYM_UNQUOTE);
    
    SYM_SPLICE_UNQUOTE = (CljObject*)&sym_splice_unquote_data;
    symbol_table_add(NULL, "splice-unquote", SYM_SPLICE_UNQUOTE);
    
    SYM_DO = (CljObject*)&sym_do_data;
    symbol_table_add(NULL, "do", SYM_DO);
    
    SYM_LOOP = (CljObject*)&sym_loop_data;
    symbol_table_add(NULL, "loop", SYM_LOOP);
    
    SYM_RECUR = (CljObject*)&sym_recur_data;
    symbol_table_add(NULL, "recur", SYM_RECUR);
    
    SYM_THROW = (CljObject*)&sym_throw_data;
    symbol_table_add(NULL, "throw", SYM_THROW);
    
    SYM_FINALLY = (CljObject*)&sym_finally_data;
    symbol_table_add(NULL, "finally", SYM_FINALLY);
    
    SYM_DEFN = (CljObject*)&sym_defn_data;
    symbol_table_add(NULL, "defn", SYM_DEFN);
    
    SYM_VAR = (CljObject*)&sym_var_data;
    symbol_table_add(NULL, "var", SYM_VAR);
    
    // Built-in functions - static structs with symbol table registration
    SYM_DEF = (CljObject*)&sym_def_data;
    symbol_table_add(NULL, "def", SYM_DEF);
    
    SYM_NS = (CljObject*)&sym_ns_data;
    symbol_table_add(NULL, "ns", SYM_NS);
    
    SYM_PLUS = (CljObject*)&sym_plus_data;
    symbol_table_add(NULL, "+", SYM_PLUS);
    
    SYM_MINUS = (CljObject*)&sym_minus_data;
    symbol_table_add(NULL, "-", SYM_MINUS);
    
    SYM_MULTIPLY = (CljObject*)&sym_multiply_data;
    symbol_table_add(NULL, "*", SYM_MULTIPLY);
    
    SYM_DIVIDE = (CljObject*)&sym_divide_data;
    symbol_table_add(NULL, "/", SYM_DIVIDE);
    
    SYM_EQUALS = (CljObject*)&sym_equals_data;
    symbol_table_add(NULL, "=", SYM_EQUALS);
    
    SYM_EQUAL = (CljObject*)&sym_equal_data;
    symbol_table_add(NULL, "equal", SYM_EQUAL);
    
    SYM_LT = (CljObject*)&sym_lt_data;
    symbol_table_add(NULL, "<", SYM_LT);
    
    SYM_GT = (CljObject*)&sym_gt_data;
    symbol_table_add(NULL, ">", SYM_GT);
    
    SYM_LE = (CljObject*)&sym_le_data;
    symbol_table_add(NULL, "<=", SYM_LE);
    
    SYM_GE = (CljObject*)&sym_ge_data;
    symbol_table_add(NULL, ">=", SYM_GE);
    
    SYM_PRINTLN = (CljObject*)&sym_println_data;
    symbol_table_add(NULL, "println", SYM_PRINTLN);
    
    SYM_PRINT = (CljObject*)&sym_print_data;
    symbol_table_add(NULL, "print", SYM_PRINT);
    
    SYM_STR = (CljObject*)&sym_str_data;
    symbol_table_add(NULL, "str", SYM_STR);
    
    SYM_CONJ = (CljObject*)&sym_conj_data;
    symbol_table_add(NULL, "conj", SYM_CONJ);
    
    SYM_NTH = (CljObject*)&sym_nth_data;
    symbol_table_add(NULL, "nth", SYM_NTH);
    
    SYM_FIRST = (CljObject*)&sym_first_data;
    symbol_table_add(NULL, "first", SYM_FIRST);
    
    SYM_REST = (CljObject*)&sym_rest_data;
    symbol_table_add(NULL, "rest", SYM_REST);
    
    SYM_COUNT = (CljObject*)&sym_count_data;
    symbol_table_add(NULL, "count", SYM_COUNT);
    
    // Additional symbols - static structs with symbol table registration
    SYM_CONS = (CljObject*)&sym_cons_data;
    symbol_table_add(NULL, "cons", SYM_CONS);
    
    SYM_SEQ = (CljObject*)&sym_seq_data;
    symbol_table_add(NULL, "seq", SYM_SEQ);
    
    SYM_NEXT = (CljObject*)&sym_next_data;
    symbol_table_add(NULL, "next", SYM_NEXT);
    
    SYM_LIST = (CljObject*)&sym_list_data;
    symbol_table_add(NULL, "list", SYM_LIST);
    
    SYM_AND = (CljObject*)&sym_and_data;
    symbol_table_add(NULL, "and", SYM_AND);
    
    SYM_OR = (CljObject*)&sym_or_data;
    symbol_table_add(NULL, "or", SYM_OR);
    
    SYM_FOR = (CljObject*)&sym_for_data;
    symbol_table_add(NULL, "for", SYM_FOR);
    
    sym_doseq_data.sym.name = strdup("doseq");
    SYM_DOSEQ = (CljObject*)&sym_doseq_data;
    symbol_table_add(NULL, "doseq", SYM_DOSEQ);
    
    sym_dotimes_data.sym.name = strdup("dotimes");
    
    // Keywords - static structs with symbol table registration
    SYM_KW_LINE = (CljObject*)&sym_kw_line_data;
    symbol_table_add(NULL, ":line", SYM_KW_LINE);
    
    SYM_KW_FILE = (CljObject*)&sym_kw_file_data;
    symbol_table_add(NULL, ":file", SYM_KW_FILE);
    
    SYM_KW_DOC = (CljObject*)&sym_kw_doc_data;
    symbol_table_add(NULL, ":doc", SYM_KW_DOC);
    
    SYM_KW_ERROR = (CljObject*)&sym_kw_error_data;
    symbol_table_add(NULL, ":error", SYM_KW_ERROR);
    
    SYM_KW_STACK = (CljObject*)&sym_kw_stack_data;
    symbol_table_add(NULL, ":stack", SYM_KW_STACK);
}

// Hilfsfunktionen für Symbol-Vergleiche
bool is_special_form(CljObject *symbol, CljObject *special_symbol) {
    return symbol == special_symbol;
}

bool is_builtin_function(CljObject *symbol, CljObject *builtin_symbol) {
    return symbol == builtin_symbol;
}
