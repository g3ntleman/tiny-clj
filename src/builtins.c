#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <math.h>
#include <ctype.h>
#include "object.h"
#include "vector.h"
#include "map.h"
#include "hashset.h"
#include "atom.h"
#include "kv_macros.h"
#include "numeric_utils.h"
#include "format_utils.h"
#include "runtime.h"
#include "memory.h"
#include "memory_profiler.h"
#include "value.h"
#include "error_messages.h"
#include "symbol.h" // Must be included before namespace.h for CljSymbol definition
#include "symbol_token.h"
#include "namespace.h"
#include "seq.h"
#include "byte_array.h"
#include "exception.h"
#include "list.h"
#include "function.h"
#include "strings.h"
#include "file_utils.h"
#include "to_string.h"
#include "event_loop.h"
#include "reader.h"
#include "parser.h"
#include "ast.h"
#include "ast_canon.h"
#include "source_resolver.h"
#include "meta.h"
#include "eval.h"
#include "platform.h"
#include "macro.h"
#include "instant.h"
#include "hashmap.h"
#include "datetime_utc.h"
#include "platform.h"
#ifdef DEBUG
#include "debug.h"
#endif
#include "builtins_strings.h"
#include "builtins_regex.h"

// -----------------------------------------------------------------------------
// Hot-path helpers
// -----------------------------------------------------------------------------
// Thunk body as CljASTCall(op, [SYM_THUNK_STATE]) so eval uses eval_ast_call, not eval_list.
static ID make_thunk_body_ast_call(ID fn_obj) {
    CljPersistentVector *args = make_vector(1, STRONG);
    if (!args) return NULL;
    vector_conj_inplace(&args, SYM_THUNK_STATE);
    CljASTCall *call = make_ast_call(fn_obj, args);
    RELEASE(args);
    return call ? call : NULL;
}

static inline ID cached_named_func(BuiltinFn fn, CljSymbol *name_sym, ID *slot) {
    if (!*slot) {
        *slot = make_named_func(fn, name_sym);
    }
    return *slot;
}

static ID g_concat_thunk_fn_obj = NULL;
static ID g_map_thunk_fn_obj = NULL;
static ID g_mapcat_thunk_fn_obj = NULL;
static ID g_range_inf_thunk_fn_obj = NULL;

void builtins_reset_cached_funcs(void) {
    g_concat_thunk_fn_obj = NULL;
    g_map_thunk_fn_obj = NULL;
    g_mapcat_thunk_fn_obj = NULL;
    g_range_inf_thunk_fn_obj = NULL;
}

// tiny-clj.datetime native functions (used by :native stubs)
ID native_datetime_civil_from_days(ID *args, unsigned int argc);
ID native_datetime_days_from_civil(ID *args, unsigned int argc);
ID native_datetime_format_iso(ID *args, unsigned int argc);

// -----------------------------------------------------------------------------
// libs' :native stubs (implemented in other compilation units or below)
// -----------------------------------------------------------------------------
ID native_tinyclj_fs_spit_bytes(ID *args, unsigned int argc);
ID native_tinyclj_fs_slurp_bytes(ID *args, unsigned int argc);
ID native_tinyclj_fs_stat(ID *args, unsigned int argc);
ID native_tinyclj_fs_list_batch(ID *args, unsigned int argc);
ID native_tinyclj_fs_delete(ID *args, unsigned int argc);
ID native_tinyclj_fs_set_size(ID *args, unsigned int argc);

ID native_tinyclj_kv_put_bytes(ID *args, unsigned int argc);
ID native_tinyclj_kv_get_bytes(ID *args, unsigned int argc);
ID native_tinyclj_kv_delete(ID *args, unsigned int argc);

ID native_tinyclj_net_udp_socket(ID *args, unsigned int argc);
ID native_tinyclj_net_on_receive(ID *args, unsigned int argc);
ID native_tinyclj_net_send_bang(ID *args, unsigned int argc);
ID native_tinyclj_net_close_bang(ID *args, unsigned int argc);
ID native_tinyclj_net_tcp_connect(ID *args, unsigned int argc);
ID native_tinyclj_net_tcp_on_receive(ID *args, unsigned int argc);
ID native_tinyclj_net_tcp_send_bang(ID *args, unsigned int argc);
ID native_tinyclj_net_tcp_close_bang(ID *args, unsigned int argc);

ID native_tinyclj_net_mdns_open(ID *args, unsigned int argc);
ID native_tinyclj_net_mdns_on_event(ID *args, unsigned int argc);
ID native_tinyclj_net_mdns_browse_bang(ID *args, unsigned int argc);
ID native_tinyclj_net_mdns_close_bang(ID *args, unsigned int argc);

static ID native_tinyclj_runtime_stats(ID *args, unsigned int argc);
static ID native_clojure_pprint_pprint_str(ID *args, unsigned int argc);

// clojure.core sequence functions (used by :native stubs)
ID native_map(ID *args, unsigned int argc);
ID native_mapcat(ID *args, unsigned int argc);
ID native_filter(ID *args, unsigned int argc);
ID native_last(ID *args, unsigned int argc);

// clojure.core namespace management (used by :native stubs)
ID native_ns_unload(ID *args, unsigned int argc);

// GPIO functions
ID native_gpio_watch(ID *args, unsigned int argc);
ID native_gpio_unwatch(ID *args, unsigned int argc);
ID native_gpio_simulate(ID *args, unsigned int argc);

ID native_add_variadic(ID *args, unsigned int argc);
ID native_sub_variadic(ID *args, unsigned int argc);
ID native_mul_variadic(ID *args, unsigned int argc);
ID native_div_variadic(ID *args, unsigned int argc);
ID native_mod(ID *args, unsigned int argc);
ID native_quot(ID *args, unsigned int argc);
ID native_bit_shift_left(ID *args, unsigned int argc);
ID native_range(ID *args, unsigned int argc);
ID native_repeat(ID *args, unsigned int argc);
ID native_lazy_seq_star(ID *args, unsigned int argc);
ID native_math_sqrt(ID *args, unsigned int argc);
// String functions moved to builtins_strings.c
ID native_symbol(ID *args, unsigned int argc);
ID native_type(ID *args, unsigned int argc);
ID native_array_map(ID *args, unsigned int argc);
ID native_vector(ID *args, unsigned int argc);
ID native_vec(ID *args, unsigned int argc);
ID native_hash_set(ID *args, unsigned int argc);
ID native_peek(ID *args, unsigned int argc);
ID native_pop(ID *args, unsigned int argc);
ID native_subvec(ID *args, unsigned int argc);
ID native_conj(ID *args, unsigned int argc);
ID native_seq(ID *args, unsigned int argc);
ID native_not(ID *args, unsigned int argc);
ID native_first(ID *args, unsigned int argc);
ID native_rest(ID *args, unsigned int argc);
ID native_concat(ID *args, unsigned int argc);
ID native_next(ID *args, unsigned int argc);
ID native_nnext(ID *args, unsigned int argc);
ID native_destructure(ID *args, unsigned int argc);
ID native_gensym(ID *args, unsigned int argc);
ID native_partition(ID *args, unsigned int argc);
ID native_some(ID *args, unsigned int argc);
ID native_cons(ID *args, unsigned int argc);
ID native_list(ID *args, unsigned int argc);
ID native_reduce(ID *args, unsigned int argc);
ID native_group_by(ID *args, unsigned int argc);
ID native_count(ID *args, unsigned int argc);
ID native_nilp(ID *args, unsigned int argc);
ID native_reverse(ID *args, unsigned int argc);
ID assoc3(ID *args, unsigned int argc);
ID native_assoc(ID *args, unsigned int argc);
ID native_dissoc(ID *args, unsigned int argc);
ID native_disj(ID *args, unsigned int argc);
ID native_merge(ID *args, unsigned int argc);
ID native_contains_p(ID *args, unsigned int argc);
ID native_update(ID *args, unsigned int argc);
ID native_into(ID *args, unsigned int argc);
ID native_select_keys(ID *args, unsigned int argc);
ID native_find(ID *args, unsigned int argc);
ID native_transient(ID *args, unsigned int argc);
ID native_persistent_bang(ID *args, unsigned int argc);
ID native_conj_bang(ID *args, unsigned int argc);
ID native_get(ID *args, unsigned int argc);
ID native_keys(ID *args, unsigned int argc);
ID native_vals(ID *args, unsigned int argc);
ID native_println(ID *args, unsigned int argc);
ID native_print(ID *args, unsigned int argc);
ID native_pr(ID *args, unsigned int argc);
ID native_prn(ID *args, unsigned int argc);
#ifdef DEBUG
ID native_print_ast(ID *args, unsigned int argc);
ID native_ast_string(ID *args, unsigned int argc);
#endif
ID native_lt(ID *args, unsigned int argc);
ID native_gt(ID *args, unsigned int argc);
ID native_le(ID *args, unsigned int argc);
ID native_ge(ID *args, unsigned int argc);
ID native_eq(ID *args, unsigned int argc);
ID native_not_eq(ID *args, unsigned int argc);
ID native_identical(ID *args, unsigned int argc);
ID native_vector_p(ID *args, unsigned int argc);
ID native_map_p(ID *args, unsigned int argc);
ID native_set_p(ID *args, unsigned int argc);
ID native_number_p(ID *args, unsigned int argc);
ID native_integer_p(ID *args, unsigned int argc);
ID native_float_p(ID *args, unsigned int argc);
ID native_string_p(ID *args, unsigned int argc);
ID native_yield(ID *args, unsigned int argc);
ID native_current_time_ms(ID *args, unsigned int argc);
ID native_keyword_p(ID *args, unsigned int argc);
ID native_keyword(ID *args, unsigned int argc);
ID native_name(ID *args, unsigned int argc);
ID native_map(ID *args, unsigned int argc);
ID native_symbol_p(ID *args, unsigned int argc);
ID native_fn_p(ID *args, unsigned int argc);
ID native_atom_p(ID *args, unsigned int argc);
ID native_char_p(ID *args, unsigned int argc);
ID native_list_p(ID *args, unsigned int argc);
ID native_sleep(ID *args, unsigned int argc);
ID native_ns_unload(ID *args, unsigned int argc);
ID native_ns_map(ID *args, unsigned int argc);
ID native_find_ns(ID *args, unsigned int argc);
ID native_all_ns(ID *args, unsigned int argc);
ID native_ns_unload(ID *args, unsigned int argc);
ID native_do(ID *args, unsigned int argc);
ID native_byte_array(ID *args, unsigned int argc);
ID native_aget(ID *args, unsigned int argc);
ID native_aset(ID *args, unsigned int argc);
ID native_alength(ID *args, unsigned int argc);
ID native_aclone(ID *args, unsigned int argc);
ID native_run_next_task(ID *args, unsigned int argc);
ID native_schedule(ID *args, unsigned int argc);
ID native_schedule_periodic(ID *args, unsigned int argc);
ID native_cancel_timer(ID *args, unsigned int argc);
ID native_atom(ID *args, unsigned int argc);
ID native_deref(ID *args, unsigned int argc);
ID native_reset_bang(ID *args, unsigned int argc);
ID native_swap_bang(ID *args, unsigned int argc);
ID native_instant_p(ID *args, unsigned int argc);
ID native_instant_days(ID *args, unsigned int argc);
ID native_instant_ms(ID *args, unsigned int argc);
ID native_slurp(ID *args, unsigned int argc);
ID native_load_file(ID *args, unsigned int argc);
ID native_spit(ID *args, unsigned int argc);
// (declarations in builtins_strings.h)
ID native_source(ID *args, unsigned int argc);
ID native_repl_dir(ID *args, unsigned int argc);
ID native_meta(ID *args, unsigned int argc);
ID native_with_meta(ID *args, unsigned int argc);
ID nth2(ID *args, unsigned int argc);

#if defined(DEBUG) && !defined(ESP32_BUILD)
ID native_stacktrace_str(ID *args, unsigned int argc);
#endif

static CljNamespace *namespace_from_value(ID value);
static int compare_symbol_names(const void *a, const void *b);

// EvalState for builtins that need it (eval, read-string, meta, require)
// NOTE: tiny-clj is currently single-threaded (macOS + ESP32), so we keep this
// as a plain global pointer (avoids TLS overhead on hot paths).
// Set/cleared in eval_function_call before/after calling builtins.
static EvalState *g_current_eval_state = NULL;

// Getter for g_current_eval_state - used by eval.c to avoid creating temporary EvalStates
EvalState *builtin_get_eval_state(void)
{
    return g_current_eval_state;
}

// Helper function to validate builtin arguments (DRY principle)
static bool validate_builtin_args(unsigned int argc, unsigned int expected, const char *func_name)
{
    if (argc != expected)
    {
        char error_msg[256];
        size_t pos = 0;
        pos = format_append(error_msg, pos, sizeof(error_msg), func_name);
        pos = format_append(error_msg, pos, sizeof(error_msg), " requires exactly ");
        pos = format_append_uint(error_msg, pos, sizeof(error_msg), expected);
        pos = format_append(error_msg, pos, sizeof(error_msg), " argument");
        if (expected != 1) {
            pos = format_append_char(error_msg, pos, sizeof(error_msg), 's');
        }
        pos = format_append(error_msg, pos, sizeof(error_msg), ", got ");
        pos = format_append_uint(error_msg, pos, sizeof(error_msg), argc);
        throw_exception(EXCEPTION_ARITY, error_msg, __FILE__, __LINE__, 0);
    }
    return true;
}

static bool list_try_nth_value(CljList *list, int index, ID *out_value)
{
    if (!out_value) return false;
    if (!list || index < 0) return false;
    if (list_empty(list)) return false;

    CljObject *current = (CljObject *)list;
    for (int j = 0; j < index; j++)
    {
        if (!current || !is_list_type(TAG(current)))
        {
            return false;
        }

        CljList *current_list = as_list(current);
        current = LIST_REST(current_list);

        if (current && !is_list_type(TAG(current)))
        {
            return false;
        }
    }

    if (!current || !is_list_type(TAG(current)))
    {
        return false;
    }

    *out_value = LIST_FIRST(as_list(current));
    return true;
}

ID nth2(ID *args, unsigned int argc)
{
    if (argc != 2 && argc != 3)
    {
        char error_msg[256];
        size_t pos = 0;
        pos = format_append(error_msg, pos, sizeof(error_msg),
                            "nth requires exactly 2 or 3 argument");
        if (argc != 1) {
            pos = format_append_char(error_msg, pos, sizeof(error_msg), 's');
        }
        pos = format_append(error_msg, pos, sizeof(error_msg), ", got ");
        pos = format_append_uint(error_msg, pos, sizeof(error_msg), argc);
        throw_exception(EXCEPTION_ARITY, error_msg, __FILE__, __LINE__, 0);
    }
    ID coll = args[0];
    ID idx = args[1];
    bool has_not_found = (argc == 3);
    ID not_found = has_not_found ? args[2] : NULL;

    // Validate index
    if (!idx || TAG(idx) != CLJ_INT)
    {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "nth requires an integer index");
        return NULL;
    }
    int i = AS_FIXNUM(idx);

    // nil collection: treat as empty/absent.
    // With default => return not-found. Without default => nil.
    if (!coll)
    {
        return has_not_found ? not_found : NULL;
    }

    if (i < 0)
    {
        if (has_not_found)
            return not_found;
        throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
                                  "nth index %d is negative", i);
        return NULL;
    }

    // Fast path: Vectors (O(1) access) - includes transient vectors
    int tag = TAG(coll);
    if (tag == CLJ_VECTOR_PERSISTENT || tag == CLJ_VECTOR_TRANSIENT)
    {
        CljPersistentVector *v = as_vector(coll);
        int count = vector_count(v);
        if (i >= count)
        {
            if (has_not_found)
                return not_found;
            throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
                                      "nth index %d is out of bounds for collection with %d elements", i, count);
            return NULL;
        }
        return vector_nth(v, i);
    }

    // Fast path: Lists (O(n) access via list_nth)
    if (is_list_type(TAG(coll)))
    {
        CljList *list = as_list(coll);
        // list_nth() validates bounds and throws exception if out of bounds.
        // For the 3-arg form, return not-found instead of throwing.
        if (has_not_found)
        {
            ID value = NULL;
            if (!list_try_nth_value(list, i, &value))
            {
                return not_found;
            }
            return value;
        }
        return list_nth(list, i);
    }

    // Slow path: Sequences (O(n) access via iterator)
    if (!is_seqable(coll))
    {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "nth not supported on this type");
        return NULL;
    }

    SeqIterator iter;
    if (!seq_iter_init(&iter, coll))
    {
        if (has_not_found)
            return not_found;
        throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
                                  "nth index %d is out of bounds for empty sequence", i);
        return NULL;
    }

    // Iterate to index i
    for (int j = 0; j < i; j++)
    {
        if (seq_iter_empty(&iter))
        {
            if (has_not_found)
                return not_found;
            throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
                                      "nth index %d is out of bounds for sequence (reached end at index %d)", i, j);
            return NULL;
        }
        seq_iter_next(&iter);
    }

    if (seq_iter_empty(&iter))
    {
        if (has_not_found)
            return not_found;
        throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
                                  "nth index %d is out of bounds for sequence", i);
        return NULL;
    }

    return seq_iter_first(&iter);
}

// peek: returns last element of vector, or nil if empty
ID native_peek(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 1, "peek"))
        return NULL;
    ID vec = args[0];
    if (!vec || TAG(vec) != CLJ_VECTOR_PERSISTENT)
        return NULL;
    CljPersistentVector *v = as_vector(vec);
    int count = vector_count(v);
    if (!count)
        return NULL; // nil for empty vector
    // vector_nth returns element with lifetime tied to vector - no retain needed
    return vector_nth(v, count - 1);
}

// pop: returns new vector without last element, or empty vector if empty
// Uses Copy-on-Write: RC=1 → in-place mutation (O(1)), RC>1 → COW (O(n))
ID native_pop(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 1, "pop"))
        return NULL;
    ID vec = args[0];
    if (!vec || TAG(vec) != CLJ_VECTOR_PERSISTENT)
        return NULL;
    CljPersistentVector *v = as_vector(vec);
    int count = vector_count(v);
    if (count == 0)
    {
        // Return empty vector singleton (no memory management needed)
        return make_vector(0, false);
    }

    // Persistent pop: return new vector without last element.
    CljPersistentVector *result = vector_popped(v);
    if (!result)
        return NULL;
    return result;
}

// subvec: returns sub-vector from start (inclusive) to end (exclusive)
// (subvec v start) → sub-vector from start to end of vector
// (subvec v start end) → sub-vector from start (inclusive) to end (exclusive)
ID native_subvec(ID *args, unsigned int argc)
{
    // subvec accepts 2 or 3 arguments: (subvec v start) or (subvec v start end)
    if (argc != 2 && argc != 3)
    {
        char error_msg[256];
        size_t pos = 0;
        pos = format_append(error_msg, pos, sizeof(error_msg),
                            "subvec requires exactly 2 or 3 argument");
        if (argc != 1) {
            pos = format_append_char(error_msg, pos, sizeof(error_msg), 's');
        }
        pos = format_append(error_msg, pos, sizeof(error_msg), ", got ");
        pos = format_append_uint(error_msg, pos, sizeof(error_msg), argc);
        throw_exception(EXCEPTION_ARITY, error_msg, __FILE__, __LINE__, 0);
        return NULL;
    }

    ID vec = args[0];
    ID start_idx = args[1];
    ID end_idx = argc == 3 ? args[2] : NULL;

    // Type validation
    if (!vec || TAG(vec) != CLJ_VECTOR_PERSISTENT)
    {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "subvec requires a vector as first argument");
        return NULL;
    }

    if (!start_idx || TAG(start_idx) != CLJ_INT)
    {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "subvec requires a number as start index");
        return NULL;
    }

    CljPersistentVector *v = as_vector(vec);

    int start = AS_FIXNUM(start_idx);
    int end;

    // Determine end index: if not provided, use vector count
    if (end_idx)
    {
        if (TAG(end_idx) != CLJ_INT)
        {
            throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                      "subvec requires a number as end index");
            return NULL;
        }
        end = AS_FIXNUM(end_idx);
    }
    else
    {
        end = vector_count(v);
    }

    // Bounds validation
    if (start < 0)
    {
        throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
                                  "subvec start index %d is negative", start);
        return NULL;
    }

    int v_count = vector_count(v);
    if (end > v_count)
    {
        throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
                                  "subvec end index %d is greater than vector count %d", end, v_count);
        return NULL;
    }

    if (start > end)
    {
        throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
                                  "subvec start index %d is greater than end index %d", start, end);
        return NULL;
    }

    // Calculate sub-vector size
    int subvec_count = end - start;

    // Special case: empty sub-vector (start == end)
    if (subvec_count == 0)
    {
        return make_vector(0, STRONG); // Returns empty-vector singleton (no memory management needed)
    }

    // Create new vector and add elements using vector_conj_inplace
    CljPersistentVector *new_vec = make_vector(subvec_count, STRONG);

    // Copy elements from start to end using vector_conj_inplace
    // This keeps rc=1 for COW optimizations
    for (int i = 0; i < subvec_count; i++)
    {
        ID elem = vector_nth(v, start + i);
        if (elem)
        {
            // vector_conj_inplace retains the element internally
            vector_conj_inplace(&new_vec, RETAIN(elem));
        }
        else
        {
            vector_conj_inplace(&new_vec, NULL); // nil elements
        }
    }

    return AUTORELEASE(new_vec);
}

static ID conj2(ID vec, ID val);

ID conj2_wrapper(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 2, "conj"))
        return NULL;
    return conj2(args[0], args[1]);
}

ID conj2(ID vec, ID val)
{
    if (!vec || TAG(vec) != CLJ_VECTOR_PERSISTENT)
        return NULL;
    return vector_conj(vec, val);
}

// Generic conj function that works with BuiltinFn signature
ID native_conj(ID *args, unsigned int argc)
{
    CLJ_ASSERT(args != NULL);

    // Handle different arities like Clojure
    if (argc == 0)
    {
        // conj with no args returns nil (like Clojure)
        return NULL;
    }

    if (argc == 1)
    {
        // conj with one arg returns the collection unchanged
        return args[0]; // caller gave us a retained instance. just return it.
    }

    // For 2+ args, conj all values to the collection
    ID coll = args[0];
    if (!coll)
    {
        // conj nil with values creates a list
        CljList *result = NULL;
        for (unsigned int i = argc - 1; i >= 1; i--)
        {
            ID val = args[i];
            result = make_list(val, result);
        }
        return result;
    }

    unsigned char tag = TAG(coll);

    if (tag == CLJ_VECTOR_PERSISTENT)
    {
        CljObject *result = coll;
        for (unsigned int i = 1; i < argc; i++)
        {
            result = conj2(result, args[i]);
            if (!result)
                return NULL;
        }
        return result;
    }

    if (tag == CLJ_HASHSET)
    {
        CljHashSet *result = (CljHashSet*)coll;
        for (unsigned int i = 1; i < argc; i++)
        {
            CljHashSet *new_result = hashset_add(result, args[i]);
            if (new_result != result)
            {
                if (i > 1 || result != coll)
                {
                    RELEASE(result);
                }
                result = new_result;
            }
        }
        return result;
    }

    // Lists: conj adds to front
    if (is_list_type(tag))
    {
        CljList *result = as_list(coll);
        for (unsigned int i = 1; i < argc; i++)
        {
            result = make_list(args[i], result);
        }
        return result;
    }

    // Throw exception for unsupported collection type
    throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                    "conj not supported on this type",
                    __FILE__, __LINE__, 0);
    return NULL;
}

// First function that works with BuiltinFn signature
ID native_first(ID *args, unsigned int argc)
{
    CLJ_ASSERT(args != NULL);

    if (!validate_builtin_args(argc, 1, "first"))
        return NULL;

    ID coll = args[0];
    // Arguments are already evaluated - nil is NULL, not SYM_NIL
    if (!coll)
    {
        // first of nil returns nil
        return NULL;
    }

    // Switch on collection type (DRY: consistent pattern)
    switch (TAG(coll))
    {
    case CLJ_LIST:
    {
        // Direct access for lists (already a seq) - no allocation needed
        ID first = LIST_FIRST((CljList *)coll);
        // Arguments are evaluated, but list elements might still be SYM_NIL
        // Convert SYM_NIL to NULL (nil representation)
        if (first == SYM_NIL) return NULL;
        return RETAIN(first);
    }

    case CLJ_SEQ:
    {
        // Already a sequence - just call seq_first (DRY)
        ID first = seq_first(coll);
        return RETAIN(first);
    }

    default:
    {
        CljSeqIterator *it = make_seq(coll);
        if (!it) return NULL;
        ID result = RETAIN(seq_first(it));
        RELEASE(it);
        return result;
    }
    }
}

/** Returns a sequence over the collection, or NULL if empty/not seqable. */
ID native_seq(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "seq")) return NULL;
    ID coll = args[0];
    if (!coll || !is_seqable(coll)) return NULL;
    ID result = NULL;
    if (TAG(coll) == CLJ_LIST || TAG(coll) == CLJ_AST_NODE) {
        CljList *list_data = as_list(coll);
        if (!list_empty(list_data)) result = coll;
    } else if (TAG(coll) == CLJ_SEQ) {
        result = coll;
    } else {
        result = AUTORELEASE(make_seq(coll));
    }
    return result;
}

/** Builtin "not": returns true if x is nil or false, else false. */
ID native_not(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "not"))
        return NULL;
    ID x = args[0];
    return (!x || x == clj_false) ? clj_true : clj_false;
}

/** Returns the rest of the sequence (next), or NULL if empty/nil. */
ID native_next(ID *args, unsigned int argc) {
    CLJ_ASSERT(args != NULL);

    if (!validate_builtin_args(argc, 1, "next/rest"))
        return NULL;

    ID collection = args[0];
    if (!collection)
    {
        // next of nil returns nil
        return NULL;
    }

    // Check if collection is an immediate value (fixnum, etc.) - not seqable
    if (IS_IMMEDIATE(collection))
    {
        // Immediate values are not seqable - throw exception
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "next not supported on this type",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    // EAT-YOUR-OWN-DOG-FOOD: Use seq_next for all seqable types.
    // TODO(memory/next): Reintroduce a list fast path later if needed, but keep
    // return ownership identical for all input types per MEMORY_POLICY.
    if (!is_seqable(collection))
    {
        const char *type_name = clj_type_name(((CljObject *)collection)->type);
        char error_msg[256];
        size_t pos = 0;
        pos = format_append(error_msg, pos, sizeof(error_msg),
                            "next not supported on this type: ");
        pos = format_append(error_msg, pos, sizeof(error_msg),
                            type_name ? type_name : "unknown");
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        error_msg,
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    ID it = make_seq(collection);
    if (!it) return NULL;
    seq_next_inplace(&it);  /* COW: in-place when possible, else replace slot */
    if (!it || TAG(it) == CLJ_NIL)
        return NULL;  /* no more seq */
    // Unified ownership policy for next:
    // - heap results are returned AUTORELEASEd (caller-pool lifetime),
    // - immediates are returned as-is.
    if (!IS_IMMEDIATE(it)) return AUTORELEASE(it);
    return it;
}

// Rest function that works with BuiltinFn signature
// DRY: Simply calls native_next and converts nil to empty_list()
ID native_rest(ID *args, unsigned int argc)
{
    CLJ_ASSERT(args != NULL);

    // Call native_next (it will validate again, but that's fine for robustness)
    // If it returns nil, convert to empty_list()
    ID next_result = native_next(args, argc);
    return next_result ? next_result : empty_list();
}

// Concat function: concatenates two sequences
// Thunk is invoked as (fn (quote (state))) => args[0] is (state) or unevaluated (quote (state)).
static ID unwrap_thunk_state(ID state_id) {
    if (!state_id || TAG(state_id) != CLJ_LIST) return state_id;
    CljList *wrapper = as_list(state_id);
    if (!wrapper) return state_id;
    ID first = LIST_FIRST(wrapper);
    if (first == SYM_QUOTE) {
        CljList *rest_list = as_list(LIST_REST(wrapper));
        state_id = rest_list ? LIST_FIRST(rest_list) : state_id;
    } else {
        state_id = first;
    }
    return state_id;
}

static ID native_concat_thunk_executor(ID *args, unsigned int argc) {
    if (argc != 1) return NULL;
    ID state_id = unwrap_thunk_state(args[0]);
    if (!state_id || TAG(state_id) != CLJ_MAP_PERSISTENT) return NULL;

    CljPersistentMap *state = as_map(state_id);
    ID x_seqable = map_get_sentinel(state, SYM_CONCAT_X, NULL);
    ID y = map_get_sentinel(state, SYM_CONCAT_Y, NULL);

    ID elem = NULL;
    bool elem_owned = false;
    ID next_x = empty_list();
    ID x_seq = NULL;
    if (x_seqable && is_list_type(TAG(x_seqable))) {
        CljList *list = as_list(x_seqable);
        if (!list || list_empty(list)) {
            return y;
        }
        elem = LIST_FIRST(list);
        CljObject *rest = LIST_REST(list);
        if (rest) {
            next_x = (ID)rest;
        }
    } else {
        // Non-list path: direct seq semantics without builtin argument validation overhead.
        x_seq = make_seq(x_seqable);  // owned temporary
        if (!x_seq) {
            return y;
        }
        elem = seq_first(x_seq);
        RETAIN(elem);  // may reference seq container internals
        elem_owned = true;
        seq_next_inplace(&x_seq);
        if (x_seq) {
            next_x = x_seq;
        }
    }
    if (elem == SYM_NIL) elem = NULL;

    CljPersistentMap *rest_state = map_empty();
    map_assoc_inplace(&rest_state, SYM_CONCAT_X, next_x);
    RELEASE(x_seq);
    map_assoc_inplace(&rest_state, SYM_CONCAT_Y, y);
    ID fn_obj = cached_named_func(native_concat_thunk_executor, SYM_CONCAT_THUNK_FN, &g_concat_thunk_fn_obj);
    CljLazySeq *rest_lazy = make_lazy_seq(fn_obj);
    if (!rest_lazy) {
        RELEASE(rest_state);
        if (elem_owned) {
            RELEASE(elem);
        }
        return NULL;
    }
    rest_lazy->thunk_state = rest_state;  // ownership transfer

    // LazySeq itself is the cons-like step carrier: first + cached_rest.
    CljLazySeq *result = ALLOC(CljLazySeq, 1);
    result->base.type = CLJ_LAZY_SEQ;
    result->base.flags = 0;
    result->first = RETAIN(elem ? elem : SYM_NIL);
    result->thunk = NULL;
    result->cached_rest = rest_lazy;  // transfer ownership (rc=1 from make_lazy_seq)
    result->thunk_state = NULL;

    if (elem_owned) {
        RELEASE(elem);
    }
    return AUTORELEASE(result);
}

ID native_concat(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 2, "concat"))
        return NULL;

    ID x = args[0];
    ID y = args[1];

    // Fast empty cases.
    if (!x && !y) return empty_list();

    CljPersistentMap *state = map_empty();
    map_assoc_inplace(&state, SYM_CONCAT_X, x);
    map_assoc_inplace(&state, SYM_CONCAT_Y, y);

    ID fn_obj = cached_named_func(native_concat_thunk_executor, SYM_CONCAT_THUNK_FN, &g_concat_thunk_fn_obj);
    CljLazySeq *lazy = make_lazy_seq(fn_obj);
    if (!lazy) {
        RELEASE(state);
        return empty_list();
    }
    lazy->thunk_state = state;  // ownership transfer
    return lazy ? AUTORELEASE(lazy) : empty_list();
}

// nnext: (next (next coll)) - returns the next of the next
ID native_nnext(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 1, "nnext"))
        return NULL;

    ID first_next = native_next(args, 1);
    if (!first_next)
        return NULL;

    return native_next(&first_next, 1);
}

// nthnext: (nthnext coll n) - returns nth next of coll
ID native_nthnext(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 2, "nthnext"))
        return NULL;

    ID coll = args[0];
    if (!coll || IS_IMMEDIATE(coll))
        return NULL;

    if (!is_fixnum(args[1]))
        return NULL;
    int n = as_fixnum(args[1]);

    if (n <= 0)
    {
        // Return seq of coll for n <= 0
        SeqIterator iter;
        if (!seq_iter_init(&iter, coll) || seq_iter_empty(&iter))
        {
            return NULL;
        }
        return coll; // Return as-is for non-empty
    }

    // Apply next n times
    ID current = coll;
    for (int i = 0; i < n && current; i++)
    {
        ID next_args[1] = {current};
        current = native_next(next_args, 1);
    }

    return current;
}

// ---------------------------------------------------------------------------
// destructure: native implementation for clojure.core/destructure
// ---------------------------------------------------------------------------
static CljString *g_destructure_vec_prefix = NULL;
static CljString *g_destructure_map_prefix = NULL;
static CljSymbol *g_destructure_sym_get = NULL;
static CljSymbol *g_destructure_sym_nthnext = NULL;
static CljSymbol *g_destructure_kw_keys = NULL;
static CljSymbol *g_destructure_kw_or = NULL;

static inline CljSymbol* destructure_intern_once(CljSymbol **slot, const char *name) {
    if (!*slot) {
        *slot = intern_symbol_global(name);
    }
    return *slot;
}

static inline CljSymbol* destructure_gensym(const char *prefix, CljString **cache) {
    if (!*cache) {
        *cache = make_string(prefix);
    }
    ID arg = *cache;
    ID args[1] = {arg};
    return (CljSymbol*)native_gensym(args, 1);
}

static inline ID destructure_list3(ID a, ID b, ID c) {
    ID args[3] = {a, b, c};
    return native_list(args, 3);
}

static inline ID destructure_list4(ID a, ID b, ID c, ID d) {
    ID args[4] = {a, b, c, d};
    return native_list(args, 4);
}

static CljSymbol* destructure_keyword_from_value(ID value) {
    if (!value) return NULL;

    const char *name = NULL;
    if (TAG(value) == CLJ_SYMBOL) {
        CljSymbol *sym = as_symbol(value);
        name = sym ? sym->cname : NULL;
        if (name && name[0] == ':') name++;
    } else if (TAG(value) == CLJ_STRING) {
        name = string_data(value);
    }

    if (!name || !*name) return NULL;

    char kw_buf[SYMBOL_NAME_MAX_LEN + 2];
    size_t len = strlen(name);
    if (len > SYMBOL_NAME_MAX_LEN) len = SYMBOL_NAME_MAX_LEN;
    kw_buf[0] = ':';
    memcpy(kw_buf + 1, name, len);
    kw_buf[len + 1] = '\0';
    return intern_symbol_global(kw_buf);
}

static void destructure_seq(CljPersistentVector **bvec_slot, ID bform, ID init) {
    if (!bvec_slot || !*bvec_slot) return;

    CljPersistentVector *bvec = *bvec_slot;
    CljSymbol *gvec = destructure_gensym("vec__", &g_destructure_vec_prefix);
    vector_conj_inplace(&bvec, gvec);
    vector_conj_inplace(&bvec, init);

    CljPersistentVector *elems = NULL;
    if (bform && (TAG(bform) == CLJ_VECTOR_PERSISTENT || TAG(bform) == CLJ_VECTOR_TRANSIENT)) {
        elems = as_vector(bform);
    } else if (bform && is_seqable(bform)) {
        ID vec_args[1] = {bform};
        ID vec_id = native_vec(vec_args, 1);
        if (vec_id && TAG(vec_id) == CLJ_VECTOR_PERSISTENT) {
            elems = as_vector(vec_id);
        }
    }

    if (!elems) {
        *bvec_slot = bvec;
        return;
    }

    CljSymbol *sym_nthnext = destructure_intern_once(&g_destructure_sym_nthnext, "nthnext");
    unsigned int n = vector_count(elems);
    int idx = 0;
    int skip = 0;
    for (unsigned int i = 0; i < n; i++) {
        ID elem = vector_nth(elems, i);
        ID next = (i + 1 < n) ? vector_nth(elems, i + 1) : NULL;

        if (skip > 0) {
            skip--;
            continue;
        }

        if (elem == SYM_AMP) {
            vector_conj_inplace(&bvec, next);
            ID f = destructure_list3(sym_nthnext, gvec, fixnum(idx));
            vector_conj_inplace(&bvec, f);
            skip = 1;
        } else if (elem == SYM_KW_AS) {
            vector_conj_inplace(&bvec, next);
            vector_conj_inplace(&bvec, gvec);
            skip = 1;
        } else {
            vector_conj_inplace(&bvec, elem);
            ID f = destructure_list4(SYM_NTH, gvec, fixnum(idx), NULL);
            vector_conj_inplace(&bvec, f);
            idx++;
        }
    }

    *bvec_slot = bvec;
}

static void destructure_map(CljPersistentVector **bvec_slot, ID bform, ID init) {
    if (!bvec_slot || !*bvec_slot) return;

    CljPersistentVector *bvec = *bvec_slot;
    CljSymbol *gmap = destructure_gensym("map__", &g_destructure_map_prefix);
    vector_conj_inplace(&bvec, gmap);
    vector_conj_inplace(&bvec, init);

    CljSymbol *kw_keys = destructure_intern_once(&g_destructure_kw_keys, ":keys");
    CljSymbol *kw_or = destructure_intern_once(&g_destructure_kw_or, ":or");
    CljSymbol *sym_get = destructure_intern_once(&g_destructure_sym_get, "get");

    ID defaults = (kw_or && bform) ? map_get_sentinel(bform, kw_or, NULL) : NULL;
    ID as_sym = (bform) ? map_get_sentinel(bform, SYM_KW_AS, NULL) : NULL;
    if (as_sym) {
        vector_conj_inplace(&bvec, as_sym);
        vector_conj_inplace(&bvec, gmap);
    }

    ID keys_val = (kw_keys && bform) ? map_get_sentinel(bform, kw_keys, NULL) : NULL;
    CljPersistentVector *keys_vec = NULL;
    if (keys_val && (TAG(keys_val) == CLJ_VECTOR_PERSISTENT || TAG(keys_val) == CLJ_VECTOR_TRANSIENT)) {
        keys_vec = as_vector(keys_val);
    } else if (keys_val && is_seqable(keys_val)) {
        ID vec_args[1] = {keys_val};
        ID vec_id = native_vec(vec_args, 1);
        if (vec_id && TAG(vec_id) == CLJ_VECTOR_PERSISTENT) {
            keys_vec = as_vector(vec_id);
        }
    }

    if (!keys_vec) {
        *bvec_slot = bvec;
        return;
    }

    VECTOR_FOR_EACH(keys_vec, sym) {
        if (!sym) continue;
        if (TAG(sym) != CLJ_SYMBOL && TAG(sym) != CLJ_STRING) continue;

        CljSymbol *kw = destructure_keyword_from_value(sym);
        if (!kw) continue;

        ID default_val = defaults ? map_get_sentinel(defaults, sym, NULL) : NULL;
        ID get_form = clj_is_truthy(default_val)
                          ? destructure_list4(sym_get, gmap, kw, default_val)
                          : destructure_list3(sym_get, gmap, kw);
        vector_conj_inplace(&bvec, sym);
        vector_conj_inplace(&bvec, get_form);
    }

    *bvec_slot = bvec;
}

ID native_destructure(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 1, "destructure");

    ID bindings = args[0];
    if (!bindings || IS_IMMEDIATE(bindings)) {
        return empty_vector();
    }

    SeqIterator iter;
    if (!seq_iter_init(&iter, bindings) || seq_iter_empty(&iter)) {
        return empty_vector();
    }

    CljPersistentVector *result = make_vector(0, STRONG);

    while (!seq_iter_empty(&iter)) {
        ID bform = seq_iter_first(&iter);
        seq_iter_next(&iter);
        if (seq_iter_empty(&iter)) {
            break;
        }
        ID init = seq_iter_first(&iter);
        seq_iter_next(&iter);

        if (bform && TAG(bform) == CLJ_SYMBOL && !IS_KEYWORD(bform)) {
            vector_conj_inplace(&result, bform);
            vector_conj_inplace(&result, init);
        } else if (bform && (TAG(bform) == CLJ_VECTOR_PERSISTENT || TAG(bform) == CLJ_VECTOR_TRANSIENT)) {
            destructure_seq(&result, bform, init);
        } else if (bform && (TAG(bform) == CLJ_MAP_PERSISTENT || TAG(bform) == CLJ_MAP_TRANSIENT)) {
            destructure_map(&result, bform, init);
        } else {
            vector_conj_inplace(&result, bform);
            vector_conj_inplace(&result, init);
        }
    }

    return AUTORELEASE(result);
}

// gensym: Generate unique symbol names
ID native_gensym(ID *args, unsigned int argc)
{
    static unsigned long counter = 0;

    const char *prefix = (argc >= 1 && args[0] && TAG(args[0]) == CLJ_STRING)
                             ? clj_string_data(as_clj_string(args[0]))
                             : "G__";

    char name[256];
    size_t pos = 0;
    pos = format_append(name, pos, sizeof(name), prefix);
    pos = format_append_ulong(name, pos, sizeof(name), ++counter);
    return intern_symbol_global(name);
}

// partition: Partition collection into n-tuples (returns list of lists)
ID native_partition(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 2, "partition"))
        return NULL;

    if (!is_fixnum(args[0]) || as_fixnum(args[0]) <= 0)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "partition requires positive integer size",
                        __FILE__, __LINE__, 0);
        return NULL;
    }
    int n = as_fixnum(args[0]);

    ID coll = args[1];
    if (!coll || IS_IMMEDIATE(coll))
        return empty_list();

    SeqIterator iter;
    if (!seq_iter_init(&iter, coll) || seq_iter_empty(&iter))
    {
        return empty_list();
    }

    // Use vectors for building (efficient), convert to list at end
    CljPersistentVector *partitions = make_vector(0, STRONG);

    while (!seq_iter_empty(&iter))
    {
        CljPersistentVector *part = make_vector(n, STRONG);
        int count = 0;

        for (int i = 0; i < n && !seq_iter_empty(&iter); i++, count++)
        {
            vector_conj_inplace(&part, seq_iter_first(&iter));
            seq_iter_next(&iter);
        }

        if (count == n)
        {
            vector_conj_inplace(&partitions, part);
        }
        RELEASE(part);
    }

    // Convert vector to list for proper iteration
    unsigned int pcount = vector_count(partitions);
    CljList *result = NULL;
    for (int i = pcount - 1; i >= 0; i--)
    {
        ID elem = vector_nth(partitions, i);
        result = make_list(elem, result);
    }
    RELEASE(partitions);

    return result ? AUTORELEASE(result) : empty_list();
}

// map: apply f across 1+ colls (zips to shortest)
// Usage: (map f coll) (map f coll1 coll2 ...)
static ID native_map_thunk_executor(ID *args, unsigned int argc) {
    if (argc != 1) return NULL;
    ID state_id = unwrap_thunk_state(args[0]);
    if (!state_id || TAG(state_id) != CLJ_MAP_PERSISTENT) return NULL;

    CljPersistentMap *state = as_map(state_id);
    ID fn = map_get_sentinel(state, SYM_MAP_FN, NULL);
    ID seqs_vec_id = map_get_sentinel(state, SYM_MAP_SEQS, NULL);
    if (!fn || !seqs_vec_id || TAG(seqs_vec_id) != CLJ_VECTOR_PERSISTENT) return NULL;

    CljPersistentVector *seqs_vec = as_vector(seqs_vec_id);
    unsigned int ncolls = vector_count(seqs_vec);
    if (ncolls == 0 || ncolls > 8) return NULL;

    ID call_args[8] = {0};
    ID next_colls[8] = {0};
    for (unsigned int i = 0; i < ncolls; i++) {
        ID coll = vector_nth(seqs_vec, i);
        ID seq_obj = make_seq(coll);  // owned temporary
        if (!seq_obj) {
            for (unsigned int j = 0; j < i; j++) {
                RELEASE(call_args[j]);
            }
            return NULL; // stop at shortest
        }
        call_args[i] = seq_first(seq_obj);
        RETAIN(call_args[i]); // may reference seq container internals
        seq_next_inplace(&seq_obj);
        next_colls[i] = seq_obj; // owned next seq/list (or NULL)
    }

    EvalState *st = builtin_get_eval_state();
    if (!st) st = get_global_eval_state();
    ID mapped = eval_function_call(fn, call_args, ncolls, NULL, st);
    for (unsigned int i = 0; i < ncolls; i++) {
        RELEASE(call_args[i]);
    }

    // Advance collections (store rest for each).
    CljPersistentVector *next_seqs = make_vector(ncolls, STRONG);
    if (!next_seqs) return NULL;
    for (unsigned int i = 0; i < ncolls; i++) {
        vector_conj_inplace(&next_seqs, next_colls[i]);
        RELEASE(next_colls[i]);
    }

    CljPersistentMap *rest_state = map_empty();
    map_assoc_inplace(&rest_state, SYM_MAP_FN, fn);
    map_assoc_inplace(&rest_state, SYM_MAP_SEQS, next_seqs);
    RELEASE(next_seqs);
    CljPersistentVector *rest_env_stack = make_vector(1, STRONG);
    if (!rest_env_stack) { RELEASE(rest_state); return NULL; }
    vector_conj_inplace(&rest_env_stack, rest_state);
    RELEASE(rest_state);
    ID fn_obj = cached_named_func(native_map_thunk_executor, SYM_MAP_THUNK_FN, &g_map_thunk_fn_obj);
    ID thunk_body = make_thunk_body_ast_call(fn_obj);
    if (!thunk_body) { RELEASE(rest_env_stack); return NULL; }
    ID rest_param = SYM_THUNK_STATE;
    CljFunction *rest_thunk = make_function(&rest_param, 1, thunk_body, rest_env_stack, NULL, NULL);
    RELEASE(thunk_body);
    CljLazySeq *rest_lazy = make_lazy_seq(rest_thunk);
    if (rest_lazy && vector_count(rest_env_stack) > 0) rest_lazy->thunk_state = RETAIN(vector_nth(rest_env_stack, 0));
    RELEASE(rest_thunk);
    RELEASE(rest_env_stack);
    CljList *result = make_list(mapped, (CljList*)rest_lazy);
    RELEASE(rest_lazy);
    return AUTORELEASE(result);
}

// mapcat: lazy concat of (f x) over coll
static ID native_mapcat_thunk_executor(ID *args, unsigned int argc) {
    if (argc != 1) return NULL;
    ID state_id = unwrap_thunk_state(args[0]);
    if (!state_id || TAG(state_id) != CLJ_MAP_PERSISTENT) return NULL;

    CljPersistentMap *state = as_map(state_id);
    ID fn = map_get_sentinel(state, SYM_MAPCAT_FN, NULL);
    ID coll = map_get_sentinel(state, SYM_MAPCAT_COLL, NULL);
    ID inner = map_get_sentinel(state, SYM_MAPCAT_INNER, NULL);
    bool coll_owned = false;
    if (!fn || IS_IMMEDIATE(fn) || !(TAG(fn) == CLJ_FUNC || TAG(fn) == CLJ_CLOSURE)) return NULL;

    EvalState *st = builtin_get_eval_state();
    if (!st) st = get_global_eval_state();

    // Advance until we have a non-empty inner sequence (or outer exhausted).
    while (true) {
        if (inner) {
            ID inner_seq = make_seq(inner); // owned temporary
            if (inner_seq) {
                ID v = seq_first(inner_seq);
                RETAIN(v); // may reference seq container internals
                seq_next_inplace(&inner_seq);
                ID inner_rest = inner_seq; // owned next seq/list (or NULL)

                CljPersistentMap *rest_state = map_empty();
                map_assoc_inplace(&rest_state, SYM_MAPCAT_FN, fn);
                map_assoc_inplace(&rest_state, SYM_MAPCAT_COLL, coll);
                map_assoc_inplace(&rest_state, SYM_MAPCAT_INNER, inner_rest);
                RELEASE(inner_rest);
                ID fn_obj = cached_named_func(native_mapcat_thunk_executor, SYM_MAPCAT_THUNK_FN, &g_mapcat_thunk_fn_obj);
                CljLazySeq *rest_lazy = make_lazy_seq(fn_obj);
                if (!rest_lazy) { RELEASE(v); RELEASE(rest_state); return NULL; }
                rest_lazy->thunk_state = rest_state; // ownership transfer
                CljList *result = make_list(v, (CljList*)rest_lazy);
                RELEASE(v);
                RELEASE(rest_lazy);
                if (coll_owned) {
                    RELEASE(coll);
                    coll_owned = false;
                }
                return AUTORELEASE(result);
            }
            // inner exhausted -> fall through to advance outer
            inner = NULL;
        }

        // Fetch next outer element
        ID coll_seq = make_seq(coll); // owned temporary
        if (!coll_seq) {
            if (coll_owned) {
                RELEASE(coll);
                coll_owned = false;
            }
            return NULL;
        }

        ID outer_first = seq_first(coll_seq);
        RETAIN(outer_first); // may reference seq container internals
        seq_next_inplace(&coll_seq);
        ID outer_rest = coll_seq; // owned next seq/list (or NULL)
        ID call_args[1] = { outer_first };
        inner = eval_function_call(fn, call_args, 1, NULL, st);
        RELEASE(outer_first);
        if (coll_owned) {
            RELEASE(coll);
        }
        coll = outer_rest;
        coll_owned = (coll != NULL);
        // Loop again; either inner yields values, or we advance outer further.
    }
}

ID native_map(ID *args, unsigned int argc)
{
    if (argc < 2)
    {
        throw_exception_formatted(EXCEPTION_ARITY, __FILE__, __LINE__, 0,
                                  "map requires at least 2 arguments, got %u", argc);
        return NULL;
    }

    ID fn = args[0];
    if (!fn || IS_IMMEDIATE(fn) || !(TAG(fn) == CLJ_FUNC || TAG(fn) == CLJ_CLOSURE))
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "map requires a function as first argument",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    unsigned int ncolls = argc - 1;
    if (ncolls > 8)
    {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "map supports up to 8 collections, got %u", ncolls);
        return NULL;
    }

    // Validate and normalize inputs to sequences (one per coll).
    // If any input is nil, map returns empty.
    CljPersistentVector *seqs = make_vector(ncolls, STRONG);
    if (!seqs) return NULL;

    for (unsigned int i = 0; i < ncolls; i++) {
        ID coll = args[i + 1];
        if (!coll || IS_IMMEDIATE(coll)) {
            return empty_list();
        }
        if (!is_seqable(coll)) {
            throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "map expects seqable collections",
                            __FILE__, __LINE__, 0);
            return NULL;
        }
        // Store the collection itself. The thunk executor will call seq/first/rest lazily.
        vector_conj_inplace(&seqs, coll);
    }

    CljPersistentMap *state = map_empty();
    map_assoc_inplace(&state, SYM_MAP_FN, fn);
    map_assoc_inplace(&state, SYM_MAP_SEQS, seqs);
    RELEASE(seqs);

    CljPersistentVector *env_stack = make_vector(1, STRONG);
    if (!env_stack) { RELEASE(state); return empty_list(); }
    vector_conj_inplace(&env_stack, state);
    RELEASE(state);

    ID fn_obj = cached_named_func(native_map_thunk_executor, SYM_MAP_THUNK_FN, &g_map_thunk_fn_obj);
    ID thunk_body = make_thunk_body_ast_call(fn_obj);
    if (!thunk_body) { RELEASE(env_stack); return empty_list(); }
    ID thunk_param = SYM_THUNK_STATE;
    CljFunction *thunk = make_function(&thunk_param, 1, thunk_body, env_stack, NULL, NULL);
    RELEASE(thunk_body);
    CljLazySeq *lazy = make_lazy_seq(thunk);
    if (lazy && vector_count(env_stack) > 0) lazy->thunk_state = RETAIN(vector_nth(env_stack, 0));
    RELEASE(thunk);
    RELEASE(env_stack);
    return lazy ? AUTORELEASE(lazy) : empty_list();
}

ID native_mapcat(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 2, "mapcat"))
        return NULL;

    ID fn = args[0];
    ID coll = args[1];

    if (!fn || IS_IMMEDIATE(fn) || !(TAG(fn) == CLJ_FUNC || TAG(fn) == CLJ_CLOSURE)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "mapcat requires a function as first argument",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    if (!coll || IS_IMMEDIATE(coll)) {
        return empty_list();
    }
    if (!is_seqable(coll)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "mapcat expects a seqable collection",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    CljPersistentMap *state = map_empty();
    map_assoc_inplace(&state, SYM_MAPCAT_FN, fn);
    map_assoc_inplace(&state, SYM_MAPCAT_COLL, coll);
    map_assoc_inplace(&state, SYM_MAPCAT_INNER, NULL);

    ID fn_obj = cached_named_func(native_mapcat_thunk_executor, SYM_MAPCAT_THUNK_FN, &g_mapcat_thunk_fn_obj);
    CljLazySeq *lazy = make_lazy_seq(fn_obj);
    if (!lazy) {
        RELEASE(state);
        return empty_list();
    }
    lazy->thunk_state = state; // ownership transfer
    return lazy ? AUTORELEASE(lazy) : empty_list();
}

// filter: returns a list of items in coll where (pred item) is truthy
ID native_filter(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 2, "filter"))
        return NULL;

    ID pred = args[0];
    ID coll = args[1];

    if (!pred || IS_IMMEDIATE(pred) || !(TAG(pred) == CLJ_FUNC || TAG(pred) == CLJ_CLOSURE))
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "filter requires a function as first argument",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    if (!coll || IS_IMMEDIATE(coll))
    {
        return empty_list();
    }

    if (!is_seqable(coll))
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "filter expects a seqable collection",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    EvalState *st = builtin_get_eval_state();
    if (!st)
        st = get_global_eval_state();

    SeqIterator iter;
    if (!seq_iter_init(&iter, coll))
    {
        return empty_list();
    }

    CljPersistentVector *kept = make_vector(0, STRONG);
    if (!kept)
        return NULL;

    while (!seq_iter_empty(&iter))
    {
        ID elem = seq_iter_first(&iter);
        ID pred_result = eval_function_call(pred, &elem, 1, NULL, st);

        if (pred_result && pred_result != clj_false)
        {
            vector_conj_inplace(&kept, elem);
        }

        seq_iter_next(&iter);
    }

    unsigned int n = vector_count(kept);
    CljList *out = NULL;
    for (int i = (int)n - 1; i >= 0; i--)
    {
        ID v = vector_nth(kept, (unsigned int)i);
        CljList *next_out = make_list(v, out);
        RELEASE(out);
        out = next_out;
    }
    RELEASE(kept);
    return out ? AUTORELEASE(out) : empty_list();
}

// last: returns the last element of coll, or nil if empty
ID native_last(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 1, "last"))
        return NULL;

    ID coll = args[0];
    if (!coll || IS_IMMEDIATE(coll))
        return NULL;

    // Fast path: vector O(1) (including transient via backing)
    CljType tag = TAG(coll);
    if (tag == CLJ_VECTOR_PERSISTENT || tag == CLJ_VECTOR_TRANSIENT)
    {
        CljPersistentVector *v = as_vector(coll);
        unsigned int n = vector_count(v);
        if (n == 0)
            return NULL;
        return vector_nth(v, n - 1);
    }

    if (!is_seqable(coll))
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "last expects a seqable collection",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    SeqIterator iter;
    if (!seq_iter_init(&iter, coll))
        return NULL;

    ID last = NULL;
    while (!seq_iter_empty(&iter))
    {
        last = seq_iter_first(&iter);
        seq_iter_next(&iter);
    }

    return last;
}

// some: Returns first truthy value from predicate applied to collection
ID native_some(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 2, "some"))
        return NULL;

    ID pred = args[0];
    ID coll = args[1];

    if (!coll || IS_IMMEDIATE(coll))
        return NULL;

    EvalState *st = g_current_eval_state;
    if (!st)
    {
        throw_exception(EXCEPTION_RUNTIME, "some requires EvalState",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    SeqIterator iter;
    if (!seq_iter_init(&iter, coll))
        return NULL;

    while (!seq_iter_empty(&iter))
    {
        ID elem = seq_iter_first(&iter);
        ID result = eval_function_call(pred, &elem, 1, NULL, st);

        if (result && result != clj_false)
            return result;

        seq_iter_next(&iter);
    }

    return NULL;
}

// Cons function that works with BuiltinFn signature
ID native_cons(ID *args, unsigned int argc)
{
    CLJ_ASSERT(args != NULL);

    if (!validate_builtin_args(argc, 2, "cons"))
        return NULL;

    ID elem = args[0];
    ID coll = args[1];

    if (!elem)
        elem = NULL;

    // nil tail -> singleton list
    if (!coll)
    {
        CljList *result = make_list(elem, NULL);
        return AUTORELEASE(result);
    }

    // Proper list-like tails (lists and parsed AST lists)
    if (is_list_type(TAG(coll)))
    {
        CljList *list = as_list(coll);
        // Treat empty list singleton like nil for cons
        if (list_empty(list))
        {
            CljList *result = make_list(elem, NULL);
            return AUTORELEASE(result);
        }
        CljList *result = make_list(elem, list);
        return AUTORELEASE(result);
    }

    // Other seqables: return a seq with elem prepended.
    // We represent this as a realized LazySeq (no thunk): first is elem and
    // cached_rest is a seq over coll (or nil if coll is empty).
    if (is_seqable(coll))
    {
        // Build a strong tail reference.
        // - Existing seq/lazy-seq: retain the argument (caller may release after the call).
        // - Other seqables: create a new seq wrapper (rc=1) and transfer ownership.
        ID tail = NULL;
        unsigned char tag = TAG(coll);
        if (tag == CLJ_SEQ || tag == CLJ_LAZY_SEQ)
        {
            tail = RETAIN(coll);
        }
        else
        {
            tail = make_seq(coll);
        }

        CljLazySeq *lazy = ALLOC(CljLazySeq, 1);

        lazy->base.type = CLJ_LAZY_SEQ;
        lazy->base.flags = 0;

        // Preserve nil elements using SYM_NIL internally.
        lazy->first = RETAIN(elem ? elem : SYM_NIL);
        lazy->thunk = NULL;
        lazy->cached_rest = tail; // already a strong ref (or NULL)

        return AUTORELEASE(lazy);
    }

    // Non-seqable tail: fall back to singleton list (historical behavior)
    CljList *result = make_list(elem, NULL);
    return AUTORELEASE(result);
}

// List function that creates a list from its arguments
ID native_list(ID *args, unsigned int argc)
{
    CLJ_ASSERT(args != NULL);

    if (argc == 0) return empty_list();

    // Build list backwards; return pool-safe like other builtins.
    CljList *head = NULL;
    for (int i = argc - 1; i >= 0; i--) {
        CljList *node = make_list(args[i], head);
        RELEASE(head);
        head = node;
    }
    return AUTORELEASE(head);
}

ID native_reduce(ID *args, unsigned int argc)
{
    if (argc != 2 && argc != 3)
    {
        char error_msg[256];
        size_t pos = 0;
        pos = format_append(error_msg, pos, sizeof(error_msg),
                            "reduce requires exactly 2 or 3 arguments, got ");
        pos = format_append_uint(error_msg, pos, sizeof(error_msg), argc);
        throw_exception(EXCEPTION_ARITY, error_msg, __FILE__, __LINE__, 0);
    }

    EvalState *st = g_current_eval_state;
    CLJ_ASSERT(st && "reduce requires EvalState");

    ID fn = args[0];
    bool has_init = (argc == 3);
    ID acc = has_init ? args[1] : NULL;
    ID coll = args[has_init ? 2 : 1];
    bool acc_owned = false;

    SeqIterator iter;
    bool has_seq = false;
    if (coll && !IS_IMMEDIATE(coll))
    {
        has_seq = seq_iter_init(&iter, coll);
    }

    if (!has_seq || seq_iter_empty(&iter))
    {
        if (has_init)
        {
            return acc;
        }
        return eval_function_call(fn, NULL, 0, NULL, st);
    }

    if (!has_init)
    {
        acc = seq_iter_first(&iter);
        acc_owned = false;
        seq_iter_next(&iter);
    }

    while (!seq_iter_empty(&iter))
    {
        ID current = seq_iter_first(&iter);
        ID call_args[2] = {acc, current};
        ID new_acc = eval_function_call(fn, call_args, 2, NULL, st);
        if (new_acc && !IS_IMMEDIATE(new_acc)) {
            RETAIN(new_acc);
        }
        if (acc_owned)
            RELEASE(acc);
        acc = new_acc;
        acc_owned = (new_acc && !IS_IMMEDIATE(new_acc));
        seq_iter_next(&iter);
    }

    return acc;
}

// group-by: group elements of coll by f
ID native_group_by(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 2, "group-by"))
        return NULL;

    ID fn = args[0];
    ID coll = args[1];

    if (!fn || IS_IMMEDIATE(fn) || !(TAG(fn) == CLJ_FUNC || TAG(fn) == CLJ_CLOSURE)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "group-by requires a function as first argument",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    // Empty coll -> empty map
    if (!coll) {
        return make_map(4);
    }

    SeqIterator iter;
    if (!seq_iter_init(&iter, coll) || seq_iter_empty(&iter)) {
        return make_map(4);
    }

    EvalState *st = builtin_get_eval_state();
    if (!st) st = get_global_eval_state();

    CljPersistentMap *result = make_map(8);
    if (!result) return NULL;

    while (!seq_iter_empty(&iter)) {
        ID elem = seq_iter_first(&iter);
        ID key = eval_function_call(fn, &elem, 1, NULL, st);

        ID existing = map_get_sentinel(result, key, NULL);
        CljPersistentVector *vec = NULL;
        if (!existing) {
            vec = make_vector(0, STRONG);
        } else {
            vec = as_vector(existing);
        }
        if (!vec) {
            RELEASE(result);
            return NULL;
        }

        CljPersistentVector *new_vec = vector_conj(vec, elem);
        if (!new_vec) {
            RELEASE(result);
            return NULL;
        }

        ASSIGN(result, map_assoc(result, key, new_vec));
        if (!existing) {
            RELEASE(vec);
        }

        seq_iter_next(&iter);
    }

    return result;
}

// nil? function that checks if a value is nil
ID native_nilp(ID *args, unsigned int argc)
{
    CLJ_ASSERT(args != NULL);

    if (!validate_builtin_args(argc, 1, "nil?"))
        return NULL;

    // nil is represented as NULL in tiny-clj
    // Return true if argument is NULL, false otherwise
    if (!args[0])
    {
        return clj_true;
    }

    return clj_false;
}

// Reverse function that reverses any seqable collection
ID native_reverse(ID *args, unsigned int argc)
{
    CLJ_ASSERT(args != NULL);

    if (!validate_builtin_args(argc, 1, "reverse"))
        return NULL;

    ID coll = args[0];

    // Handle nil/empty
    if (!coll)
    {
        return empty_list();
    }

    // Use SeqIterator for all seqable types (lists, vectors, strings, etc.)
    SeqIterator iter;
    if (!seq_iter_init(&iter, coll))
    {
        return empty_list();
    }

    // Build reversed list by consing elements to front
    CljList *result = NULL;
    while (!seq_iter_empty(&iter))
    {
        ID elem = seq_iter_first(&iter);
        // nil elements are preserved (no if check)
        result = make_list(elem, result);
        seq_iter_next(&iter);
    }

    return result ? AUTORELEASE(result) : empty_list();
}

ID assoc3(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 3, "assoc"))
        return NULL;
    ID coll = args[0];
    ID key = args[1];
    ID val = args[2];

    if (!coll)
        return NULL;

    unsigned char coll_tag = TAG(coll);

    // Handle vectors
    if (coll_tag == CLJ_VECTOR_PERSISTENT)
    {
        if (!key || TAG(key) != CLJ_INT)
            return NULL;
        int i = AS_FIXNUM(key);
        CljPersistentVector *v = as_vector(coll);
        if (i < 0 || (unsigned int)i >= vector_count(v))
            return NULL;
        // Use COW-based vector_assoc (automatically handles RC=1 in-place, RC>1 COW)
        CljPersistentVector *result = vector_assoc(coll, i, val);
        if (!result)
            return NULL;
        return result;
    }

    // Handle maps
    if (coll_tag == CLJ_MAP_PERSISTENT || coll_tag == CLJ_MAP_TRANSIENT)
    {
        // Note: key can be NULL (nil) - that's a valid key in Clojure!
        return map_assoc(coll, key, val);
    }

    // Unsupported collection type
    return NULL;
}

// assoc: multiple kv pairs (Clojure semantics)
ID native_assoc(ID *args, unsigned int argc)
{
    if (argc < 3 || (argc % 2) == 0)
    {
        char error_msg[160];
        size_t pos = 0;
        pos = format_append(error_msg, pos, sizeof(error_msg), "assoc requires an odd number of arguments >= 3, got ");
        pos = format_append_uint(error_msg, pos, sizeof(error_msg), argc);
        (void)pos;
        throw_exception(EXCEPTION_ARITY, error_msg, __FILE__, __LINE__, 0);
    }

    ID result = args[0];

    for (unsigned int i = 1; i + 1 < argc; i += 2)
    {
        ID key = args[i];
        ID val = args[i + 1];

        // Clojure semantics: (assoc nil k v ...) => start from empty map.
        if (!result)
        {
            result = make_map(4);
            if (!result)
                return NULL;
        }

        ID tmp_args[3] = {result, key, val};
        result = assoc3(tmp_args, 3);
        if (!result)
            return NULL;
    }

    return AUTORELEASE(result);
}

// dissoc: Remove keys from map (supports multiple keys like Clojure)
ID native_dissoc(ID *args, unsigned int argc)
{
    // dissoc requires at least 1 argument (the map)
    if (argc < 1)
    {
        throw_exception(EXCEPTION_ARITY,
                        "dissoc requires at least 1 argument (map), got 0",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    ID map = args[0];
    if (!map)
        return NULL;

    unsigned char map_tag = TAG(map);
    // Only support maps
    if (map_tag != CLJ_MAP_PERSISTENT && map_tag != CLJ_MAP_TRANSIENT)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "dissoc only works on maps",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    // If no keys to remove, return the map as-is
    if (argc == 1)
    {
        return map;
    }

    // Remove keys one by one (Clojure semantics: multiple keys supported)
    CljPersistentMap *result = map;
    bool result_owned = false;
    for (unsigned int i = 1; i < argc; i++)
    {
        ID key = args[i];
        if (!key)
            continue; // Skip NULL keys

        CljPersistentMap *new_result = map_remove(result, key);
        if (new_result != result)
        {
            // map_remove returns pool-managed maps on replacement path;
            // retain while we keep it across loop iterations.
            RETAIN(new_result);
            if (result_owned)
            {
                RELEASE(result);
            }
            result = new_result;
            result_owned = true;
        }
    }

    // If we retained an intermediate/new map, return it pool-managed.
    if (result_owned)
    {
        AUTORELEASE(result);
    }
    return result;
}

// disj: Remove keys from set (supports multiple keys like Clojure)
ID native_disj(ID *args, unsigned int argc)
{
    if (argc < 1)
    {
        throw_exception(EXCEPTION_ARITY,
                        "disj requires at least 1 argument (set), got 0",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    ID set_obj = args[0];
    if (!set_obj)
        return NULL;

    if (TAG(set_obj) != CLJ_HASHSET)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "disj only works on sets",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    if (argc == 1)
    {
        return set_obj;
    }

    CljHashSet *result = (CljHashSet*)set_obj;
    for (unsigned int i = 1; i < argc; i++)
    {
        ID key = args[i];
        CljHashSet *new_result = hashset_remove(result, key);
        if (new_result != result)
        {
            if (i > 1 || result != set_obj)
            {
                RELEASE(result);
            }
            result = new_result;
        }
    }

    return result;
}

// merge: Combines multiple maps (later maps override earlier ones)
// Usage: (merge) (merge m1) (merge m1 m2) (merge m1 m2 m3 ...)
ID native_merge(ID *args, unsigned int argc)
{
    // (merge) with no args returns nil
    if (argc == 0)
        return NULL;

    // (merge nil) returns nil
    if (argc == 1 && !args[0])
        return NULL;

    // Start with first non-nil map
    CljPersistentMap *result = NULL;
    unsigned int start_idx = 0;

    for (unsigned int i = 0; i < argc; i++)
    {
        if (args[i])
        {
            unsigned char tag = TAG(args[i]);
            if (tag == CLJ_MAP_PERSISTENT || tag == CLJ_MAP_TRANSIENT)
            {
                result = args[i];
                start_idx = i + 1;
                break;
            }
        }
    }

    // All arguments were nil
    if (!result)
        return NULL;

    // Merge remaining maps
    for (unsigned int i = start_idx; i < argc; i++)
    {
        ID m = args[i];
        if (!m)
            continue; // Skip nil

        unsigned char m_tag = TAG(m);
        if (m_tag != CLJ_MAP_PERSISTENT && m_tag != CLJ_MAP_TRANSIENT)
        {
            throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                            "merge only works on maps",
                            __FILE__, __LINE__, 0);
            return NULL;
        }

        CljPersistentMap *new_result = map_merge(result, m, true); // overwrite=true
        if (new_result != result && i > start_idx)
        {
            // Don't release original arg
        }
        result = new_result;
    }

    return result;
}

// contains?: Check if collection contains key
// Usage: (contains? coll key)
ID native_contains_p(ID *args, unsigned int argc)
{
    if (argc != 2)
    {
        throw_exception_formatted(EXCEPTION_ARITY, __FILE__, __LINE__, 0,
                                  "contains? expects 2 arguments, got %u", argc);
        return NULL;
    }

    ID coll = args[0];
    ID key = args[1];

    // nil collection returns false
    if (!coll)
        return clj_false;

    CljType tag = TAG(coll);
    switch (tag)
    {
    case CLJ_MAP_PERSISTENT:
    case CLJ_MAP_TRANSIENT:
        return map_contains(coll, key) ? clj_true : clj_false;

    case CLJ_VECTOR_PERSISTENT:
    case CLJ_VECTOR_TRANSIENT:{
        // For vectors, contains? checks if the index exists (not the value).
        // In Clojure: (contains? [3 4 5] 0) => true (index 0 exists)
        //            (contains? [3 4 5] 3) => false (index 3 doesn't exist, only 0,1,2)
        // Key must be an integer index (fixnum or CLJ_FIXNUM)
        if (TAG(key) != CLJ_FIXNUM)
            return clj_false;
        long idx = as_fixnum(key);
        unsigned int count = vector_count(coll);
        return (idx >= 0 && (unsigned long)idx < count) ? clj_true : clj_false;
    }

    case CLJ_HASHSET:
        return hashset_contains((CljHashSet*)coll, key) ? clj_true : clj_false;

    default:
        return clj_false;
    }
}

// update: Apply function to value at key
// Usage: (update m k f) (update m k f arg1) (update m k f arg1 arg2 ...)
ID native_update(ID *args, unsigned int argc)
{
    if (argc < 3)
    {
        throw_exception_formatted(EXCEPTION_ARITY, __FILE__, __LINE__, 0,
                                  "update expects at least 3 arguments, got %u", argc);
        return NULL;
    }

    EvalState *st = g_current_eval_state;
    CLJ_ASSERT(st && "update requires EvalState");

    ID coll = args[0];
    ID key = args[1];
    ID func = args[2];

    // nil collection returns nil
    if (!coll)
        return NULL;

    CljType tag = TAG(coll);
    if (tag != CLJ_MAP_PERSISTENT && tag != CLJ_MAP_TRANSIENT)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "update only works on maps",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    // Get current value (nil if not found)
    ID current_val = map_get_sentinel(coll, key, NULL);

    // Build function call args: [current_val, extra_args...]
    unsigned int fn_argc = 1 + (argc - 3);
    ID fn_args[fn_argc];
    fn_args[0] = current_val;
    for (unsigned int i = 3; i < argc; i++)
    {
        fn_args[i - 2] = args[i];
    }

    // Call the function
    ID new_val = eval_function_call(func, fn_args, fn_argc, NULL, st);

    // assoc the new value
    return map_assoc(coll, key, new_val);
}

// into: Add all items from source into target collection
// Usage: (into to from) (into to xform from)
ID native_into(ID *args, unsigned int argc)
{
    if (argc < 2 || argc > 3)
    {
        throw_exception_formatted(EXCEPTION_ARITY, __FILE__, __LINE__, 0,
                                  "into expects 2 or 3 arguments, got %u", argc);
        return NULL;
    }

    // TODO: Support transducers (3-arg version)
    if (argc == 3)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "into with transducer not yet implemented",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    ID to = args[0];
    ID from = args[1];

    // nil source returns target unchanged
    if (!from)
        return to;

    // nil target - create appropriate empty collection based on source type
    if (!to)
    {
        // Default to vector if target is nil
        to = make_vector(0, STRONG);
    }

    CljType to_tag = TAG(to);

    // Handle vector target
    if (to_tag == CLJ_VECTOR_PERSISTENT || to_tag == CLJ_VECTOR_TRANSIENT)
    {
    CljPersistentVector *result = to;

        // Iterate over source
        CljType from_tag = TAG(from);
        if (from_tag == CLJ_VECTOR_PERSISTENT || from_tag == CLJ_VECTOR_TRANSIENT)
        {
            VECTOR_FOR_EACH(from, elem)
            {
                result = vector_conj(result, elem);
            }
        }
        else if (from_tag == CLJ_MAP_PERSISTENT || from_tag == CLJ_MAP_TRANSIENT)
        {
            // Map entries become [k v] vectors
            CljPersistentMap *m = from;
            for (int i = 0; i < m->capacity; i++)
            {
                ID key = KV_KEY(m->data, i);
                if (key)
                {
                    ID val = KV_VALUE(m->data, i);
                    CljPersistentVector *entry = make_vector(2, STRONG);
                    entry = vector_conj(entry, key);
                    entry = vector_conj(entry, val);
                    result = vector_conj(result, entry);
                }
            }
        }
        else if (from_tag == CLJ_LIST || from_tag == CLJ_AST_NODE)
        {
            // Iterate over list (CLJ_LIST or CLJ_AST_NODE - both use same structure)
            CljList *list = from;
            LIST_FOR_EACH(list, elem)
            {
                result = vector_conj(result, elem);
            }
        }

        return result;
    }

    // Handle map target
    if (to_tag == CLJ_MAP_PERSISTENT || to_tag == CLJ_MAP_TRANSIENT)
    {
        CljPersistentMap *result = to;

        CljType from_tag = TAG(from);
        if (from_tag == CLJ_MAP_PERSISTENT || from_tag == CLJ_MAP_TRANSIENT)
        {
            // Merge maps
            result = map_merge(result, from, true);
        }
        else if (from_tag == CLJ_VECTOR_PERSISTENT || from_tag == CLJ_VECTOR_TRANSIENT)
        {
            // Vector of [k v] pairs
            VECTOR_FOR_EACH(from, entry)
            {
                unsigned char entry_tag = entry ? TAG(entry) : 0;
                if (entry_tag == CLJ_VECTOR_PERSISTENT || entry_tag == CLJ_VECTOR_TRANSIENT)
                {
                    CljPersistentVector *pair = entry;
                    if (vector_count(pair) >= 2)
                    {
                        ASSIGN(result, map_assoc(result, vector_nth(pair, 0), vector_nth(pair, 1)));
                    }
                }
            }
        }

        return result;
    }

    throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                    "into: unsupported target collection type",
                    __FILE__, __LINE__, 0);
    return NULL;
}

// select-keys: Return map with only specified keys
// Usage: (select-keys map [key1 key2 ...])
ID native_select_keys(ID *args, unsigned int argc)
{
    if (argc != 2)
    {
        throw_exception_formatted(EXCEPTION_ARITY, __FILE__, __LINE__, 0,
                                  "select-keys expects 2 arguments, got %u", argc);
        return NULL;
    }

    ID m = args[0];
    ID keys = args[1];

    // nil map returns empty map
    if (!m)
        return map_empty();

    CljType tag = TAG(m);
    if (tag != CLJ_MAP_PERSISTENT && tag != CLJ_MAP_TRANSIENT)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "select-keys first argument must be a map",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    // nil keys returns empty map
    if (!keys)
        return map_empty();

    CljType keys_tag = TAG(keys);
    if (keys_tag != CLJ_VECTOR_PERSISTENT && keys_tag != CLJ_VECTOR_TRANSIENT &&
        keys_tag != CLJ_LIST && keys_tag != CLJ_AST_NODE)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "select-keys second argument must be a sequence",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    CljPersistentMap *result = map_empty();
    CljPersistentMap *source = m;

    if (keys_tag == CLJ_VECTOR_PERSISTENT || keys_tag == CLJ_VECTOR_TRANSIENT)
    {
        VECTOR_FOR_EACH(keys, key)
        {
            if (map_contains(source, key))
            {
                ID val = map_get(source, key);
                ASSIGN(result, map_assoc(result, key, val));
            }
        }
    }
    else if (keys_tag == CLJ_LIST || keys_tag == CLJ_AST_NODE)
    {
        CljList *list = keys;
        LIST_FOR_EACH(list, key)
        {
            if (map_contains(source, key))
            {
                ID val = map_get(source, key);
                ASSIGN(result, map_assoc(result, key, val));
            }
        }
    }

    return result;
}

// find: Return [key value] entry for key, or nil if not found
// Usage: (find map key)
ID native_find(ID *args, unsigned int argc)
{
    if (argc != 2)
    {
        throw_exception_formatted(EXCEPTION_ARITY, __FILE__, __LINE__, 0,
                                  "find expects 2 arguments, got %u", argc);
        return NULL;
    }

    ID m = args[0];
    ID key = args[1];

    // nil map returns nil
    if (!m)
        return NULL;

    CljType tag = TAG(m);
    if (tag != CLJ_MAP_PERSISTENT && tag != CLJ_MAP_TRANSIENT)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "find first argument must be a map",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    CljPersistentMap *map = m;

    if (!map_contains(map, key))
    {
        return NULL; // Key not found
    }

    ID val = map_get(map, key);

    // Return [key value] vector
    CljPersistentVector *entry = make_vector(2, STRONG);
    entry = vector_conj(entry, key);
    entry = vector_conj(entry, val);

    return entry;
}

// Transient functions
ID native_transient(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 1, "transient"))
        return NULL;

    ID coll = args[0];
    if (!coll)
        return NULL;

    uint16_t tag = TAG(coll);
    switch (tag)
    {
    case CLJ_VECTOR_PERSISTENT:
        return AUTORELEASE(vector_transient(as_persistent_vector(coll)));
    case CLJ_MAP_PERSISTENT:
        return map_transient(as_persistent_map(coll));
    case CLJ_VECTOR_TRANSIENT:
    case CLJ_MAP_TRANSIENT:
        // transient on transient returns the same object
        return coll;
    default:
        break;
    }

    // Throw exception for unsupported collection type
    throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                    "transient requires a collection at position 1",
                    __FILE__, __LINE__, 0);
    return NULL;
}

ID native_persistent_bang(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 1, "persistent!"))
        return NULL;

    ID coll = args[0];
    if (!coll)
        return NULL;

    uint16_t tag = TAG(coll);
    switch (tag)
    {
    case CLJ_VECTOR_TRANSIENT:
        return vector_persistent(coll);
    case CLJ_MAP_TRANSIENT:
        return map_persistent(as_transient_map(coll));
    case CLJ_VECTOR_PERSISTENT:
    case CLJ_MAP_PERSISTENT:
        // persistent! on persistent returns the same object
        return coll;
    default:
        break;
    }

    // Throw exception for unsupported collection type
    throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                    "persistent! requires a transient collection at position 1",
                    __FILE__, __LINE__, 0);
    return NULL;
}

ID native_conj_bang(ID *args, unsigned int argc)
{
    if (argc < 2)
        return (NULL);

    ID coll = args[0];
    if (!coll)
        return NULL;

    int tag = TAG(coll);
    if (tag == CLJ_VECTOR_TRANSIENT)
    {
        CljPersistentVector *result = coll;
        // vector_conj automatically handles transient vectors correctly (always in-place)
        for (unsigned int i = 1; i < argc; i++)
        {
            result = vector_conj(result, args[i]);
            if (!result)
                return NULL;
        }
        return result;
    }
    else if (tag == CLJ_MAP_TRANSIENT)
    {
        if (argc != 3)
            return NULL; // conj! for maps needs key-value pair
        map_conj(as_transient_map(coll), args[1], args[2]);
        return coll;
    }

    // Throw exception for unsupported collection type
    throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                    "conj! requires a transient collection at position 1",
                    __FILE__, __LINE__, 0);
    return NULL;
}

ID native_get(ID *args, unsigned int argc)
{
    if (argc < 2 || argc > 3)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "get requires 2 or 3 arguments",
                        __FILE__, __LINE__, 0);
        return NULL;
    }
    ID map = args[0];
    ID key_obj = args[1];
    ID not_found = argc == 3 ? args[2] : NULL;
    // Note: key can be NULL (nil) - that's a valid key in Clojure!
    if (!map)
        return NULL;

    // Convert SYM_NIL to NULL for key lookup
    ID key = (key_obj && TAG(key_obj) == CLJ_SYMBOL && key_obj == SYM_NIL)
                 ? NULL
                 : key_obj;

    int tag = TAG(map);
    if (tag == CLJ_MAP_PERSISTENT || tag == CLJ_MAP_TRANSIENT)
    {
        return map_get_sentinel(map, key, not_found);
    }

    return not_found ? not_found : NULL; // Return not_found or nil for unsupported types
}

ID native_count(ID *args, unsigned int argc)
{
    CLJ_ASSERT(args != NULL);

    if (!validate_builtin_args(argc, 1, "count"))
        return NULL;
    ID coll = args[0];
    // Clojure behavior: (count nil) => 0
    if (!coll)
    {
        return fixnum(0);
    }

    int tag = TAG(coll);

    // Handle CLJ_SEQ (sequences from rest, etc.)
    if (tag == CLJ_SEQ)
    {
        return fixnum(seq_count(coll));
    }

    if (coll)
    {
        if (tag == CLJ_MAP_PERSISTENT || tag == CLJ_MAP_TRANSIENT)
        {
            return (fixnum(map_count(coll)));
        }
        else if (tag == CLJ_VECTOR_PERSISTENT || tag == CLJ_VECTOR_TRANSIENT)
        {
            CljPersistentVector *vec = as_vector(coll);
            return (fixnum(vec ? vector_count(vec) : 0));
        }
        else if (is_list_type(tag))
        {
            CljList *list = as_list(coll);
            return (fixnum(list ? list_count(list) : 0));
        }
        else if (tag == CLJ_HASHSET)
        {
            return fixnum((int)hashset_count((CljHashSet*)coll));
        }
        else if (tag == CLJ_STRING)
        {
            CljString *str = (CljString *)coll;

            // Return string length directly
            return fixnum(str->length);
        }
    }

    return (fixnum(0)); // Default count for unsupported types
}

ID native_keys(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 1, "keys"))
        return NULL;
    ID map = args[0];
    if (!map)
        return (NULL);

    int tag = TAG(map);
    if (tag == CLJ_MAP_PERSISTENT || tag == CLJ_MAP_TRANSIENT)
    {
        return map_keys(map);
    }

    return NULL; // Return nil for unsupported types
}

ID native_vals(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 1, "vals"))
        return NULL;
    ID map = args[0];
    if (!map)
        return (NULL);

    int tag = TAG(map);
    if (tag == CLJ_MAP_PERSISTENT || tag == CLJ_MAP_TRANSIENT)
    {
        return map_vals(map);
    }

    return NULL; // Return nil for unsupported types
}

ID native_type(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 1, "type"))
        return NULL;
    CljValue val = args[0];
    if (!val)
        return SYM_NIL;

    // Get the tag to determine immediate vs heap object
    uint8_t tag = get_tag(val);

    // Return namespace-qualified symbols in clojure.lang namespace
    // Switch on tag for immediate values
    switch (tag)
    {
    case TAG_FIXNUM:
        return intern_symbol(SYM_CLOJURE_LANG, "Long");
    case TAG_CHAR:
        return intern_symbol(SYM_CLOJURE_LANG, "Character");
    case TAG_BOOL:
    {
        int special_type = as_special(val);
        if (special_type == SPECIAL_TRUE || special_type == SPECIAL_FALSE)
        {
            return intern_symbol(SYM_CLOJURE_LANG, "Boolean");
        }
        return intern_symbol(SYM_CLOJURE_LANG, "Special");
    }
    case TAG_FIXED:
        return intern_symbol(SYM_CLOJURE_LANG, "Double");
    case TAG_POINTER:
        // Heap object - continue to object type switch
        break;
    default:
        return intern_symbol(SYM_CLOJURE_LANG, "Unknown");
    }

    // Handle heap objects
    CljObject *obj = val;

    // Check for keyword (symbol with ':' prefix)
    if (IS_KEYWORD(obj))
    {
        return intern_symbol(SYM_CLOJURE_LANG, "Keyword");
    }

    // Switch on object type for heap objects
    switch (obj->type)
    {
    case CLJ_SYMBOL:
        return intern_symbol(SYM_CLOJURE_LANG, "Symbol");
    case CLJ_STRING:
        return intern_symbol(SYM_CLOJURE_LANG, "String");
    case CLJ_VECTOR_PERSISTENT:
        return intern_symbol(SYM_CLOJURE_LANG, "PersistentVector");
    case CLJ_VECTOR_TRANSIENT:return intern_symbol(SYM_CLOJURE_LANG, "TransientVector");
    case CLJ_MAP_TRANSIENT:
        return intern_symbol(SYM_CLOJURE_LANG, "TransientArrayMap");
    case CLJ_MAP_PERSISTENT:
        return intern_symbol(SYM_CLOJURE_LANG, "PersistentArrayMap");
    case CLJ_LIST:
        return intern_symbol(SYM_CLOJURE_LANG, "PersistentList");
    case CLJ_FUNC:
        return intern_symbol(SYM_CLOJURE_LANG, "IFn");
    case CLJ_CLOSURE:
        return intern_symbol(SYM_CLOJURE_LANG, "IFn");
    case CLJ_EXCEPTION:
        return intern_symbol(SYM_CLOJURE_LANG, "Exception");
    default:
        // Fallback: use type name but still in clojure.lang namespace
        return intern_symbol(SYM_CLOJURE_LANG, clj_type_name(obj->type));
    }
}

ID native_array_map(ID *args, unsigned int argc)
{
    // Must have even number of arguments (key-value pairs)
    if (argc % 2 != 0)
    {
        // Return empty map instead of nil for odd number of args
        return make_map(0);
    }

    // Create map with appropriate capacity
    int pair_count = argc / 2;

    // Handle empty map case specially
    if (pair_count == 0)
    {
        return make_map(0);
    }

    CljPersistentMap *map = make_map(pair_count);

    // Add all key-value pairs
    // CRITICAL: map_assoc may return a new map (COW), so we must use the result
    for (unsigned int i = 0; i < argc; i += 2)
    {
        ID key = args[i];
        ID value = args[i + 1];
        CljPersistentMap *updated_map = map_assoc(map, key, value);
        ASSIGN(map, updated_map);
    }

    return AUTORELEASE(map);
}

ID native_vector(ID *args, unsigned int argc)
{
    // This is the same singleton returned by make_vector(0, STRONG)
    if (argc == 0)
    {
        return vector_empty_singleton; // Returns empty-vector singleton (no memory management needed)
    }

    // Create vector with capacity+1 to avoid COW when adding all elements
    // (vector_conj uses COW when count >= capacity, so we need capacity > argc)
    CljPersistentVector *v = make_vector(argc + 1, STRONG);

    // Add all elements using vector_conj
    for (unsigned int i = 0; i < argc; i++)
    {
        ASSIGN(v, vector_conj(v, args[i]));
    }

    return AUTORELEASE(v);
}

ID native_hash_set(ID *args, unsigned int argc)
{
    CljHashSet *set = make_hashset(argc > 0 ? argc * 2 : 0);
    if (!set) return NULL;

    for (unsigned int i = 0; i < argc; i++) {
        hashset_add_inplace(&set, args[i]);
    }

    return AUTORELEASE(set);
}

// vec: converts a sequence to a vector
// (vec coll) => vector with elements from coll
// If coll is already a vector, returns same vector (No-Op)
ID native_vec(ID *args, unsigned int argc)
{
    // Arity check: vec accepts exactly 1 argument
    if (!validate_builtin_args(argc, 1, "vec"))
        return NULL;

    ID coll = args[0];

    // If nil, return empty vector singleton (Clojure behavior: '() is nil, (vec '()) => [])
    // empty_vector() returns singleton - no memory management needed
    if (!coll)
    {
        return empty_vector();
    }

    // If already a vector, return same object (No-Op - Clojure behavior).
    // Eval path values are already pool-safe; do not AUTORELEASE again here.
    if (TAG(coll) == CLJ_VECTOR_PERSISTENT)
    {
        return coll;
    }

    // Check if collection is seqable
    if (!is_seqable(coll))
    {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "vec requires a seqable collection");
        return NULL;
    }

    // Use stack-based iterator to iterate through collection (avoid code duplication)
    // More efficient than heap-based make_seq and avoids extra allocations
    SeqIterator iter;
    if (!seq_iter_init(&iter, coll))
    {
        // Empty collection - return empty vector singleton (Clojure behavior: (vec '()) => [])
        // empty_vector() returns singleton - no memory management needed
        return empty_vector();
    }

    // Check if iterator is empty (using singleton pattern)
    if (seq_iter_empty(&iter))
    {
        return empty_vector(); // Returns singleton - no memory management needed
    }

    // Create vector with default capacity (vector_conj will grow automatically)
    // make_vector throws OOM exception or returns valid object
    CljPersistentVector *vec = make_vector(4, STRONG);
    if (!vec)
    {
        throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                  "Failed to create vector");
        return NULL;
    }

    // Iterate through sequence and add elements using vector_conj (reuse existing logic)
    // This avoids code duplication and reuses COW-based vector_conj.
    // Note: nil (NULL) is a valid value in Clojure collections
    while (!seq_iter_empty(&iter))
    {
        ID elem_id = seq_iter_first(&iter);
        // Use vector_conj_owned to keep ownership local in this loop.
        // It may return a new vector when growth is needed (COW).
        // Note: vector_conj accepts NULL (nil) as valid element
        // Keep ownership local and avoid extra retains that force rc>1 and COW churn.
        CljPersistentVector *next_vec = vector_conj_owned(vec, elem_id);
        if (!next_vec) {
            RELEASE(vec);
            return NULL;
        }
        if (next_vec != vec) {
            RELEASE(vec);
            vec = next_vec;
        }

        // Move to next element (reuse existing seq_iter_next API)
        seq_iter_next(&iter);
    }

    return AUTORELEASE(vec);
}

// Event-loop: run-next-task builtin
ID native_run_next_task(ID *args, unsigned int argc)
{
    (void)args;
    if (argc != 0)
        return NULL;
    EvalState *st = get_global_eval_state();
    CljPersistentMap *env = NULL;
    bool ran = false;
    TRY
    {
        ran = event_loop_run_next(env, st);
    }
    CATCH(ex)
    {
        // Exception occurred - return false (no task was executed)
        // Don't propagate exception to caller
        ran = false;
    }
    END_TRY
    return ran ? clj_true : clj_false;
}

static CljSymbol *g_timer_kw_id = NULL;
static CljSymbol *g_timer_kw_period_ms = NULL;

static inline CljSymbol *timer_kw_id(void)
{
    if (!g_timer_kw_id) g_timer_kw_id = intern_symbol_global(":id");
    return g_timer_kw_id;
}

static inline CljSymbol *timer_kw_period_ms(void)
{
    if (!g_timer_kw_period_ms) g_timer_kw_period_ms = intern_symbol_global(":period-ms");
    return g_timer_kw_period_ms;
}

static bool parse_timer_fn_or_opts(ID arg,
                                   const char *fn_name,
                                   ID *out_fn_obj,
                                   ID *out_timer_key,
                                   int *out_period_ms,
                                   bool *out_period_from_opts)
{
    if (out_fn_obj) *out_fn_obj = NULL;
    if (out_timer_key) *out_timer_key = NULL;
    if (out_period_ms) *out_period_ms = 0;
    if (out_period_from_opts) *out_period_from_opts = false;
    if (!arg) return false;

    unsigned char arg_tag = TAG(arg);
    if (arg_tag == CLJ_FUNC || arg_tag == CLJ_CLOSURE)
    {
        if (out_fn_obj) *out_fn_obj = arg;
        return true;
    }

    if (arg_tag != CLJ_MAP_PERSISTENT)
    {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "%s expects a function or options map", fn_name);
        return false;
    }

    CljPersistentMap *opts = (CljPersistentMap *)arg;
    ID fn_obj = map_get_sentinel(opts, SYM_KW_FN, NOT_FOUND);
    unsigned char fn_tag = fn_obj ? TAG(fn_obj) : 0;
    if (fn_obj == NOT_FOUND || !fn_obj || (fn_tag != CLJ_FUNC && fn_tag != CLJ_CLOSURE))
    {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "%s options map requires :fn function", fn_name);
        return false;
    }

    if (out_fn_obj) *out_fn_obj = fn_obj;

    ID key_obj = map_get_sentinel(opts, timer_kw_id(), NOT_FOUND);
    if (key_obj != NOT_FOUND && out_timer_key) *out_timer_key = key_obj;

    ID period_obj = map_get_sentinel(opts, timer_kw_period_ms(), NOT_FOUND);
    if (period_obj != NOT_FOUND)
    {
        if (!period_obj || TAG(period_obj) != CLJ_INT)
        {
            throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                      "%s options map :period-ms must be an integer", fn_name);
            return false;
        }
        int period_ms = as_fixnum(period_obj);
        if (period_ms < 0)
        {
            throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                      "%s options map :period-ms must be non-negative", fn_name);
            return false;
        }
        if (out_period_ms) *out_period_ms = period_ms;
        if (out_period_from_opts) *out_period_from_opts = true;
    }

    return true;
}

// Timer: schedule builtin - schedule timer (one-shot by default, periodic via opts :period-ms)
ID native_schedule(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 2, "schedule"))
        return NULL;

    // First argument: delay in milliseconds (must be integer)
    ID delay_obj = args[0];
    if (!delay_obj || TAG(delay_obj) != CLJ_INT)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "schedule delay must be an integer",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    int delay_ms = as_fixnum(delay_obj);
    if (delay_ms < 0)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "schedule delay must be non-negative",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    ID fn_obj = NULL;
    ID timer_key = NULL;
    int period_ms = 0;
    bool period_from_opts = false;
    if (!parse_timer_fn_or_opts(args[1], "schedule", &fn_obj, &timer_key, &period_ms, &period_from_opts))
        return NULL;
    bool periodic = period_from_opts && period_ms > 0;

    // CRITICAL: Retain the function before passing to timer_enqueue
    // The function may be in an autorelease pool that will be popped when this function returns
    // timer_enqueue will retain it again, but we need to ensure it survives until then
    RETAIN(fn_obj);

    if (timer_key) {
        (void)timer_upsert_named(timer_key, fn_obj, (int64_t)delay_ms, periodic, (int64_t)period_ms);
    } else {
        (void)timer_enqueue(fn_obj, (int64_t)delay_ms, periodic, (int64_t)period_ms);
    }

    // schedule returns nil (like go blocks)
    return NULL;
}

// Timer: schedule-periodic builtin - schedule a periodic timer (or named periodic via opts)
ID native_schedule_periodic(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 3, "schedule-periodic"))
        return NULL;

    // First argument: initial delay in milliseconds (must be integer)
    ID delay_obj = args[0];
    if (!delay_obj || TAG(delay_obj) != CLJ_INT)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "schedule-periodic delay must be an integer",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    int delay_ms = as_fixnum(delay_obj);
    if (delay_ms < 0)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "schedule-periodic delay must be non-negative",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    // Second argument: period in milliseconds (must be integer)
    ID period_obj = args[1];
    if (!period_obj || TAG(period_obj) != CLJ_INT)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "schedule-periodic period must be an integer",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    int period_ms = as_fixnum(period_obj);
    if (period_ms <= 0)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "schedule-periodic period must be positive",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    ID fn_obj = NULL;
    ID timer_key = NULL;
    int period_from_opts = 0;
    bool has_period_from_opts = false;
    if (!parse_timer_fn_or_opts(args[2], "schedule-periodic", &fn_obj, &timer_key, &period_from_opts, &has_period_from_opts))
        return NULL;
    if (has_period_from_opts && period_from_opts > 0 && period_from_opts != period_ms)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "schedule-periodic options :period-ms must match period argument",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    // CRITICAL: Retain the function before passing to timer_enqueue
    // The function may be in an autorelease pool that will be popped when this function returns
    // timer_enqueue will retain it again, but we need to ensure it survives until then
    RETAIN(fn_obj);

    int32_t timer_id = 0;
    if (timer_key) {
        timer_id = timer_upsert_named(timer_key, fn_obj, (int64_t)delay_ms, true, (int64_t)period_ms);
    } else {
        timer_id = timer_enqueue(fn_obj, (int64_t)delay_ms, true, (int64_t)period_ms);
    }

    // schedule-periodic returns timer ID as integer
    return timer_id > 0 ? fixnum(timer_id) : NULL;
}

// Timer: cancel-timer builtin - cancel by timer ID or by named key
ID native_cancel_timer(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 1, "cancel-timer"))
        return NULL;

    bool cancelled = false;
    ID cancel_arg = args[0];
    if (cancel_arg && TAG(cancel_arg) == CLJ_INT)
    {
        int timer_id = as_fixnum(cancel_arg);
        if (timer_id <= 0)
        {
            throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                            "cancel-timer timer-id must be positive",
                            __FILE__, __LINE__, 0);
            return NULL;
        }
        cancelled = timer_cancel(timer_id);
    }
    else
    {
        cancelled = timer_cancel_named(cancel_arg);
    }

    // Return true if cancelled, false if not found
    return cancelled ? clj_true : clj_false;
}

// Legacy builtin table and apply_builtin removed - all builtins now use namespace registration

// Arithmetic functions - native_*_variadic implement operations directly (no wrappers)

// ============================================================================
// PRINT HELPER FUNCTION (DRY Principle)
// ============================================================================

// Common helper function for all print functions
static void print_helper(ID *args, unsigned int argc, bool readable, bool newline)
{
    if (argc < 1)
        return;

    // Print all arguments separated by spaces
    for (unsigned int i = 0; i < argc; i++)
    {
        if (args[i])
        {
            CljString *str = readable ? pr_str(args[i]) : print_str(args[i]);
            if (str)
            {
                platform_put_string(NULL, string_data(str));
            }

            // Add space between arguments (except for the last one)
            if (i < argc - 1)
            {
                platform_put_char(NULL, ' ');
            }
        }
    }

    // Add newline if requested
    if (newline)
    {
        platform_put_char(NULL, '\n');
    }
}

// ============================================================================
// NATIVE PRINT FUNCTIONS (using print_helper)
// ============================================================================

ID native_print(ID *args, unsigned int argc)
{
    print_helper(args, argc, false, false); // not readable, no newline
    return NULL;
}

ID native_println(ID *args, unsigned int argc)
{
    print_helper(args, argc, false, true); // not readable, with newline
    return NULL;
}

ID native_pr(ID *args, unsigned int argc)
{
    print_helper(args, argc, true, false); // readable, no newline
    return NULL;
}

ID native_prn(ID *args, unsigned int argc)
{
    print_helper(args, argc, true, true); // readable, with newline
    return NULL;
}

#ifdef DEBUG
ID native_print_ast(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 1, "print-ast");

    ID arg = args[0];
    if (!arg)
    {
        printf("nil\n");
        return NULL;
    }

    const char *ast_str = print_ast(arg);
    if (ast_str)
    {
        printf("%s\n", ast_str);
        // print_ast returns a newly allocated string that must be freed
        CLJ_FREE((void *)ast_str);
    }
    return NULL;
}

ID native_ast_string(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 1, "ast-string");

    ID arg = args[0];
    // Distinguish between NULL (evaluated nil) and SYM_NIL (unevaluated nil symbol)
    if (!arg)
    {
        return make_string("nil"); // NULL = evaluated nil
    }

    // Check if arg is SYM_NIL (unevaluated nil symbol)
    if (arg == SYM_NIL)
    {
        return make_string("SYM:nil");
    }

    const char *ast_str = print_ast(arg);
    if (ast_str)
    {
        CljString *result = make_string(ast_str);
        // print_ast returns a newly allocated string that must be freed
        CLJ_FREE((void *)ast_str);
        return AUTORELEASE(result);
    }

    return make_string("(error: could not generate AST string)");
}
#endif

// ============================================================================
// HELPER FUNCTIONS (DRY Principle)
// ============================================================================

// Helper function to apply saturation to fixed-point values
static int32_t apply_saturation(int32_t acc_fixed)
{
    if (acc_fixed > 268435455)
        acc_fixed = 268435455;
    if (acc_fixed < -268435456)
        acc_fixed = -268435456;
    return acc_fixed;
}

// Helper function to create fixed-point result
static ID create_fixed_result(int32_t acc_fixed)
{
    acc_fixed = apply_saturation(acc_fixed);
    return (ID)(uintptr_t)((acc_fixed << TAG_BITS) | TAG_FIXED);
}

// Helper function to throw arithmetic overflow exceptions (DRY principle)
static ID throw_arithmetic_overflow(const char *err_msg, int a, int b)
{
    throw_exception_formatted(EXCEPTION_ARITHMETIC, __FILE__, __LINE__, 0, err_msg, a, b);
    return NULL;
}

// Helper function to throw fixed-point overflow exceptions
static ID throw_fixed_overflow(const char *err_msg)
{
    throw_exception_formatted(EXCEPTION_ARITHMETIC, __FILE__, __LINE__, 0, err_msg);
    return NULL;
}

static inline bool is_numeric_tag(uint16_t tag)
{
    return tag == CLJ_INT || tag == CLJ_FLOAT;
}

static inline ID throw_expected_number(void)
{
    throw_exception_formatted(EXCEPTION_TYPE, __FILE__, __LINE__, 0, ERR_EXPECTED_NUMBER);
    return NULL;
}

// Helper function to create fixnum result
static ID create_fixnum_result(int acc_i)
{
    return fixnum(acc_i);
}

// Helper function to extract raw fixed-point value
static int32_t extract_fixed_value(ID arg)
{
    return (int32_t)((intptr_t)arg >> TAG_BITS);
}

// Helper function to convert fixnum to fixed-point
static int32_t fixnum_to_fixed(int fixnum)
{
    return fixnum << 13;
}

// ============================================================================
// STRING FUNCTIONS - moved to builtins_strings.c
// ============================================================================

// ============================================================================
// Namespace introspection functions
// ============================================================================

// ns-map: Returns the mappings map of a namespace
// Usage: (ns-map ns-name) or (ns-map 'ns-name)
// Returns a map of all symbols to their values in the namespace
ID native_ns_map(ID *args, unsigned int argc)
{
    CLJ_ASSERT(argc == 1 && "ns-map: arity check failed");
    if (argc != 1)
    {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "ns-map expects exactly 1 argument, got %u", argc);
        return NULL;
    }

    ID ns_arg = args[0];
    if (!ns_arg)
    {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "ns-map: argument must not be nil");
        return NULL;
    }

    CljNamespace *target_ns = NULL;
    int tag = TAG(ns_arg);

    if (tag == CLJ_SYMBOL)
    {
        target_ns = ns_find_by_symbol(as_symbol(ns_arg));
    }
    else if (tag == CLJ_STRING)
    {
        target_ns = ns_find(string_data(ns_arg));
    }
    else if (tag == CLJ_NAMESPACE)
    {
        target_ns = (CljNamespace *)ns_arg;
    }
    else
    {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "ns-map: argument must be a symbol, string, or namespace");
        return NULL;
    }

    if (!target_ns)
    {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "Namespace not found");
        return NULL;
    }

    return target_ns->mappings ? target_ns->mappings : make_map(0);
}

// find-ns: Returns the namespace object for the given name
// Usage: (find-ns 'ns-name)
// Clojure-compatible: only symbols are accepted; strings cause a type error.
// Returns the namespace object or nil if not found
ID native_find_ns(ID *args, unsigned int argc)
{
    CLJ_ASSERT(argc == 1 && "find-ns: arity check failed");
    if (argc != 1)
    {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "find-ns expects exactly 1 argument, got %u", argc);
        return NULL;
    }

    ID ns_arg = args[0];
    if (!ns_arg)
        return NULL; // nil -> nil (Clojure-compatible)

    int tag = TAG(ns_arg);
    if (tag == CLJ_SYMBOL)
    {
        return ns_find_by_symbol(as_symbol(ns_arg));
    }

    throw_exception_formatted(EXCEPTION_TYPE, __FILE__, __LINE__, 0,
                              "find-ns: argument must be a symbol"); return NULL;
}

// all-ns: Returns a list of all namespace objects
// Usage: (all-ns)
ID native_all_ns(ID *args, unsigned int argc)
{
    (void)args;
    if (argc != 0)
    {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "all-ns expects no arguments, got %u", argc);
        return NULL;
    }

    if (!g_runtime.ns_registry)
    {
        return empty_list();
    }

    ID result = NULL;
    MAP_FOR_EACH(g_runtime.ns_registry, key, val)
    {
        (void)key;
        if (!val)
        {
            continue;
        }
        result = make_list(val, result);
    }

    if (!result)
    {
        return empty_list();
    }

    return AUTORELEASE(result);
}

// ns-unload: remove a namespace from the global registry.
// Usage: (ns-unload 'some.ns) or (ns-unload "some.ns")
// Returns true if unloaded, false if namespace was not found.
ID native_ns_unload(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 1, "ns-unload"))
        return NULL;

    ID ns_arg = args[0];
    if (!ns_arg)
        return clj_false;

    const char *ns_name = NULL;
    CljSymbol *name_sym = NULL;
    unsigned char tag = TAG(ns_arg);

    if (tag == CLJ_SYMBOL)
    {
        name_sym = as_symbol(ns_arg);
        ns_name = name_sym ? name_sym->cname : NULL;
    }
    else if (tag == CLJ_STRING)
    {
        ns_name = string_data(ns_arg);
        name_sym = ns_name ? intern_symbol_global(ns_name) : NULL;
    }
    else
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "ns-unload expects a symbol or string namespace name",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    if (!ns_name || !name_sym)
        return clj_false;

    // Never unload clojure.core (or user) - keep runtime stable.
    if (strcmp(ns_name, "clojure.core") == 0 || strcmp(ns_name, "user") == 0)
        return clj_false;

    if (!g_runtime.ns_registry)
        return clj_false;

    CljNamespace *ns = ns_find_by_symbol(name_sym);
    if (!ns)
        return clj_false;


    // Explicitly release namespace-owned maps so objects stored in mappings are released
    // immediately (tests assert that function objects are released after ns-unload).
    if (ns->mappings) {
        RELEASE(ns->mappings);
        ns->mappings = make_map(0);
    }
    if (ns->aliases) {
        RELEASE(ns->aliases);
        ns->aliases = make_map(0);
    }
    if (ns->macro_mappings) {
        RELEASE(ns->macro_mappings);
        ns->macro_mappings = NULL;
    }


    // Remove from registry; releasing the old registry map releases the namespace object.
    map_dissoc(g_runtime.ns_registry, name_sym);

    // Clear resolve cache (unqualified symbol resolution relies on it)
    ns_invalidate_resolve_cache();

    return clj_true;
}

// Helper for dir: convert argument to namespace
static CljNamespace *namespace_from_value(ID value)
{
    if (!value)
    {
        return NULL;
    }

    int tag = TAG(value);
    if (tag == CLJ_NAMESPACE)
    {
        return (CljNamespace *)value;
    }
    else if (tag == CLJ_SYMBOL)
    {
        if (IS_KEYWORD(value))
        {
            return NULL;
        }
        return ns_find_by_symbol(as_symbol(value));
    }
    else if (tag == CLJ_STRING)
    {
        return ns_find(string_data((CljString *)value));
    }
    return NULL;
}

static int compare_symbol_names(const void *lhs, const void *rhs)
{
    const char *left = lhs ? *(const char *const *)lhs : NULL;
    const char *right = rhs ? *(const char *const *)rhs : NULL;
    if (left == right)
        return 0;
    if (!left)
        return 1;
    if (!right)
        return -1;
    return strcmp(left, right);
}

// source: Print source code for a function (native implementation for clojure.repl namespace)
// Usage: (source 'function-name) or (source function-var)
// Note: In Clojure, source is a normal function, not a special form
// The argument is evaluated: (source 'inc) evaluates 'inc to the symbol inc, then resolves it
ID native_source(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 1, "source");

    ID arg = args[0];
    if (!arg)
    {
        return NULL;
    }

    // If arg is already a function (closure), use it directly
    ID target_func = NULL;
    if (TAG(arg) == CLJ_CLOSURE)
    {
        target_func = arg;
    }
    else if (TAG(arg) == CLJ_SYMBOL && !IS_KEYWORD(arg))
    {
        // If arg is a symbol, resolve it to get the function
        // g_current_eval_state is thread-local and defined at the top of this file
        if (g_current_eval_state)
        {
            target_func = ns_resolve(g_current_eval_state, as_symbol(arg));
        }
    }

    // If we have a function, print its full definition AST: (fn [params*] body)
    if (target_func != NOT_FOUND && target_func && TAG(target_func) == CLJ_CLOSURE)
    {
        CljFunction *fn = as_function(target_func);
        if (fn)
        {
            ID params_id = NULL;
            if (fn->params)
            {
                params_id = fn->params;
            }
            ID body_id = fn->body ? fn->body : NULL;

            bool previous_mode = strings_set_special_form_rendering(false);
            CljString *params_repr = to_string(params_id);
            CljString *body_repr = to_string(body_id);
            strings_set_special_form_rendering(previous_mode);

            if (!params_repr || !body_repr)
            {
                return NULL;
            }

            RETAIN(params_repr);
            RETAIN(body_repr);

            platform_put_string(NULL, "(fn ");
            platform_put_string(NULL, string_data(params_repr));
            platform_put_string(NULL, " ");
            platform_put_string(NULL, string_data(body_repr));
            platform_put_string(NULL, ")\n");

            RELEASE(body_repr);
            RELEASE(params_repr);
            return NULL;
        }
    }

    // Otherwise, return NULL (source not available)
    return NULL;
}

ID native_repl_dir(ID *args, unsigned int argc)
{
    CHECK_ARITY_MAX(argc, 1, "dir");

    EvalState *st = g_current_eval_state;
    CljNamespace *target_ns = NULL;

    if (argc == 0)
    {
        if (st && st->current_ns)
        {
            target_ns = st->current_ns;
        }
        else
        {
            target_ns = ns_find("user");
            if (!target_ns)
            {
                target_ns = ns_find("clojure.core");
            }
        }
    }
    else
    {
        target_ns = namespace_from_value(args[0]);
    }

    if (!target_ns || !target_ns->mappings)
    {
        printf("Namespace not found\n");
        return NULL;
    }

    int entry_count = 0;
    MAP_FOR_EACH(target_ns->mappings, key, value)
    {
        if (key && TAG(key) == CLJ_SYMBOL)
        {
            entry_count++;
        }
    }

    if (entry_count == 0)
    {
        return NULL;
    }

    const char **names = (const char **)CLJ_MALLOC(sizeof(char *) * entry_count);

    int idx_names = 0;
    MAP_FOR_EACH(target_ns->mappings, key, value)
    {
        if (key && TAG(key) == CLJ_SYMBOL)
        {
            CljSymbol *sym = as_symbol(key);
            if (sym && sym->cname)
            {
                names[idx_names++] = sym->cname;
            }
        }
    }

    qsort(names, idx_names, sizeof(char *), compare_symbol_names);
    for (int i = 0; i < idx_names; i++)
    {
        if (names[i])
        {
            printf("%s\n", names[i]);
        }
    }
    CLJ_FREE(names);
    return NULL;
}

// retain-count: Return retain count of an object (native implementation for tiny-clj namespace)
// Usage: (tiny-clj/retain-count obj) - returns the reference count as an integer
ID native_retain_count(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 1, "retain-count");

    ID obj = args[0];
    if (!obj)
    {
        // nil has retain count 0
        return fixnum(0);
    }

    // Get retain count using retain_count from memory.h
    int rc = retain_count(obj);
    return fixnum(rc);
}

// clojure.core/get-thread-bindings: snapshot current dynamic bindings
// Returns a map of dynamic var symbols (e.g. *x*) to their current values (may be nil/NULL).
ID native_get_thread_bindings(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 0, "get-thread-bindings");
    (void)args;

    // Prefer the current eval state when running inside evaluation.
    EvalState *st = g_current_eval_state ? g_current_eval_state : get_global_eval_state();

    CljPersistentMap *out = map_empty();
    RETAIN(out);

    if (st && st->dynamic_bindings) {
        CljPersistentVector *bindings = vector_persistent(st->dynamic_bindings);
        unsigned int depth = bindings ? vector_count(bindings) : 0;
        for (unsigned int i = 0; i < depth; i++) {
            ID frame_id = vector_nth(bindings, i);
            if (!frame_id || TAG(frame_id) != CLJ_MAP_PERSISTENT) continue;
            ASSIGN(out, map_merge(out, (CljPersistentMap*)frame_id, true));
        }
    }

    // Include *ns* as a dynamic binding snapshot (implemented as EvalState.current_ns).
    if (st && st->current_ns) {
        ASSIGN(out, map_assoc(out, SYM_NS_STAR, st->current_ns));
    }

    return AUTORELEASE(out);
}

#if defined(DEBUG) && !defined(ESP32_BUILD)
// clojure.stacktrace/stacktrace-str: return native (C) backtrace string captured in CLJException
// Intended to be used by libs/clojure/stacktrace.clj to build vector-of-frames on demand.
ID native_stacktrace_str(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 1, "stacktrace-str");

    ID ex_obj = args[0];
    if (!ex_obj || TAG(ex_obj) != CLJ_EXCEPTION)
    {
        return NULL;
    }

    CLJException *ex = (CLJException *)ex_obj;
    if (!ex->stacktrace)
    {
        return NULL;
    }

    return AUTORELEASE(RETAIN(ex->stacktrace));
}
#endif

ID native_meta(ID *args, unsigned int argc);

// ============================================================================
// Native function lookup table for stubs
// Uses CljSymbol* for efficient pointer comparison (symbols are interned)
// Statically initialized at compile-time using static symbol data structures
typedef struct
{
    CljSymbol *clojure_symbol; // Clojure function symbol (e.g., &sym_trim_data.sym)
    BuiltinFn native_func;     // Native C function pointer
} NativeFunctionEntry;

#if defined(DEBUG) && !defined(ESP32_BUILD)
// Qualified-name entry for clojure.stacktrace/stacktrace-str.
// We store it as an un-namespaced static symbol and rely on native_function_lookup's
// qualified-name string fallback (avoids eager symbol-table init for clojure.stacktrace).
static StaticSymbolData sym_stacktrace_str_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "clojure.stacktrace/stacktrace-str"}};
#endif

// Qualified-name entries for tiny-clj.datetime native stubs.
// Stored as pseudo-qualified cname and rely on native_function_lookup's qualified-name fallback.
static StaticSymbolData sym_tinyclj_datetime_civil_from_days_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.datetime/civil-from-days"}};
static StaticSymbolData sym_tinyclj_datetime_days_from_civil_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.datetime/days-from-civil"}};
static StaticSymbolData sym_tinyclj_datetime_format_iso_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.datetime/format-iso"}};

// Pseudo-qualified cname entries for libs' :native stubs.
// Stored as un-namespaced static symbols and rely on native_function_lookup's qualified-name fallback.
static StaticSymbolData sym_clojure_pprint_pprint_str_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "clojure.pprint/pprint-str"}};

static StaticSymbolData sym_tinyclj_runtime_stats_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.runtime/stats"}};

static StaticSymbolData sym_tinyclj_fs_spit_bytes_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.fs/spit-bytes"}};
static StaticSymbolData sym_tinyclj_fs_slurp_bytes_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.fs/slurp-bytes"}};
static StaticSymbolData sym_tinyclj_fs_stat_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.fs/stat"}};
static StaticSymbolData sym_tinyclj_fs_list_batch_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.fs/list-batch"}};
static StaticSymbolData sym_tinyclj_fs_delete_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.fs/delete!"}};
static StaticSymbolData sym_tinyclj_fs_set_size_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.fs/set-size!"}};

static StaticSymbolData sym_tinyclj_kv_put_bytes_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-db.kv/put-bytes"}};
static StaticSymbolData sym_tinyclj_kv_get_bytes_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-db.kv/get-bytes"}};
static StaticSymbolData sym_tinyclj_kv_delete_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-db.kv/delete!"}};

static StaticSymbolData sym_tinyclj_net_udp_socket_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.net/udp-socket"}};
static StaticSymbolData sym_tinyclj_net_on_receive_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.net/on-receive"}};
static StaticSymbolData sym_tinyclj_net_send_bang_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.net/send!"}};
static StaticSymbolData sym_tinyclj_net_close_bang_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.net/close!"}};
static StaticSymbolData sym_tinyclj_net_tcp_connect_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.net/tcp-connect"}};
static StaticSymbolData sym_tinyclj_net_tcp_on_receive_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.net/tcp-on-receive"}};
static StaticSymbolData sym_tinyclj_net_tcp_send_bang_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.net/tcp-send!"}};
static StaticSymbolData sym_tinyclj_net_tcp_close_bang_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.net/tcp-close!"}};

static StaticSymbolData sym_tinyclj_net_mdns_open_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.net.mdns/open"}};
static StaticSymbolData sym_tinyclj_net_mdns_on_event_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.net.mdns/on-event"}};
static StaticSymbolData sym_tinyclj_net_mdns_browse_bang_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.net.mdns/browse!"}};
static StaticSymbolData sym_tinyclj_net_mdns_close_bang_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.net.mdns/close!"}};

// Compile-time initialized lookup table (DRY: avoids runtime initialization)
// Uses static symbol data structures (&sym_*_data.sym) for compile-time references
static const NativeFunctionEntry native_function_table[] = {
    // clojure.repl functions
    {&sym_source_data.sym, native_source},
    {&sym_dir_data.sym, native_repl_dir},
#if defined(DEBUG) && !defined(ESP32_BUILD)
    {&sym_stacktrace_str_qualified_data.sym, native_stacktrace_str},
#endif
    {&sym_retain_count_data.sym, native_retain_count},

    // tiny-clj.datetime functions
    {&sym_tinyclj_datetime_civil_from_days_qualified_data.sym, native_datetime_civil_from_days},
    {&sym_tinyclj_datetime_days_from_civil_qualified_data.sym, native_datetime_days_from_civil},
    {&sym_tinyclj_datetime_format_iso_qualified_data.sym, native_datetime_format_iso},

    // libs' :native stubs (pseudo-qualified cname entries)
    {&sym_clojure_pprint_pprint_str_qualified_data.sym, native_clojure_pprint_pprint_str},
    {&sym_tinyclj_runtime_stats_qualified_data.sym, native_tinyclj_runtime_stats},
    {&sym_tinyclj_fs_spit_bytes_qualified_data.sym, native_tinyclj_fs_spit_bytes},
    {&sym_tinyclj_fs_slurp_bytes_qualified_data.sym, native_tinyclj_fs_slurp_bytes},
    {&sym_tinyclj_fs_stat_qualified_data.sym, native_tinyclj_fs_stat},
    {&sym_tinyclj_fs_list_batch_qualified_data.sym, native_tinyclj_fs_list_batch},
    {&sym_tinyclj_fs_set_size_qualified_data.sym, native_tinyclj_fs_set_size},
    {&sym_tinyclj_fs_delete_qualified_data.sym, native_tinyclj_fs_delete},
    {&sym_tinyclj_kv_put_bytes_qualified_data.sym, native_tinyclj_kv_put_bytes},
    {&sym_tinyclj_kv_get_bytes_qualified_data.sym, native_tinyclj_kv_get_bytes},
    {&sym_tinyclj_kv_delete_qualified_data.sym, native_tinyclj_kv_delete},
    {&sym_tinyclj_net_udp_socket_qualified_data.sym, native_tinyclj_net_udp_socket},
    {&sym_tinyclj_net_on_receive_qualified_data.sym, native_tinyclj_net_on_receive},
    {&sym_tinyclj_net_send_bang_qualified_data.sym, native_tinyclj_net_send_bang},
    {&sym_tinyclj_net_close_bang_qualified_data.sym, native_tinyclj_net_close_bang},
    {&sym_tinyclj_net_tcp_connect_qualified_data.sym, native_tinyclj_net_tcp_connect},
    {&sym_tinyclj_net_tcp_on_receive_qualified_data.sym, native_tinyclj_net_tcp_on_receive},
    {&sym_tinyclj_net_tcp_send_bang_qualified_data.sym, native_tinyclj_net_tcp_send_bang},
    {&sym_tinyclj_net_tcp_close_bang_qualified_data.sym, native_tinyclj_net_tcp_close_bang},
    {&sym_tinyclj_net_mdns_open_qualified_data.sym, native_tinyclj_net_mdns_open},
    {&sym_tinyclj_net_mdns_on_event_qualified_data.sym, native_tinyclj_net_mdns_on_event},
    {&sym_tinyclj_net_mdns_browse_bang_qualified_data.sym, native_tinyclj_net_mdns_browse_bang},
    {&sym_tinyclj_net_mdns_close_bang_qualified_data.sym, native_tinyclj_net_mdns_close_bang},

    // clojure.core functions
    {&sym_get_thread_bindings_data.sym, native_get_thread_bindings},
    {&sym_meta_data.sym, native_meta},
    {&sym_with_meta_data.sym, native_with_meta},
    {&sym_reduce_data.sym, native_reduce},
    {&sym_list_data.sym, native_list},
    {&sym_destructure_data.sym, native_destructure},
    {&sym_map_data.sym, native_map},
    {&sym_mapcat_data.sym, native_mapcat},
    {&sym_filter_data.sym, native_filter},
    {&sym_group_by_data.sym, native_group_by},
    {&sym_last_data.sym, native_last},
    {&sym_gpio_watch_data.sym, native_gpio_watch},
    {&sym_gpio_unwatch_data.sym, native_gpio_unwatch},
    {&sym_gpio_simulate_data.sym, native_gpio_simulate},
    {&sym_ns_unload_data.sym, native_ns_unload},
    {&sym_plus_data.sym, native_add_variadic},
    {&sym_minus_data.sym, native_sub_variadic},
    {&sym_multiply_data.sym, native_mul_variadic},
    {&sym_divide_data.sym, native_div_variadic},
    {&sym_mod_data.sym, native_mod},
    {&sym_quot_data.sym, native_quot},
    {&sym_bit_shift_left_data.sym, native_bit_shift_left},
    {&sym_range_data.sym, native_range},
    {&sym_repeat_data.sym, native_repeat},
    {&sym_lazy_seq_star_data.sym, native_lazy_seq_star},
    {&sym_math_sqrt_data.sym, native_math_sqrt},
    {&sym_sqrt_data.sym, native_math_sqrt},
    {&sym_format_data.sym, native_format},
    {&sym_str_data.sym, native_str},
    {&sym_subs_data.sym, native_subs},
    {&sym_symbol_data.sym, native_symbol},
    {&sym_type_data.sym, native_type},
    {&sym_array_map_data.sym, native_array_map},
    {&sym_vector_data.sym, native_vector},
    {&sym_vec_data.sym, native_vec},
    {&sym_hash_set_data.sym, native_hash_set},
    {&sym_nth_data.sym, nth2},
    {&sym_peek_data.sym, native_peek},
    {&sym_pop_data.sym, native_pop},
    {&sym_subvec_data.sym, native_subvec},
    {&sym_conj_data.sym, native_conj},
    {&sym_seq_data.sym, native_seq},
    {&sym_not_data.sym, native_not},
    {&sym_first_data.sym, native_first},
    {&sym_rest_data.sym, native_rest},
    {&sym_concat_data.sym, native_concat},
    {&sym_concat2_data.sym, native_concat},
    {&sym_next_data.sym, native_next},
    {&sym_nnext_data.sym, native_nnext},
    {&sym_nthnext_data.sym, native_nthnext},
    {&sym_gensym_data.sym, native_gensym},
    {&sym_partition_data.sym, native_partition},
    {&sym_some_data.sym, native_some},
    {&sym_cons_data.sym, native_cons},
    {&sym_list_data.sym, native_list},
    {&sym_count_data.sym, native_count},
    {&sym_nilp_data.sym, native_nilp},
    {&sym_reverse_data.sym, native_reverse},
    {&sym_assoc_data.sym, native_assoc},
    {&sym_dissoc_data.sym, native_dissoc},
    {&sym_disj_data.sym, native_disj},
    {&sym_merge_data.sym, native_merge},
    {&sym_contains_p_data.sym, native_contains_p},
    {&sym_update_data.sym, native_update},
    {&sym_into_data.sym, native_into},
    {&sym_select_keys_data.sym, native_select_keys},
    {&sym_find_data.sym, native_find},
    {&sym_transient_data.sym, native_transient},
    {&sym_persistent_bang_data.sym, native_persistent_bang},
    {&sym_conj_bang_data.sym, native_conj_bang},
    {&sym_get_data.sym, native_get},
    {&sym_keys_data.sym, native_keys},
    {&sym_vals_data.sym, native_vals},
    {&sym_println_data.sym, native_println},
    {&sym_print_data.sym, native_print},
    {&sym_pr_data.sym, native_pr},
    {&sym_prn_data.sym, native_prn},
    {&sym_lt_data.sym, native_lt},
    {&sym_gt_data.sym, native_gt},
    {&sym_le_data.sym, native_le},
    {&sym_ge_data.sym, native_ge},
    {&sym_equals_data.sym, native_eq},
    {&sym_not_eq_data.sym, native_not_eq},
    {&sym_identical_data.sym, native_identical},
    {&sym_vector_p_data.sym, native_vector_p},
    {&sym_map_p_data.sym, native_map_p},
    {&sym_set_p_data.sym, native_set_p},
    {&sym_number_p_data.sym, native_number_p},
    {&sym_integer_p_data.sym, native_integer_p},
    {&sym_float_p_data.sym, native_float_p},
    {&sym_string_p_data.sym, native_string_p},
    {&sym_keyword_p_data.sym, native_keyword_p},
    {&sym_keyword_data.sym, native_keyword},
    {&sym_name_data.sym, native_name},
    {&sym_symbol_p_data.sym, native_symbol_p},
    {&sym_fn_p_data.sym, native_fn_p},
    {&sym_atom_p_data.sym, native_atom_p},
    {&sym_char_p_data.sym, native_char_p},
    {&sym_list_p_data.sym, native_list_p},
    {&sym_yield_data.sym, native_yield},
    {&sym_current_time_ms_data.sym, native_current_time_ms},
    {&sym_ns_map_data.sym, native_ns_map},
    {&sym_find_ns_data.sym, native_find_ns},
    {&sym_all_ns_data.sym, native_all_ns},
    {&sym_do_data.sym.base, native_do},
    {&sym_byte_array_data.sym, native_byte_array},
    {&sym_aget_data.sym, native_aget},
    {&sym_aset_data.sym, native_aset},
    {&sym_alength_data.sym, native_alength},
    {&sym_aclone_data.sym, native_aclone},
    {&sym_run_next_task_data.sym, native_run_next_task},
    {&sym_schedule_data.sym, native_schedule},
    {&sym_schedule_periodic_data.sym, native_schedule_periodic},
    {&sym_cancel_timer_data.sym, native_cancel_timer},
    {&sym_atom_data.sym, native_atom},
    {&sym_deref_data.sym, native_deref},
    {&sym_reset_bang_data.sym, native_reset_bang},
    {&sym_swap_bang_data.sym, native_swap_bang},
    {&sym_slurp_data.sym, native_slurp},
    {&sym_spit_data.sym, native_spit},
#ifdef DEBUG
    {&sym_ast_string_data.sym, native_ast_string},
#endif
    {NULL, NULL} // Sentinel
};

// Lookup native function by Clojure symbol
// Fast path: pointer comparison (symbols are interned)
// Fallback: string comparison for symbols created at runtime (not pre-interned)
BuiltinFn native_function_lookup(CljSymbol *symbol)
{
    if (!symbol)
        return NULL;

    const char *cname = symbol->cname;
    const char *ns_name = (symbol->ns_name) ? symbol->ns_name->cname : NULL;

    // Build qualified name once (if needed)
    char qualified_name[128] = {0};
    if (ns_name)
    {
        size_t pos = 0;
        pos = format_append(qualified_name, pos, sizeof(qualified_name), ns_name);
        pos = format_append_char(qualified_name, pos, sizeof(qualified_name), '/');
        format_append(qualified_name, pos, sizeof(qualified_name), cname);
    }

    // Single pass through table with all checks
    for (int i = 0; native_function_table[i].clojure_symbol != NULL; i++)
    {
        CljSymbol *table_sym = native_function_table[i].clojure_symbol;
        if (!table_sym)
            continue;

        // Fast path: pointer equality (interned symbols)
        if (table_sym == symbol)
        {
            return native_function_table[i].native_func;
        }

        // Fallback: string comparison (for runtime-created symbols)
        if (!cname || !table_sym->cname)
            continue;

        const char *table_ns = table_sym->ns_name ? table_sym->ns_name->cname : NULL;

        if (ns_name)
        {
            // Qualified lookup: match ns/name or pseudo-qualified cname
            if (table_ns && strcmp(table_ns, ns_name) == 0 && strcmp(table_sym->cname, cname) == 0)
            {
                return native_function_table[i].native_func;
            }
            if (!table_ns && strcmp(table_sym->cname, qualified_name) == 0)
            {
                return native_function_table[i].native_func;
            }
            // clojure.core special case: match unqualified entries
            if (!table_ns && strcmp(ns_name, "clojure.core") == 0 && strcmp(table_sym->cname, cname) == 0)
            {
                return native_function_table[i].native_func;
            }
        }
        else
        {
            // Unqualified lookup: match by name only
            if (strcmp(table_sym->cname, cname) == 0)
            {
                return native_function_table[i].native_func;
            }
        }
    }

    // Split-out lookup tables
    BuiltinFn string_fn = builtins_strings_native_function_lookup(symbol);
    if (string_fn)
        return string_fn;

    return NULL;
}

// Meta function: (meta obj) - returns metadata map or nil
ID native_meta(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 1, "meta");

    ID obj = args[0];
    if (!obj)
    {
        return NULL; // nil -> nil
    }

#if defined(META_ENABLED) && META_ENABLED
    // If obj is a symbol, resolve it to get the actual value (function, var, etc.)
    // This allows (meta trim) to work by resolving trim to the function first
    ID target_obj = obj;
    if (TAG(obj) == CLJ_SYMBOL)
    {
        // Get current eval state from builtin context to resolve symbol
        // Forward declaration for static variable
        if (g_current_eval_state && g_current_eval_state->current_ns)
        {
            ID resolved = ns_resolve(g_current_eval_state, (CljSymbol *)obj);
            if (resolved != NOT_FOUND)
            {
                target_obj = resolved;
            }
        }
    }

    ID meta = meta_get(target_obj);
    if (meta)
    {
        RETAIN(meta); // meta_get doesn't retain, so we need to retain for return
        return meta;
    }
#endif // META_ENABLED

    return NULL; // No metadata -> nil
}

// With-meta function: (with-meta obj meta-map) - returns obj with new metadata
ID native_with_meta(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 2, "with-meta");

    ID obj = args[0];
    ID meta_map = args[1];

    // nil with any metadata returns nil
    if (!obj)
    {
        return NULL;
    }

    // Immediates (numbers, keywords, etc.) don't support metadata
    if (IS_IMMEDIATE(obj))
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "with-meta: immediates don't support metadata", __FILE__, __LINE__, 0);
        return NULL;
    }

    // Strings don't support metadata in Clojure
    if (TAG(obj) == CLJ_STRING)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "with-meta: strings don't support metadata", __FILE__, __LINE__, 0);
        return NULL;
    }

#if defined(META_ENABLED) && META_ENABLED
    // Set metadata on the object
    // NOTE: This mutates the object. For true immutability, we'd need to copy.
    // For now, this matches the common pattern in embedded Clojure implementations.
    meta_set(obj, meta_map);
    return RETAIN(obj);
#else
    (void)meta_map;
    return RETAIN(obj);
#endif
}

// Get macro function by symbol: (get-macro 'name) -> macro-fn or nil
ID native_get_macro(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 1, "get-macro");
    if (!args[0] || TAG(args[0]) != CLJ_SYMBOL || !g_current_eval_state)
        return NULL;
    CljFunction *macro = lookup_macro_resolve(g_current_eval_state, as_symbol(args[0]));
    return macro ? RETAIN(macro) : NULL;
}

// Apply function to arguments: (apply f args) or (apply f a b c args)
ID native_apply(ID *args, unsigned int argc)
{
    if (argc < 2)
    {
        throw_exception(EXCEPTION_ARITY, "apply requires at least 2 arguments", __FILE__, __LINE__, 0);
    }

    ID fn = args[0];
    unsigned char fn_tag = fn ? TAG(fn) : 0;
    if (fn_tag != CLJ_FUNC && fn_tag != CLJ_CLOSURE)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "apply: first argument must be a function", __FILE__, __LINE__, 0);
        return NULL;
    }

    // Build args array: fixed args + sequence args
    ID call_args[64];
    int n = 0;

    // Copy fixed args (args[1] to args[argc-2])
    for (unsigned int i = 1; i < argc - 1 && n < 64; i++)
    {
        call_args[n++] = args[i];
    }

    // Append sequence args from last argument
    ID last = args[argc - 1];
    if (last)
    {
        unsigned char tag = TAG(last);
        if (is_list_type(tag))
        {
            for (CljList *l = as_list(last); l && n < 64; l = l->rest ? as_list(l->rest) : NULL)
            {
                call_args[n++] = l->first;
            }
        }
        else if (tag == CLJ_VECTOR_PERSISTENT || tag == CLJ_VECTOR_TRANSIENT)
        {
            CljPersistentVector *v = as_vector(last);
            int cnt = vector_count(v);
            for (int i = 0; i < cnt && n < 64; i++)
            {
                call_args[n++] = vector_nth(v, i);
            }
        }
    }

    return eval_function_call(fn, call_args, n, NULL, g_current_eval_state);
}

// Create symbol from string (with optional namespace)
ID native_symbol(ID *args, unsigned int argc)
{
    // symbol accepts 1 or 2 arguments: (symbol "name") or (symbol "ns" "name")
    if (argc != 1 && argc != 2)
    {
        char error_msg[256];
        size_t pos = 0;
        pos = format_append(error_msg, pos, sizeof(error_msg),
                            "symbol requires exactly 1 or 2 argument");
        if (argc != 1) {
            pos = format_append_char(error_msg, pos, sizeof(error_msg), 's');
        }
        pos = format_append(error_msg, pos, sizeof(error_msg), ", got ");
        pos = format_append_uint(error_msg, pos, sizeof(error_msg), argc);
        throw_exception(EXCEPTION_ARITY, error_msg, __FILE__, __LINE__, 0);
    }

    const char *ns = NULL;
    const char *cname = NULL;

    if (argc == 2)
    {
        // Two arguments: namespace (can be nil) and name
        ID ns_arg = args[0];
        ID name_arg = args[1];

        // Namespace can be nil (NULL) or a string
        if (ns_arg && TAG(ns_arg) != CLJ_STRING)
        {
            throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                      "symbol namespace must be a string or nil");
            return NULL;
        }

        if (!name_arg || TAG(name_arg) != CLJ_STRING)
        {
            throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                      "symbol requires a string for name");
            return NULL;
        }

        // Extract namespace (can be NULL if nil was passed)
        if (ns_arg)
        {
            ns = string_data(ns_arg);
        }
        else
        {
            ns = NULL; // nil namespace
        }

        cname = string_data(name_arg);
    }
    else
    {
        // One argument: name only
        ID name_arg = args[0];

        if (!name_arg || TAG(name_arg) != CLJ_STRING)
        {
            throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                      "symbol requires a string argument");
            return NULL;
        }

        CljString *name_str = (CljString *)name_arg;
        cname = clj_string_data(name_str);
    }

    // Create symbol from string(s)
    CljSymbol *ns_name_sym = ns ? intern_symbol_global(ns) : NULL;
    CljSymbol *sym = intern_symbol(ns_name_sym, cname);
    if (!sym)
    {
        throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                  "Failed to create symbol from string");
        return NULL;
    }

    // intern_symbol returns a retained symbol, but builtin functions should return AUTORELEASE
    return AUTORELEASE(sym);
}

// File I/O: slurp - read entire file as string (KV + embedded resolver)
ID native_slurp(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 1, "slurp"))
        return NULL;

    // Convert argument to CljString, then get C-string data
    CljString *filename_str_obj = to_string(args[0]);
    if (!filename_str_obj)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "slurp requires a string or symbol argument",
                        __FILE__, __LINE__, 0);
        return NULL;
    }
    const char *filename_str = string_data(filename_str_obj);

    ID bytes = resolve_path_to_bytes(filename_str);
    CljString *result = NULL;
    if (bytes) {
        result = string_view_from_byte_array(bytes);
    } else {
        // Fallback to host filesystem (non-embedded paths).
        result = file_slurp(filename_str);
    }
    if (!result) {
        return NULL;
    }
    return AUTORELEASE(result);
}

static bool eval_source_in_current_state(CljString *src, const char *src_name, EvalState *st);

// load-file: read and evaluate all forms in a file (Clojure standard function)
// DRY: Uses eval_source_in_current_state for the actual evaluation
ID native_load_file(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 1, "load-file"))
        return NULL;

    // Get filename as string
    CljString *filename_str_obj = to_string(args[0]);
    if (!filename_str_obj)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "load-file requires a string argument",
                        __FILE__, __LINE__, 0);
        return NULL;
    }
    const char *filename = string_data(filename_str_obj);

    // Resolve file content (KV + embedded) with filesystem fallback.
    ID bytes = resolve_path_to_bytes(filename);
    CljString *content = NULL;
    if (bytes) {
        content = string_view_from_byte_array(bytes);
    } else {
        content = file_slurp(filename);
    }
    if (!content) {
        return NULL;
    }

    // Get EvalState (use global state)
    EvalState *st = g_current_eval_state ? g_current_eval_state : get_global_eval_state();

    // DRY: Use eval_source_in_current_state (same as require uses)
    bool ok = eval_source_in_current_state(content, filename, st);
    if (ok && st && st->current_ns) {
        st->current_ns->loaded = true;
    }
    RELEASE(content);

    // load-file returns nil (like Clojure)
    return ok ? NULL : NULL;
}

// ----------------------------------------------------------------------------
// REQUIRE IMPLEMENTATION (Clojure-like namespace loader)
// ----------------------------------------------------------------------------
static char *namespace_to_relpath(const char *ns_name)
{
    if (!ns_name)
        return NULL;
    size_t len = strlen(ns_name);
    // Worst case: all chars + possible slashes + ".clj" + NUL
    char *buf = (char *)CLJ_MALLOC(len + 5);
    for (size_t i = 0; i < len; i++)
    {
        char c = ns_name[i];
        if (c == '.')
            buf[i] = '/';
        else
            buf[i] = c;  // keep hyphen so tiny-clj.runtime -> tiny-clj/runtime.clj
    }
    buf[len] = '\0';
    strcat(buf, ".clj");
    return buf;
}

static bool eval_source_in_current_state(CljString *src, const char *src_name, EvalState *st)
{
    if (!src || !st)
        return false;
    static int debug_require_errors = -1;
    if (debug_require_errors == -1) {
        const char *v = getenv("TINYCLJ_DEBUG_REQUIRE_ERRORS");
        debug_require_errors = (v && v[0] && strcmp(v, "0") != 0) ? 1 : 0;
    }
    int success_count = 0;
    Reader reader;
    reader_init_with_length(&reader, string_data(src), (size_t)string_length(src));
    if (src_name && src_name[0])
    {
        reader_set_source_name(&reader, src_name);
    }
    else if (st && st->current_ns && st->current_ns->name && st->current_ns->name->cname)
    {
        reader_set_source_name(&reader, st->current_ns->name->cname);
    }
    else
    {
        reader_set_source_name(&reader, "<namespace>");
    }

    // Bound autorelease tracking per top-level form.
    // This prevents a single (ns ... (:require ...)) from accumulating thousands of
    // autoreleased temporaries while loading dependencies.
    while (!reader_is_eof(&reader))
    {
        reader_skip_all(&reader);
        if (reader_is_eof(&reader))
            break;

        // Save reader position before parsing to detect if we're stuck
        size_t pos_before = reader_offset(&reader);

        WITH_AUTORELEASE_POOL({
        CljValue form = NULL;
        TRY
        {
            form = value_by_parsing_expr(&reader, st);
            if (!form)
            {
                if (reader_is_eof(&reader))
                {
                    autorelease_pool_drain_to_depth(_restore);
                    break;
                }
                while (!reader_is_eof(&reader) && reader_current(&reader) != '\n')
                    reader_next(&reader);
                if (!reader_is_eof(&reader))
                    reader_next(&reader);
                autorelease_pool_drain_to_depth(_restore);
                continue;
            }

            (void)eval_parsed(form, st, NULL);
            success_count++;

            size_t pos_after = reader_offset(&reader);
            if (pos_after == pos_before && !reader_is_eof(&reader))
                reader_next(&reader);
        }
        CATCH(ex)
        {
            if (debug_require_errors) {
                const char *etype = (ex && ex->type[0]) ? ex->type : "Exception";
                const char *emsg = (ex && ex->message[0]) ? ex->message : "Unknown error";
                const char *efile = (ex && ex->file[0]) ? ex->file : "<unknown>";
                int eline = ex ? ex->line : 0;
                const char *label = (src_name && src_name[0]) ? src_name : "<namespace>";
                fprintf(stderr, "[require] %s: %s (%s:%d) [%s]\n", label, emsg, efile, eline, etype);
            }
            while (!reader_is_eof(&reader) && reader_current(&reader) != '\n')
                reader_next(&reader);
            if (!reader_is_eof(&reader))
                reader_next(&reader);
        }
        END_TRY
        });
    }
    // Return true if at least some expressions succeeded (partial loading is OK)
    return success_count > 0;
}

/**
 * @brief Copy symbols from source namespace to target namespace
 * @param source_ns Source namespace
 * @param target_ns Target namespace
 * @param symbols Vector of symbols to copy
 */
static void copy_symbols_to_namespace(CljNamespace *source_ns, CljNamespace *target_ns, CljObject *symbols)
{
    if (!source_ns || !target_ns || !symbols)
        return;

    if (!symbols || TAG(symbols) != CLJ_VECTOR_PERSISTENT)
        return;

    CljPersistentVector *vec = as_vector(symbols);
    VECTOR_FOR_EACH(vec, sym)
    {
        if (!sym || TAG(sym) != CLJ_SYMBOL)
        {
            continue;
        }

        // CRITICAL: Mappings use qualified symbols as keys (except clojure.core uses unqualified)
        // Must qualify the symbol with source namespace name for lookup (unless source is clojure.core)
        CljSymbol *sym_obj = as_symbol(sym);
        CljSymbol *lookup_sym = sym_obj;
        if (source_ns->name == SYM_CLOJURE_CORE)
        {
            // clojure.core uses unqualified symbols as keys - use symbol as-is
            lookup_sym = sym_obj;
        }
        else if (sym_obj && !sym_obj->ns_name && source_ns->name && source_ns->name->cname)
        {
            // Other namespaces: qualify the symbol for lookup
            lookup_sym = intern_symbol(source_ns->name, sym_obj->cname);
            if (!lookup_sym)
            {
                lookup_sym = sym_obj; // Fallback to original
            }
        }

        // Look up symbol in source namespace (missing -> NOT_FOUND, nil is a valid value)
        CljObject *val = map_get(source_ns->mappings, lookup_sym);
        if (val != NOT_FOUND)
        {
            // Copy to target namespace using ns_define_refer for :refer (stores unqualified symbol)
            ns_define_refer(target_ns, sym, val);
        }
        // sym lifetime is tied to vector - no release needed
    }
}

/**
 * @brief Copy all symbols from source namespace to target namespace
 * @param source_ns Source namespace
 * @param target_ns Target namespace
 */
static void copy_all_symbols_to_namespace(CljNamespace *source_ns, CljNamespace *target_ns)
{
    if (!source_ns || !target_ns || !source_ns->mappings)
        return;

    CljPersistentMap *map = source_ns->mappings;
    if (!map)
        return;

    // Iterate through all mappings in source namespace
    // Keys are qualified symbols (e.g., test.referall/var1 or clojure.repl/doc)
    // EXCEPTION: clojure.core uses unqualified symbols as keys (ns_name = NULL)
    // We need to extract the unqualified symbol name and copy it to target namespace
    MAP_FOR_EACH(map, key, val)
    {
        if (key && val && TAG(key) == CLJ_SYMBOL)
        {
            CljSymbol *key_sym = as_symbol(key);
            if (key_sym && key_sym->cname)
            {
                const char *unqualified_name = NULL;

                if (source_ns->name == SYM_CLOJURE_CORE)
                {
                    // clojure.core uses unqualified symbols as keys - cname is already unqualified
                    unqualified_name = key_sym->cname;
                }
                else
                {
                    // Other namespaces: extract unqualified name from qualified symbol
                    // Qualified symbols have format "namespace/name" (e.g., "clojure.repl/doc")
                    const char *cname = key_sym->cname;
                    const char *slash = strrchr(cname, '/');
                    unqualified_name = slash ? slash + 1 : cname;
                }

                // Create unqualified symbol for target namespace
                // Use ns_define_refer to store unqualified symbol (like Clojure/JVM)
                CljSymbol *unqualified_sym = intern_symbol_global(unqualified_name);
                if (unqualified_sym)
                {
                    ns_define_refer(target_ns, unqualified_sym, val);
                }
            }
        }
    }
}

bool load_namespace_from_bytes(EvalState *st, const char *ns_name, ID bytes, const char *source_path)
{
    if (!st || !ns_name || !source_path || !bytes || TAG(bytes) != CLJ_BYTE_ARRAY) return false;
    CljString *source_str = string_view_from_byte_array(bytes);
    if (!source_str) return false;
    CljNamespace *orig_ns = st->current_ns;
    CljNamespace *orig_resolve_ns = st->resolve_ns;
    CljNamespace *target_ns = ns_get_or_create(ns_name, NULL);
    if (!target_ns) { RELEASE(source_str); return false; }
    if (target_ns->loaded) {
        RELEASE(source_str);
        return true;
    }
    st->current_ns = target_ns;
    st->resolve_ns = target_ns;
    bool ok = eval_source_in_current_state(source_str, source_path, st);
    target_ns->loaded = true;
    st->current_ns = orig_ns;
    st->resolve_ns = orig_resolve_ns;
    RELEASE(source_str);
    return ok;
}

/**
 * @brief Process a single require spec (Symbol or Vector)
 * @param spec Require spec (Symbol or Vector [namespace :as alias] or [namespace :refer ...])
 * @param st Evaluation state
 * @return true on success, false on error
 */
static bool process_require_spec(ID spec, EvalState *st)
{
    if (!spec || !st)
        return false;

    const char *ns_name = NULL;
    CljObject *alias_sym = NULL;
    CljObject *refer_syms = NULL;
    bool refer_all = false;

    CljPersistentVector *vec = NULL;

    // Handle simple Symbol case: (require 'namespace)
    if (TAG(spec) == CLJ_SYMBOL)
    {
        CljSymbol *sym = as_symbol(spec);
        if (!sym || !sym->cname)
            return false;
        ns_name = sym->cname;
    }
    // Handle Vector case: [namespace :as alias] or [namespace :refer [syms]]
    else if (TAG(spec) == CLJ_VECTOR_PERSISTENT)
    {
        vec = as_vector(spec);
        if (vector_count(vec) < 1)
            return false;

        // First element should be namespace name (Symbol or String)
        CljObject *ns_obj = vector_nth(vec, 0);
        if (!ns_obj)
            return false;

        if (ns_obj && TAG(ns_obj) == CLJ_SYMBOL)
        {
            CljSymbol *ns_sym = as_symbol(ns_obj);
            if (!ns_sym || !ns_sym->cname)
            {
                return false;
            }
            ns_name = ns_sym->cname;
        }
        else
        {
            CljString *ns_str_obj = to_string(ns_obj);
            if (!ns_str_obj)
            {
                return false;
            }
            ns_name = string_data(ns_str_obj);
        }
        // ns_obj lifetime is tied to vector - no release needed

        // Parse keywords: :as, :refer
        int vec_count = vector_count(vec);
        for (int i = 1; i < vec_count; i++)
        {
            CljObject *elem = vector_nth(vec, i);
            if (!elem)
                continue;

            // Check if it's a keyword (Symbol starting with :)
            if (elem && TAG(elem) == CLJ_SYMBOL)
            {
                CljSymbol *kw = as_symbol(elem);
                if (!kw || !kw->cname)
                {
                    continue;
                }

                // Use pointer comparison for :as and :refer keywords (more efficient and reliable)
                if (kw == SYM_KW_AS)
                {
                    // :as alias
                    if (i + 1 < vec_count)
                    {
                        alias_sym = vector_nth(vec, i + 1);
                        // Don't release alias_sym - it's stored for later use
                        i++; // Skip next element
                    }
                }
                else if (kw == SYM_KW_REFER)
                {
                    // :refer [symbols] or :refer :all
                    if (i + 1 < vec_count)
                    {
                        CljObject *refer_arg = vector_nth(vec, i + 1);
                        if (refer_arg && TAG(refer_arg) == CLJ_SYMBOL)
                        {
                            CljSymbol *refer_sym = as_symbol(refer_arg);
                            if (refer_sym && refer_sym->cname && strcmp(refer_sym->cname, ":all") == 0)
                            {
                                refer_all = true;
                            }
                        }
                        else if (refer_arg && TAG(refer_arg) == CLJ_VECTOR_PERSISTENT)
                        {
                            refer_syms = refer_arg;
                            // Don't release refer_syms - it's stored for later use
                        }
                        i++; // Skip next element
                    }
                }
            }
        }
    }
    else
    {
        return false;
    }

    if (!ns_name)
        return false;

    // Load namespace (existing logic)
    // CRITICAL: Check if namespace exists, but don't skip loading if it only has native functions
    // A namespace might exist because native functions were registered, but Clojure code hasn't been loaded yet
    CljNamespace *existing = ns_find(ns_name);
    if (existing)
    {
        // Generic idempotence: if namespace source has been loaded once, don't re-evaluate.
        if (existing->loaded)
        {
            // Namespace already fully loaded - just set alias/refer if needed
            // CRITICAL: Ensure st->current_ns is valid before setting alias
            // This is the namespace where the alias should be stored
            if (st && st->current_ns)
            {
                // Set alias if provided
                // NOTE: alias_sym is extracted from vector in lines 2227-2234
                // It should be set by the time we reach here if :as was in the vector
                // CRITICAL: Use same logic as the working case (line 2412) for consistency
                if (alias_sym && TAG(alias_sym) == CLJ_SYMBOL)
                {
                    ID ns_name_sym = intern_symbol_global(ns_name);
                    if (ns_name_sym)
                    {
                        ns_set_alias(st->current_ns, alias_sym, ns_name_sym);
                    }
                }
                // Handle refer
                if (refer_all)
                {
                    copy_all_symbols_to_namespace(existing, st->current_ns);
                }
                else if (refer_syms)
                {
                    copy_symbols_to_namespace(existing, st->current_ns, refer_syms);
                }
            }
            // ns_name is from autoreleased CljString - no free needed
            return true;
        }
        // Fall through to load Clojure code even though namespace exists
    }

    // Convert namespace to relative path
    char *rel = namespace_to_relpath(ns_name);
    if (!rel)
    {
        return false;
    }

    // Search order: /libs/<rel>, then /<rel> (project root)
    char libs_path[256] = {0};
    size_t libs_pos = 0;
    libs_pos = format_append(libs_path, libs_pos, sizeof(libs_path), "/libs/");
    format_append(libs_path, libs_pos, sizeof(libs_path), rel);

    char root_path[256] = {0};
    size_t root_pos = 0;
    root_pos = format_append(root_path, root_pos, sizeof(root_path), "/");
    format_append(root_path, root_pos, sizeof(root_path), rel);

    const char *source_path = NULL;
    ID bytes = resolve_path_to_bytes(libs_path);
    if (bytes)
    {
        source_path = libs_path;
    }
    else
    {
        bytes = resolve_path_to_bytes(root_path);
        if (bytes)
        {
            source_path = root_path;
        }
    }

    if (!bytes)
    {
        char error_msg[256];
        size_t pos = 0;
        pos = format_append(error_msg, pos, sizeof(error_msg),
                            "Require failed: namespace '");
        pos = format_append(error_msg, pos, sizeof(error_msg), ns_name);
        pos = format_append(error_msg, pos, sizeof(error_msg),
                            "' not found (expected file ");
        pos = format_append(error_msg, pos, sizeof(error_msg), root_path);
        pos = format_append(error_msg, pos, sizeof(error_msg), " or ");
        pos = format_append(error_msg, pos, sizeof(error_msg), libs_path);
        format_append_char(error_msg, pos, sizeof(error_msg), ')');
        throw_exception(EXCEPTION_FILE_NOT_FOUND, error_msg, __FILE__, __LINE__, 0);
    }

    if (TAG(bytes) != CLJ_BYTE_ARRAY) {
        CLJ_FREE(rel);
        throw_exception(EXCEPTION_FILE_NOT_FOUND,
                        "Require: resolved resource is not a byte array (internal error)",
                        __FILE__, __LINE__, 0);
        return false;
    }
    bool ok = load_namespace_from_bytes(st, ns_name, bytes, source_path);
    CLJ_FREE(rel);

    // CRITICAL: Don't fail completely if some expressions failed to load
    // Some functions may have been successfully defined even if others failed
    // This allows partial loading (e.g., if one function has an error, others still work)
    // We only return false if the namespace itself couldn't be created
    if (!ok)
    {
        // Check if namespace was at least created (even if loading had errors)
        CljNamespace *loaded_ns = ns_find(ns_name);
        if (!loaded_ns)
        {
            // Namespace wasn't even created - this is a real failure
            // ns_name is from autoreleased CljString - no free needed
            return false;
        }
        // Namespace exists but some expressions failed - continue anyway
        // This allows partial success (some functions loaded, others didn't)
    }

    // Now that namespace is loaded, set alias/refer if needed
    // Note: st->current_ns has been restored to orig_ns, so aliases are set in the correct namespace
    CljNamespace *loaded_ns = ns_find(ns_name);
    if (loaded_ns)
    {
        if (alias_sym && TAG(alias_sym) == CLJ_SYMBOL)
        {
            ID ns_name_sym = intern_symbol_global(ns_name);
            if (ns_name_sym)
            {
                ns_set_alias(st->current_ns, alias_sym, ns_name_sym);
            }
        }
        if (refer_all)
        {
            copy_all_symbols_to_namespace(loaded_ns, st->current_ns);
        }
        else if (refer_syms)
        {
            copy_symbols_to_namespace(loaded_ns, st->current_ns, refer_syms);
        }
    }

    // Free ns_name if it was allocated
    // ns_name is from autoreleased CljString - no free needed

    return true;
}

bool require_namespace_by_name(EvalState *st, const char *ns_name)
{
    if (!st || !ns_name) return false;
    CljSymbol *sym = intern_symbol_global(ns_name);
    if (!sym) return false;
    return process_require_spec((ID)sym, st);
}

static ID normalize_require_spec(ID spec, bool *needs_release)
{
    if (needs_release)
    {
        *needs_release = false;
    }
    if (!spec)
    {
        return spec;
    }

    unsigned int tag = TAG(spec);
    if (tag == CLJ_SYMBOL || tag == CLJ_VECTOR_PERSISTENT)
    {
        return spec;
    }

    if (is_list_type(tag))
    {
        CljList *list = as_list(spec);
        if (!list)
            return spec;

        // Handle (quote symbol) => symbol
        ID first = LIST_FIRST(list);
        if (first && TAG(first) == CLJ_SYMBOL)
        {
            CljSymbol *first_sym = as_symbol(first);
            if (first_sym == SYM_QUOTE)
            {
                // (quote x) => x
                ID quoted = LIST_REST(list) ? LIST_FIRST(as_list(LIST_REST(list))) : NULL;
                unsigned char quoted_tag = quoted ? TAG(quoted) : 0;
                if (quoted_tag == CLJ_SYMBOL || quoted_tag == CLJ_VECTOR_PERSISTENT)
                {
                    return quoted;
                }
            }
        }

        // Convert list to vector for other cases
        CljPersistentVector *vec = make_vector(4, STRONG);
        if (!vec)
        {
            return NULL;
        }
        CljTransientVector *transient_vec = vector_transient(vec);
        RELEASE(vec);
        if (!transient_vec)
        {
            return NULL;
        }

        CljList *current = list;
        LIST_FOR_EACH(current, elem)
        {
            vector_push(transient_vec, elem);
        }

        CljPersistentVector *persistent_vec = vector_persistent(transient_vec);
        if (persistent_vec) {
            RETAIN(persistent_vec);
        }
        RELEASE(transient_vec);
        if (!persistent_vec)
        {
            return NULL;
        }
        if (needs_release)
        {
            *needs_release = true;
        }
        return persistent_vec;
    }

    return spec;
}

ID native_require(ID *args, unsigned int argc)
{
    if (argc == 0)
    {
        throw_exception(EXCEPTION_ARITY, "require requires at least 1 argument", __FILE__, __LINE__, 0);
    }

    ID normalized_specs[argc];
    bool needs_release[argc];
    memset(needs_release, 0, sizeof(needs_release));

    EvalState *st = g_current_eval_state ? g_current_eval_state : get_global_eval_state();

    for (unsigned int i = 0; i < argc; i++)
    {
        bool release_spec = false;
        ID spec = normalize_require_spec(args[i], &release_spec);
        if (!spec)
        {
            for (unsigned int j = 0; j < i; j++)
            {
                if (needs_release[j])
                {
                    RELEASE(normalized_specs[j]);
                }
            }
            return NULL;
        }
        normalized_specs[i] = spec;
        needs_release[i] = release_spec;

        unsigned char spec_tag = spec ? TAG(spec) : 0;
        if (spec && spec_tag != CLJ_SYMBOL && spec_tag != CLJ_VECTOR_PERSISTENT)
        {
            throw_exception(EXCEPTION_TYPE, "require expects a symbol or vector", __FILE__, __LINE__, 0);
            for (unsigned int j = 0; j <= i; j++)
            {
                if (needs_release[j])
                {
                    RELEASE(normalized_specs[j]);
                }
            }
            return NULL;
        }
    }

    for (unsigned int i = 0; i < argc; i++)
    {
        if (!process_require_spec(normalized_specs[i], st))
        {
            for (unsigned int j = 0; j < argc; j++)
            {
                if (needs_release[j])
                {
                    RELEASE(normalized_specs[j]);
                }
            }
            return NULL;
        }
    }

    for (unsigned int i = 0; i < argc; i++)
    {
        if (needs_release[i])
        {
            RELEASE(normalized_specs[i]);
        }
    }
    return NULL; // Clojure-compatible: require returns nil
}

// File I/O: spit - write string to file
#ifdef ESP32_BUILD
ID native_spit(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 2, "spit"))
        return NULL;
    (void)args;
    // No-op on ESP32 (filesystem may be unavailable).
    return NULL;
}
#else
ID native_spit(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 2, "spit"))
        return NULL;

    // Convert first argument (filename) to CljString, then get C-string data
    CljString *filename_str_obj = to_string(args[0]);
    if (!filename_str_obj)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "spit requires a string or symbol as first argument (filename)",
                        __FILE__, __LINE__, 0);
        return NULL;
    }
    const char *filename_str = string_data(filename_str_obj);

    // Convert second argument (content) to CljString, then get C-string data
    CljString *content_str_obj = to_string(args[1]);
    if (!content_str_obj)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "spit requires a string or symbol as second argument (content)",
                        __FILE__, __LINE__, 0);
        return NULL;
    }
    const char *content_str = string_data(content_str_obj);

    // Open file for writing (overwrites if exists)
    FILE *fp = fopen(filename_str, "w");
    if (!fp)
    {
        char error_msg[256];
        const char *err = strerror(errno);
        size_t pos = 0;
        pos = format_append(error_msg, pos, sizeof(error_msg),
                            "Cannot open file '");
        pos = format_append(error_msg, pos, sizeof(error_msg), filename_str);
        pos = format_append(error_msg, pos, sizeof(error_msg), "' for writing: ");
        format_append(error_msg, pos, sizeof(error_msg), err);
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, error_msg,
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    // Write content to file
    size_t content_len = string_length(content_str_obj);
    size_t bytes_written = fwrite(content_str, 1, content_len, fp);

    // Check for write errors
    if (bytes_written != content_len)
    {
        char error_msg[256];
        const char *err = strerror(errno);
        size_t pos = 0;
        pos = format_append(error_msg, pos, sizeof(error_msg),
                            "Error writing to file '");
        pos = format_append(error_msg, pos, sizeof(error_msg), filename_str);
        pos = format_append(error_msg, pos, sizeof(error_msg), "': ");
        format_append(error_msg, pos, sizeof(error_msg), err);
        fclose(fp);
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, error_msg,
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    // Ensure file is flushed
    if (fflush(fp) != 0)
    {
        char error_msg[256];
        const char *err = strerror(errno);
        size_t pos = 0;
        pos = format_append(error_msg, pos, sizeof(error_msg),
                            "Error flushing file '");
        pos = format_append(error_msg, pos, sizeof(error_msg), filename_str);
        pos = format_append(error_msg, pos, sizeof(error_msg), "': ");
        format_append(error_msg, pos, sizeof(error_msg), err);
        fclose(fp);
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, error_msg,
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    // Cleanup
    fclose(fp);

    // spit returns nil
    return NULL;
}
#endif // ESP32_BUILD

// Binary operations (inline for performance)
// Variadische Number-Reducer mit Single-Pass und Float-Promotion
ID native_add_variadic(ID *args, unsigned int argc)
{
    if (argc == 0)
        return create_fixnum_result(0);
    if (argc == 1)
        return RETAIN(args[0]);

    bool sawFixed = false;
    int acc_i = 0;
    int32_t acc_fixed = 0;

    for (unsigned int i = 0; i < argc; i++)
    {
        uint16_t tag = TAG(args[i]);
        if (!is_numeric_tag(tag)) {
            return throw_expected_number();
        }
        if (!sawFixed)
        {
            switch (tag)
            {
            case CLJ_INT:
            {
                int new_val = AS_FIXNUM(args[i]);
                // Check for integer overflow before addition
                if (acc_i > 0 && new_val > INT_MAX - acc_i)
                {
                    // Overflow detected - throw exception
                    return throw_arithmetic_overflow(ERR_INTEGER_OVERFLOW_ADDITION, acc_i, new_val);
                }
                else if (acc_i < 0 && new_val < INT_MIN - acc_i)
                {
                    // Underflow detected - throw exception
                    return throw_arithmetic_overflow(ERR_INTEGER_UNDERFLOW_ADDITION, acc_i, new_val);
                }
                else
                {
                    acc_i += new_val;
                }
                break;
            }
            case CLJ_FLOAT:
            {
                sawFixed = true;
                // Check for fixed-point overflow before conversion using original values
                float acc_f = (float)acc_i;
                float val_f = AS_FIXED(args[i]);
                float result = acc_f + val_f;
                if (result > 262144.0f || result < -262144.0f)
                { // Max fixed-point range
                    return throw_fixed_overflow(ERR_FIXED_OVERFLOW_ADDITION);
                }
                acc_fixed = fixnum_to_fixed(acc_i) + extract_fixed_value(args[i]);
                break;
            }
            }
        }
        else
        {
            int32_t val;
            switch (tag)
            {
            case CLJ_INT:
                val = fixnum_to_fixed(AS_FIXNUM(args[i]));
                break;
            case CLJ_FLOAT:
                val = extract_fixed_value(args[i]);
                break;
            }

            // Check for fixed-point addition overflow using original values
            float acc_f = (float)acc_fixed / 8192.0f;
            float val_f;
            switch (tag)
            {
            case CLJ_INT:
                val_f = (float)AS_FIXNUM(args[i]);
                break;
            case CLJ_FLOAT:
                val_f = AS_FIXED(args[i]);
                break;
            }
            float result = acc_f + val_f;
            if (result > 262144.0f || result < -262144.0f)
            { // Max fixed-point range
                return throw_fixed_overflow(ERR_FIXED_OVERFLOW_ADDITION);
            }

            acc_fixed += val;
        }
    }

    return sawFixed ? create_fixed_result(acc_fixed) : create_fixnum_result(acc_i);
}

ID native_mul_variadic(ID *args, unsigned int argc)
{
    if (argc == 0)
        return create_fixnum_result(1);
    if (argc == 1)
        return RETAIN(args[0]);

    bool sawFixed = false;
    int acc_i = 1;
    int32_t acc_fixed = 0;

    for (unsigned int i = 0; i < argc; i++)
    {
        uint16_t tag = TAG(args[i]);
        if (!is_numeric_tag(tag)) {
            return throw_expected_number();
        }
        if (!sawFixed)
        {
            switch (tag)
            {
            case CLJ_INT:
            {
                int new_val = AS_FIXNUM(args[i]);
                // Check for integer overflow before multiplication
                if (acc_i != 0 && new_val != 0)
                {
                    bool would_overflow = false;
                    if (new_val > 0)
                    {
                        // Standard overflow check for positive multiplier
                        would_overflow = (acc_i > INT_MAX / new_val || acc_i < INT_MIN / new_val);
                    }
                    else
                    {
                        // Negative multiplier: check based on sign of accumulator
                        // Positive * negative = negative: check if result < INT_MIN
                        // Negative * negative = positive: check if result > INT_MAX
                        // Special case: new_val == -1
                        if (new_val == -1)
                        {
                            // acc_i * -1 = -acc_i
                            // Overflow if: acc_i == INT_MIN (would make -acc_i overflow)
                            // Note: acc_i can't be > INT_MAX since it's an int
                            would_overflow = (acc_i == INT_MIN);
                        }
                        else
                        {
                            // For acc_i > 0 and new_val < 0: check if acc_i * new_val < INT_MIN
                            //   => acc_i > INT_MIN / new_val (since new_val is negative, division rounds toward 0)
                            // For acc_i < 0 and new_val < 0: check if acc_i * new_val > INT_MAX
                            //   => acc_i < INT_MAX / new_val (since both are negative, division rounds toward 0)
                            would_overflow = (acc_i > 0)
                                                 ? (acc_i > INT_MIN / new_val)  // acc_i * new_val < INT_MIN if acc_i > INT_MIN / new_val
                                                 : (acc_i < INT_MAX / new_val); // acc_i * new_val > INT_MAX if acc_i < INT_MAX / new_val
                        }
                    }
                    if (would_overflow)
                    {
                        return throw_arithmetic_overflow(ERR_INTEGER_OVERFLOW_MULTIPLICATION, acc_i, new_val);
                    }
                    acc_i *= new_val;
                }
                else
                {
                    acc_i *= new_val; // Safe: one operand is 0
                }
                break;
            }
            case CLJ_FLOAT:
            {
                sawFixed = true;
                // Check for fixed-point overflow before conversion
                float acc_f = (float)acc_i;
                float val_f = AS_FIXED(args[i]);
                if (acc_f != 0.0f && val_f != 0.0f)
                {
                    // Check if multiplication would exceed fixed-point range
                    float result = acc_f * val_f;
                    if (result > 262144.0f || result < -262144.0f)
                    { // Max fixed-point range
                        return throw_fixed_overflow(ERR_FIXED_OVERFLOW_MULTIPLICATION);
                    }
                }
                acc_fixed = (fixnum_to_fixed(acc_i) * extract_fixed_value(args[i])) >> 13;
                break;
            }
            }
        }
        else
        {
            int32_t val;
            switch (tag)
            {
            case CLJ_INT:
                val = fixnum_to_fixed(AS_FIXNUM(args[i]));
                break;
            case CLJ_FLOAT:
                val = extract_fixed_value(args[i]);
                break;
            }

            // Check for fixed-point multiplication overflow
            float acc_f = (float)acc_fixed / 8192.0f;
            float val_f;
            switch (tag)
            {
            case CLJ_INT:
                val_f = (float)AS_FIXNUM(args[i]);
                break;
            case CLJ_FLOAT:
                val_f = AS_FIXED(args[i]);
                break;
            }
            if (acc_f != 0.0f && val_f != 0.0f)
            {
                float result = acc_f * val_f;
                if (result > 262144.0f || result < -262144.0f)
                { // Max fixed-point range
                    return throw_fixed_overflow(ERR_FIXED_OVERFLOW_MULTIPLICATION);
                }
            }

            acc_fixed = (acc_fixed * val) >> 13; // Fixed-Point Multiplikation mit Shift
        }
    }

    return sawFixed ? create_fixed_result(acc_fixed) : create_fixnum_result(acc_i);
}

ID native_sub_variadic(ID *args, unsigned int argc)
{
    CHECK_ARITY_MIN(argc, 1, "sub");
    if (argc == 1)
    {
        if (!args[0])
        {
            throw_exception_formatted(EXCEPTION_TYPE, __FILE__, __LINE__, 0, ERR_EXPECTED_NUMBER);
        }
        uint16_t tag = TAG(args[0]);
        if (!is_numeric_tag(tag))
        {
            return throw_expected_number();
        }
        switch (tag)
        {
        case CLJ_INT:
            return create_fixnum_result(-AS_FIXNUM(args[0]));
        case CLJ_FLOAT:
        default:
            return create_fixed_result(-extract_fixed_value(args[0]));
        }
    }

    bool sawFixed = false;
    int32_t acc_fixed = 0;
    int acc_i = 0;

    uint16_t tag0 = TAG(args[0]);
    if (!is_numeric_tag(tag0)) {
        return throw_expected_number();
    }
    switch (tag0)
    {
    case CLJ_INT:
        acc_i = AS_FIXNUM(args[0]);
        break;
    case CLJ_FLOAT:
    default:
        sawFixed = true;
        acc_fixed = extract_fixed_value(args[0]);
        break;
    }

    for (unsigned int i = 1; i < argc; i++)
    {
        uint16_t tag = TAG(args[i]);
        if (!is_numeric_tag(tag)) {
            return throw_expected_number();
        }
        if (!sawFixed)
        {
            switch (tag)
            {
            case CLJ_INT:
            {
                int new_val = AS_FIXNUM(args[i]);
                // Check for integer overflow/underflow before subtraction
                if (acc_i > 0 && new_val < acc_i - INT_MAX)
                {
                    // Overflow detected - throw exception
                    return throw_arithmetic_overflow(ERR_INTEGER_OVERFLOW_SUBTRACTION, acc_i, new_val);
                }
                else if (acc_i < 0 && new_val > acc_i - INT_MIN)
                {
                    // Underflow detected - throw exception
                    return throw_arithmetic_overflow(ERR_INTEGER_UNDERFLOW_SUBTRACTION, acc_i, new_val);
                }
                else
                {
                    acc_i -= new_val;
                }
                break;
            }
            case CLJ_FLOAT:
            {
                acc_fixed = fixnum_to_fixed(acc_i);
                sawFixed = true;
                int32_t val = 0;
                switch (tag) {
                case CLJ_INT:
                    val = fixnum_to_fixed(AS_FIXNUM(args[i]));
                    break;
                case CLJ_FLOAT:
                    val = extract_fixed_value(args[i]);
                    break;
                default:
                    return throw_expected_number();
                }
                acc_fixed -= val;
                break;
            }
            }
        }
        else
        {
            int32_t val = 0;
            switch (tag) {
            case CLJ_INT:
                val = fixnum_to_fixed(AS_FIXNUM(args[i]));
                break;
            case CLJ_FLOAT:
                val = extract_fixed_value(args[i]);
                break;
            default:
                return throw_expected_number();
            }
            acc_fixed -= val;
        }
    }

    return sawFixed ? create_fixed_result(acc_fixed) : create_fixnum_result(acc_i);
}

ID native_mod(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 2, "mod"))
        return NULL;

    uint16_t tag_a = TAG(args[0]);
    uint16_t tag_b = TAG(args[1]);
    if (!is_numeric_tag(tag_a) || !is_numeric_tag(tag_b)) {
        return throw_expected_number();
    }
    if (tag_a == CLJ_INT && tag_b == CLJ_INT)
    {
        int a = AS_FIXNUM(args[0]);
        int b = AS_FIXNUM(args[1]);
        if (b == 0)
        {
            throw_exception_formatted(EXCEPTION_DIVISION_BY_ZERO, __FILE__, __LINE__, 0,
                                      "Division by zero: %d %% %d", a, b);
            return NULL;
        }
        return create_fixnum_result(a % b);
    }

    // For fixed-point or mixed types, convert to fixed and compute
    int32_t a_fixed;
    switch (TAG(args[0]))
    {
    case CLJ_INT:
        a_fixed = fixnum_to_fixed(AS_FIXNUM(args[0]));
        break;
    case CLJ_FLOAT:
        a_fixed = extract_fixed_value(args[0]);
        break;
    default:
        return throw_expected_number();
    }
    int32_t b_fixed;
    switch (TAG(args[1]))
    {
    case CLJ_INT:
        b_fixed = fixnum_to_fixed(AS_FIXNUM(args[1]));
        break;
    case CLJ_FLOAT:
        b_fixed = extract_fixed_value(args[1]);
        break;
    default:
        return throw_expected_number();
    }

    if (b_fixed == 0)
    {
        throw_exception_formatted(EXCEPTION_DIVISION_BY_ZERO, __FILE__, __LINE__, 0,
                                  "Division by zero in mod");
        return NULL;
    }

    // For fixed-point, we need to compute modulo at the fixed-point scale
    // This is a simplified version - for full precision, we'd need to handle the fixed-point arithmetic
    int a_int = a_fixed >> 13;
    int b_int = b_fixed >> 13;
    if (b_int == 0)
    {
        throw_exception_formatted(EXCEPTION_DIVISION_BY_ZERO, __FILE__, __LINE__, 0,
                                  "Division by zero: %d %% %d", a_int, b_int);
        return NULL;
    }
    return create_fixnum_result(a_int % b_int);
}

ID native_quot(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 2, "quot"))
        return NULL;

    uint16_t tag_a = TAG(args[0]);
    uint16_t tag_b = TAG(args[1]);
    if (!is_numeric_tag(tag_a) || !is_numeric_tag(tag_b)) {
        return throw_expected_number();
    }
    if (tag_a == CLJ_INT && tag_b == CLJ_INT)
    {
        int a = AS_FIXNUM(args[0]);
        int b = AS_FIXNUM(args[1]);
        if (b == 0)
        {
            throw_exception_formatted(EXCEPTION_DIVISION_BY_ZERO, __FILE__, __LINE__, 0,
                                      "Division by zero: %d / %d", a, b);
            return NULL;
        }
        // Clojure quot truncates toward zero (C integer division already does this)
        return create_fixnum_result(a / b);
    }

    // For fixed-point or mixed types, convert to fixed and compute
    int32_t a_fixed;
    switch (TAG(args[0]))
    {
    case CLJ_INT:
        a_fixed = fixnum_to_fixed(AS_FIXNUM(args[0]));
        break;
    case CLJ_FLOAT:
        a_fixed = extract_fixed_value(args[0]);
        break;
    default:
        return throw_expected_number();
    }
    int32_t b_fixed;
    switch (TAG(args[1]))
    {
    case CLJ_INT:
        b_fixed = fixnum_to_fixed(AS_FIXNUM(args[1]));
        break;
    case CLJ_FLOAT:
        b_fixed = extract_fixed_value(args[1]);
        break;
    default:
        return throw_expected_number();
    }

    if (b_fixed == 0)
    {
        throw_exception_formatted(EXCEPTION_DIVISION_BY_ZERO, __FILE__, __LINE__, 0,
                                  "Division by zero in quot");
        return NULL;
    }

    // For fixed-point, compute quotient at the fixed-point scale
    int a_int = a_fixed >> 13;
    int b_int = b_fixed >> 13;
    if (b_int == 0)
    {
        throw_exception_formatted(EXCEPTION_DIVISION_BY_ZERO, __FILE__, __LINE__, 0,
                                  "Division by zero: %d / %d", a_int, b_int);
        return NULL;
    }
    return create_fixnum_result(a_int / b_int);
}

ID native_bit_shift_left(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 2, "bit-shift-left"))
        return NULL;

    // Both arguments must be integers
    if (TAG(args[0]) != CLJ_INT || TAG(args[1]) != CLJ_INT)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "bit-shift-left requires integer arguments",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    int a = AS_FIXNUM(args[0]);
    int b = AS_FIXNUM(args[1]);

    // Clojure bit-shift-left: shift left by b bits
    // Note: C left shift is undefined for negative shift amounts or shift >= width
    if (b < 0 || b >= 32)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "bit-shift-left shift amount must be 0-31",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    return create_fixnum_result(a << b);
}

static ID native_range_infinite_thunk_executor(ID *targs, unsigned int targc) {
    if (targc != 1) return NULL;
    ID state_id = unwrap_thunk_state(targs[0]);
    if (!state_id || TAG(state_id) != CLJ_MAP_PERSISTENT) return NULL;

    CljPersistentMap *state = as_map(state_id);
    ID cur_id = map_get_sentinel(state, SYM_RANGE_CUR, NULL);
    if (!is_fixnum(cur_id)) return NULL;
    int cur = AS_FIXNUM(cur_id);

    CljPersistentMap *rest_state = map_empty();
    map_assoc_inplace(&rest_state, SYM_RANGE_CUR, fixnum(cur + 1));
    CljPersistentVector *rest_env_stack = make_vector(1, STRONG);
    if (!rest_env_stack) { RELEASE(rest_state); return NULL; }
    vector_conj_inplace(&rest_env_stack, rest_state);
    RELEASE(rest_state);
    ID fn_obj = cached_named_func(native_range_infinite_thunk_executor, SYM_RANGE_INF_THUNK_FN, &g_range_inf_thunk_fn_obj);
    ID thunk_body = make_thunk_body_ast_call(fn_obj);
    if (!thunk_body) { RELEASE(rest_env_stack); return NULL; }
    ID rest_param = SYM_THUNK_STATE;
    CljFunction *rest_thunk = make_function(&rest_param, 1, thunk_body, rest_env_stack, NULL, NULL);
    RELEASE(thunk_body);
    CljLazySeq *rest_lazy = make_lazy_seq(rest_thunk);
    if (rest_lazy && vector_count(rest_env_stack) > 0) rest_lazy->thunk_state = RETAIN(vector_nth(rest_env_stack, 0));
    RELEASE(rest_thunk);
    RELEASE(rest_env_stack);
    CljList *result = make_list(fixnum(cur), (CljList*)rest_lazy);
    RELEASE(rest_lazy);
    return AUTORELEASE(result);
}

ID native_range(ID *args, unsigned int argc)
{
    CHECK_ARITY_RANGE(argc, 0, 3, "range");
    ID native_lazy_seq_star(ID * args, unsigned int argc);

    // 0-arity: infinite lazy range starting at 0, step 1.
    if (argc == 0) {
        CljPersistentMap *state = map_empty();
        map_assoc_inplace(&state, SYM_RANGE_CUR, fixnum(0));
        CljPersistentVector *env_stack = make_vector(1, STRONG);
        if (!env_stack) { RELEASE(state); return NULL; }
        vector_conj_inplace(&env_stack, state);
        RELEASE(state);
        ID fn_obj = cached_named_func(native_range_infinite_thunk_executor, SYM_RANGE_INF_THUNK_FN, &g_range_inf_thunk_fn_obj);
        ID thunk_body = make_thunk_body_ast_call(fn_obj);
        if (!thunk_body) { RELEASE(env_stack); return NULL; }
        ID thunk_param = SYM_THUNK_STATE;
        CljFunction *thunk = make_function(&thunk_param, 1, thunk_body, env_stack, NULL, NULL);
        RELEASE(thunk_body);
        CljLazySeq *lazy = make_lazy_seq(thunk);
        if (lazy && vector_count(env_stack) > 0) lazy->thunk_state = RETAIN(vector_nth(env_stack, 0));
        RELEASE(thunk);
        RELEASE(env_stack);
        return lazy ? AUTORELEASE(lazy) : NULL;
    }

    int start = 0, end = 0, step = 1;

    if (argc == 1)
    {
        // (range end) => [0 1 2 ... end-1]
        end = AS_FIXNUM(args[0]);
        start = 0;
        step = 1;
    }
    else if (argc == 2)
    {
        // (range start end) => [start start+1 ... end-1]
        start = AS_FIXNUM(args[0]);
        end = AS_FIXNUM(args[1]);
        step = 1;
    }
    else
    {
        // (range start end step) => [start start+step ... end-step]
        start = AS_FIXNUM(args[0]);
        end = AS_FIXNUM(args[1]);
        step = AS_FIXNUM(args[2]);
        if (step == 0)
        {
            throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "range step cannot be zero",
                            __FILE__, __LINE__, 0);
            return NULL;
        }
    }

    // Calculate size
    int size = 0;
    if (step > 0)
    {
        if (start >= end)
            size = 0;
        else
            size = (end - start + step - 1) / step;
    }
    else
    {
        if (start <= end)
            size = 0;
        else
            size = (start - end - step - 1) / (-step);
    }

    if (size < 0)
        size = 0;

    // Return empty vector singleton if size is 0
    if (size == 0)
    {
        return empty_vector();
    }

    // Create vector with calculated capacity
    ID vec = make_vector(size, STRONG);
    CljPersistentVector *v = as_vector(vec);

    // Fill vector
    for (int i = start; (step > 0) ? (i < end) : (i > end); i += step)
    {
        ID val = create_fixnum_result(i);
        v = vector_conj(v, val);
    }

    return AUTORELEASE(vec);
}

ID native_repeat(ID *args, unsigned int argc)
{
    CLJ_ASSERT(args != NULL);

    int count = 0;
    ID value;

    if (argc == 1)
    {
        // (repeat x) - use large count instead of infinite sequence
        // TODO: When lazy sequences are implemented, replace this with a proper lazy infinite sequence
        // Use large but practical value (1 million should be enough for most use cases)
        count = 1000000;
        value = args[0];
    }
    else if (argc == 2)
    {
        // (repeat n x) - create vector with n repetitions of x
        if (TAG(args[0]) != CLJ_INT)
        {
            throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "repeat count must be an integer",
                            __FILE__, __LINE__, 0);
            return NULL;
        }
        count = AS_FIXNUM(args[0]);
        if (count < 0)
        {
            throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "repeat count cannot be negative",
                            __FILE__, __LINE__, 0);
            return NULL;
        }
        value = args[1];
    }
    else
    {
        char error_msg[256];
        size_t pos = 0;
        pos = format_append(error_msg, pos, sizeof(error_msg),
                            "repeat requires 1 or 2 arguments, got ");
        pos = format_append_uint(error_msg, pos, sizeof(error_msg), argc);
        throw_exception(EXCEPTION_ARITY, error_msg, __FILE__, __LINE__, 0);
        return NULL;
    }

    if (count == 0)
    {
        return empty_vector();
    }

    ID vec = make_vector(count, STRONG);
    CljPersistentVector *v = as_vector(vec);

    for (int i = 0; i < count; i++)
    {
        ID val = RETAIN(value);
        v = vector_conj(v, val);
        RELEASE(val);
    }

    return AUTORELEASE(vec);
}

ID native_lazy_seq_star(ID *args, unsigned int argc)
{
    CLJ_ASSERT(args != NULL);

    if (!validate_builtin_args(argc, 1, "lazy-seq*"))
        return NULL;

    ID f = args[0];
    if (!f || IS_IMMEDIATE(f) || !(TAG(f) == CLJ_FUNC || TAG(f) == CLJ_CLOSURE))
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "lazy-seq* requires a function (0-arity thunk)",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    CljLazySeq *lazy = make_lazy_seq(f);
    return lazy ? AUTORELEASE(lazy) : NULL;
}

ID native_math_sqrt(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 1, "Math/sqrt"))
        return NULL;

    float val = 0.f;
    uint16_t tag = TAG(args[0]);
    if (!is_numeric_tag(tag)) {
        return throw_expected_number();
    }
    switch (tag) {
    case CLJ_INT:
        val = (float)AS_FIXNUM(args[0]);
        break;
    case CLJ_FLOAT:
        val = AS_FIXED(args[0]);
        break;
    default:
        return throw_expected_number();
    }

    if (val < 0)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "Math/sqrt argument cannot be negative",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    double sqrt_result = sqrt((double)val);
    return create_fixed_result((int32_t)round(sqrt_result * (1 << 13)));
}

// native_format moved to builtins_strings.c

// Set current EvalState (called by eval_function_call before calling builtins)
void builtin_set_eval_state(EvalState *st)
{
    g_current_eval_state = st;
}

ID native_eval(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 1, "eval"))
        return NULL;

    if (!g_current_eval_state)
    {
        throw_exception(EXCEPTION_RUNTIME, "eval: EvalState not available",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    // Same canonicalization as require/load: symbol tokens, quote, non-special calls -> AST_CALL
    ID form = args[0];
    if (IS_IMMEDIATE(form)) return form;
    form = canonicalize_ast(form, g_current_eval_state);
    return eval_canonical_form(form, g_current_eval_state, NULL);
}

ID native_read_string(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 1, "read-string"))
        return NULL;

    if (!g_current_eval_state)
    {
        throw_exception(EXCEPTION_RUNTIME, "read-string: EvalState not available",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    // First argument must be a string
    if (TAG(args[0]) != CLJ_STRING)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "read-string argument must be a string",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    CljString *str = (CljString *)args[0];
    if (!str)
        return NULL;

    // Parse the string using parse_from_string (handles non-NUL-terminated views)
    ID parsed = parse_from_string(str, g_current_eval_state);

    // parse returns AUTORELEASE objects
    return parsed;
}

ID native_div_variadic(ID *args, unsigned int argc)
{
    CHECK_ARITY_MIN(argc, 1, "div");
    if (argc == 1)
    {
        if (!args[0])
        {
            throw_exception_formatted(EXCEPTION_TYPE, __FILE__, __LINE__, 0, ERR_EXPECTED_NUMBER);
        }
        uint16_t tag = TAG(args[0]);
        if (!is_numeric_tag(tag))
        {
            return throw_expected_number();
        }
        switch (tag)
        {
        case CLJ_INT:
        {
            int x = AS_FIXNUM(args[0]);
            if (x == 0)
            {
                // Division by zero - throw exception
                throw_exception_formatted(EXCEPTION_DIVISION_BY_ZERO, __FILE__, __LINE__, 0,
                                          "Division by zero: 1 / %d", x);
                return NULL;
            }
            if (1 % x == 0)
                return create_fixnum_result(1 / x);
            return create_fixed_result(fixnum_to_fixed(1) / x);
        }
        case CLJ_FLOAT:
        {
            int32_t x = extract_fixed_value(args[0]);
            if (x == 0)
            {
                // Division by zero - throw exception
                throw_exception_formatted(EXCEPTION_DIVISION_BY_ZERO, __FILE__, __LINE__, 0,
                                          "Division by zero: 1 / %d", x >> 13);
                return NULL;
            }
            return create_fixed_result(fixnum_to_fixed(1) / x);
        }
        }
    }

    bool sawFixed = false;
    int32_t acc_fixed = 0;
    int acc_i = 0;

    uint16_t tag0 = TAG(args[0]);
    if (!is_numeric_tag(tag0)) {
        return throw_expected_number();
    }
    switch (tag0)
    {
    case CLJ_INT:
        acc_i = AS_FIXNUM(args[0]);
        break;
    case CLJ_FLOAT:
        sawFixed = true;
        acc_fixed = extract_fixed_value(args[0]);
        break;
    }

    for (unsigned int i = 1; i < argc; i++)
    {
        uint16_t tag = TAG(args[i]);
        if (!is_numeric_tag(tag)) {
            return throw_expected_number();
        }
        if (!sawFixed)
        {
            switch (tag)
            {
            case CLJ_INT:
            {
                int d = AS_FIXNUM(args[i]);
                if (d == 0)
                {
                    // Division by zero - throw exception
                    throw_exception_formatted(EXCEPTION_DIVISION_BY_ZERO, __FILE__, __LINE__, 0,
                                              "Division by zero: %d / %d", acc_i, d);
                    return NULL;
                }
                if (acc_i % d == 0)
                {
                    acc_i /= d;
                }
                else
                {
                    sawFixed = true;
                    acc_fixed = fixnum_to_fixed(acc_i) / d; // Fixnum zu Fixed promoten
                }
                break;
            }
            case CLJ_FLOAT:
            {
                if (!sawFixed)
                {
                    acc_fixed = fixnum_to_fixed(acc_i);
                    sawFixed = true;
                }
                int32_t d = 0;
                switch (tag) {
                case CLJ_INT:
                    d = fixnum_to_fixed(AS_FIXNUM(args[i]));
                    break;
                case CLJ_FLOAT:
                    d = extract_fixed_value(args[i]);
                    break;
                default:
                    return throw_expected_number();
                }
                if (d == 0)
                {
                    // Division by zero - throw exception
                    throw_exception_formatted(EXCEPTION_DIVISION_BY_ZERO, __FILE__, __LINE__, 0,
                                              "Division by zero: %d / %d", acc_fixed >> 13, d >> 13);
                    return NULL;
                }
                else
                {
                    acc_fixed = (acc_fixed << 13) / d; // Fixed-Point Division mit Shift
                }
                break;
            }
            }
        }
        else
        {
            int32_t d;
            switch (TAG(args[i]))
            {
            case CLJ_INT:
                d = fixnum_to_fixed(AS_FIXNUM(args[i]));
                break;
            default:
                d = extract_fixed_value(args[i]);
                break;
            }
            if (d == 0)
            {
                // Division by zero - throw exception
                throw_exception_formatted(EXCEPTION_DIVISION_BY_ZERO, __FILE__, __LINE__, 0,
                                          "Division by zero: %d / %d", acc_fixed >> 13, d >> 13);
                return NULL;
            }
            else
            {
                acc_fixed = (acc_fixed << 13) / d; // Fixed-Point Division mit Shift
            }
        }
    }

    return sawFixed ? create_fixed_result(acc_fixed) : create_fixnum_result(acc_i);
}

// Arithmetic functions - native_*_variadic implement operations directly (no wrappers)

// ============================================================================
// BYTE ARRAY BUILTINS
// ============================================================================

ID native_byte_array(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 1, "byte-array"))
        return NULL;

    // If argument is a fixnum, create array with that size
    switch (TAG(args[0]))
    {
    case CLJ_INT:
    {
        int size = AS_FIXNUM(args[0]);
        if (size < 0)
        {
            throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                      "byte-array size must be non-negative, got %d", size);
            return NULL;
        }
        return make_byte_array(size);
    }
    default:
        break;
    }

    // Otherwise, treat as sequence and create array from values
    ID seq = args[0];
    if (!seq)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "byte-array argument must be a number or sequence",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    // For now, only support vectors as sequences
    CljType seq_tag = TAG(seq);
    if (seq_tag == CLJ_VECTOR_PERSISTENT || seq_tag == CLJ_VECTOR_TRANSIENT)
    {
        CljPersistentVector *vec = as_vector(seq);
        int count = vector_count(vec);
        CljValue arr = (CljValue)make_byte_array(count);

        int i = 0;
        VECTOR_FOR_EACH(vec, elem)
        {
            if (!elem || TAG(elem) != CLJ_INT)
            {
                RELEASE(arr);
                RELEASE(elem);
                throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "byte-array sequence elements must be numbers",
                                __FILE__, __LINE__, 0);
                return NULL;
            }
            int val = AS_FIXNUM(elem);
            // elem lifetime is tied to vector - no release needed
            if (val < 0 || val > 255)
            {
                RELEASE(arr);
                throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                          "byte values must be 0-255, got %d", val);
                return NULL;
            }
            byte_array_set(arr, i, (uint8_t)val);
            i++;
        }

        return arr;
    }

    throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "byte-array currently only supports vectors as sequences",
                    __FILE__, __LINE__, 0);
    return NULL;
}

ID native_aget(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 2, "aget"))
        return NULL;

    ID arr = args[0];
    if (!arr || TAG(arr) != CLJ_BYTE_ARRAY)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "aget first argument must be a byte-array",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    if (TAG(args[1]) != CLJ_INT)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "aget index must be a number",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    int index = AS_FIXNUM(args[1]);
    uint8_t value = byte_array_get((CljValue)arr, index);
    return fixnum(value);
}

ID native_aset(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 3, "aset"))
        return NULL;

    ID arr = args[0];
    if (!arr || TAG(arr) != CLJ_BYTE_ARRAY)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "aset first argument must be a byte-array",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    if (TAG(args[1]) != CLJ_INT)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "aset index must be a number",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    if (TAG(args[2]) != CLJ_INT)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "aset value must be a number",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    int index = AS_FIXNUM(args[1]);
    int value = AS_FIXNUM(args[2]);

    if (value < 0 || value > 255)
    {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "byte value must be 0-255, got %d", value);
        return NULL;
    }

    byte_array_set((CljValue)arr, index, (uint8_t)value);
    return args[2]; // Return the value (Clojure-compatible)
}

ID native_alength(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 1, "alength"))
        return NULL;

    ID arr = args[0];
    if (!arr || TAG(arr) != CLJ_BYTE_ARRAY)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "alength argument must be a byte-array",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    int length = byte_array_length((CljValue)arr);
    return fixnum(length);
}

ID native_aclone(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 1, "aclone"))
        return NULL;

    ID arr = args[0];
    if (!arr || TAG(arr) != CLJ_BYTE_ARRAY)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "aclone argument must be a byte-array",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    return byte_array_clone((CljValue)arr);
}

// Comparison operators as native functions
ID native_lt(ID *args, unsigned int argc)
{
    (void)argc; // Suppress unused parameter warning
    CompareResult result;
    if (!compare_numeric_values(args[0], args[1], &result))
    {
        throw_exception(EXCEPTION_TYPE, "Expected number for < comparison",
                        __FILE__, __LINE__, 0);
        return NULL;
    }
    return (result == COMPARE_LESS) ? clj_true : clj_false;
}

ID native_gt(ID *args, unsigned int argc)
{
    (void)argc; // Suppress unused parameter warning
    CompareResult result;
    if (!compare_numeric_values(args[0], args[1], &result))
    {
        throw_exception(EXCEPTION_TYPE, "Expected number for > comparison",
                        __FILE__, __LINE__, 0);
        return NULL;
    }
    return (result == COMPARE_GREATER) ? clj_true : clj_false;
}

ID native_le(ID *args, unsigned int argc)
{
    (void)argc; // Suppress unused parameter warning
    CompareResult result;
    if (!compare_numeric_values(args[0], args[1], &result))
    {
        throw_exception(EXCEPTION_TYPE, "Expected number for <= comparison",
                        __FILE__, __LINE__, 0);
        return NULL;
    }
    return (result == COMPARE_LESS || result == COMPARE_EQUAL) ? clj_true : clj_false;
}

ID native_ge(ID *args, unsigned int argc)
{
    (void)argc; // Suppress unused parameter warning
    CompareResult result;
    if (!compare_numeric_values(args[0], args[1], &result))
    {
        throw_exception(EXCEPTION_TYPE, "Expected number for >= comparison",
                        __FILE__, __LINE__, 0);
        return NULL;
    }
    return (result == COMPARE_GREATER || result == COMPARE_EQUAL) ? clj_true : clj_false;
}

ID native_eq(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 2, "="))
        return NULL;

    ID a = args[0];
    ID b = args[1];

    // Clojure semantics: nil is a valid value for =
    if (!a && !b) return clj_true;
    if (!a || !b) return clj_false;

    // Try numeric comparison first
    float val_a, val_b;
    switch (TAG(a))
    {
    case CLJ_INT:
        val_a = (float)as_fixnum((CljValue)a);
        break;
    case CLJ_FLOAT:
        val_a = as_fixed((CljValue)a);
        break;
    default:
        // Not numeric, use general equality
        return clj_equal(a, b) ? clj_true : clj_false;
    }

    switch (TAG(b))
    {
    case CLJ_INT:
        val_b = (float)as_fixnum((CljValue)b);
        break;
    case CLJ_FLOAT:
        val_b = as_fixed((CljValue)b);
        break;
    default:
        // Not numeric, use general equality
        return clj_equal(a, b) ? clj_true : clj_false;
    }

    return val_a == val_b ? clj_true : clj_false;
}

ID native_not_eq(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 2, "not="))
        return NULL;

    ID a = args[0];
    ID b = args[1];

    if (!a || !b)
    {
        // Both nil: equal, so not= returns false
        if (!a && !b)
            return clj_false;
        // One nil, one not: not equal, so not= returns true
        return clj_true;
    }

    // Try numeric comparison first
    float val_a, val_b;
    bool a_numeric = false, b_numeric = false;
    switch (TAG(a))
    {
    case CLJ_INT:
        val_a = (float)as_fixnum((CljValue)a);
        a_numeric = true;
        break;
    case CLJ_FLOAT:
        val_a = as_fixed((CljValue)a);
        a_numeric = true;
        break;
    default:
        break;
    }

    switch (TAG(b))
    {
    case CLJ_INT:
        val_b = (float)as_fixnum((CljValue)b);
        b_numeric = true;
        break;
    case CLJ_FLOAT:
        val_b = as_fixed((CljValue)b);
        b_numeric = true;
        break;
    default:
        break;
    }

    // If both numeric, compare numerically
    if (a_numeric && b_numeric)
    {
        return val_a != val_b ? clj_true : clj_false;
    }

    // Otherwise use general equality, then invert
    bool equal = clj_equal(a, b);
    return equal ? clj_false : clj_true;
}

ID native_identical(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 2, "identical?"))
        return clj_false;
    return (args[0] == args[1]) ? clj_true : clj_false;
}

ID native_vector_p(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 1, "vector?"))
        return clj_false;
    return (args[0] && TAG(args[0]) == CLJ_VECTOR_PERSISTENT) ? clj_true : clj_false;
}

ID native_map_p(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 1, "map?"))
        return clj_false;
    return (args[0] && TAG(args[0]) == CLJ_MAP_PERSISTENT) ? clj_true : clj_false;
}

ID native_set_p(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 1, "set?"))
        return clj_false;
    return (args[0] && TAG(args[0]) == CLJ_HASHSET) ? clj_true : clj_false;
}

// ============================================================================
// Type Predicates (new)
// ============================================================================

ID native_number_p(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 1, "number?");
    return (is_fixnum(args[0]) || is_fixed(args[0])) ? clj_true : clj_false;
}

ID native_integer_p(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 1, "integer?");
    return is_fixnum(args[0]) ? clj_true : clj_false;
}

ID native_float_p(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 1, "float?");
    return is_fixed(args[0]) ? clj_true : clj_false;
}

ID native_string_p(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 1, "string?");
    return (args[0] && TAG(args[0]) == CLJ_STRING) ? clj_true : clj_false;
}

ID native_keyword_p(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 1, "keyword?");
    ID arg = args[0];
    if (IS_KEYWORD(arg)) return clj_true;
    if (arg && TAG(arg) == CLJ_SYMBOL_TOKEN) {
        CljSymbolToken *tok = (CljSymbolToken*)arg;
        const char *name = symbol_token_data(tok);
        return (name && name[0] == ':') ? clj_true : clj_false;
    }
    return clj_false;
}

// Keyword = symbol with cname[0]==':', same interning. Normalize: strip leading ':' from inputs.
// (keyword "name") => keyword with ns = current namespace, cname = ":name" (like ::name in reader).
// (keyword "namespace" "name") => :namespace/name. Leading colons stripped from both args.
ID native_keyword(ID *args, unsigned int argc)
{
    CHECK_ARITY_RANGE(argc, 1, 2, "keyword");
    if (argc == 2) {
        const char *ns_str = NULL, *name_str = NULL;
        if (TAG(args[0]) == CLJ_STRING) ns_str = string_data(args[0]);
        else if (TAG(args[0]) == CLJ_SYMBOL) { CljSymbol *s = as_symbol(args[0]); ns_str = s ? s->cname : NULL; }
        if (TAG(args[1]) == CLJ_STRING) name_str = string_data(args[1]);
        else if (TAG(args[1]) == CLJ_SYMBOL) { CljSymbol *s = as_symbol(args[1]); name_str = s ? s->cname : NULL; }
        if (!ns_str || !*ns_str || !name_str || !*name_str) return NULL;
        while (*ns_str == ':') ns_str++;
        if (!*ns_str) return NULL;
        const char *name_clean = (*name_str == ':') ? name_str + 1 : name_str;
        if (!*name_clean) return NULL;
        CljSymbol *ns_sym = intern_symbol_global(ns_str);
        char kw_cname[256] = {0};
        size_t pos = format_append_char(kw_cname, 0, sizeof(kw_cname), ':');
        format_append(kw_cname, pos, sizeof(kw_cname), name_clean);
        return intern_symbol(ns_sym, kw_cname);
    }
    ID arg = args[0];
    if (!arg) return NULL;
    if (IS_KEYWORD(arg)) return arg;
    const char *name = NULL;
    if (TAG(arg) == CLJ_STRING) name = string_data(arg);
    else if (TAG(arg) == CLJ_SYMBOL) { CljSymbol *sym = as_symbol(arg); name = sym ? sym->cname : NULL; }
    if (!name || !*name) return NULL;
    while (*name == ':') name++;
    if (!*name) return NULL;
    char kw_name[256] = {0};
    size_t pos = format_append_char(kw_name, 0, sizeof(kw_name), ':');
    format_append(kw_name, pos, sizeof(kw_name), name);
    EvalState *st = g_current_eval_state;
    if (st && st->current_ns && st->current_ns->name)
        return intern_symbol(st->current_ns->name, kw_name);
    return intern_symbol_global(kw_name);
}

// (name x) - returns the name string of a symbol or keyword (without namespace or colon)
// (name :foo) => "foo"
// (name 'bar) => "bar"
ID native_name(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 1, "name");
    ID arg = args[0];
    const unsigned char tag = TAG(arg);

    if (!arg)
        return NULL;

    switch (tag) {
        case CLJ_STRING:
            return arg;  // String? Return as-is
        case CLJ_SYMBOL: {
            CljSymbol *sym = as_symbol(arg);
            if (!sym || !sym->cname)
                return NULL;

            const char *name = sym->cname;
            // Skip leading colon for keywords
            if (name[0] == ':')
                name++;

            return make_string(name);
        }
        case CLJ_SYMBOL_TOKEN: {
            const char *name = symbol_token_data((CljSymbolToken*)arg);
            if (!name || !*name) return NULL;
            if (name[0] == ':') name++;
            return make_string(name);
        }
        default:
            break;
    }

    return NULL;
}

ID native_symbol_p(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 1, "symbol?");
    return (args[0] && TAG(args[0]) == CLJ_SYMBOL && !IS_KEYWORD(args[0])) ? clj_true : clj_false;
}

ID native_fn_p(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 1, "fn?");
    if (!args[0])
        return clj_false;
    unsigned char tag = TAG(args[0]);
    return (tag == CLJ_FUNC || tag == CLJ_CLOSURE) ? clj_true : clj_false;
}

ID native_atom_p(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 1, "atom?");
    return (args[0] && TAG(args[0]) == CLJ_ATOM) ? clj_true : clj_false;
}

ID native_char_p(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 1, "char?");
    return is_character(args[0]) ? clj_true : clj_false;
}

ID native_list_p(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 1, "list?");
    // Treat CLJ_LIST and CLJ_AST_NODE as list-like. Macros/quasiquote operate on
    // parsed forms, which are typically CLJ_AST_NODE.
    return (args[0] && is_list_type(TAG(args[0]))) ? clj_true : clj_false;
}

// native_time removed: time is now only a special form (eval_time)
// This ensures time can measure actual evaluation time, not pre-evaluated arguments

// -----------------------------------------------------------------------------
// yield/current-time-ms hooks (override in tests if needed)
// -----------------------------------------------------------------------------
__attribute__((weak)) void tinyclj_runloop_once_for_yield(unsigned int timeout_ms) {
    platform_runloop_run_once(timeout_ms);
}

__attribute__((weak)) uint32_t tinyclj_current_time_ms_for_sleep(void) {
    return platform_current_time_ms();
}

// -----------------------------------------------------------------------------
// yield/current-time-ms native primitives (used by clojure.core :native stubs)
// -----------------------------------------------------------------------------
ID native_yield(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 1, "yield"))
        return NULL;

    ID ms_obj = args[0];
    if (!ms_obj || TAG(ms_obj) != CLJ_INT)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "yield requires integer milliseconds",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    int ms = as_fixnum(ms_obj);
    if (ms < 0)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "yield requires non-negative milliseconds",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    tinyclj_runloop_once_for_yield((unsigned int)ms);
    return NULL;
}

ID native_current_time_ms(ID *args, unsigned int argc)
{
    (void)args;
    if (!validate_builtin_args(argc, 0, "current-time-ms"))
        return NULL;

    // IMPORTANT: CLJ_INT is a fixnum (29-bit). Keep the value bounded.
    // platform_current_time_ms() is defined to return milliseconds within a 24h window.
    uint32_t ms = tinyclj_current_time_ms_for_sleep();
    if (ms >= 86400000u) ms = ms % 86400000u;
    return fixnum((int32_t)ms);
}

// ============================================================================
// ATOM FUNCTIONS
// ============================================================================

// Native atom implementation
ID native_atom(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 1, "atom"))
        return NULL;

    ID value = args[0]; // Can be NULL (nil) or immediate
    CljAtom *atom = make_atom(value);

    return atom;
}

// Native deref implementation
ID native_deref(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 1, "deref"))
        return NULL;

    ID obj = args[0];
    if (!obj || TAG(obj) != CLJ_ATOM)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "deref requires an atom",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    CljAtom *atom = as_atom(obj);
    ID value = atom_deref(atom);

    return value; // Can be NULL (nil) or immediate
}

// Native reset! implementation
ID native_reset_bang(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 2, "reset!"))
        return NULL;

    ID obj = args[0];
    if (!obj || TAG(obj) != CLJ_ATOM)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "reset! requires an atom",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    CljAtom *atom = as_atom(obj);
    ID new_value = args[1]; // Can be NULL (nil) or immediate

    ID result = atom_reset(atom, new_value);

    return result; // Returns new value (can be NULL/nil or immediate)
}

// Native swap! implementation
ID native_swap_bang(ID *args, unsigned int argc)
{
    if (argc < 2)
    {
        throw_exception(EXCEPTION_ARITY, "swap! requires at least 2 arguments (atom and function)",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    ID obj = args[0];
    if (!obj || TAG(obj) != CLJ_ATOM)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "swap! requires an atom",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    CljAtom *atom = as_atom(obj);
    ID fn = args[1];

    if (!fn)
    {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "swap! requires a function",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    // Prepare additional arguments (if any)
    ID *fn_args = NULL;
    unsigned int fn_argc = 0;

    if (argc > 2)
    {
        fn_argc = argc - 2;
        // Use malloc instead of calloc - array is immediately filled
        fn_args = (ID *)CLJ_MALLOC(fn_argc * sizeof(ID));

        for (unsigned int i = 0; i < fn_argc; i++)
        {
            fn_args[i] = args[i + 2];
        }
    }

    ID result = atom_swap(atom, fn, fn_args, fn_argc);

    if (fn_args)
    {
        CLJ_FREE(fn_args);
    }

    return result; // Returns new value (can be NULL/nil or immediate)
}

// Note: def and ns are special forms (not builtins) because they require non-evaluated arguments.
// They are handled in eval_ast_call() (AST_CALL dispatch) and eval_list() only for legacy list forms.

// now: Atomic timestamp as map {:days epoch-days :ms millis-in-day}
// Single gettimeofday() call ensures consistency (no race condition at midnight)
ID native_now(ID *args, unsigned int argc)
{
    (void)args;
    if (argc != 0)
    {
        throw_exception(EXCEPTION_ARITY, "now takes no arguments", NULL, 0, 0);
        return NULL;
    }

    struct timeval tv;
    gettimeofday(&tv, NULL);

    // Calculate both values from same timestamp
    int32_t days = (int32_t)(tv.tv_sec / 86400);
    int32_t sec_in_day = tv.tv_sec % 86400;
    int32_t millis = sec_in_day * 1000 + tv.tv_usec / 1000;

    // Return Instant (days + millis-in-day)
    return AUTORELEASE(make_instant(days, (uint32_t)millis));
}

// -----------------------------------------------------------------------------
// libs support: tiny-clj.runtime/stats + clojure.pprint/pprint-str
// -----------------------------------------------------------------------------

static ID native_tinyclj_runtime_stats(ID *args, unsigned int argc)
{
    (void)args;
    if (argc != 0)
    {
        throw_exception(EXCEPTION_ARITY, "tiny-clj.runtime/stats takes no arguments", __FILE__, __LINE__, 0);
    }

    const char *os_name =
#if defined(__APPLE__)
        "darwin";
#elif defined(__linux__) && !defined(ESP_PLATFORM)
        "linux";
#elif defined(ESP_PLATFORM)
        "ESP/IDF";
#else
        "unknown";
#endif

    ID v_os = make_string(os_name);
    ID v_ver = make_string("0.3");

    CljPersistentMap *m;
#if defined(BUILD_EPOCH_SECONDS)
    int32_t days = (int32_t)(BUILD_EPOCH_SECONDS / 86400);
    uint32_t millis = (uint32_t)((BUILD_EPOCH_SECONDS % 86400) * 1000);
    ID k_build_time = intern_symbol_global(":build-time");
    ID v_build_time = make_instant(days, millis);
    m = make_map_from_kv(3, SYM_KW_OS, v_os, SYM_KW_VERSION, v_ver, k_build_time, v_build_time);
    RELEASE(v_os);
    RELEASE(v_ver);
    RELEASE(v_build_time);
#else
    m = make_map_from_kv(2, SYM_KW_OS, v_os, SYM_KW_VERSION, v_ver);
    RELEASE(v_os);
    RELEASE(v_ver);
#endif

    if (!m) return NULL;

#define MAP_REASSIGN(var, expr) do { \
    CljPersistentMap *_next_map = (expr); \
    if (_next_map != (var)) { \
        RETAIN(_next_map); \
        RELEASE(var); \
        (var) = _next_map; \
    } \
} while (0)

    {
        const char *os_ver = platform_os_version();
        if (os_ver && os_ver[0] != '\0' && SYM_KW_OS_VERSION) {
            ID v_os_ver = make_string(os_ver);
            MAP_REASSIGN(m, map_assoc(m, SYM_KW_OS_VERSION, v_os_ver));
            RELEASE(v_os_ver);
        }
    }

#if defined(DEBUG)
    // Debug diagnostics: symbols, namespaces, heap (always available)
    MAP_REASSIGN(m, map_assoc(m, SYM_KW_SYMBOLS, fixnum((int32_t)hashset_count(g_runtime.symbol_table))));
    MAP_REASSIGN(m, map_assoc(m, SYM_KW_NAMESPACES, fixnum((int32_t)map_count(g_runtime.ns_registry))));
    
    // Heap usage (always tracked in DEBUG, same keys as full profiling)
    size_t bytes_current = g_memory_stats.current_memory_usage;
    size_t bytes_peak = g_memory_stats.peak_memory_usage;
    int32_t fc = (bytes_current > (size_t)FIXNUM_MAX) ? (int32_t)FIXNUM_MAX : (int32_t)bytes_current;
    int32_t fp = (bytes_peak > (size_t)FIXNUM_MAX) ? (int32_t)FIXNUM_MAX : (int32_t)bytes_peak;
    MAP_REASSIGN(m, map_assoc(m, SYM_KW_BYTES_CURRENT, fixnum(fc)));
    MAP_REASSIGN(m, map_assoc(m, SYM_KW_BYTES_PEAK, fixnum(fp)));
#endif

#if defined(DEBUG)
    // Memory stats: always include a minimal map; extend when profiling is enabled.
    CljPersistentMap *ms = make_map(12);
    if (ms) {
        MAP_REASSIGN(ms, map_assoc(ms, SYM_KW_BYTES_CURRENT, fixnum(fc)));
        MAP_REASSIGN(ms, map_assoc(ms, SYM_KW_BYTES_PEAK, fixnum(fp)));

#if defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
        // Extended profiling stats (nested map with per-type breakdown)
        #define CLAMP_FIXNUM(val) ((val) > (size_t)FIXNUM_MAX ? (int32_t)FIXNUM_MAX : (int32_t)(val))

        MAP_REASSIGN(ms, map_assoc(ms, SYM_KW_RAW_BYTES_CURRENT, fixnum(CLAMP_FIXNUM(g_memory_stats.raw_bytes_current))));
        MAP_REASSIGN(ms, map_assoc(ms, SYM_KW_RAW_BYTES_PEAK, fixnum(CLAMP_FIXNUM(g_memory_stats.raw_bytes_peak))));
        MAP_REASSIGN(ms, map_assoc(ms, SYM_KW_RAW_BLOCKS_CURRENT, fixnum(CLAMP_FIXNUM(g_memory_stats.raw_blocks_current))));
        MAP_REASSIGN(ms, map_assoc(ms, SYM_KW_RAW_BLOCKS_PEAK, fixnum(CLAMP_FIXNUM(g_memory_stats.raw_blocks_peak))));
        MAP_REASSIGN(ms, map_assoc(ms, SYM_KW_TOTAL_ALLOCATIONS, fixnum(CLAMP_FIXNUM(g_memory_stats.total_allocations))));
        MAP_REASSIGN(ms, map_assoc(ms, SYM_KW_TOTAL_DEALLOCATIONS, fixnum(CLAMP_FIXNUM(g_memory_stats.total_deallocations))));
        MAP_REASSIGN(ms, map_assoc(ms, SYM_KW_MEMORY_LEAKS, fixnum(CLAMP_FIXNUM(g_memory_stats.memory_leaks))));

        // Per-type breakdown
        CljPersistentMap *by_type = make_map(0);
        if (by_type) {
            for (int ti = 0; ti < CLJ_TYPE_COUNT; ti++) {
                size_t bc = g_memory_stats.bytes_current_by_type[ti];
                size_t bp = g_memory_stats.bytes_peak_by_type[ti];
                if (bc == 0 && bp == 0) continue;

                CljPersistentMap *row = make_map(4);
                if (!row) break;
                MAP_REASSIGN(row, map_assoc(row, SYM_KW_BYTES_CURRENT, fixnum(CLAMP_FIXNUM(bc))));
                MAP_REASSIGN(row, map_assoc(row, SYM_KW_BYTES_PEAK, fixnum(CLAMP_FIXNUM(bp))));
                MAP_REASSIGN(row, map_assoc(row, SYM_KW_ALLOC_COUNT, fixnum(CLAMP_FIXNUM(g_memory_stats.allocations_by_type[ti]))));
                MAP_REASSIGN(row, map_assoc(row, SYM_KW_DEALLOC_COUNT, fixnum(CLAMP_FIXNUM(g_memory_stats.deallocations_by_type[ti]))));
                ID type_name = make_string(clj_type_name((CljType)ti));
                MAP_REASSIGN(by_type, map_assoc(by_type, type_name, row));
                RELEASE(type_name);
                RELEASE(row);
            }
            MAP_REASSIGN(ms, map_assoc(ms, SYM_KW_BYTES_BY_TYPE, by_type));
            RELEASE(by_type);
        }

        #undef CLAMP_FIXNUM
#endif

        // Platform heap free (e.g. ESP32); only when available
        {
            size_t heap_free = platform_heap_bytes_free();
            if (heap_free != (size_t)-1) {
                int32_t hf = (heap_free > (size_t)FIXNUM_MAX) ? (int32_t)FIXNUM_MAX : (int32_t)heap_free;
                CljSymbol *kw = intern_symbol_global(":heap-bytes-free");
                if (kw) MAP_REASSIGN(ms, map_assoc(ms, kw, fixnum(hf)));
            }
        }
        // Optional external RAM total (e.g. PSRAM on ESP32)
        {
            size_t ext_ram = platform_external_ram_bytes_total();
            if (ext_ram != (size_t)-1 && SYM_KW_EXTERNAL_RAM_TOTAL) {
                int32_t er = (ext_ram > (size_t)FIXNUM_MAX) ? (int32_t)FIXNUM_MAX : (int32_t)ext_ram;
                MAP_REASSIGN(ms, map_assoc(ms, SYM_KW_EXTERNAL_RAM_TOTAL, fixnum(er)));
            }
        }

        MAP_REASSIGN(m, map_assoc(m, SYM_KW_MEMORY_STATS, ms));
        RELEASE(ms);
    }
#endif

#if !defined(DEBUG)
    // Minimal :memory-stats when platform provides heap (e.g. ESP32 Release)
    {
        size_t heap_free = platform_heap_bytes_free();
        if (heap_free != (size_t)-1) {
            CljPersistentMap *ms = make_map(2);
            if (ms) {
                int32_t hf = (heap_free > (size_t)FIXNUM_MAX) ? (int32_t)FIXNUM_MAX : (int32_t)heap_free;
                CljSymbol *kw_free = intern_symbol_global(":heap-bytes-free");
                if (kw_free) MAP_REASSIGN(ms, map_assoc(ms, kw_free, fixnum(hf)));
                size_t heap_total = platform_heap_bytes_total();
                if (heap_total != (size_t)-1) {
                    int32_t ht = (heap_total > (size_t)FIXNUM_MAX) ? (int32_t)FIXNUM_MAX : (int32_t)heap_total;
                    CljSymbol *kw_total = intern_symbol_global(":heap-bytes-total");
                    if (kw_total) MAP_REASSIGN(ms, map_assoc(ms, kw_total, fixnum(ht)));
                }
                size_t ext_ram = platform_external_ram_bytes_total();
                if (ext_ram != (size_t)-1 && SYM_KW_EXTERNAL_RAM_TOTAL) {
                    int32_t er = (ext_ram > (size_t)FIXNUM_MAX) ? (int32_t)FIXNUM_MAX : (int32_t)ext_ram;
                    MAP_REASSIGN(ms, map_assoc(ms, SYM_KW_EXTERNAL_RAM_TOTAL, fixnum(er)));
                }
                CljSymbol *kw_ms = intern_symbol_global(":memory-stats");
                if (kw_ms) MAP_REASSIGN(m, map_assoc(m, kw_ms, ms));
                RELEASE(ms);
            }
        }
    }
#endif

    // Optional :hardware map (flat: model, cores, revision, gpio-pin-count, psram-bytes, wifi, ble, bt, ...)
    {
        PlatformHardwareInfo hw;
        platform_hardware_info(&hw);
        if (hw.valid) {
            CljPersistentMap *hm = make_map(16);
            if (hm) {
                if (SYM_KW_MODEL) {
                    ID model = make_string(hw.model);
                    MAP_REASSIGN(hm, map_assoc(hm, SYM_KW_MODEL, model));
                    RELEASE(model);
                }
                if (SYM_KW_CORES) MAP_REASSIGN(hm, map_assoc(hm, SYM_KW_CORES, fixnum((int32_t)hw.cores)));
                if (SYM_KW_REVISION) MAP_REASSIGN(hm, map_assoc(hm, SYM_KW_REVISION, fixnum((int32_t)hw.revision)));
                if (SYM_KW_GPIO_PIN_COUNT && hw.gpio_pin_count > 0) {
                    MAP_REASSIGN(hm, map_assoc(hm, SYM_KW_GPIO_PIN_COUNT, fixnum((int32_t)hw.gpio_pin_count)));
                }
                {
                    size_t psram = platform_external_ram_bytes_total();
                    if (psram != (size_t)-1 && SYM_KW_PSRAM_BYTES) {
                        int32_t pb = (psram > (size_t)FIXNUM_MAX) ? (int32_t)FIXNUM_MAX : (int32_t)psram;
                        MAP_REASSIGN(hm, map_assoc(hm, SYM_KW_PSRAM_BYTES, fixnum(pb)));
                    }
                }
                if (hw.features != 0) {
                    if (SYM_KW_WIFI) MAP_REASSIGN(hm, map_assoc(hm, SYM_KW_WIFI, (hw.features & PLATFORM_HW_FEATURE_WIFI_BGN) ? clj_true : clj_false));
                    if (SYM_KW_BLE) MAP_REASSIGN(hm, map_assoc(hm, SYM_KW_BLE, (hw.features & PLATFORM_HW_FEATURE_BLE) ? clj_true : clj_false));
                    if (SYM_KW_BT) MAP_REASSIGN(hm, map_assoc(hm, SYM_KW_BT, (hw.features & PLATFORM_HW_FEATURE_BT) ? clj_true : clj_false));
                    if (SYM_KW_EMB_FLASH) MAP_REASSIGN(hm, map_assoc(hm, SYM_KW_EMB_FLASH, (hw.features & PLATFORM_HW_FEATURE_EMB_FLASH) ? clj_true : clj_false));
                    if (SYM_KW_EMB_PSRAM) MAP_REASSIGN(hm, map_assoc(hm, SYM_KW_EMB_PSRAM, (hw.features & PLATFORM_HW_FEATURE_EMB_PSRAM) ? clj_true : clj_false));
                    if (SYM_KW_IEEE802154) MAP_REASSIGN(hm, map_assoc(hm, SYM_KW_IEEE802154, (hw.features & PLATFORM_HW_FEATURE_IEEE802154) ? clj_true : clj_false));
                }
                if (SYM_KW_HARDWARE) MAP_REASSIGN(m, map_assoc(m, SYM_KW_HARDWARE, hm));
                RELEASE(hm);
            }
        }
    }

#undef MAP_REASSIGN

    return AUTORELEASE(m);
}

// tiny pprint helpers (file-local)
static const int PPRINT_INDENT = 2;
static const size_t PPRINT_INLINE_MAX = 0; // force multiline layout to match clojure.pprint expectations in tests

struct pprint_ctx {
    char *buf;
    size_t *pos;
    size_t max;
};

static void pprint_append(struct pprint_ctx *ctx, const char *s)
{
    *(ctx->pos) = format_append(ctx->buf, *(ctx->pos), ctx->max, s);
}

static int pprint_fits_inline(ID v)
{
    CljString *s = to_string(v);
    if (!s) return 0;
    const char *data = string_data(s);
    return data && strlen(data) <= PPRINT_INLINE_MAX;
}

static void pprint_walk(ID v, int indent, struct pprint_ctx *ctx)
{
    unsigned char tag = v ? TAG(v) : CLJ_NIL;

    // Maps
    if (tag == CLJ_MAP_PERSISTENT) {
        if (pprint_fits_inline(v)) {
            CljString *s = to_string(v);
            pprint_append(ctx, s ? string_data(s) : "nil");
            return;
        }
        pprint_append(ctx, "{\n");
        CljPersistentMap *m = (CljPersistentMap*)v;
        MAP_FOR_EACH(m, k, val) {
            for (int i = 0; i < indent + PPRINT_INDENT; i++) pprint_append(ctx, " ");
            CljString *ks = to_string(k);
            pprint_append(ctx, ks ? string_data(ks) : "nil");
            pprint_append(ctx, " ");
            // Recurse for value
            pprint_walk(val, indent + PPRINT_INDENT, ctx);
            pprint_append(ctx, "\n");
        }
        for (int i = 0; i < indent; i++) pprint_append(ctx, " ");
        pprint_append(ctx, "}");
        return;
    }

    // Vectors
    if (tag == CLJ_VECTOR_PERSISTENT || tag == CLJ_VECTOR_TRANSIENT) {
        if (pprint_fits_inline(v)) {
            CljString *s = to_string(v);
            pprint_append(ctx, s ? string_data(s) : "nil");
            return;
        }
        pprint_append(ctx, "[\n");
        CljPersistentVector *vec = (tag == CLJ_VECTOR_TRANSIENT)
            ? vector_persistent(as_transient_vector(v))
            : as_persistent_vector(v);
        unsigned int cnt = vector_count(vec);
        for (unsigned int i = 0; i < cnt; i++) {
            ID elem = vector_nth(vec, i);
            for (int j = 0; j < indent + PPRINT_INDENT; j++) pprint_append(ctx, " ");
            pprint_walk(elem, indent + PPRINT_INDENT, ctx);
            pprint_append(ctx, "\n");
        }
        for (int i = 0; i < indent; i++) pprint_append(ctx, " ");
        pprint_append(ctx, "]");
        return;
    }

    // Lists / seqs
    if (tag == CLJ_LIST || tag == CLJ_AST_NODE || tag == CLJ_SEQ || tag == CLJ_LAZY_SEQ) {
        if (pprint_fits_inline(v)) {
            CljString *s = to_string(v);
            pprint_append(ctx, s ? string_data(s) : "nil");
            return;
        }
        pprint_append(ctx, "(\n");
        SeqIterator it;
        if (v && seq_iter_init(&it, v)) {
            while (!seq_iter_empty(&it)) {
                ID e = seq_iter_first(&it);
                for (int j = 0; j < indent + PPRINT_INDENT; j++) pprint_append(ctx, " ");
                pprint_walk(e, indent + PPRINT_INDENT, ctx);
                pprint_append(ctx, "\n");
                seq_iter_next(&it);
            }
        }
        for (int i = 0; i < indent; i++) pprint_append(ctx, " ");
        pprint_append(ctx, ")");
        return;
    }

    // Fallback: render with to_string
    CljString *s = to_string(v);
    pprint_append(ctx, s ? string_data(s) : "nil");
}

static ID native_clojure_pprint_pprint_str(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 1, "clojure.pprint/pprint-str"))
        return NULL;

    ID x = args[0];

    // Keep this embedded-friendly: fixed-size stack buffer, no recursion, no sorting.
    char out[4096];
    size_t pos = 0;

    struct pprint_ctx ctx = { .buf = out, .pos = &pos, .max = sizeof(out) };
    pprint_walk(x, 0, &ctx);

    return make_string(out);
}

ID native_instant_p(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 1, "inst?"))
        return NULL;
    return (TAG(args[0]) == CLJ_INSTANT) ? clj_true : clj_false;
}

ID native_instant_days(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 1, "instant-days"))
        return NULL;
    if (TAG(args[0]) != CLJ_INSTANT)
    {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "instant-days expects an Instant");
        return NULL;
    }
    return fixnum(clj_instant_days(args[0]));
}

ID native_instant_ms(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 1, "instant-ms"))
        return NULL;
    if (TAG(args[0]) != CLJ_INSTANT)
    {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "instant-ms expects an Instant");
        return NULL;
    }
    return fixnum((int32_t)clj_instant_ms(args[0]));
}

// tiny-clj.datetime/civil-from-days: (civil-from-days unix-days) => {:year y :month m :day d}
ID native_datetime_civil_from_days(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 1, "civil-from-days"))
        return NULL;
    if (!is_fixnum(args[0]))
    {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "civil-from-days expects an integer days value");
        return NULL;
    }

    int32_t unix_days = (int32_t)as_fixnum(args[0]);
    int year = 0, month = 0, day = 0;
    tinyclj_civil_from_days_utc(unix_days, &year, &month, &day);

    // Keys are keywords (symbols with ':' prefix)
    if (!SYM_KW_YEAR || !SYM_KW_MONTH || !SYM_KW_DAY)
        return NULL;

    CljPersistentMap *m = map_empty();
    ASSIGN(m, map_assoc(m, SYM_KW_YEAR, fixnum(year)));
    ASSIGN(m, map_assoc(m, SYM_KW_MONTH, fixnum(month)));
    ASSIGN(m, map_assoc(m, SYM_KW_DAY, fixnum(day)));
    return m;
}

// tiny-clj.datetime/days-from-civil: (days-from-civil year month day) => unix-days
ID native_datetime_days_from_civil(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 3, "days-from-civil"))
        return NULL;
    if (!is_fixnum(args[0]) || !is_fixnum(args[1]) || !is_fixnum(args[2]))
    {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "days-from-civil expects integer year month day");
        return NULL;
    }

    int year = as_fixnum(args[0]);
    int month = as_fixnum(args[1]);
    int day = as_fixnum(args[2]);

    int32_t unix_days = clj_days_from_civil_utc(year, month, day);
    return fixnum((int)unix_days);
}

// tiny-clj.datetime/format-iso: (format-iso {:year y :month m :day d :hour h :minute min :second sec}) => "YYYY-MM-DDTHH:MM:SS"
ID native_datetime_format_iso(ID *args, unsigned int argc)
{
    if (!validate_builtin_args(argc, 1, "format-iso"))
        return NULL;

    ID map = args[0];
    if (!map)
        return NULL;

    unsigned char tag = TAG(map);
    if (tag != CLJ_MAP_PERSISTENT && tag != CLJ_MAP_TRANSIENT)
    {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "format-iso expects a map");
        return NULL;
    }

    if (!SYM_KW_YEAR || !SYM_KW_MONTH || !SYM_KW_DAY || !SYM_KW_HOUR || !SYM_KW_MINUTE || !SYM_KW_SECOND)
        return NULL;

    ID v_year = map_get(map, SYM_KW_YEAR);
    ID v_month = map_get(map, SYM_KW_MONTH);
    ID v_day = map_get(map, SYM_KW_DAY);
    ID v_hour = map_get(map, SYM_KW_HOUR);
    ID v_minute = map_get(map, SYM_KW_MINUTE);
    ID v_second = map_get(map, SYM_KW_SECOND);

    if (!is_fixnum(v_year) || !is_fixnum(v_month) || !is_fixnum(v_day) ||
        !is_fixnum(v_hour) || !is_fixnum(v_minute) || !is_fixnum(v_second))
    {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "format-iso expects integer :year :month :day :hour :minute :second");
        return NULL;
    }

    int year = as_fixnum(v_year);
    int month = as_fixnum(v_month);
    int day = as_fixnum(v_day);
    int hour = as_fixnum(v_hour);
    int minute = as_fixnum(v_minute);
    int second = as_fixnum(v_second);

    char buf[32];
    // Fixed-width formatting without libc printf machinery.
    // Enforce the supported range (ISO with 4-digit year).
    if (year < 0 || year > 9999 ||
        month < 0 || month > 99 ||
        day < 0 || day > 99 ||
        hour < 0 || hour > 99 ||
        minute < 0 || minute > 99 ||
        second < 0 || second > 99)
    {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "format-iso expects :year in [0,9999] and other fields in [0,99]");
        return NULL;
    }

    buf[0] = (char)('0' + (year / 1000) % 10);
    buf[1] = (char)('0' + (year / 100) % 10);
    buf[2] = (char)('0' + (year / 10) % 10);
    buf[3] = (char)('0' + year % 10);
    buf[4] = '-';
    buf[5] = (char)('0' + (month / 10) % 10);
    buf[6] = (char)('0' + month % 10);
    buf[7] = '-';
    buf[8] = (char)('0' + (day / 10) % 10);
    buf[9] = (char)('0' + day % 10);
    buf[10] = 'T';
    buf[11] = (char)('0' + (hour / 10) % 10);
    buf[12] = (char)('0' + hour % 10);
    buf[13] = ':';
    buf[14] = (char)('0' + (minute / 10) % 10);
    buf[15] = (char)('0' + minute % 10);
    buf[16] = ':';
    buf[17] = (char)('0' + (second / 10) % 10);
    buf[18] = (char)('0' + second % 10);
    buf[19] = '\0';

    return AUTORELEASE(make_string(buf));
}

// do: Evaluate expressions sequentially, return last value
// Note: As a builtin, arguments are already evaluated, so we just return the last one
ID native_do(ID *args, unsigned int argc)
{
    if (argc == 0)
    {
        // Empty do: (do) returns nil
        return NULL;
    }

    // Arguments are already evaluated by eval_arg, so we just return the last one
    ID last = args[argc - 1];
    return last;
}

// dotimes: Execute expression n times with variable bound to 0, 1, ..., n-1
// dotimes is now implemented as a special form, not a builtin

// ============================================================================
// REGEX FUNCTIONS - moved to builtins_regex.c
// ============================================================================

// Register a native builtin function.
// - Unqualified symbols (e.g. "count") → registered in clojure.core
// - Qualified clojure.* symbols (e.g. "clojure.repl/source") → registered in their namespace
// - Other qualified symbols (e.g. "tiny-clj.runtime/stats") → NOT registered here;
//   they remain in native_function_table only and require explicit (require 'ns)
//   followed by (defn ... :native) to become available (Clojure-compatible behavior).
static void register_builtin(const char *cname, BuiltinFn func)
{
    // Check if name is a qualified symbol (e.g., "clojure.repl/source")
    const char *slash = strchr(cname, '/');
    CljNamespace *target_ns;
    const char *symbol_name;

    if (slash && slash > cname && slash[1] != '\0')
    {
        // Qualified symbol: split into namespace and name
        size_t ns_len = slash - cname;

        // CLOJURE COMPATIBILITY: Only auto-register clojure.* namespaces.
        // Other namespaces (tiny-clj.*, user libs) must use (require 'ns) first,
        // then (defn ... :native) will look up the function in native_function_table.
        if (ns_len < 8 || strncmp(cname, "clojure.", 8) != 0) {
            // Not a clojure.* namespace - skip direct registration.
            // The function is already in native_function_table and will be
            // activated when the .clj file defines (defn foo [] :native).
            return;
        }

        char *ns_name = (char *)CLJ_MALLOC(ns_len + 1);
        strncpy(ns_name, cname, ns_len);
        ns_name[ns_len] = '\0';

        symbol_name = slash + 1;
        target_ns = ns_get_or_create(ns_name, NULL);
        CLJ_FREE(ns_name);
    }
    else
    {
        // Unqualified symbol: register in clojure.core
        target_ns = ns_get_or_create("clojure.core", NULL);
        symbol_name = cname;
    }

    if (!target_ns)
    {
        return;
    }

    // Namespace is already registered in ns_registry via ns_register
    // No need for special cache handling

    // Register the builtin in target namespace
    ID symbol = NULL;
    if (slash && slash > cname) {
        // Qualified: store with explicit namespace so ns mappings use qualified keys
        symbol = intern_symbol(target_ns->name, symbol_name);
    } else {
        symbol = intern_symbol_global(symbol_name);
    }
    ID func_obj = make_named_func(func, intern_symbol_global(cname));
    if (symbol && func_obj)
    {
        ns_define(target_ns, symbol, func_obj);

        // Add metadata to native function (:name and :ns)
#if defined(META_ENABLED) && META_ENABLED
        // Ensure special symbols are initialized
        init_special_symbols();

        // Create metadata map with :name and :ns
        CljPersistentMap *meta_map = make_map(4);
        if (meta_map)
        {
            // Add :name (function name as string)
            if (SYM_KW_NAME && symbol_name && symbol_name[0] != '\0')
            {
                CljString *name_str = make_string(symbol_name);
                if (name_str)
                {
                    ASSIGN(meta_map, map_assoc(meta_map, SYM_KW_NAME, name_str));
                    RELEASE(name_str);
                }
            }

            // Add :ns (namespace name as symbol)
            if (SYM_KW_NS && target_ns && target_ns->name)
            {
                ASSIGN(meta_map, map_assoc(meta_map, SYM_KW_NS, target_ns->name));
            }

            // Set metadata on function object
            meta_set(func_obj, meta_map);
            RELEASE(meta_map);
        }
#endif // META_ENABLED

        // Builtin registered successfully
    }
    else
    {
        // Failed to register builtin
    }
}

// GPIO functions for clojure.core
//
// - Host builds: keep lightweight stubs (used by macOS tests via gpio-simulate!)
// - ESP32 builds: real implementations live in src/gpio_esp32.c
#ifndef ESP32_BUILD
ID native_gpio_watch(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 2, "gpio-watch");
    (void)args;
    return fixnum(1);
}

ID native_gpio_unwatch(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 1, "gpio-unwatch");
    (void)args;
    return NULL;
}

ID native_gpio_simulate(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 2, "gpio-simulate!");
    (void)args;
    return NULL;
}
#endif

void register_builtins()
{
    WITH_AUTORELEASE_POOL({
    // NOTE: Most functions are now registered via :native stubs in clojure.core.clj
    // This allows metadata (docstrings) to be properly attached.
    // Only functions needed in --no-core mode are registered here directly.

    // Functions needed before clojure.core.clj is loaded (--no-core mode)
    register_builtin("eval", native_eval);
    register_builtin("read-string", native_read_string);
    register_builtin("require", native_require);
    register_builtin("load-file", native_load_file);

    // Arithmetic functions - needed for tests and --no-core mode
    register_builtin("+", native_add_variadic);
    register_builtin("-", native_sub_variadic);
    register_builtin("*", native_mul_variadic);
    register_builtin("/", native_div_variadic);
    register_builtin("mod", native_mod);
    register_builtin("quot", native_quot);

    // Only register functions needed for macro expansion: defn uses (list 'def name (cons 'fn ...))
    // seq is used by first, rest, next
    register_builtin("list", native_list);
    register_builtin("cons", native_cons);
    register_builtin("seq", native_seq);

    // Used in clojure.core before their (def ...) is evaluated
    register_builtin("not", native_not);
    register_builtin("atom", native_atom);

    // NOTE: clojure.string functions are NOT registered here as builtins.
    // They are defined in libs/clojure/string.clj and loaded via require.

    // Regex functions (needed for clojure.string and general regex support)
    register_builtin("regex?", native_regex_p);
    register_builtin("re-pattern", native_re_pattern);
    register_builtin("re-find", native_re_find);
    register_builtin("re-matches", native_re_matches);
    register_builtin("re-seq", native_re_seq);
    // This allows metadata (docstrings) to be properly attached.

    // NOTE: clojure.repl/source is registered here because it's in a different namespace
    // and needs to be available before clojure.repl.clj is loaded.
    register_builtin("clojure.repl/source", native_source);
    register_builtin("clojure.repl/dir", native_repl_dir);
    register_builtin("tiny-clj/retain-count", native_retain_count);

#ifdef DEBUG
    // Debug functions for tiny-clj.runtime namespace
    register_builtin("tiny-clj.runtime/print-ast", native_print_ast);
    register_builtin("tiny-clj.runtime/ast-string", native_ast_string);
#endif

    // Meta functions
    register_builtin("meta", native_meta);
    register_builtin("with-meta", native_with_meta);

    // tiny-clj.runtime
    register_builtin("tiny-clj.runtime/stats", native_tinyclj_runtime_stats);

    // Time functions
    register_builtin("now", native_now);
    register_builtin("inst?", native_instant_p);
    register_builtin("instant-days", native_instant_days);
    register_builtin("instant-ms", native_instant_ms);

    // Macro functions
    register_builtin("get-macro", native_get_macro);

    // Apply function
    register_builtin("apply", native_apply);

    // Prime concat thunk function object during setup so tests do not pay
    // one-time cached_named_func allocation in per-test heap assertions.
    (void)cached_named_func(native_concat_thunk_executor, SYM_CONCAT_THUNK_FN, &g_concat_thunk_fn_obj);

    });
}
