#ifndef TINY_CLJ_NAMESPACE_H
#define TINY_CLJ_NAMESPACE_H

#include "object.h"
#include "memory.h"
#include "symbol.h"  // Include symbol.h for CljSymbol definition
#include <stdbool.h>

// Include map.h for CljMap type
// Note: This may create a circular dependency if value.h includes namespace.h
// But since CljMap is an anonymous struct typedef, we can't use forward declaration
#include <subjective-c/map.h>

// Namespace structure - subtype of CljObject
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtypedef-redefinition"
typedef struct CljNamespace {
    CljObject base;           // type + rc (4 bytes) - must be first field
    CljSymbol *name;          // z.B. 'user', 'math' (Symbol instead of CljObject* for type safety)
    CljMap *mappings;         // Map: Symbol → CljObject (def, defn, vars)
    CljMap *macro_mappings;   // Map: Symbol → CljFunction (Macro-Registry)
    CljMap *aliases;          // Map: Symbol → Symbol (Alias → full namespace name)
    const char *filename;    // optional: associated file
} CljNamespace;
#pragma GCC diagnostic pop

// EvalState structure including namespaces and exception handling
typedef struct {
    CljObject *expr;
    CljObject *result;
    int pc;
    int step_budget;
    CljObject **stack;
    int sp;
    int stack_capacity;
    struct CljVector *pool;
    int finished;
    CljNamespace *current_ns; // current namespace (*ns*)
} EvalState;

// Global namespace registry is now in g_runtime.ns_registry (CljMap*)

// Namespace functions
CljNamespace* make_namespace(const char *cname, const char *file);
CljNamespace* ns_get_or_create(const char *cname, const char *file);
ID ns_resolve(EvalState *st, CljSymbol *sym);
CljNamespace* ns_load_file(EvalState *st, const char *ns_name, const char *filename);
void ns_register(CljNamespace *ns);
CljNamespace* ns_find(const char *cname);
CljNamespace* ns_find_by_symbol(CljSymbol *name_symbol);  // Fast lookup with symbol (avoids intern_symbol call)
CljNamespace* ns_find_for_object(CljObject *obj);  // Find namespace containing object
void ns_define(CljNamespace *ns, ID symbol, ID value);
void ns_define_refer(CljNamespace *ns, ID symbol, ID value);  // For :refer - stores unqualified symbol
void ns_invalidate_resolve_cache(void);  // Invalidate resolve cache (sets to NULL)
void ns_cleanup(void);
int ns_reset_registry(void);  // Reset and reinitialize namespace registry

// Namespace alias functions
ID ns_get_alias(CljNamespace *ns, ID alias);
void ns_set_alias(CljNamespace *ns, ID alias, ID ns_name);

// EvalState functions
EvalState* get_global_eval_state(void);  // Get global thread-local EvalState
void reset_eval_state(void);      // Reset global state for test isolation
void reset_eval_state_current_ns(void);  // Clear current_ns pointer (for cleanup)
EvalState* evalstate_new(bool load_core);  // Returns global state (for compatibility)
void evalstate_free(EvalState *st);  // No-op for global state (for compatibility)
void evalstate_set_ns(EvalState *st, const char *ns_name);
void evalstate_reset(EvalState **st_ptr, bool load_core);

// Exception handling
void eval_error(const char *msg, EvalState *st);
void parse_error(const char *msg, EvalState *st);
CljObject* eval_try(CljObject *form, EvalState *st);
CljObject* eval_catch(CljObject *form, EvalState *st);

// List helpers moved to list.h

#endif
