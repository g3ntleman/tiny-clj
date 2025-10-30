#ifndef TINY_CLJ_SYMBOLS_H
#define TINY_CLJ_SYMBOLS_H

#include "object.h"
#include <stdbool.h>

// CljSymbol struct definition
#define SYMBOL_NAME_MAX_LEN 32

typedef struct {
    CljObject base;
    struct CljNamespace *ns;
    const char *name;
} CljSymbol;

// Type-safe casting
static inline CljSymbol* as_symbol(ID obj) {
    return (CljSymbol*)assert_type((CljObject*)obj, CLJ_SYMBOL);
}

// Globale Symbol-Pointer für Spezialformen (direkt als CljObject*)
extern CljObject *SYM_TRY;
extern CljObject *SYM_CATCH;
extern CljObject *SYM_IF;
extern CljObject *SYM_COND;
extern CljObject *SYM_LET;
extern CljObject *SYM_FN;
extern CljObject *SYM_DEF;
extern CljObject *SYM_DEFN;
extern CljObject *SYM_VAR;
extern CljObject *SYM_QUOTE;
extern CljObject *SYM_QUASIQUOTE;
extern CljObject *SYM_UNQUOTE;
extern CljObject *SYM_SPLICE_UNQUOTE;
extern CljObject *SYM_DO;
extern CljObject *SYM_LOOP;
extern CljObject *SYM_RECUR;
extern CljObject *SYM_THROW;
extern CljObject *SYM_FINALLY;
extern CljObject *SYM_NS;
extern CljObject *SYM_GO;
extern CljObject *SYM_TIME;

// Globale Symbol-Pointer für Builtin-Funktionen
extern CljObject *SYM_PLUS;
extern CljObject *SYM_MINUS;
extern CljObject *SYM_MULTIPLY;
extern CljObject *SYM_DIVIDE;
extern CljObject *SYM_EQUALS;
extern CljObject *SYM_EQUAL;
extern CljObject *SYM_LT;
extern CljObject *SYM_GT;
extern CljObject *SYM_LE;
extern CljObject *SYM_GE;
extern CljObject *SYM_PRINTLN;
extern CljObject *SYM_PRINT;
extern CljObject *SYM_STR;
extern CljObject *SYM_CONJ;
extern CljObject *SYM_NTH;
extern CljObject *SYM_FIRST;
extern CljObject *SYM_REST;
extern CljObject *SYM_COUNT;

// Additional symbols for optimization
extern CljObject *SYM_CONS;
extern CljObject *SYM_SEQ;
extern CljObject *SYM_NEXT;
extern CljObject *SYM_LIST;
extern CljObject *SYM_AND;
extern CljObject *SYM_OR;
extern CljObject *SYM_FOR;
extern CljObject *SYM_DOSEQ;
extern CljObject *SYM_DOTIMES;

// Globale Symbol-Pointer für Keywords
extern CljObject *SYM_KW_LINE;
extern CljObject *SYM_KW_FILE;
extern CljObject *SYM_KW_DOC;
extern CljObject *SYM_KW_ERROR;
extern CljObject *SYM_KW_STACK;

// Symbol interning with a real symbol table
typedef struct SymbolEntry {
    char *ns;
    char *name;
    CljObject *symbol;
    struct SymbolEntry *next;
} SymbolEntry;

extern SymbolEntry *symbol_table;

CljObject* intern_symbol(const char *ns, const char *name);
CljObject* intern_symbol_global(const char *name);  // Without namespace
SymbolEntry* symbol_table_add(const char *ns, const char *name, CljObject *symbol);
void symbol_table_cleanup();
int symbol_count();

// Initialisierung der globalen Symbole
void init_special_symbols();

#endif
