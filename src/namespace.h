#ifndef TINY_CLJ_NAMESPACE_H
#define TINY_CLJ_NAMESPACE_H

#include "object.h"
#include "memory.h"
#include "symbol.h"  // Include symbol.h for CljSymbol definition
#include "vector.h"
#include <stdbool.h>

// Include map.h for CljPersistentMap type
// Note: This may create a circular dependency if value.h includes namespace.h
// But since CljPersistentMap is an anonymous struct typedef, we can't use forward declaration
#include "map.h"

// Namespace structure - subtype of CljObject
struct CljNamespace {
    CljObject base;           // type + rc (4 bytes) - must be first field
    bool loaded;              // true once namespace source has been loaded/evaluated
    CljSymbol *name;          // z.B. 'user', 'math'
    CljPersistentMap *mappings;         // Map: Symbol → CljObject (def, defn, vars)
    CljPersistentMap *private_mappings; // Set-like map: private Symbol → true
    CljPersistentMap *macro_mappings;   // Map: Symbol → CljFunction (Macro-Registry)
    CljPersistentMap *aliases;          // Map: Symbol → Symbol (Alias → full namespace name)
    const char *filename;     // optional: associated file
};
typedef struct CljNamespace CljNamespace;

// EvalState structure including namespaces and exception handling
typedef struct {
    CljObject *expr;
    CljObject *result;
    int pc;
    int step_budget;
    CljObject **stack;
    int sp;
    int stack_capacity;
    CljPersistentVector *pool;
    CljTransientVector *dynamic_bindings; // transient vector: stack of binding frame maps
    int finished;
    CljNamespace *current_ns; // dynamic current namespace (*ns*)
    CljNamespace *resolve_ns; // namespace used for unqualified symbol resolution (defaults to current_ns)
} EvalState;

// Global namespace registry is now in g_runtime.ns_registry (CljTransientMap*)

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
void ns_mark_private(CljNamespace *ns, ID symbol);
bool ns_is_private(CljNamespace *ns, CljSymbol *symbol);
void ns_invalidate_resolve_cache(void);  // Invalidate resolve cache (sets to NULL)
void ns_begin_resolve_cache_batch(void); // Coalesce repeated invalidations into one epoch bump
void ns_end_resolve_cache_batch(void);
void ns_cleanup(void);
int ns_reset_registry(void);  // Reset and reinitialize namespace registry

// Namespace alias functions
ID ns_get_alias(CljNamespace *ns, CljSymbol *alias);
void ns_set_alias(CljNamespace *ns, ID alias, ID ns_name);

// EvalState functions
EvalState* get_global_eval_state(void);  // Get global thread-local EvalState
void reset_eval_state(void);      // Reset global state for test isolation
void reset_eval_state_current_ns(void);  // Clear current_ns pointer (for cleanup)
EvalState* evalstate_new(bool load_core);  // Returns global state (for compatibility)
void evalstate_free(EvalState *st);  // No-op for global state (for compatibility)
void evalstate_set_ns(EvalState *st, const char *ns_name);
void evalstate_reset(EvalState **st_ptr, bool load_core);

// Dynamic binding stack helpers
void evalstate_pop_dynamic_bindings_to(EvalState *st, unsigned int depth);

// Exception handling
void eval_error(const char *msg, EvalState *st);
void parse_error(const char *msg, EvalState *st);

// List helpers moved to list.h

#endif
