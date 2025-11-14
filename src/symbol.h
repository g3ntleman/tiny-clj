#ifndef TINY_CLJ_SYMBOLS_H
#define TINY_CLJ_SYMBOLS_H

#include "object.h"
#include <stdbool.h>

// CljSymbol struct definition
#define SYMBOL_NAME_MAX_LEN 64

typedef struct {
    CljObject base;
    struct CljNamespace *ns;
    const char *name;
} CljSymbol;

// Type-safe casting
static inline CljSymbol* as_symbol(ID obj) {
    return (CljSymbol*)assert_type((CljObject*)obj, CLJ_SYMBOL);
}

// Check if an object is a keyword (symbol starting with ':')
static inline bool is_keyword(ID obj) {
    if (!obj || TAG(obj) != CLJ_SYMBOL) return false;
    CljSymbol *sym = as_symbol(obj);
    return sym->name && sym->name[0] == ':';
}
#define IS_KEYWORD(obj) is_keyword(obj)

// Globale Symbol-Pointer für Spezialformen
extern CljSymbol *SYM_TRY;
extern CljSymbol *SYM_CATCH;
extern CljSymbol *SYM_IF;
extern CljSymbol *SYM_COND;
extern CljSymbol *SYM_WHEN;
extern CljSymbol *SYM_WHILE;
extern CljSymbol *SYM_LET;
extern CljSymbol *SYM_FN;
extern CljSymbol *SYM_DEF;
extern CljSymbol *SYM_DEFN;
extern CljSymbol *SYM_VAR;
extern CljSymbol *SYM_QUOTE;
extern CljSymbol *SYM_QUASIQUOTE;
extern CljSymbol *SYM_UNQUOTE;
extern CljSymbol *SYM_SPLICE_UNQUOTE;
extern CljSymbol *SYM_DO;
extern CljSymbol *SYM_LOOP;
extern CljSymbol *SYM_RECUR;
extern CljSymbol *SYM_THROW;
extern CljSymbol *SYM_FINALLY;
extern CljSymbol *SYM_NS;
extern CljSymbol *SYM_GO;
extern CljSymbol *SYM_TIME;
extern CljSymbol *SYM_DEREF;
extern CljSymbol *SYM_NIL;

// Globale Symbol-Pointer für Builtin-Funktionen
extern CljSymbol *SYM_PLUS;
extern CljSymbol *SYM_MINUS;
extern CljSymbol *SYM_MULTIPLY;
extern CljSymbol *SYM_DIVIDE;
extern CljSymbol *SYM_EQUALS;
extern CljSymbol *SYM_EQUAL;
extern CljSymbol *SYM_LT;
extern CljSymbol *SYM_GT;
extern CljSymbol *SYM_LE;
extern CljSymbol *SYM_GE;
extern CljSymbol *SYM_PRINTLN;
extern CljSymbol *SYM_PRINT;
extern CljSymbol *SYM_STR;
extern CljSymbol *SYM_CONJ;
extern CljSymbol *SYM_NTH;
extern CljSymbol *SYM_FIRST;
extern CljSymbol *SYM_REST;
extern CljSymbol *SYM_COUNT;

// Additional symbols for optimization
extern CljSymbol *SYM_CONS;
extern CljSymbol *SYM_SEQ;
extern CljSymbol *SYM_NEXT;
extern CljSymbol *SYM_LIST;
extern CljSymbol *SYM_AND;
extern CljSymbol *SYM_OR;
extern CljSymbol *SYM_FOR;
extern CljSymbol *SYM_DOSEQ;
extern CljSymbol *SYM_DOTIMES;

// Globale Symbol-Pointer für Keywords
extern CljSymbol *SYM_KW_LINE;
extern CljSymbol *SYM_KW_FILE;
extern CljSymbol *SYM_KW_DOC;
extern CljSymbol *SYM_KW_ERROR;
extern CljSymbol *SYM_KW_STACK;
extern CljSymbol *SYM_KW_NS;

// Global symbol for clojure.core namespace name (for fast comparison)
extern CljSymbol *SYM_CLOJURE_CORE;

// Additional symbols for hot path optimization
extern CljSymbol *SYM_NS_STAR;

// Symbol interning with a real symbol table
typedef struct SymbolEntry {
    char *ns;
    char *name;
    CljObject *symbol;
    struct SymbolEntry *next;
} SymbolEntry;

extern SymbolEntry *symbol_table;

CljSymbol* make_symbol(const char *name, const char *ns);
CljSymbol* intern_symbol(const char *ns, const char *name);
CljSymbol* intern_symbol_global(const char *name);  // Without namespace
SymbolEntry* symbol_table_add(const char *ns, const char *name, CljSymbol *symbol);
void symbol_table_cleanup();

// Initialisierung der globalen Symbole
void init_special_symbols();

#endif
