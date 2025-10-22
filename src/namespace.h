#ifndef TINY_CLJ_NAMESPACE_H
#define TINY_CLJ_NAMESPACE_H

#include "object.h"
#include "memory.h"
#include <stdbool.h>

// Namespace structure
typedef struct CljNamespace {
    CljObject *name;          // z.B. 'user', 'math'
    CljMap *mappings;      // Map: Symbol → CljObject (def, defn, vars)
    const char *filename;    // optional: zugeordnetes File
    struct CljNamespace *next;
} CljNamespace;

// EvalState structure including namespaces and exception handling
typedef struct {
    CljObject *expr;
    CljObject *result;
    int pc;
    int step_budget;
    CljObject **stack;
    int sp;
    int stack_capacity;
    struct CljObjectPool *pool;
    int finished;
    CljNamespace *current_ns; // current namespace (*ns*)
    
    // Note: Exception handling moved to global exception stack (independent of EvalState)
    const char *file;         // current file
    int line;                 // current line
    int col;                  // current column
} EvalState;

// Global namespace registry
extern CljNamespace *ns_registry;

// Namespace functions
CljNamespace* ns_get_or_create(const char *name, const char *file);
ID ns_resolve(EvalState *st, CljObject *sym);
CljNamespace* ns_load_file(EvalState *st, const char *ns_name, const char *filename);
void ns_register(CljNamespace *ns);
CljNamespace* ns_find(const char *name);
void ns_define(EvalState *st, CljObject *symbol, CljObject *value);

// EvalState functions
EvalState* evalstate();
EvalState* evalstate_new();
void evalstate_free(EvalState *st);
void evalstate_set_ns(EvalState *st, const char *ns_name);

// Optimized EvalState functions
EvalState* evalstate_new_lazy();
void evalstate_ensure_initialized(EvalState *st);

// Exception handling
void eval_error(const char *msg, EvalState *st);
void parse_error(const char *msg, EvalState *st);
CljObject* eval_try(CljObject *form, EvalState *st);
CljObject* eval_catch(CljObject *form, EvalState *st);
CljObject* eval_expr_simple(CljObject *expr, EvalState *st);

// List helpers for try/catch
ID list_first(CljList *list);
ID list_nth(CljList *list, int n);
int list_count(CljList *list);
bool is_list(ID v);
bool is_symbol(ID v, const char *name);

#endif
