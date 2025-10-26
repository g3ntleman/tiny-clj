#ifndef TINY_CLJ_BUILTINS_H
#define TINY_CLJ_BUILTINS_H

#include "object.h"
#include "runtime.h"

typedef ID (*CljNativeFn)(ID *args, unsigned int argc);

// Legacy builtin table types removed - all builtins now use namespace registration

// Example implementations
ID nth2(ID *args, unsigned int argc);
ID conj2(ID vec, ID val);
ID assoc3(ID *args, unsigned int argc);
ID native_if(ID *args, unsigned int argc);
ID native_type(ID *args, unsigned int argc);
ID native_array_map(ID *args, unsigned int argc);

// Function value constructors
ID make_named_func(BuiltinFn fn, void *env, const char *name);

// Legacy apply_builtin removed - all builtins now use namespace registration

// Builtins registration
void register_builtins();

// Variadic functions (Phase 1)
ID native_str(ID *args, unsigned int argc);
// Arithmetic wrapper functions removed - using *_variadic directly
ID native_add_variadic(ID *args, unsigned int argc);
ID native_sub_variadic(ID *args, unsigned int argc);
ID native_mul_variadic(ID *args, unsigned int argc);
ID native_div_variadic(ID *args, unsigned int argc);

// Transient functions
ID native_transient(ID *args, unsigned int argc);
ID native_persistent(ID *args, unsigned int argc);
ID native_conj_bang(ID *args, unsigned int argc);

// Sequence functions with validation
ID native_first(ID *args, unsigned int argc);
ID native_rest(ID *args, unsigned int argc);
ID native_cons(ID *args, unsigned int argc);
ID native_count(ID *args, unsigned int argc);
ID native_conj(ID *args, unsigned int argc);

// Comparison operators
ID native_lt(ID *args, unsigned int argc);
ID native_gt(ID *args, unsigned int argc);
ID native_le(ID *args, unsigned int argc);
ID native_ge(ID *args, unsigned int argc);
ID native_eq(ID *args, unsigned int argc);

#endif
