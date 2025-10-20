#include "symbol.h"
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

// Initialisierung der globalen Symbole
void init_special_symbols() {
    // Spezialformen - Cast nur beim Bootstrapping
    SYM_TRY = intern_symbol_global("try");
    SYM_CATCH = intern_symbol_global("catch");
    SYM_IF = intern_symbol_global("if");
    SYM_LET = intern_symbol_global("let");
    SYM_FN = intern_symbol_global("fn");
    SYM_DEF = intern_symbol_global("def");
    SYM_DEFN = intern_symbol_global("defn");
    SYM_QUOTE = intern_symbol_global("quote");
    SYM_QUASIQUOTE = intern_symbol_global("quasiquote");
    SYM_UNQUOTE = intern_symbol_global("unquote");
    SYM_SPLICE_UNQUOTE = intern_symbol_global("splice-unquote");
    SYM_DO = intern_symbol_global("do");
    SYM_LOOP = intern_symbol_global("loop");
    SYM_RECUR = intern_symbol_global("recur");
    SYM_THROW = intern_symbol_global("throw");
    SYM_FINALLY = intern_symbol_global("finally");
    SYM_NS = intern_symbol_global("ns");
    
    // Builtin-Funktionen
    SYM_PLUS = intern_symbol_global("+");
    SYM_MINUS = intern_symbol_global("-");
    SYM_MULTIPLY = intern_symbol_global("*");
    SYM_DIVIDE = intern_symbol_global("/");
    SYM_EQUALS = intern_symbol_global("=");
    SYM_EQUAL = intern_symbol_global("equal");
    SYM_LT = intern_symbol_global("<");
    SYM_GT = intern_symbol_global(">");
    SYM_LE = intern_symbol_global("<=");
    SYM_GE = intern_symbol_global(">=");
    SYM_PRINTLN = intern_symbol_global("println");
    SYM_PRINT = intern_symbol_global("print");
    SYM_STR = intern_symbol_global("str");
    SYM_CONJ = intern_symbol_global("conj");
    SYM_NTH = intern_symbol_global("nth");
    SYM_FIRST = intern_symbol_global("first");
    SYM_REST = intern_symbol_global("rest");
    SYM_COUNT = intern_symbol_global("count");
    
    // Additional symbols for optimization
    SYM_CONS = intern_symbol_global("cons");
    SYM_SEQ = intern_symbol_global("seq");
    SYM_NEXT = intern_symbol_global("next");
    SYM_LIST = intern_symbol_global("list");
    SYM_AND = intern_symbol_global("and");
    SYM_OR = intern_symbol_global("or");
    SYM_FOR = intern_symbol_global("for");
    SYM_DOSEQ = intern_symbol_global("doseq");
    SYM_DOTIMES = intern_symbol_global("dotimes");
    
    // Keywords
    SYM_KW_LINE = intern_symbol_global(":line");
    SYM_KW_FILE = intern_symbol_global(":file");
    SYM_KW_DOC = intern_symbol_global(":doc");
    SYM_KW_ERROR = intern_symbol_global(":error");
    SYM_KW_STACK = intern_symbol_global(":stack");
}

// Hilfsfunktionen für Symbol-Vergleiche
bool is_special_form(CljObject *symbol, CljObject *special_symbol) {
    return symbol == special_symbol;
}

bool is_builtin_function(CljObject *symbol, CljObject *builtin_symbol) {
    return symbol == builtin_symbol;
}
