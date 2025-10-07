#ifndef TINY_CLJ_SYMBOLS_H
#define TINY_CLJ_SYMBOLS_H

#include "object.h"

// Globale Symbol-Pointer für Spezialformen (direkt als CljObject*)
extern CljObject *SYM_TRY;
extern CljObject *SYM_CATCH;
extern CljObject *SYM_IF;
extern CljObject *SYM_LET;
extern CljObject *SYM_FN;
extern CljObject *SYM_DEF;
extern CljObject *SYM_DEFN;
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

// Globale Symbol-Pointer für Builtin-Funktionen
extern CljObject *SYM_PLUS;
extern CljObject *SYM_MINUS;
extern CljObject *SYM_MULTIPLY;
extern CljObject *SYM_DIVIDE;
extern CljObject *SYM_EQUALS;
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

// Globale Symbol-Pointer für Keywords
extern CljObject *SYM_KW_LINE;
extern CljObject *SYM_KW_FILE;
extern CljObject *SYM_KW_DOC;
extern CljObject *SYM_KW_ERROR;
extern CljObject *SYM_KW_STACK;

// Initialisierung der globalen Symbole
void init_special_symbols();

// Hilfsfunktionen für Symbol-Vergleiche
int is_special_form(CljObject *symbol, CljObject *special_symbol);
int is_builtin_function(CljObject *symbol, CljObject *builtin_symbol);

#endif
