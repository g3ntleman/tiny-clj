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

// Event-loop builtins
ID native_run_next_task(ID *args, unsigned int argc);
ID native_identical(ID *args, unsigned int argc);
ID native_vector_p(ID *args, unsigned int argc);

// Time functions
// native_time removed: time is now only a special form
ID native_time_micro(ID *args, unsigned int argc);
ID native_sleep(ID *args, unsigned int argc);

// Special forms converted to builtins
ID native_def(ID *args, unsigned int argc);

// Print functions
ID native_print(ID *args, unsigned int argc);
ID native_println(ID *args, unsigned int argc);
ID native_pr(ID *args, unsigned int argc);
ID native_prn(ID *args, unsigned int argc);

// Loop constructs converted to builtins
// Note: dotimes is now implemented as a special form, not a builtin

#endif
