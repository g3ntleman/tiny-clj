#include <stdbool.h>
#include "runtime.h"
#include "namespace.h"
#include "seq.h"
#include "strings.h"

// Provide a zero-initialized runtime instance so Subjective-C can link
TinyClJRuntime g_runtime = {0};

// Minimal namespace lookup stub – no namespaces during unit tests
CljNamespace* ns_find_for_object(CljObject *obj) {
    (void)obj;
    return NULL;
}

CljNamespace* ns_find(const char *cname) {
    (void)cname;
    return NULL;
}

CljNamespace* ns_find_by_symbol(CljSymbol *name_symbol) {
    (void)name_symbol;
    return NULL;
}

// Provide stub implementations for the seq iterator helpers used by to_string()
bool seq_iter_empty(const SeqIterator *iter) {
    (void)iter;
    return true;
}

ID seq_iter_first(const SeqIterator *iter) {
    (void)iter;
    return (ID)NULL;
}

bool seq_iter_next(SeqIterator *iter) {
    (void)iter;
    return false;
}

// to_string stub no longer needed - exception.c now uses clj_to_string() from callbacks.c

#define DECLARE_SPECIAL_SYMBOL(name, literal) \
    static CljSymbol sym_##name = { .base = { CLJ_SYMBOL, SINGLETON_RC }, .ns_name = NULL, .cname = literal }; \
    CljSymbol *SYM_##name = &sym_##name

DECLARE_SPECIAL_SYMBOL(IF, "if");
DECLARE_SPECIAL_SYMBOL(LET, "let");
DECLARE_SPECIAL_SYMBOL(DEFN, "defn");
DECLARE_SPECIAL_SYMBOL(DEF, "def");
DECLARE_SPECIAL_SYMBOL(FN, "fn");
DECLARE_SPECIAL_SYMBOL(DO, "do");
DECLARE_SPECIAL_SYMBOL(COND, "cond");
DECLARE_SPECIAL_SYMBOL(WHEN, "when");
DECLARE_SPECIAL_SYMBOL(WHILE, "while");
DECLARE_SPECIAL_SYMBOL(QUOTE, "quote");
DECLARE_SPECIAL_SYMBOL(RECUR, "recur");
DECLARE_SPECIAL_SYMBOL(AND, "and");
DECLARE_SPECIAL_SYMBOL(OR, "or");
DECLARE_SPECIAL_SYMBOL(NS, "ns");
DECLARE_SPECIAL_SYMBOL(TRY, "try");
DECLARE_SPECIAL_SYMBOL(CATCH, "catch");
DECLARE_SPECIAL_SYMBOL(THROW, "throw");
DECLARE_SPECIAL_SYMBOL(FINALLY, "finally");
DECLARE_SPECIAL_SYMBOL(VAR, "var");
DECLARE_SPECIAL_SYMBOL(LOOP, "loop");
DECLARE_SPECIAL_SYMBOL(GO, "go");
DECLARE_SPECIAL_SYMBOL(TIME, "time");

#undef DECLARE_SPECIAL_SYMBOL

