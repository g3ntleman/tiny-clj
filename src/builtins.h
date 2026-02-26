#ifndef TINY_CLJ_BUILTINS_H
#define TINY_CLJ_BUILTINS_H

#include "object.h"
#include "namespace.h"  // For EvalState
#include "runtime.h"

typedef ID (*CljNativeFn)(ID *args, unsigned int argc);

// Legacy builtin table types removed - all builtins now use namespace registration

// Example implementations
ID nth2(ID *args, unsigned int argc);
ID conj2(ID vec, ID val);
ID assoc3(ID *args, unsigned int argc);
ID native_dissoc(ID *args, unsigned int argc);
ID native_type(ID *args, unsigned int argc);
ID native_array_map(ID *args, unsigned int argc);
ID native_vector(ID *args, unsigned int argc);

// Function value constructors
// (moved) make_named_func is declared in function.h

// Legacy apply_builtin removed - all builtins now use namespace registration

// Builtins registration
void register_builtins(void);
void builtins_reset_cached_funcs(void);

// Native function lookup for stubs
// Returns NULL if not found
BuiltinFn native_function_lookup(CljSymbol *symbol);
bool builtin_native_fn_needs_eval_state(BuiltinFn fn);

// Variadic functions (Phase 1)
ID native_str(ID *args, unsigned int argc);
// Arithmetic wrapper functions removed - using *_variadic directly
ID native_add_variadic(ID *args, unsigned int argc);
ID native_sub_variadic(ID *args, unsigned int argc);
ID native_mul_variadic(ID *args, unsigned int argc);
ID native_div_variadic(ID *args, unsigned int argc);
ID native_mod(ID *args, unsigned int argc);
ID native_quot(ID *args, unsigned int argc);
ID native_bit_shift_left(ID *args, unsigned int argc);
ID native_range(ID *args, unsigned int argc);
ID native_repeat(ID *args, unsigned int argc);
ID native_math_sqrt(ID *args, unsigned int argc);
ID native_format(ID *args, unsigned int argc);
ID native_eval(ID *args, unsigned int argc);
ID native_read_string(ID *args, unsigned int argc);
ID native_meta(ID *args, unsigned int argc);
void builtin_set_eval_state(EvalState *st);
EvalState* builtin_get_eval_state(void);

// Transient functions
ID native_transient(ID *args, unsigned int argc);
ID native_persistent_bang(ID *args, unsigned int argc);
ID native_conj_bang(ID *args, unsigned int argc);

// Sequence functions with validation
ID native_seq(ID *args, unsigned int argc);
ID native_not(ID *args, unsigned int argc);
ID native_first(ID *args, unsigned int argc);
ID native_rest(ID *args, unsigned int argc);
ID native_next(ID *args, unsigned int argc);
ID native_cons(ID *args, unsigned int argc);
ID native_list(ID *args, unsigned int argc);
ID native_count(ID *args, unsigned int argc);
ID native_conj(ID *args, unsigned int argc);
ID native_reverse(ID *args, unsigned int argc);
ID native_require(ID *args, unsigned int argc);
/** Load a namespace by name using the same logic as (require 'ns-name). Returns true on success. */
bool require_namespace_by_name(EvalState *st, const char *ns_name);
/** Load namespace from already-resolved bytes (path used for source context). Shared by require and load_clojure_core. */
bool load_namespace_from_bytes(EvalState *st, const char *ns_name, ID bytes, const char *source_path);

/** Optional C-side init function called once after a namespace is first loaded.
 *  Registered via ns_register_init(). Runs after all Clojure forms in the
 *  namespace source have been evaluated (record descriptors exist, etc.). */
typedef bool (*NsInitFn)(EvalState *st);
void ns_register_init(const char *ns_name, NsInitFn init_fn);

// Comparison operators
ID native_lt(ID *args, unsigned int argc);
ID native_gt(ID *args, unsigned int argc);
ID native_le(ID *args, unsigned int argc);
ID native_ge(ID *args, unsigned int argc);
ID native_eq(ID *args, unsigned int argc);
ID native_not_eq(ID *args, unsigned int argc);

// Event-loop builtins
ID native_run_next_task(ID *args, unsigned int argc);
ID native_identical(ID *args, unsigned int argc);
ID native_vector_p(ID *args, unsigned int argc);
ID native_atom_p(ID *args, unsigned int argc);

// Timer builtins
ID native_schedule(ID *args, unsigned int argc);
ID native_schedule_periodic(ID *args, unsigned int argc);

// Time functions
// native_time removed: time is now only a special form
ID native_sleep(ID *args, unsigned int argc);

// Note: def and ns are special forms (not builtins) because they require non-evaluated arguments

// Print functions
ID native_print(ID *args, unsigned int argc);
ID native_println(ID *args, unsigned int argc);
ID native_pr(ID *args, unsigned int argc);
ID native_prn(ID *args, unsigned int argc);

// Atom functions
ID native_atom(ID *args, unsigned int argc);
ID native_deref(ID *args, unsigned int argc);
ID native_reset_bang(ID *args, unsigned int argc);
ID native_swap_bang(ID *args, unsigned int argc);

// String functions (clojure.string namespace)
ID native_trim(ID *args, unsigned int argc);
ID native_upper_case(ID *args, unsigned int argc);
ID native_lower_case(ID *args, unsigned int argc);
ID native_last_index_of(ID *args, unsigned int argc);
ID native_string_reverse(ID *args, unsigned int argc);

// REPL functions (clojure.repl namespace)
ID native_source(ID *args, unsigned int argc);

// Loop constructs converted to builtins
// Note: dotimes is now implemented as a special form, not a builtin

// Audio builtins
ID native_audio_load_track(ID *args, unsigned int argc);
ID native_audio_unload_track(ID *args, unsigned int argc);
ID native_audio_play_music(ID *args, unsigned int argc);
ID native_audio_stop_track(ID *args, unsigned int argc);
ID native_audio_stop_music(ID *args, unsigned int argc);
ID native_audio_play_sfx(ID *args, unsigned int argc);
ID native_audio_stop_all(ID *args, unsigned int argc);
ID native_audio_set_track_volume(ID *args, unsigned int argc);
ID native_audio_set_music_volume(ID *args, unsigned int argc);
ID native_audio_on_finished(ID *args, unsigned int argc);
ID native_audio_play_test_tone(ID *args, unsigned int argc);
ID native_audio_host_status(ID *args, unsigned int argc);

#endif
