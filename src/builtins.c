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
#include "atom.h"
#include "kv_macros.h"
#include "numeric_utils.h"
#include "runtime.h"
#include "memory.h"
#include "value.h"
#include "error_messages.h"
#include "symbol.h"  // Must be included before namespace.h for CljSymbol definition
#include "namespace.h"
#include "seq.h"
#include "byte_array.h"
#include "exception.h"
#include "list.h"
#include "function.h"
#include "strings.h"
#include "to_string.h"
#include "event_loop.h"
#include "reader.h"
#include "parser.h"
#include "meta.h"
#include "eval.h"

// Forward declaration for eval_body_with_env
extern ID eval_body_with_env(ID body, CljMap *env);

// Forward declarations for native functions used in native_function_table
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
ID native_str(ID *args, unsigned int argc);
ID native_subs(ID *args, unsigned int argc);
ID native_symbol(ID *args, unsigned int argc);
ID native_type(ID *args, unsigned int argc);
ID native_array_map(ID *args, unsigned int argc);
ID native_vector(ID *args, unsigned int argc);
ID native_vec(ID *args, unsigned int argc);
ID native_peek(ID *args, unsigned int argc);
ID native_pop(ID *args, unsigned int argc);
ID native_subvec(ID *args, unsigned int argc);
ID native_conj(ID *args, unsigned int argc);
ID native_first(ID *args, unsigned int argc);
ID native_rest(ID *args, unsigned int argc);
ID native_next(ID *args, unsigned int argc);
ID native_cons(ID *args, unsigned int argc);
ID native_list(ID *args, unsigned int argc);
ID native_reduce(ID *args, unsigned int argc);
ID native_count(ID *args, unsigned int argc);
ID native_nilp(ID *args, unsigned int argc);
ID native_reverse(ID *args, unsigned int argc);
ID assoc3(ID *args, unsigned int argc);
ID native_dissoc(ID *args, unsigned int argc);
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
ID native_lt(ID *args, unsigned int argc);
ID native_gt(ID *args, unsigned int argc);
ID native_le(ID *args, unsigned int argc);
ID native_ge(ID *args, unsigned int argc);
ID native_eq(ID *args, unsigned int argc);
ID native_not_eq(ID *args, unsigned int argc);
ID native_identical(ID *args, unsigned int argc);
ID native_vector_p(ID *args, unsigned int argc);
ID native_map_p(ID *args, unsigned int argc);
ID native_number_p(ID *args, unsigned int argc);
ID native_integer_p(ID *args, unsigned int argc);
ID native_float_p(ID *args, unsigned int argc);
ID native_string_p(ID *args, unsigned int argc);
ID native_keyword_p(ID *args, unsigned int argc);
ID native_symbol_p(ID *args, unsigned int argc);
ID native_fn_p(ID *args, unsigned int argc);
ID native_char_p(ID *args, unsigned int argc);
ID native_list_p(ID *args, unsigned int argc);
ID native_sleep(ID *args, unsigned int argc);
ID native_ns_map(ID *args, unsigned int argc);
ID native_find_ns(ID *args, unsigned int argc);
ID native_all_ns(ID *args, unsigned int argc);
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
#ifndef ESP32_BUILD
ID native_slurp(ID *args, unsigned int argc);
ID native_spit(ID *args, unsigned int argc);
ID native_load_file(ID *args, unsigned int argc);
#endif
ID native_trim(ID *args, unsigned int argc);
ID native_upper_case(ID *args, unsigned int argc);
ID native_lower_case(ID *args, unsigned int argc);
ID native_last_index_of(ID *args, unsigned int argc);
ID native_string_reverse(ID *args, unsigned int argc);
ID native_source(ID *args, unsigned int argc);
ID native_repl_dir(ID *args, unsigned int argc);
ID native_meta(ID *args, unsigned int argc);
ID native_with_meta(ID *args, unsigned int argc);
ID nth2(ID *args, unsigned int argc);

static CljNamespace* namespace_from_value(ID value);
static int compare_symbol_names(const void *a, const void *b);

// Thread-local EvalState for builtins that need it (eval, read-string, meta, require)
// Set/cleared in eval_function_call before/after calling builtins
_Thread_local EvalState *g_current_eval_state = NULL;

// Getter for g_current_eval_state - used by eval.c to avoid creating temporary EvalStates
EvalState* builtin_get_eval_state(void) {
    return g_current_eval_state;
}

// Helper function to validate builtin arguments (DRY principle)
static bool validate_builtin_args(unsigned int argc, unsigned int expected, const char *func_name) {
    if (argc != expected) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
                "%s requires exactly %u argument%s, got %u",
                func_name, expected, expected == 1 ? "" : "s", argc);
        throw_exception(EXCEPTION_ARITY, error_msg, __FILE__, __LINE__, 0);
        return false;
    }
    return true;
}

ID nth2(ID *args, unsigned int argc) {
    // nth accepts 2 or 3 arguments: (nth coll index) or (nth coll index not-found)
    // Clojure-compatible: supports vectors (O(1)), lists (O(n)), and sequences (O(n))
    if (argc != 2 && argc != 3) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
                "nth requires exactly 2 or 3 argument%s, got %u",
                argc == 1 ? "" : "s", argc);
        throw_exception(EXCEPTION_ARITY, error_msg, __FILE__, __LINE__, 0);
        return NULL;
    }
    ID coll = args[0];
    ID idx = args[1];
    ID not_found = argc == 3 ? args[2] : NULL;
    (void)not_found;  // Unused - nth ignores default value (Clojure behavior)

    // Validate index
    if (!idx || TAG(idx) != CLJ_INT) {
        return throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                "nth requires an integer index");
    }
    int i = AS_FIXNUM(idx);
    if (i < 0) {
        return throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
                "nth index %d is negative", i);
    }

    // Handle nil collection
    if (!coll) {
        return throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
                "nth index %d is out of bounds for nil collection", i);
    }

    // Fast path: Vectors (O(1) access) - includes transient vectors
    int tag = TAG(coll);
    if (tag == CLJ_VECTOR || tag == CLJ_VECTOR_TRANSIENT) {
        CljVector *v = as_vector(coll);
        int count = vector_count(v);
        if (!v || i >= count) {
            // Out of bounds: throw exception
            return throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
                    "nth index %d is out of bounds for collection with %d elements", i, count);
        }
        // Index is valid - check if element is nil
        // vector_nth throws exception if out of bounds
        // vector_nth returns element with lifetime tied to vector - no retain needed
        return vector_nth(v, i);
    }

    // Fast path: Lists (O(n) access via list_nth)
    if (list_type_matches(TAG(coll))) {
        CljList *list = as_list(coll);
        if (!list) {
            return throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
                    "nth index %d is out of bounds for nil collection", i);
        }
        // list_nth throws exception if index is out-of-bounds
        // list_nth returns element with lifetime tied to list - no retain needed
        return list_nth(list, i);
    }

    // Slow path: Sequences (O(n) access via iterator)
    if (!is_seqable(coll)) {
        return throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                "nth not supported on this type");
    }

    SeqIterator iter;
    if (!seq_iter_init(&iter, coll)) {
        return throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
                "nth index %d is out of bounds for empty sequence", i);
    }

    // Iterate to index i
    for (int j = 0; j < i; j++) {
        if (seq_iter_empty(&iter)) {
            return throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
                    "nth index %d is out of bounds for sequence (reached end at index %d)", i, j);
        }
        seq_iter_next(&iter);
    }

    if (seq_iter_empty(&iter)) {
        return throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
                "nth index %d is out of bounds for sequence", i);
    }

    return seq_iter_first(&iter);
}

// peek: returns last element of vector, or nil if empty
ID native_peek(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "peek")) return NULL;
    ID vec = args[0];
    if (!vec || TAG(vec) != CLJ_VECTOR) return NULL;
    CljVector *v = as_vector(vec);
    int count = vector_count(v);
    if (!count) return NULL;  // nil for empty vector
    // vector_nth returns element with lifetime tied to vector - no retain needed
    return vector_nth(v, count - 1);
}

// pop: returns new vector without last element, or empty vector if empty
// Uses Copy-on-Write: RC=1 → in-place mutation (O(1)), RC>1 → COW (O(n))
ID native_pop(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "pop")) return NULL;
    ID vec = args[0];
    if (!vec || TAG(vec) != CLJ_VECTOR) return NULL;
    CljVector *v = as_vector(vec);
    int count = vector_count(v);
    if (!v || count == 0) {
        // Return empty vector singleton (no memory management needed)
        return make_vector(0, CLJ_VECTOR);
    }

    // Use vector_pop() which handles RC=1 (in-place) and RC>1 (COW) automatically
    CljVector *result = vector_pop(v);
    if (!result) return NULL;
    return result;  // vec is retained by caller, result is already retained
}

// subvec: returns sub-vector from start (inclusive) to end (exclusive)
// (subvec v start) → sub-vector from start to end of vector
// (subvec v start end) → sub-vector from start (inclusive) to end (exclusive)
// Clojure-compatible: always creates new immutable vector (O(n) operation)
ID native_subvec(ID *args, unsigned int argc) {
    // subvec accepts 2 or 3 arguments: (subvec v start) or (subvec v start end)
    if (argc != 2 && argc != 3) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
                "subvec requires exactly 2 or 3 argument%s, got %u",
                argc == 1 ? "" : "s", argc);
        throw_exception(EXCEPTION_ARITY, error_msg, __FILE__, __LINE__, 0);
        return NULL;
    }

    ID vec = args[0];
    ID start_idx = args[1];
    ID end_idx = argc == 3 ? args[2] : NULL;

    // Type validation
    if (!vec || TAG(vec) != CLJ_VECTOR) {
        return throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                "subvec requires a vector as first argument");
    }

    if (!start_idx || TAG(start_idx) != CLJ_INT) {
        return throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                "subvec requires a number as start index");
    }

    CljVector *v = as_vector(vec);
    if (!v) return NULL;

    int start = AS_FIXNUM(start_idx);
    int end;

    // Determine end index: if not provided, use vector count
    if (end_idx) {
        if (TAG(end_idx) != CLJ_INT) {
            return throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                    "subvec requires a number as end index");
        }
        end = AS_FIXNUM(end_idx);
    } else {
        end = vector_count(v);
    }

    // Bounds validation
    if (start < 0) {
        return throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
                "subvec start index %d is negative", start);
    }

    int v_count = vector_count(v);
    if (end > v_count) {
        return throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
                "subvec end index %d is greater than vector count %d", end, v_count);
    }

    if (start > end) {
        return throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
                "subvec start index %d is greater than end index %d", start, end);
    }

    // Calculate sub-vector size
    int subvec_count = end - start;

    // Special case: empty sub-vector (start == end)
    if (subvec_count == 0) {
        return make_vector(0, CLJ_VECTOR);  // Returns empty-vector singleton (no memory management needed)
    }

    // Create new vector and add elements using vector_conj
    ID new_vec_obj = make_vector(subvec_count, CLJ_VECTOR);
    CljVector *new_vec = as_vector(new_vec_obj);
    if (!new_vec) return NULL;

    // Copy elements from start to end using vector_conj
    for (int i = 0; i < subvec_count; i++) {
        ID elem = vector_nth(v, start + i);
        if (elem) {
            // vector_conj retains the element, so we need to retain it first
            new_vec = vector_conj(new_vec, RETAIN(elem));
        } else {
            new_vec = vector_conj(new_vec, NULL);  // nil elements
        }
        new_vec_obj = (CljValue)new_vec;
    }

    return AUTORELEASE(new_vec_obj);
}

// Forward declaration
ID conj2(ID vec, ID val);

ID conj2_wrapper(ID *args, int argc) {
    if (!validate_builtin_args(argc, 2, "conj")) return NULL;
    return conj2(args[0], args[1]);
}

ID conj2(ID vec, ID val) {
    if (!vec || TAG(vec) != CLJ_VECTOR) return NULL;
    // Use COW-based vector_conj (automatically handles RC=1 in-place, RC>1 COW)
    CljVector* result = vector_conj((CljVector*)vec, val);
    if (!result) return NULL;
    return RETAIN(result);
}

// Generic conj function that works with BuiltinFn signature
ID native_conj(ID *args, unsigned int argc) {
    CLJ_ASSERT(args != NULL);

    // Handle different arities like Clojure
    if (argc == 0) {
        // conj with no args returns nil (like Clojure)
        return NULL;
    }

    if (argc == 1) {
        // conj with one arg returns the collection unchanged
        return args[0]; // caller gave us a retained instance. just return it.
    }

    // For 2+ args, conj all values to the collection
    ID coll = args[0];
    if (!coll) {
        // conj nil with values creates a list
        CljList *result = NULL;
        for (unsigned int i = argc - 1; i >= 1; i--) {
            CljObject *val = args[i];
            result = make_list(val, result);
        }
        return result;
    }

    if (coll && TAG(coll) == CLJ_VECTOR) {
        CljObject *result = coll;
        for (unsigned int i = 1; i < argc; i++) {
            CljObject *val = args[i];
            result = conj2(result, val);
            if (!result) return NULL;
        }
        return (result);
    }

    // Throw exception for unsupported collection type
    throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                    "conj not supported on this type",
                    __FILE__, __LINE__, 0);
    return NULL;
}

// First function that works with BuiltinFn signature
ID native_first(ID *args, unsigned int argc) {
    CLJ_ASSERT(args != NULL);

    if (!validate_builtin_args(argc, 1, "first")) return NULL;

    CljObject *coll = args[0];
    if (!coll) {
        // first of nil returns nil
        return NULL;
    }

    // Switch on collection type (DRY: consistent pattern)
    switch (coll->type) {
        case CLJ_LIST: {
            // Direct access for lists (already a seq) - no allocation needed
            CljObject *first = LIST_FIRST((CljList*)coll);
            return first;  // Return existing object directly - no memory management needed
        }

        case CLJ_SEQ: {
            // Already a sequence - just call seq_first (DRY)
            return seq_first(coll);
        }

        default: {
            // Use seq implementation for other types (vectors, maps, strings)
            CljSeqIterator *seq = make_seq(coll);
            if (!seq) return NULL;

            ID result = seq_first(seq);
            RELEASE(seq);

            return result;
        }
    }
}

// Next function that works with BuiltinFn signature
// Clojure-compatible: returns nil if sequence is empty, otherwise rest sequence
ID native_next(ID *args, unsigned int argc) {
    CLJ_ASSERT(args != NULL);

    if (!validate_builtin_args(argc, 1, "next/rest")) return NULL;

    ID coll_id = args[0];
    if (!coll_id) {
        // next of nil returns nil
        return NULL;
    }

    // Check if coll is an immediate value (fixnum, etc.) - not seqable
    if (IS_IMMEDIATE(coll_id)) {
        // Immediate values are not seqable - throw exception
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "next not supported on this type",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    CljObject *coll = coll_id;

    // EAT-YOUR-OWN-DOG-FOOD: Use seq_next for all seqable types
    // This consolidates the logic and eliminates duplication
    // For CLJ_LIST, seq_next handles it efficiently (returns CLJ_LIST directly)
    // For CLJ_VECTOR, CLJ_SEQ, and other seqable types, seq_next handles them via seq_rest

    // Check if collection is seqable before trying to create seq
    if (!is_seqable(coll)) {
        // Not seqable - throw exception with type name for debugging
        const char *type_name = clj_type_name(coll->type);
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
                "next not supported on this type: %s",
                type_name ? type_name : "unknown");
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        error_msg,
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    // Try to create a seq from the collection
    CljSeqIterator *seq = make_seq(coll);
    if (!seq) {
        // Empty or not seqable - return nil
        return NULL;
    }

    // CRITICAL: Check if make_seq returned the original object (if coll was already a CLJ_SEQ)
    // If so, we must NOT release it, as it's owned by the caller (args[0])
    bool seq_is_original = (seq == (CljSeqIterator*)coll);

    bool reused_seq = false;
    ID result = NULL;

    if (!seq_is_original && seq && seq->iter.seq_type != CLJ_LIST && seq->base.rc == 1) {
        ID moved = seq_next_inplace(seq);
        if (moved) {
            result = moved;
            reused_seq = true;
        }
    }

    if (!reused_seq) {
        // Return next of the created seq
        result = seq_next(seq);
    }

    // seq_next now returns AUTORELEASE objects (already in pool) or NULL
    // For CLJ_LIST, seq_next returns AUTORELEASE(RETAIN(...)) - already in pool
    // For other types, seq_next returns new CljSeqIterator objects (rc=1) - need AUTORELEASE
    // Note: seq_next never returns immediate values, only NULL or heap objects (CLJ_LIST or CLJ_SEQ)
    if (result && TAG(result) == CLJ_SEQ) {
        // Only seq_next results that are freshly allocated seq iterators
        // (TAG == CLJ_SEQ) still need to be autoreleased. LIST results that
        // came from seq_next are already autoreleased inside seq_next.
        result = AUTORELEASE(result);
    }

    // Only release the seq if we created it (not if it was the original object)
    // If seq_is_original, the caller (eval_and_call_native) will release args[0]
    if (!seq_is_original && !reused_seq) {
        RELEASE(seq);
    }
    return result;
}

// Rest function that works with BuiltinFn signature
// DRY: Simply calls native_next and converts nil to empty_list()
ID native_rest(ID *args, unsigned int argc) {
    CLJ_ASSERT(args != NULL);

    // Call native_next (it will validate again, but that's fine for robustness)
    // If it returns nil, convert to empty_list()
    ID next_result = native_next(args, argc);
    return next_result ? next_result : empty_list();
}

// Cons function that works with BuiltinFn signature
ID native_cons(ID *args, unsigned int argc) {
    CLJ_ASSERT(args != NULL);

    if (!validate_builtin_args(argc, 2, "cons")) return NULL;

    CljObject *elem = args[0];
    CljObject *coll = args[1];

    if (!elem) elem = NULL;

    CljObject *result = NULL;

    // nil oder leer
    if (!coll) {
        result = (CljObject*)make_list(elem, NULL);
        return AUTORELEASE(result);
    }

    // Typ-basierte Behandlung
    switch (coll->type) {
        case CLJ_LIST:
        case CLJ_SEQ:
            result = (CljObject*)make_list(elem, (CljList*)coll);
            return AUTORELEASE(result);

        default: {
            // Vektor oder andere → zu Seq konvertieren
            CljSeqIterator *seq = make_seq(coll);
            if (!seq) {
                result = (CljObject*)make_list(elem, NULL);
            } else {
                result = (CljObject*)make_list(elem, (CljList*)seq);
            }
            return AUTORELEASE(result);
        }
    }
}

// List function that creates a list from its arguments
ID native_list(ID *args, unsigned int argc) {
    CLJ_ASSERT(args != NULL);

    // If no arguments, return empty list
    if (argc == 0) {
        return empty_list();
    }

    // Build list backwards (from end to start) using make_list
    CljList *result = NULL;
    for (int i = argc - 1; i >= 0; i--) {
        result = make_list(args[i], result);
    }
    return AUTORELEASE(result);
}

ID native_reduce(ID *args, unsigned int argc) {
    if (argc != 2 && argc != 3) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
                 "reduce requires exactly 2 or 3 arguments, got %u", argc);
        throw_exception(EXCEPTION_ARITY, error_msg, __FILE__, __LINE__, 0);
        return NULL;
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
    if (coll && !IS_IMMEDIATE(coll)) {
        has_seq = seq_iter_init(&iter, coll);
    }

    if (!has_seq || seq_iter_empty(&iter)) {
        if (has_init) {
            return acc;
        }
        return eval_function_call(fn, NULL, 0, NULL, st);
    }

    if (!has_init) {
        acc = seq_iter_first(&iter);
        acc_owned = false;
        seq_iter_next(&iter);
    }

    while (!seq_iter_empty(&iter)) {
        ID current = seq_iter_first(&iter);
        ID call_args[2] = {acc, current};
        ID new_acc = eval_function_call(fn, call_args, 2, NULL, st);
        if (acc_owned) {
            RELEASE(acc);
        }
        acc = new_acc;
        acc_owned = true;
        seq_iter_next(&iter);
    }

    return acc;
}

// nil? function that checks if a value is nil
ID native_nilp(ID *args, unsigned int argc) {
    CLJ_ASSERT(args != NULL);

    if (!validate_builtin_args(argc, 1, "nil?")) return NULL;

    // nil is represented as NULL in tiny-clj
    // Return true if argument is NULL, false otherwise
    if (!args[0]) {
        return clj_true;
    }

    return clj_false;
}

// Reverse function that reverses a list
ID native_reverse(ID *args, unsigned int argc) {
    CLJ_ASSERT(args != NULL);

    if (!validate_builtin_args(argc, 1, "reverse")) return NULL;

    CljObject *coll = args[0];

    // Handle nil/empty
    if (!coll) {
        return empty_list();
    }

    // Handle lists
    if (coll && list_type_matches(TAG(coll))) {
        // Safe cast - we already checked is_type
        CljList *list = (CljList*)coll;
        // Use list_count to check if list is empty (handles nil elements correctly)
        if (!list || list_count(list) == 0) {
            return empty_list();
        }

        // Build reversed list by traversing and consing
        CljList *result = NULL;
        CljObject *current = coll;

        while (current && list_type_matches(TAG(current))) {
            // Safe cast - we already checked is_type
            CljList *list = (CljList*)current;
            if (!list) break;

            CljObject *first = list->first;
            if (first) {
                CljList *new_result = make_list(first, result);
                RELEASE(result);
                result = new_result;
            }

            current = list->rest;
        }

        // If result is NULL, return empty list
        if (!result) {
            return empty_list();
        }

        return result;
    }

    // Handle vectors and other seqable collections
    // Convert to seq and build reversed list
    CljSeqIterator *seq = make_seq(coll);
    if (!seq) {
        // Empty or not seqable - return empty list
        return empty_list();
    }

    // Build reversed list by iterating through sequence
    CljList *result = NULL;
    while (!seq_empty(seq)) {
        CljObject *first = seq_first(seq);
        if (first) {
            CljList *new_result = make_list(first, result);
            RELEASE(result);
            result = new_result;
        }
        // Advance iterator to next position
        if (!seq_iter_next(&seq->iter)) {
            break; // End of sequence
        }
    }

    // Release seq object
    RELEASE(seq);

    // If result is NULL, return empty list
    if (!result) {
        return empty_list();
    }

    return result;
}

ID assoc3(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 3, "assoc")) return NULL;
    CljObject *coll = args[0];
    CljObject *key = args[1];
    CljObject *val = args[2];

    if (!coll) return NULL;

    // Handle vectors
    if (coll && TAG(coll) == CLJ_VECTOR) {
        if (!key || TAG(key) != CLJ_INT) return NULL;
        int i = AS_FIXNUM(key);
        CljVector *v = as_vector(coll);
        if (!v || i < 0 || (unsigned int)i >= vector_count(v)) return NULL;
        // Use COW-based vector_assoc (automatically handles RC=1 in-place, RC>1 COW)
        CljVector* result = vector_assoc((CljVector*)coll, i, val);
        if (!result) return NULL;
        return RETAIN(result);
    }

    // Handle maps
    if (coll && TAG(coll) == CLJ_MAP) {
        // Note: key can be NULL (nil) - that's a valid key in Clojure!
        CljMap *result = map_assoc((CljMap*)coll, key, val);
        return RETAIN(result);
    }

    // Unsupported collection type
    return NULL;
}

// dissoc: Remove keys from map (supports multiple keys like Clojure)
ID native_dissoc(ID *args, unsigned int argc) {
    // dissoc requires at least 1 argument (the map)
    if (argc < 1) {
        throw_exception(EXCEPTION_ARITY,
                       "dissoc requires at least 1 argument (map), got 0",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    CljObject *map = args[0];
    if (!map) return NULL;

    // Only support maps
    if (TAG(map) != CLJ_MAP && TAG(map) != CLJ_MAP_TRANSIENT) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                       "dissoc only works on maps",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    // If no keys to remove, return the map as-is
    if (argc == 1) {
        return AUTORELEASE(RETAIN(map));
    }

    // Remove keys one by one (Clojure semantics: multiple keys supported)
    CljMap *result = (CljMap*)map;
    for (unsigned int i = 1; i < argc; i++) {
        CljObject *key = args[i];
        if (!key) continue;  // Skip NULL keys

        // map_remove returns a new map (or original if key not found)
        CljMap *new_result = map_remove(result, key);
        if (new_result != (CljMap*)result) {
            // New map was created - release old one if it was retained
            if (i > 1 || result != (CljMap*)map) {
                RELEASE(result);
            }
            result = new_result;
        }
    }

    // Return autoreleased result
    return AUTORELEASE(RETAIN(result));
}

// merge: Combines multiple maps (later maps override earlier ones)
// Usage: (merge) (merge m1) (merge m1 m2) (merge m1 m2 m3 ...)
ID native_merge(ID *args, unsigned int argc) {
    // (merge) with no args returns nil
    if (argc == 0) return NULL;
    
    // (merge nil) returns nil
    if (argc == 1 && !args[0]) return NULL;
    
    // Start with first non-nil map
    CljMap *result = NULL;
    unsigned int start_idx = 0;
    
    for (unsigned int i = 0; i < argc; i++) {
        if (args[i] && (TAG(args[i]) == CLJ_MAP || TAG(args[i]) == CLJ_MAP_TRANSIENT)) {
            result = (CljMap*)args[i];
            start_idx = i + 1;
            break;
        }
    }
    
    // All arguments were nil
    if (!result) return NULL;
    
    // Merge remaining maps
    for (unsigned int i = start_idx; i < argc; i++) {
        ID m = args[i];
        if (!m) continue;  // Skip nil
        
        if (TAG(m) != CLJ_MAP && TAG(m) != CLJ_MAP_TRANSIENT) {
            throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                           "merge only works on maps",
                           __FILE__, __LINE__, 0);
            return NULL;
        }
        
        CljMap *new_result = map_merge(result, (CljMap*)m, true);  // overwrite=true
        if (new_result != result && i > start_idx) {
            // Don't release original arg
        }
        result = new_result;
    }
    
    return AUTORELEASE(RETAIN(result));
}

// contains?: Check if collection contains key
// Usage: (contains? coll key)
ID native_contains_p(ID *args, unsigned int argc) {
    if (argc != 2) {
        throw_exception_formatted(EXCEPTION_ARITY, __FILE__, __LINE__, 0,
                                  "contains? expects 2 arguments, got %u", argc);
        return NULL;
    }
    
    ID coll = args[0];
    ID key = args[1];
    
    // nil collection returns false
    if (!coll) return clj_false;
    
    CljType tag = TAG(coll);
    switch (tag) {
        case CLJ_MAP:
        case CLJ_MAP_TRANSIENT:
            return map_contains((CljMap*)coll, key) ? clj_true : clj_false;
        
        case CLJ_VECTOR:
        case CLJ_VECTOR_TRANSIENT:
        case CLJ_VECTOR_TRANSIENT_WEAK: {
            // For vectors, key must be an integer index
            if (!is_fixnum(key)) return clj_false;
            long idx = as_fixnum(key);
            unsigned int count = vector_count((CljVector*)coll);
            return (idx >= 0 && (unsigned long)idx < count) ? clj_true : clj_false;
        }
        
        default:
            return clj_false;
    }
}

// update: Apply function to value at key
// Usage: (update m k f) (update m k f arg1) (update m k f arg1 arg2 ...)
ID native_update(ID *args, unsigned int argc) {
    if (argc < 3) {
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
    if (!coll) return NULL;
    
    CljType tag = TAG(coll);
    if (tag != CLJ_MAP && tag != CLJ_MAP_TRANSIENT) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                       "update only works on maps",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    // Get current value (nil if not found)
    ID current_val = map_get((CljMap*)coll, key, NULL);
    
    // Build function call args: [current_val, extra_args...]
    unsigned int fn_argc = 1 + (argc - 3);
    ID fn_args[fn_argc];
    fn_args[0] = current_val;
    for (unsigned int i = 3; i < argc; i++) {
        fn_args[i - 2] = args[i];
    }
    
    // Call the function
    ID new_val = eval_function_call(func, fn_args, fn_argc, NULL, st);
    
    // assoc the new value
    CljMap *result = map_assoc((CljMap*)coll, key, new_val);
    return AUTORELEASE(RETAIN(result));
}

// into: Add all items from source into target collection
// Usage: (into to from) (into to xform from)
ID native_into(ID *args, unsigned int argc) {
    if (argc < 2 || argc > 3) {
        throw_exception_formatted(EXCEPTION_ARITY, __FILE__, __LINE__, 0,
                                  "into expects 2 or 3 arguments, got %u", argc);
        return NULL;
    }
    
    // TODO: Support transducers (3-arg version)
    if (argc == 3) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                       "into with transducer not yet implemented",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    ID to = args[0];
    ID from = args[1];
    
    // nil source returns target unchanged
    if (!from) return to ? AUTORELEASE(RETAIN(to)) : NULL;
    
    // nil target - create appropriate empty collection based on source type
    if (!to) {
        // Default to vector if target is nil
        to = make_vector(0, CLJ_VECTOR);
    }
    
    CljType to_tag = TAG(to);
    
    // Handle vector target
    if (to_tag == CLJ_VECTOR || to_tag == CLJ_VECTOR_TRANSIENT || to_tag == CLJ_VECTOR_TRANSIENT_WEAK) {
        CljVector *result = (CljVector*)to;
        
        // Iterate over source
        CljType from_tag = TAG(from);
        if (from_tag == CLJ_VECTOR || from_tag == CLJ_VECTOR_TRANSIENT || from_tag == CLJ_VECTOR_TRANSIENT_WEAK) {
            unsigned int count = vector_count((CljVector*)from);
            for (unsigned int i = 0; i < count; i++) {
                result = vector_conj(result, vector_nth((CljVector*)from, i));
            }
        } else if (from_tag == CLJ_MAP || from_tag == CLJ_MAP_TRANSIENT) {
            // Map entries become [k v] vectors
            CljMap *m = (CljMap*)from;
            for (int i = 0; i < m->capacity; i++) {
                ID key = KV_KEY(m->data, i);
                if (key) {
                    ID val = KV_VALUE(m->data, i);
                    CljVector *entry = make_vector(2, CLJ_VECTOR);
                    entry = vector_conj(entry, key);
                    entry = vector_conj(entry, val);
                    result = vector_conj(result, entry);
                }
            }
        } else if (from_tag == CLJ_LIST || from_tag == CLJ_AST_NODE) {
            // Iterate over list (CLJ_LIST or CLJ_AST_NODE - both use same structure)
            CljList *list = (CljList*)from;
            while (list && !list_empty(list)) {
                result = vector_conj(result, LIST_FIRST(list));
                list = as_list(LIST_REST(list));
            }
        }
        
        return AUTORELEASE(RETAIN(result));
    }
    
    // Handle map target
    if (to_tag == CLJ_MAP || to_tag == CLJ_MAP_TRANSIENT) {
        CljMap *result = (CljMap*)to;
        
        CljType from_tag = TAG(from);
        if (from_tag == CLJ_MAP || from_tag == CLJ_MAP_TRANSIENT) {
            // Merge maps
            result = map_merge(result, (CljMap*)from, true);
        } else if (from_tag == CLJ_VECTOR || from_tag == CLJ_VECTOR_TRANSIENT) {
            // Vector of [k v] pairs
            unsigned int count = vector_count((CljVector*)from);
            for (unsigned int i = 0; i < count; i++) {
                ID entry = vector_nth((CljVector*)from, i);
                if (entry && (TAG(entry) == CLJ_VECTOR || TAG(entry) == CLJ_VECTOR_TRANSIENT)) {
                    CljVector *pair = (CljVector*)entry;
                    if (vector_count(pair) >= 2) {
                        result = map_assoc(result, vector_nth(pair, 0), vector_nth(pair, 1));
                    }
                }
            }
        }
        
        return AUTORELEASE(RETAIN(result));
    }
    
    throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                   "into: unsupported target collection type",
                   __FILE__, __LINE__, 0);
    return NULL;
}

// select-keys: Return map with only specified keys
// Usage: (select-keys map [key1 key2 ...])
ID native_select_keys(ID *args, unsigned int argc) {
    if (argc != 2) {
        throw_exception_formatted(EXCEPTION_ARITY, __FILE__, __LINE__, 0,
                                  "select-keys expects 2 arguments, got %u", argc);
        return NULL;
    }
    
    ID m = args[0];
    ID keys = args[1];
    
    // nil map returns empty map
    if (!m) return map_empty();
    
    CljType tag = TAG(m);
    if (tag != CLJ_MAP && tag != CLJ_MAP_TRANSIENT) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                       "select-keys first argument must be a map",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    // nil keys returns empty map
    if (!keys) return map_empty();
    
    CljType keys_tag = TAG(keys);
    if (keys_tag != CLJ_VECTOR && keys_tag != CLJ_VECTOR_TRANSIENT && 
        keys_tag != CLJ_VECTOR_TRANSIENT_WEAK && keys_tag != CLJ_LIST && keys_tag != CLJ_AST_NODE) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                       "select-keys second argument must be a sequence",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    CljMap *result = map_empty();
    CljMap *source = (CljMap*)m;
    
    if (keys_tag == CLJ_VECTOR || keys_tag == CLJ_VECTOR_TRANSIENT || keys_tag == CLJ_VECTOR_TRANSIENT_WEAK) {
        unsigned int count = vector_count((CljVector*)keys);
        for (unsigned int i = 0; i < count; i++) {
            ID key = vector_nth((CljVector*)keys, i);
            if (map_contains(source, key)) {
                ID val = map_get(source, key, NULL);
                result = map_assoc(result, key, val);
            }
        }
    } else if (keys_tag == CLJ_LIST || keys_tag == CLJ_AST_NODE) {
        CljList *list = (CljList*)keys;
        while (list && !list_empty(list)) {
            ID key = LIST_FIRST(list);
            if (map_contains(source, key)) {
                ID val = map_get(source, key, NULL);
                result = map_assoc(result, key, val);
            }
            list = as_list(LIST_REST(list));
        }
    }
    
    return AUTORELEASE(RETAIN(result));
}

// find: Return [key value] entry for key, or nil if not found
// Usage: (find map key)
ID native_find(ID *args, unsigned int argc) {
    if (argc != 2) {
        throw_exception_formatted(EXCEPTION_ARITY, __FILE__, __LINE__, 0,
                                  "find expects 2 arguments, got %u", argc);
        return NULL;
    }
    
    ID m = args[0];
    ID key = args[1];
    
    // nil map returns nil
    if (!m) return NULL;
    
    CljType tag = TAG(m);
    if (tag != CLJ_MAP && tag != CLJ_MAP_TRANSIENT) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                       "find first argument must be a map",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    CljMap *map = (CljMap*)m;
    
    if (!map_contains(map, key)) {
        return NULL;  // Key not found
    }
    
    ID val = map_get(map, key, NULL);
    
    // Return [key value] vector
    CljVector *entry = make_vector(2, CLJ_VECTOR);
    entry = vector_conj(entry, key);
    entry = vector_conj(entry, val);
    
    return AUTORELEASE(RETAIN(entry));
}

// Transient functions
ID native_transient(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "transient")) return NULL;

    ID coll = args[0];
    if (!coll) return NULL;

    uint16_t tag = TAG(coll);
    switch (tag) {
        case CLJ_VECTOR:
            return vector_transient((CljVector*)coll);
        case CLJ_MAP:
            return map_transient(coll);
        case CLJ_VECTOR_TRANSIENT:
        case CLJ_MAP_TRANSIENT:
            // Clojure-compatible: transient on transient returns the same object
            return coll;
        default:
            break;
    }

    // Throw exception for unsupported collection type (Clojure-compatible)
    throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                    "transient requires a collection at position 1",
                    __FILE__, __LINE__, 0);
    return NULL;
}

ID native_persistent_bang(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "persistent!")) return NULL;

    ID coll = args[0];
    if (!coll) return NULL;

    uint16_t tag = TAG(coll);
    switch (tag) {
        case CLJ_VECTOR_TRANSIENT:
            return vector_persistent((CljVector*)coll);
        case CLJ_MAP_TRANSIENT:
            return map_persistent(coll);
        case CLJ_VECTOR:
        case CLJ_MAP:
            // Clojure-compatible: persistent! on persistent returns the same object
            return coll;
        default:
            break;
    }

    // Throw exception for unsupported collection type (Clojure-compatible)
    throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                    "persistent! requires a transient collection at position 1",
                    __FILE__, __LINE__, 0);
    return NULL;
}

ID native_conj_bang(ID *args, unsigned int argc) {
    if (argc < 2) return (NULL);

    CljObject *coll = args[0];
    if (!coll) return NULL;

    int tag = TAG(coll);
    if (tag == CLJ_VECTOR_TRANSIENT || tag == CLJ_VECTOR_TRANSIENT_WEAK) {
        CljVector *result = (CljVector*)coll;
        // vector_conj automatically handles transient vectors correctly (always in-place)
        for (unsigned int i = 1; i < argc; i++) {
            result = vector_conj(result, args[i]);
            if (!result) return NULL;
        }
        return (CljObject*)result;
    } else if (tag == CLJ_MAP_TRANSIENT) {
        if (argc != 3) return NULL; // conj! for maps needs key-value pair
        return (CljObject*)map_conj((CljMap*)coll, (CljValue)args[1], (CljValue)args[2]);
    }

    // Throw exception for unsupported collection type (Clojure-compatible)
    throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                    "conj! requires a transient collection at position 1",
                    __FILE__, __LINE__, 0);
    return NULL;
}

ID native_get(ID *args, unsigned int argc) {
    if (argc < 2 || argc > 3) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                       "get requires 2 or 3 arguments",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    CljObject *map = args[0];
    CljObject *key_obj = args[1];
    ID not_found = argc == 3 ? args[2] : NULL;
    // Note: key can be NULL (nil) - that's a valid key in Clojure!
    if (!map) return NULL;

    // Convert SYM_NIL to NULL for key lookup
    ID key = (key_obj && TAG(key_obj) == CLJ_SYMBOL && key_obj == (CljObject*)SYM_NIL)
        ? NULL
        : key_obj;

    int tag = TAG(map);
    if (tag == CLJ_MAP || tag == CLJ_MAP_TRANSIENT) {
        return map_get((CljMap*)map, key, not_found);
    }

    return not_found ? not_found : NULL; // Return not_found or nil for unsupported types
}

ID native_count(ID *args, unsigned int argc) {
    CLJ_ASSERT(args != NULL);

    if (!validate_builtin_args(argc, 1, "count")) return NULL;
    CljObject *coll = args[0];
    // Clojure behavior: (count nil) => 0
    if (!coll) {
        return fixnum(0);
    }

    // Handle CLJ_SEQ (sequences from rest, etc.)
    if (coll && TAG(coll) == CLJ_SEQ) {
        return fixnum(seq_count(coll));
    }

    if (coll) {
        int tag = TAG(coll);
        if (tag == CLJ_MAP || tag == CLJ_MAP_TRANSIENT) {
            return (fixnum(map_count((CljMap*)coll)));
        } else if (tag == CLJ_VECTOR || tag == CLJ_VECTOR_TRANSIENT) {
            CljVector *vec = as_vector(coll);
            return (fixnum(vec ? vector_count(vec) : 0));
        } else if (list_type_matches(tag)) {
            CljList *list = as_list(coll);
            return (fixnum(list ? list_count(list) : 0));
        } else if (tag == CLJ_STRING) {
            CljString *str = (CljString*)coll;

            // Return string length directly
            return fixnum(str->length);
        }
    }

    return (fixnum(0)); // Default count for unsupported types
}

ID native_keys(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "keys")) return NULL;
    CljObject *map = args[0];
    if (!map) return (NULL);

    int tag = TAG(map);
    if (tag == CLJ_MAP || tag == CLJ_MAP_TRANSIENT) {
        return map_keys((CljMap*)map);
    }

    return NULL; // Return nil for unsupported types
}

ID native_vals(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "vals")) return NULL;
    CljObject *map = args[0];
    if (!map) return (NULL);

    int tag = TAG(map);
    if (tag == CLJ_MAP || tag == CLJ_MAP_TRANSIENT) {
        return map_vals((CljMap*)map);
    }

    return NULL; // Return nil for unsupported types
}

ID native_type(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "type")) return NULL;
    CljValue val = args[0];
    if (!val) return SYM_NIL;

    // Get the tag to determine immediate vs heap object
    uint8_t tag = get_tag(val);

    // Return namespace-qualified symbols in clojure.lang namespace (Clojure-compatible)
    // Switch on tag for immediate values
    switch (tag) {
        case TAG_FIXNUM:
            return intern_symbol(SYM_CLOJURE_LANG, "Long");
        case TAG_CHAR:
            return intern_symbol(SYM_CLOJURE_LANG, "Character");
        case TAG_BOOL: {
            int special_type = as_special(val);
            if (special_type == SPECIAL_TRUE || special_type == SPECIAL_FALSE) {
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
    if (IS_KEYWORD(obj)) {
        return intern_symbol(SYM_CLOJURE_LANG, "Keyword");
    }

    // Switch on object type for heap objects
    switch (obj->type) {
        case CLJ_SYMBOL:
            return intern_symbol(SYM_CLOJURE_LANG, "Symbol");
        case CLJ_STRING:
            return intern_symbol(SYM_CLOJURE_LANG, "String");
        case CLJ_VECTOR:
            return intern_symbol(SYM_CLOJURE_LANG, "PersistentVector");
        case CLJ_VECTOR_TRANSIENT:
        case CLJ_VECTOR_TRANSIENT_WEAK:
            return intern_symbol(SYM_CLOJURE_LANG, "TransientVector");
        case CLJ_MAP_TRANSIENT:
            return intern_symbol(SYM_CLOJURE_LANG, "TransientArrayMap");
        case CLJ_MAP:
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

ID native_array_map(ID *args, unsigned int argc) {
    // Must have even number of arguments (key-value pairs)
    if (argc % 2 != 0) {
        // Return empty map instead of nil for odd number of args
        return make_map(0);
    }

    // Create map with appropriate capacity
    int pair_count = argc / 2;

    // Handle empty map case specially
    if (pair_count == 0) {
        return make_map(0);
    }

    CljMap *map = make_map(pair_count);

    // Add all key-value pairs
    // CRITICAL: map_assoc may return a new map (COW), so we must use the result
    for (unsigned int i = 0; i < argc; i += 2) {
        CljObject *key = args[i];
        CljObject *value = args[i + 1];
        CljMap *updated_map = map_assoc(map, key, value);
        ASSIGN(map, updated_map);
    }

    return AUTORELEASE(map);
}

ID native_vector(ID *args, unsigned int argc) {
    // Clojure-compatible: (vector) returns empty vector singleton
    // This is the same singleton returned by make_vector(0, CLJ_VECTOR)
    if (argc == 0) {
        return vector_empty_singleton;  // Returns empty-vector singleton (no memory management needed)
    }

    // Create vector with capacity+1 to avoid COW when adding all elements
    // (vector_conj uses COW when count >= capacity, so we need capacity > argc)
    CljVector *v = make_vector(argc + 1, CLJ_VECTOR);

    // Add all elements using vector_conj (Clojure-compatible: all args are retained)
    for (unsigned int i = 0; i < argc; i++) {
        ASSIGN(v, vector_conj(v, args[i]));
    }

    return AUTORELEASE(v);
}

// vec: converts a sequence to a vector
// (vec coll) => vector with elements from coll
// Clojure-compatible: if coll is already a vector, returns same vector (No-Op)
ID native_vec(ID *args, unsigned int argc) {
    // Arity check: vec accepts exactly 1 argument
    if (!validate_builtin_args(argc, 1, "vec")) return NULL;

    CljObject *coll = args[0];

    // If nil, return empty vector singleton (Clojure behavior: '() is nil, (vec '()) => [])
    // empty_vector() returns singleton - no memory management needed
    if (!coll) {
        return empty_vector();
    }

    // If already a vector, return same object (No-Op - Clojure behavior)
    // Note: coll is already AUTORELEASEd by eval_arg, so we need to AUTORELEASE it again
    // to ensure it's in the caller's pool
    if (coll && TAG(coll) == CLJ_VECTOR) {
        return AUTORELEASE(coll);
    }

    // Check if collection is seqable
    if (!is_seqable(coll)) {
        return throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                "vec requires a seqable collection");
    }

    // Use stack-based iterator to iterate through collection (avoid code duplication)
    // This is more efficient than heap-based make_seq and avoids memory leaks
    SeqIterator iter;
    if (!seq_iter_init(&iter, coll)) {
        // Empty collection - return empty vector singleton (Clojure behavior: (vec '()) => [])
        // empty_vector() returns singleton - no memory management needed
        return empty_vector();
    }

    // Check if iterator is empty (using singleton pattern)
    if (seq_iter_empty(&iter)) {
        return empty_vector();  // Returns singleton - no memory management needed
    }

    // Create vector with default capacity (vector_conj will grow automatically)
    // make_vector throws OOM exception or returns valid object
    CljVector* vec = make_vector(4, CLJ_VECTOR);
    if (!vec) {
        return throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                "Failed to create vector");
    }

    // Iterate through sequence and add elements using vector_conj (reuse existing logic)
    // This avoids code duplication and reuses COW-based vector_conj
    // Note: nil (NULL) is a valid value in Clojure collections
    while (!seq_iter_empty(&iter)) {
        ID elem_id = seq_iter_first(&iter);
        // Use vector_conj to add element (handles growth automatically via COW)
        // vector_conj may return a new vector if capacity was exceeded (COW)
        // Note: vector_conj accepts NULL (nil) as valid element
        // Use ASSIGN to safely update vec (handles retain/release automatically)
        ASSIGN(vec, vector_conj(vec, elem_id));

        // Move to next element (reuse existing seq_iter_next API)
        seq_iter_next(&iter);
    }

    return AUTORELEASE(vec);
}

// make_func() wrapper removed - use make_named_func(fn, env, NULL) directly

ID make_named_func(BuiltinFn fn, void *env, const char *cname) {
    CljCFunc *func = ALLOC(CljCFunc, 1);
    if (!func) return NULL;

    func->base.type = CLJ_FUNC;
    func->base.rc = 1;
    func->fn = (CljObject* (*)(CljObject **, int))fn; // Cast to expected signature
    func->env = env;

    // Safely handle name parameter
    if (cname && strlen(cname) > 0) {
        // Allocate memory for the name to avoid issues with string literals
        func->name = ALLOC(char, strlen(cname) + 1);
        if (func->name) {
            strcpy((char*)func->name, cname);
        }
    } else {
        func->name = NULL;
    }

    return func;
}
// Event-loop: run-next-task builtin
ID native_run_next_task(ID *args, unsigned int argc) {
    (void)args;
    if (argc != 0) return NULL;
    EvalState *st = get_global_eval_state();
    CljMap *env = NULL;
    bool ran = false;
    TRY {
        ran = event_loop_run_next(env, st);
    } CATCH(ex) {
        // Exception occurred - return false (no task was executed)
        // Don't propagate exception to caller
        ran = false;
    } END_TRY
    return ran ? clj_true : clj_false;
}

// Timer: schedule builtin - schedule a one-time timer
ID native_schedule(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 2, "schedule")) return NULL;

    // First argument: delay in milliseconds (must be integer)
    CljObject *delay_obj = args[0];
    if (!delay_obj || TAG(delay_obj) != CLJ_INT) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                       "schedule delay must be an integer",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    int delay_ms = as_fixnum((CljValue)delay_obj);
    if (delay_ms < 0) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                       "schedule delay must be non-negative",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    // Second argument: function to execute (must be a function)
    CljObject *fn_obj = args[1];
    if (!fn_obj || (TAG(fn_obj) != CLJ_FUNC && TAG(fn_obj) != CLJ_CLOSURE)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                       "schedule requires a function as second argument",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    // Create zero-arity wrapper function: (fn [] (fn_obj))
    // The function should be called with zero arguments
    CljFunction *func = as_function(fn_obj);
    if (!func) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                       "schedule requires a valid function",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    // CRITICAL: Retain the function before passing to timer_enqueue
    // The function may be in an autorelease pool that will be popped when this function returns
    // timer_enqueue will retain it again, but we need to ensure it survives until then
    RETAIN(fn_obj);

    // Enqueue timer task
    timer_enqueue(fn_obj, (int64_t)delay_ms, false, 0);

    // Release our reference - timer_enqueue has retained it
    RELEASE(fn_obj);

    // schedule returns nil (like go blocks)
    return NULL;
}

// Timer: schedule-periodic builtin - schedule a periodic timer
ID native_schedule_periodic(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 3, "schedule-periodic")) return NULL;

    // First argument: initial delay in milliseconds (must be integer)
    CljObject *delay_obj = args[0];
    if (!delay_obj || TAG(delay_obj) != CLJ_INT) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                       "schedule-periodic delay must be an integer",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    int delay_ms = as_fixnum((CljValue)delay_obj);
    if (delay_ms < 0) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                       "schedule-periodic delay must be non-negative",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    // Second argument: period in milliseconds (must be integer)
    CljObject *period_obj = args[1];
    if (!period_obj || TAG(period_obj) != CLJ_INT) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                       "schedule-periodic period must be an integer",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    int period_ms = as_fixnum((CljValue)period_obj);
    if (period_ms <= 0) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                       "schedule-periodic period must be positive",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    // Third argument: function to execute (must be a function)
    CljObject *fn_obj = args[2];
    if (!fn_obj || (TAG(fn_obj) != CLJ_FUNC && TAG(fn_obj) != CLJ_CLOSURE)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                       "schedule-periodic requires a function as third argument",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    // CRITICAL: Retain the function before passing to timer_enqueue
    // The function may be in an autorelease pool that will be popped when this function returns
    // timer_enqueue will retain it again, but we need to ensure it survives until then
    RETAIN(fn_obj);

    // Enqueue periodic timer task and get timer ID
    int32_t timer_id = timer_enqueue(fn_obj, (int64_t)delay_ms, true, (int64_t)period_ms);

    // Release our reference - timer_enqueue has retained it
    RELEASE(fn_obj);

    // schedule-periodic returns timer ID as integer
    return timer_id > 0 ? fixnum(timer_id) : NULL;
}

// Timer: cancel-timer builtin - cancel a timer by ID
ID native_cancel_timer(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "cancel-timer")) return NULL;

    // First argument: timer ID (must be integer)
    CljObject *timer_id_obj = args[0];
    if (!timer_id_obj || TAG(timer_id_obj) != CLJ_INT) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                       "cancel-timer timer-id must be an integer",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    int timer_id = as_fixnum((CljValue)timer_id_obj);
    if (timer_id <= 0) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                       "cancel-timer timer-id must be positive",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    // Cancel the timer
    bool cancelled = timer_cancel(timer_id);

    // Return true if cancelled, false if not found
    return cancelled ? clj_true : clj_false;
}


// Legacy builtin table and apply_builtin removed - all builtins now use namespace registration

// Arithmetic functions - native_*_variadic implement operations directly (no wrappers)

// ============================================================================
// PRINT HELPER FUNCTION (DRY Principle)
// ============================================================================

// Common helper function for all print functions
static void print_helper(ID *args, unsigned int argc, bool readable, bool newline) {
    if (argc < 1) return;

    // Print all arguments separated by spaces
    for (unsigned int i = 0; i < argc; i++) {
        if (args[i]) {
            CljString *str = readable ? pr_str(args[i]) : print_str(args[i]);
            if (str) {
                printf("%s", string_data(str));
            }

            // Add space between arguments (except for the last one)
            if (i < argc - 1) {
                printf(" ");
            }
        }
    }

    // Add newline if requested
    if (newline) {
        printf("\n");
    }

    // Flush stdout to ensure output appears immediately (important for timers/go blocks)
    fflush(stdout);
}

// ============================================================================
// NATIVE PRINT FUNCTIONS (using print_helper)
// ============================================================================

ID native_print(ID *args, unsigned int argc) {
    print_helper(args, argc, false, false);  // not readable, no newline
    return NULL;
}

ID native_println(ID *args, unsigned int argc) {
    print_helper(args, argc, false, true);   // not readable, with newline
    return NULL;
}

ID native_pr(ID *args, unsigned int argc) {
    print_helper(args, argc, true, false);   // readable, no newline
    return NULL;
}

ID native_prn(ID *args, unsigned int argc) {
    print_helper(args, argc, true, true);    // readable, with newline
    return NULL;
}

// ============================================================================
// HELPER FUNCTIONS (DRY Principle)
// ============================================================================

// Helper function to apply saturation to fixed-point values
static int32_t apply_saturation(int32_t acc_fixed) {
    if (acc_fixed > 268435455) acc_fixed = 268435455;
    if (acc_fixed < -268435456) acc_fixed = -268435456;
    return acc_fixed;
}

// Helper function to create fixed-point result
static ID create_fixed_result(int32_t acc_fixed) {
    acc_fixed = apply_saturation(acc_fixed);
    return ((CljObject*)(uintptr_t)((acc_fixed << TAG_BITS) | TAG_FIXED));
}

// Helper function to throw arithmetic overflow exceptions (DRY principle)
static ID throw_arithmetic_overflow(const char* err_msg, int a, int b) {
    return throw_exception_formatted(EXCEPTION_ARITHMETIC, __FILE__, __LINE__, 0, err_msg, a, b);
}

// Helper function to throw fixed-point overflow exceptions
static ID throw_fixed_overflow(const char* err_msg) {
    return throw_exception_formatted(EXCEPTION_ARITHMETIC, __FILE__, __LINE__, 0, err_msg);
}

// Helper function to create fixnum result
static ID create_fixnum_result(int acc_i) {
    return fixnum(acc_i);
}

// Helper function to extract raw fixed-point value
static int32_t extract_fixed_value(ID arg) {
    return (int32_t)((intptr_t)arg >> TAG_BITS);
}

// Helper function to convert fixnum to fixed-point
static int32_t fixnum_to_fixed(int fixnum) {
    return fixnum << 13;
}

// ============================================================================
// VARIADIC FUNCTIONS (Phase 1)
// ============================================================================

// String concatenation (variadic)
ID native_str(ID *args, unsigned int argc) {
    if (argc == 0) {
        return make_string("");
    }

    // Optimization: If only one argument and it's already a string, return it directly
    if (argc == 1 && args[0] && TAG(args[0]) == CLJ_STRING) {
        return args[0];
    }

    // Calculate total length
    size_t total_len = 0;
    for (unsigned int i = 0; i < argc; i++) {
        if (args[i] && TAG(args[i]) == CLJ_STRING) {
            total_len += string_length(args[i]);
        } else {
            CljString *s = to_string(args[i]);
            if (s) {
                total_len += string_length(s);
            }
        }
    }

    // Allocate CljString buffer directly
    CljString *result = make_string_buffer(total_len);
    char *buffer = result->data;

    // Concatenate all strings
    for (unsigned int i = 0; i < argc; i++) {
        if (args[i] && TAG(args[i]) == CLJ_STRING) {
            strcat(buffer, string_data(args[i]));
        } else {
            CljString *s = to_string(args[i]);
            if (s) {
                strcat(buffer, string_data(s));
            }
        }
    }

    return (CljObject*)result;
}

// String substring: (subs s start) or (subs s start end)
ID native_subs(ID *args, unsigned int argc) {
    if (argc != 2 && argc != 3) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
                "subs requires exactly 2 or 3 argument%s, got %u",
                argc == 2 ? "" : "s", argc);
        throw_exception(EXCEPTION_ARITY, error_msg, __FILE__, __LINE__, 0);
        return NULL;
    }

    ID str_arg = args[0];
    ID start_arg = args[1];
    ID end_arg = argc == 3 ? args[2] : NULL;

    // Validate string argument
    if (!str_arg || TAG(str_arg) != CLJ_STRING) {
        return throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                "subs requires a string as first argument");
    }

    // Validate start index
    if (!start_arg || TAG(start_arg) != CLJ_INT) {
        return throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                "subs requires a number as start index");
    }

    CljString *str = as_clj_string(str_arg);
    int start = AS_FIXNUM(start_arg);

    // Cache string length to avoid multiple calls
    int str_len = string_length(str);
    int end;

    // Determine end index: if not provided, use string length
    if (end_arg) {
        if (TAG(end_arg) != CLJ_INT) {
            return throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                    "subs requires a number as end index");
        }
        end = AS_FIXNUM(end_arg);
    } else {
        end = str_len;
    }

    // Bounds validation
    if (start < 0) {
        return throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
                "subs start index %d is negative", start);
    }

    if (end > str_len) {
        return throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
                "subs end index %d is greater than string length %d", end, str_len);
    }

    if (start > end) {
        return throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
                "subs start index %d is greater than end index %d", start, end);
    }

    // Calculate substring length
    int substr_len = end - start;

    // Special case: empty substring (start == end)
    if (substr_len == 0) {
        return string_empty_singleton;
    }

    // Create CljString directly without temporary C-string
    CljString *result = make_string_buffer(substr_len);
    const char *str_data = string_data(str);
    memcpy(result->data, str_data + start, substr_len);
    result->data[substr_len] = '\0';

    return AUTORELEASE(result);
}

// String trim: (trim s) - removes whitespace from both ends
ID native_trim(ID *args, unsigned int argc) {
    if (argc != 1) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
                "trim requires exactly 1 argument, got %u", argc);
        throw_exception(EXCEPTION_ARITY, error_msg, __FILE__, __LINE__, 0);
        return NULL;
    }

    ID str_arg = args[0];

    // Handle nil
    if (!str_arg) {
        return NULL; // nil -> nil
    }

    // Validate string argument
    if (TAG(str_arg) != CLJ_STRING) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                "trim requires a string argument");
        return NULL;
    }

    CljString *str = as_clj_string(str_arg);
    const char *str_data = string_data(str);
    int str_len = string_length(str);

    // Find start (skip leading whitespace)
    int start = 0;
    while (start < str_len && (str_data[start] == ' ' || str_data[start] == '\t' ||
                               str_data[start] == '\n' || str_data[start] == '\r')) {
        start++;
    }

    // Find end (skip trailing whitespace)
    int end = str_len - 1;
    while (end >= start && (str_data[end] == ' ' || str_data[end] == '\t' ||
                            str_data[end] == '\n' || str_data[end] == '\r')) {
        end--;
    }

    // Calculate trimmed length
    int trimmed_len = end - start + 1;

    // Special case: empty string or all whitespace
    if (trimmed_len <= 0) {
        return string_empty_singleton;
    }

    // Create CljString directly without temporary C-string
    CljString *result = make_string_buffer(trimmed_len);
    memcpy(result->data, str_data + start, trimmed_len);
    result->data[trimmed_len] = '\0';
    return AUTORELEASE(result);
}

// String upper-case: (upper-case s) - converts string to upper-case
ID native_upper_case(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "upper-case");

    ID str_arg = args[0];

    // Handle nil
    if (!str_arg) {
        return NULL; // nil -> nil
    }

    // Validate string argument
    if (TAG(str_arg) != CLJ_STRING) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "upper-case requires a string argument");
        return NULL;
    }

    CljString *str = as_clj_string(str_arg);
    const char *str_data = string_data(str);
    uint16_t str_len = string_length(str);

    if (str_len == 0) {
        return string_empty_singleton;
    }

    // Convert to upper-case directly in CljString buffer
    CljString *result = make_string_buffer(str_len);
    for (uint16_t i = 0; i < str_len; i++) {
        result->data[i] = (char)toupper((unsigned char)str_data[i]);
    }
    result->data[str_len] = '\0';
    return AUTORELEASE(result);
}

// String lower-case: (lower-case s) - converts string to lower-case
ID native_lower_case(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "lower-case");

    ID str_arg = args[0];

    // Handle nil
    if (!str_arg) {
        return NULL; // nil -> nil
    }

    // Validate string argument
    if (TAG(str_arg) != CLJ_STRING) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "lower-case requires a string argument");
        return NULL;
    }

    CljString *str = as_clj_string(str_arg);
    const char *str_data = string_data(str);
    uint16_t str_len = string_length(str);

    if (str_len == 0) {
        return string_empty_singleton;
    }

    // Convert to lower-case directly in CljString buffer
    CljString *result = make_string_buffer(str_len);
    for (uint16_t i = 0; i < str_len; i++) {
        result->data[i] = (char)tolower((unsigned char)str_data[i]);
    }
    result->data[str_len] = '\0';
    return AUTORELEASE(result);
}

// String last-index-of: (last-index-of s value) or (last-index-of s value from-index)
ID native_last_index_of(ID *args, unsigned int argc) {
    CHECK_ARITY_RANGE(argc, 2, 3, "last-index-of");

    ID str_arg = args[0];
    ID value_arg = args[1];
    ID from_index_arg = argc == 3 ? args[2] : NULL;

    // Validate string argument
    if (!str_arg || TAG(str_arg) != CLJ_STRING) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "last-index-of requires a string as first argument");
        return NULL;
    }

    // Validate value argument
    if (!value_arg || TAG(value_arg) != CLJ_STRING) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "last-index-of requires a string as second argument");
        return NULL;
    }

    CljString *str = as_clj_string(str_arg);
    CljString *value = as_clj_string(value_arg);
    if (!str || !value) return NULL;

    const char *str_data = string_data(str);
    const char *value_data = clj_string_data(value);
    uint16_t str_len = string_length(str);
    uint16_t value_len = string_length(value);

    // Handle empty value
    if (value_len == 0) {
        // Empty string always found at end
        return fixnum(str_len);
    }

    // Validate from-index if provided
    int from_index = str_len - 1; // Default: search from end
    if (from_index_arg) {
        if (TAG(from_index_arg) != CLJ_INT) {
            throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                      "last-index-of requires an integer as from-index");
            return NULL;
        }
        from_index = as_fixnum(from_index_arg);
        if (from_index < 0) from_index = 0;
        if (from_index >= str_len) from_index = str_len - 1;
    }

    // Search backwards from from_index
    for (int i = from_index; i >= 0; i--) {
        if (i + value_len > str_len) continue;

        // Check if substring matches
        bool match = true;
        for (uint16_t j = 0; j < value_len; j++) {
            if (str_data[i + j] != value_data[j]) {
                match = false;
                break;
            }
        }

        if (match) {
            return fixnum(i);
        }
    }

    // Not found
    return NULL; // nil
}

// String reverse: (reverse s) - reverses a string (not lists)
ID native_string_reverse(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "reverse");

    ID str_arg = args[0];

    // Handle nil
    if (!str_arg) {
        return NULL; // nil -> nil
    }

    // Validate string argument
    if (TAG(str_arg) != CLJ_STRING) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "reverse requires a string argument");
        return NULL;
    }

    CljString *str = as_clj_string(str_arg);
    const char *str_data = string_data(str);
    uint16_t str_len = string_length(str);

    if (str_len == 0) {
        return string_empty_singleton;
    }

    // Reverse string directly in CljString buffer
    CljString *result = make_string_buffer(str_len);
    for (uint16_t i = 0; i < str_len; i++) {
        result->data[i] = str_data[str_len - 1 - i];
    }
    result->data[str_len] = '\0';
    return AUTORELEASE(result);
}

// ============================================================================
// Namespace introspection functions
// ============================================================================

// ns-map: Returns the mappings map of a namespace
// Usage: (ns-map ns-name) or (ns-map 'ns-name)
// Returns a map of all symbols to their values in the namespace
ID native_ns_map(ID *args, unsigned int argc) {
    CLJ_ASSERT(argc == 1 && "ns-map: arity check failed");
    if (argc != 1) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "ns-map expects exactly 1 argument, got %u", argc);
        return NULL;
    }
    
    ID ns_arg = args[0];
    if (!ns_arg) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "ns-map: argument must not be nil");
        return NULL;
    }

    CljNamespace *target_ns = NULL;
    int tag = TAG(ns_arg);
    
    if (tag == CLJ_SYMBOL) {
        target_ns = ns_find_by_symbol(as_symbol(ns_arg));
    } else if (tag == CLJ_STRING) {
        target_ns = ns_find(string_data(ns_arg));
    } else if (tag == CLJ_NAMESPACE) {
        target_ns = (CljNamespace*)ns_arg;
    } else {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "ns-map: argument must be a symbol, string, or namespace");
        return NULL;
    }

    if (!target_ns) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "Namespace not found");
        return NULL;
    }

    return target_ns->mappings ? target_ns->mappings : make_map(0);
}

// find-ns: Returns the namespace object for the given name
// Usage: (find-ns 'ns-name) or (find-ns "ns-name")
// Returns the namespace object or nil if not found
ID native_find_ns(ID *args, unsigned int argc) {
    CLJ_ASSERT(argc == 1 && "find-ns: arity check failed");
    if (argc != 1) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "find-ns expects exactly 1 argument, got %u", argc);
        return NULL;
    }
    
    ID ns_arg = args[0];
    if (!ns_arg) return NULL; // nil -> nil (Clojure-compatible)

    int tag = TAG(ns_arg);
    if (tag == CLJ_SYMBOL) {
        return ns_find_by_symbol(as_symbol(ns_arg));
    } else if (tag == CLJ_STRING) {
        return ns_find(string_data(ns_arg));
    }
    
    throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                              "find-ns: argument must be a symbol or string");
    return NULL;
}

// all-ns: Returns a list of all namespace objects
// Usage: (all-ns)
ID native_all_ns(ID *args, unsigned int argc) {
    (void)args;
    if (argc != 0) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "all-ns expects no arguments, got %u", argc);
        return NULL;
    }

    if (!g_runtime.ns_registry) {
        return empty_list();
    }

    CljObject *result = NULL;
    MAP_FOR_EACH(g_runtime.ns_registry, key, val) {
        (void)key;
        if (!val) {
            continue;
        }
        result = (CljObject*)make_list(val, (CljList*)result);
    }

    if (!result) {
        return empty_list();
    }

    return AUTORELEASE(result);
}

// Helper for dir: convert argument to namespace
static CljNamespace* namespace_from_value(ID value) {
    if (!value) {
        return NULL;
    }

    int tag = TAG(value);
    if (tag == CLJ_NAMESPACE) {
        return (CljNamespace*)value;
    } else if (tag == CLJ_SYMBOL) {
        if (IS_KEYWORD(value)) {
            return NULL;
        }
        return ns_find_by_symbol(as_symbol(value));
    } else if (tag == CLJ_STRING) {
        return ns_find(string_data((CljString*)value));
    }
    return NULL;
}

static int compare_symbol_names(const void *lhs, const void *rhs) {
    const char *left = lhs ? *(const char* const*)lhs : NULL;
    const char *right = rhs ? *(const char* const*)rhs : NULL;
    if (left == right) return 0;
    if (!left) return 1;
    if (!right) return -1;
    return strcmp(left, right);
}

// source: Print source code for a function (native implementation for clojure.repl namespace)
// Usage: (source 'function-name) or (source function-var)
// Note: In Clojure, source is a normal function, not a special form
// The argument is evaluated: (source 'inc) evaluates 'inc to the symbol inc, then resolves it
ID native_source(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "source");
    
    ID arg = args[0];
    if (!arg) {
        printf("Source not available\n");
        return NULL;
    }
    
    // If arg is already a function (closure), use it directly
    ID target_func = NULL;
    if (TAG(arg) == CLJ_CLOSURE) {
        target_func = arg;
    } else if (TAG(arg) == CLJ_SYMBOL && !IS_KEYWORD(arg)) {
        // If arg is a symbol, resolve it to get the function
        // g_current_eval_state is thread-local and defined at the top of this file
        if (g_current_eval_state) {
            target_func = ns_resolve(g_current_eval_state, as_symbol(arg));
        }
    }
    
    // If we have a function, print its full definition AST: (fn [params*] body)
    if (target_func && TAG(target_func) == CLJ_CLOSURE) {
        CljFunction *fn = as_function(target_func);
        if (fn) {
    ID params_id = NULL;
    if (fn->params) {
        params_id = fn->params;
    }
            ID body_id = fn->body ? fn->body : NULL;

            bool previous_mode = strings_set_special_form_rendering(false);
            CljString *params_repr = to_string(params_id);
            CljString *body_repr = to_string(body_id);
            strings_set_special_form_rendering(previous_mode);

            if (!params_repr || !body_repr) {
                printf("Source not available\n");
                return NULL;
            }

            RETAIN(params_repr);
            RETAIN(body_repr);

            printf("(fn %s %s)\n", string_data(params_repr), string_data(body_repr));

            RELEASE(body_repr);
            RELEASE(params_repr);
            return NULL;
        }
    }
    
    // Otherwise, print the argument as-is
    ID args_str[] = { arg };
    CljString *result = native_str(args_str, 1);
    if (result) {
        printf("%s\n", string_data(result));
        RELEASE(result);
    } else {
        printf("Source not available\n");
    }
    return NULL;
}

ID native_repl_dir(ID *args, unsigned int argc) {
    CHECK_ARITY_MAX(argc, 1, "dir");

    EvalState *st = g_current_eval_state;
    CljNamespace *target_ns = NULL;

    if (argc == 0) {
        if (st && st->current_ns) {
            target_ns = st->current_ns;
        } else {
            target_ns = ns_find("user");
            if (!target_ns) {
                target_ns = ns_find("clojure.core");
            }
        }
    } else {
        target_ns = namespace_from_value(args[0]);
    }

    if (!target_ns || !target_ns->mappings) {
        printf("Namespace not found\n");
        return NULL;
    }

    int entry_count = 0;
    MAP_FOR_EACH(target_ns->mappings, key, value) {
        if (key && TAG(key) == CLJ_SYMBOL) {
            entry_count++;
        }
    }

    if (entry_count == 0) {
        return NULL;
    }

    const char **names = (const char**)malloc(sizeof(char*) * entry_count);
    if (!names) {
        throw_oom();
        return NULL;
    }

    int idx_names = 0;
    MAP_FOR_EACH(target_ns->mappings, key, value) {
        if (key && TAG(key) == CLJ_SYMBOL) {
            CljSymbol *sym = as_symbol(key);
            if (sym && sym->cname) {
                names[idx_names++] = sym->cname;
            }
        }
    }

    qsort(names, idx_names, sizeof(char*), compare_symbol_names);
    for (int i = 0; i < idx_names; i++) {
        if (names[i]) {
            printf("%s\n", names[i]);
        }
    }
    free(names);
    return NULL;
}

// rt: Return retain count of an object (native implementation for clojure.repl namespace)
// Usage: (rt obj) - returns the reference count as an integer
ID native_rt(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "rt");
    
    ID obj = args[0];
    if (!obj) {
        // nil has retain count 0
        return fixnum(0);
    }
    
    // Get retain count using get_retain_count from memory.h
    int rc = get_retain_count(obj);
    return fixnum(rc);
}

// Forward declarations for native functions used in lookup table
ID native_meta(ID *args, unsigned int argc);

// ============================================================================
// Native function lookup table for stubs
// Uses CljSymbol* for efficient pointer comparison (symbols are interned)
// Statically initialized at compile-time using static symbol data structures
typedef struct {
    CljSymbol *clojure_symbol;  // Clojure function symbol (e.g., &sym_trim_data.sym)
    BuiltinFn native_func;      // Native C function pointer
} NativeFunctionEntry;

// Compile-time initialized lookup table (DRY: avoids runtime initialization)
// Uses static symbol data structures (&sym_*_data.sym) for compile-time references
static const NativeFunctionEntry native_function_table[] = {
    // clojure.string functions
    {&sym_trim_data.sym, native_trim},
    {&sym_upper_case_data.sym, native_upper_case},
    {&sym_lower_case_data.sym, native_lower_case},
    {&sym_last_index_of_data.sym, native_last_index_of},
    {&sym_string_reverse_data.sym, native_string_reverse},
    // clojure.repl functions
    {&sym_source_data.sym, native_source},
    {&sym_dir_data.sym, native_repl_dir},
    {&sym_rt_data.sym, native_rt},
    // clojure.core functions
    {&sym_meta_data.sym, native_meta},
    {&sym_with_meta_data.sym, native_with_meta},
    {&sym_reduce_data.sym, native_reduce},
    {&sym_plus_data.sym, native_add_variadic},
    {&sym_minus_data.sym, native_sub_variadic},
    {&sym_multiply_data.sym, native_mul_variadic},
    {&sym_divide_data.sym, native_div_variadic},
    {&sym_mod_data.sym, native_mod},
    {&sym_quot_data.sym, native_quot},
    {&sym_bit_shift_left_data.sym, native_bit_shift_left},
    {&sym_range_data.sym, native_range},
    {&sym_repeat_data.sym, native_repeat},
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
    {&sym_nth_data.sym, nth2},
    {&sym_peek_data.sym, native_peek},
    {&sym_pop_data.sym, native_pop},
    {&sym_subvec_data.sym, native_subvec},
    {&sym_conj_data.sym, native_conj},
    {&sym_first_data.sym, native_first},
    {&sym_rest_data.sym, native_rest},
    {&sym_next_data.sym, native_next},
    {&sym_cons_data.sym, native_cons},
    {&sym_list_data.sym, native_list},
    {&sym_count_data.sym, native_count},
    {&sym_nilp_data.sym, native_nilp},
    {&sym_reverse_data.sym, native_reverse},
    {&sym_assoc_data.sym, assoc3},
    {&sym_dissoc_data.sym, native_dissoc},
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
    {&sym_number_p_data.sym, native_number_p},
    {&sym_integer_p_data.sym, native_integer_p},
    {&sym_float_p_data.sym, native_float_p},
    {&sym_string_p_data.sym, native_string_p},
    {&sym_keyword_p_data.sym, native_keyword_p},
    {&sym_symbol_p_data.sym, native_symbol_p},
    {&sym_fn_p_data.sym, native_fn_p},
    {&sym_char_p_data.sym, native_char_p},
    {&sym_list_p_data.sym, native_list_p},
    {&sym_sleep_data.sym, native_sleep},
    {&sym_ns_map_data.sym, native_ns_map},
    {&sym_find_ns_data.sym, native_find_ns},
    {&sym_all_ns_data.sym, native_all_ns},
    {(CljSymbol*)&sym_do_data.sym, native_do},
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
#ifndef ESP32_BUILD
    {&sym_slurp_data.sym, native_slurp},
    {&sym_spit_data.sym, native_spit},
#endif
    {NULL, NULL}  // Sentinel
};

// Lookup native function by Clojure symbol
// Uses pointer comparison for efficiency (symbols are interned)
// Returns NULL if not found
BuiltinFn native_function_lookup(CljSymbol *symbol) {
    if (!symbol) {
        return NULL;
    }

    // Compare symbol pointers directly (efficient due to interning)
    // Note: We compare with the static symbol data structures, but since
    // init_special_symbols() sets up SYM_TRIM = &sym_trim_data.sym and
    // adds it to the symbol table, intern_symbol("clojure.string", "trim")
    // will return the same pointer (SYM_TRIM) due to symbol interning.
    for (int i = 0; native_function_table[i].clojure_symbol != NULL; i++) {
        if (native_function_table[i].clojure_symbol == symbol) {
            return native_function_table[i].native_func;
        }
    }
    
    const char *cname = symbol->cname ? symbol->cname : NULL;
    const char *ns_name = (symbol->ns_name && symbol->ns_name->cname) ? symbol->ns_name->cname : NULL;
    if (!cname) {
        return NULL;
    }

    // First, try to find an entry whose namespace matches exactly (if symbol is qualified)
    if (ns_name && ns_name[0]) {
        char qualified_name[128];
        snprintf(qualified_name, sizeof(qualified_name), "%s/%s", ns_name, cname);

        for (int i = 0; native_function_table[i].clojure_symbol != NULL; i++) {
            CljSymbol *table_sym = native_function_table[i].clojure_symbol;
            if (!table_sym || !table_sym->cname) continue;
            if (table_sym->ns_name && table_sym->ns_name->cname &&
                strcmp(table_sym->ns_name->cname, ns_name) == 0 &&
                strcmp(table_sym->cname, cname) == 0) {
                return native_function_table[i].native_func;
            }
        }

        // Special case: clojure.core entries are stored without namespace (for compatibility)
        // If no namespaced entry was found, allow matching unqualified entries only for clojure.core
        if (strcmp(ns_name, "clojure.core") == 0) {
            for (int i = 0; native_function_table[i].clojure_symbol != NULL; i++) {
                CljSymbol *table_sym = native_function_table[i].clojure_symbol;
                if (!table_sym || table_sym->ns_name || !table_sym->cname) continue;
                if (strcmp(table_sym->cname, cname) == 0) {
                    return native_function_table[i].native_func;
                }
            }
        }

        // Handle pseudo-qualified names stored as single cname (e.g., "Math/sqrt")
        for (int i = 0; native_function_table[i].clojure_symbol != NULL; i++) {
            CljSymbol *table_sym = native_function_table[i].clojure_symbol;
            if (!table_sym || table_sym->ns_name || !table_sym->cname) continue;
            if (strcmp(table_sym->cname, qualified_name) == 0) {
                return native_function_table[i].native_func;
            }
        }
        return NULL;
    }

    // Unqualified symbol: fall back to name match (for cases where symbol pointers differ)
    for (int i = 0; native_function_table[i].clojure_symbol != NULL; i++) {
        CljSymbol *table_sym = native_function_table[i].clojure_symbol;
        if (table_sym && table_sym->cname && strcmp(table_sym->cname, cname) == 0) {
            return native_function_table[i].native_func;
        }
    }

    return NULL;
}

// Meta function: (meta obj) - returns metadata map or nil
ID native_meta(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "meta");

    ID obj = args[0];
    if (!obj) {
        return NULL; // nil -> nil
    }

#ifdef ENABLE_META
    // If obj is a symbol, resolve it to get the actual value (function, var, etc.)
    // This allows (meta trim) to work by resolving trim to the function first
    ID target_obj = obj;
    if (TAG(obj) == CLJ_SYMBOL) {
        // Get current eval state from builtin context to resolve symbol
        // Forward declaration for static variable
        if (g_current_eval_state && g_current_eval_state->current_ns) {
            ID resolved = ns_resolve(g_current_eval_state, (CljSymbol*)obj);
            if (resolved) {
                target_obj = resolved;
            }
        }
    }

    ID meta = meta_get(target_obj);
    if (meta) {
        RETAIN(meta); // meta_get doesn't retain, so we need to retain for return
        return meta;
    }
#endif // ENABLE_META

    return NULL; // No metadata -> nil
}

// With-meta function: (with-meta obj meta-map) - returns obj with new metadata
ID native_with_meta(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 2, "with-meta");

    ID obj = args[0];
    ID meta_map = args[1];

    // nil with any metadata returns nil
    if (!obj) {
        return NULL;
    }

    // Immediates (numbers, keywords, etc.) don't support metadata
    if (IS_IMMEDIATE(obj)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
            "with-meta: immediates don't support metadata", __FILE__, __LINE__, 0);
        return NULL;
    }

    // Strings don't support metadata in Clojure
    if (TAG(obj) == CLJ_STRING) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
            "with-meta: strings don't support metadata", __FILE__, __LINE__, 0);
        return NULL;
    }

#ifdef ENABLE_META
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

// Create symbol from string (with optional namespace)
ID native_symbol(ID *args, unsigned int argc) {
    // symbol accepts 1 or 2 arguments: (symbol "name") or (symbol "ns" "name")
    if (argc != 1 && argc != 2) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
                "symbol requires exactly 1 or 2 argument%s, got %u",
                argc == 1 ? "" : "s", argc);
        throw_exception(EXCEPTION_ARITY, error_msg, __FILE__, __LINE__, 0);
        return NULL;
    }

    const char *ns = NULL;
    const char *cname = NULL;

    if (argc == 2) {
        // Two arguments: namespace (can be nil) and name
        ID ns_arg = args[0];
        ID name_arg = args[1];

        // Namespace can be nil (NULL) or a string
        if (ns_arg && TAG(ns_arg) != CLJ_STRING) {
            throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                    "symbol namespace must be a string or nil");
            return NULL;
        }

        if (!name_arg || TAG(name_arg) != CLJ_STRING) {
            throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                    "symbol requires a string for name");
            return NULL;
        }

        // Extract namespace (can be NULL if nil was passed)
        if (ns_arg) {
            ns = string_data(ns_arg);
        } else {
            ns = NULL;  // nil namespace
        }

        cname = string_data(name_arg);
    } else {
        // One argument: name only
        ID name_arg = args[0];

        if (!name_arg || TAG(name_arg) != CLJ_STRING) {
            throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                    "symbol requires a string argument");
            return NULL;
        }

        CljString *name_str = (CljString*)name_arg;
        cname = clj_string_data(name_str);
    }

    // Create symbol from string(s)
    CljSymbol *ns_name_sym = ns ? intern_symbol_global(ns) : NULL;
    CljSymbol *sym = intern_symbol(ns_name_sym, cname);
    if (!sym) {
        throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                "Failed to create symbol from string");
        return NULL;
    }

    // intern_symbol returns a retained symbol, but builtin functions should return AUTORELEASE
    return AUTORELEASE(sym);
}

// File I/O: slurp - read entire file as string
#ifndef ESP32_BUILD
#include "file_utils.h"

ID native_slurp(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "slurp")) return NULL;

    // Convert argument to CljString, then get C-string data
    CljString *filename_str_obj = to_string(args[0]);
    if (!filename_str_obj) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                       "slurp requires a string or symbol argument",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    const char *filename_str = string_data(filename_str_obj);

    // Use file_slurp utility function
    // file_slurp throws exceptions on errors (file not found, etc.)
    CljString *result = file_slurp(filename_str);

    // file_slurp throws exception on errors, so if we get here, result is valid
    return result ? AUTORELEASE(result) : NULL;
}

// Forward declaration for eval_source_in_current_state (defined below in require section)
static bool eval_source_in_current_state(const char *src, const char *src_name, EvalState *st);

// load-file: read and evaluate all forms in a file (Clojure standard function)
// DRY: Uses eval_source_in_current_state for the actual evaluation
ID native_load_file(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "load-file")) return NULL;

    // Get filename as string
    CljString *filename_str_obj = to_string(args[0]);
    if (!filename_str_obj) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                       "load-file requires a string argument",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    const char *filename = string_data(filename_str_obj);

    // Read file content
    CljString *content = file_slurp(filename);
    if (!content) {
        return NULL; // file_slurp already threw exception
    }

    // Get EvalState (use global state)
    EvalState *st = g_current_eval_state ? g_current_eval_state : get_global_eval_state();

    // DRY: Use eval_source_in_current_state (same as require uses)
    const char *src = string_data(content);
    bool ok = eval_source_in_current_state(src, filename, st);

    // load-file returns nil (like Clojure)
    return ok ? NULL : NULL;
}
#endif // ESP32_BUILD

// ----------------------------------------------------------------------------
// REQUIRE IMPLEMENTATION (Clojure-like namespace loader)
// ----------------------------------------------------------------------------
#ifndef ESP32_BUILD
static char* namespace_to_relpath(const char *ns_name) {
    if (!ns_name) return NULL;
    size_t len = strlen(ns_name);
    // Worst case: all chars + possible slashes + ".clj" + NUL
    char *buf = (char*)malloc(len + 5);
    if (!buf) return NULL;
    for (size_t i = 0; i < len; i++) {
        char c = ns_name[i];
        if (c == '.') buf[i] = '/';
        else if (c == '-') buf[i] = '_'; // Clojure file mapping: hyphen -> underscore
        else buf[i] = c;
    }
    buf[len] = '\0';
    strcat(buf, ".clj");
    return buf;
}

static char* read_file_once(const char *path) {
    if (!path) return NULL;
    FILE *fp = fopen(path, "r");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    long sz = ftell(fp);
    if (sz < 0) { fclose(fp); return NULL; }
    if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return NULL; }
    char *buffer = (char*)malloc((size_t)sz + 1);
    if (!buffer) { fclose(fp); return NULL; }
    size_t n = fread(buffer, 1, (size_t)sz, fp);
    buffer[n] = '\0';
    fclose(fp);
    return buffer;
}

static void store_resolved_path(char *dest, size_t dest_size, const char *value) {
    if (!dest || dest_size == 0) return;
    if (!value) {
        dest[0] = '\0';
        return;
    }
    strncpy(dest, value, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

static char* read_file_cstr(const char *path, char *resolved_path, size_t resolved_path_size) {
    char *buffer = read_file_once(path);
    if (buffer) {
        store_resolved_path(resolved_path, resolved_path_size, path);
        return buffer;
    }
    
    // If path is relative, also try "../path" and "../../path" to support running from build directories
    if (!path || path[0] == '\0') {
        return NULL;
    }
    
    char parent_path[512];
    if (snprintf(parent_path, sizeof(parent_path), "../%s", path) < (int)sizeof(parent_path)) {
        buffer = read_file_once(parent_path);
        if (buffer) {
            store_resolved_path(resolved_path, resolved_path_size, parent_path);
            return buffer;
        }
    }
    if (snprintf(parent_path, sizeof(parent_path), "../../%s", path) < (int)sizeof(parent_path)) {
        buffer = read_file_once(parent_path);
        if (buffer) {
            store_resolved_path(resolved_path, resolved_path_size, parent_path);
            return buffer;
        }
    }
    
    return NULL;
}

static bool eval_source_in_current_state(const char *src, const char *src_name, EvalState *st) {
    if (!src || !st) return false;
    int success_count = 0;
    WITH_AUTORELEASE_POOL({
        Reader reader;
        reader_init(&reader, src);
        if (src_name && src_name[0]) {
            reader_set_source_name(&reader, src_name);
        } else if (st && st->current_ns && st->current_ns->name && st->current_ns->name->cname) {
            reader_set_source_name(&reader, st->current_ns->name->cname);
        } else {
            reader_set_source_name(&reader, "<namespace>");
        }
        while (!reader_is_eof(&reader)) {
            reader_skip_all(&reader);
            if (reader_is_eof(&reader)) break;

            // Save reader position before parsing to detect if we're stuck
            size_t pos_before = reader_offset(&reader);

            CljValue form = NULL;
            TRY {
                form = value_by_parsing_expr(&reader, st);
                if (!form) {
                    if (reader_is_eof(&reader)) break;
                    // Parse failed - skip to next line to avoid infinite loop
                    while (!reader_is_eof(&reader) && reader_current(&reader) != '\n') reader_next(&reader);
                    if (!reader_is_eof(&reader)) reader_next(&reader);
                    continue;
                }
                (void)eval_parsed(form, st, NULL);
                // value_by_parsing_expr returns AUTORELEASE object
                success_count++;

                // Check if reader advanced (to detect infinite loops)
                size_t pos_after = reader_offset(&reader);
                if (pos_after == pos_before && !reader_is_eof(&reader)) {
                    // Reader didn't advance - skip character to avoid infinite loop
                    reader_next(&reader);
                }
            } CATCH(ex) {
                // Skip to next line to avoid infinite loop
                while (!reader_is_eof(&reader) && reader_current(&reader) != '\n') reader_next(&reader);
                if (!reader_is_eof(&reader)) reader_next(&reader);
            } END_TRY
        }
    });
    // Return true if at least some expressions succeeded (partial loading is OK)
    return success_count > 0;
}

/**
 * @brief Copy symbols from source namespace to target namespace
 * @param source_ns Source namespace
 * @param target_ns Target namespace
 * @param symbols Vector of symbols to copy
 */
static void copy_symbols_to_namespace(CljNamespace *source_ns, CljNamespace *target_ns, CljObject *symbols) {
    if (!source_ns || !target_ns || !symbols) return;

    if (!symbols || TAG(symbols) != CLJ_VECTOR) return;

    CljVector *vec = as_vector(symbols);
    int count = vector_count(vec);
    for (int i = 0; i < count; i++) {
        CljObject *sym = vector_nth(vec, i);
        if (!sym || TAG(sym) != CLJ_SYMBOL) {
            RELEASE(sym);
            continue;
        }

        // CRITICAL: Mappings use qualified symbols as keys (except clojure.core uses unqualified)
        // Must qualify the symbol with source namespace name for lookup (unless source is clojure.core)
        CljSymbol *sym_obj = as_symbol(sym);
        CljSymbol *lookup_sym = sym_obj;
        if (source_ns->name == SYM_CLOJURE_CORE) {
            // clojure.core uses unqualified symbols as keys - use symbol as-is
            lookup_sym = sym_obj;
        } else if (sym_obj && !sym_obj->ns_name && source_ns->name && source_ns->name->cname) {
            // Other namespaces: qualify the symbol for lookup
            lookup_sym = intern_symbol(source_ns->name, sym_obj->cname);
            if (!lookup_sym) {
                lookup_sym = sym_obj; // Fallback to original
            }
        }

        // Look up symbol in source namespace
        CljObject *val = map_get((CljValue)source_ns->mappings, (CljValue)lookup_sym, NULL);
        if (val) {
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
static void copy_all_symbols_to_namespace(CljNamespace *source_ns, CljNamespace *target_ns) {
    if (!source_ns || !target_ns || !source_ns->mappings) return;

        CljMap *map = source_ns->mappings;
    if (!map) return;

    // Iterate through all mappings in source namespace
    // Keys are qualified symbols (e.g., test.referall/var1 or clojure.repl/doc)
    // EXCEPTION: clojure.core uses unqualified symbols as keys (ns_name = NULL)
    // We need to extract the unqualified symbol name and copy it to target namespace
    MAP_FOR_EACH(map, key, val) {
        if (key && val && TAG(key) == CLJ_SYMBOL) {
            CljSymbol *key_sym = as_symbol(key);
            if (key_sym && key_sym->cname) {
                const char *unqualified_name = NULL;
                
                if (source_ns->name == SYM_CLOJURE_CORE) {
                    // clojure.core uses unqualified symbols as keys - cname is already unqualified
                    unqualified_name = key_sym->cname;
                } else {
                    // Other namespaces: extract unqualified name from qualified symbol
                    // Qualified symbols have format "namespace/name" (e.g., "clojure.repl/doc")
                    const char *cname = key_sym->cname;
                    const char *slash = strrchr(cname, '/');
                    unqualified_name = slash ? slash + 1 : cname;
                }
                
                // Create unqualified symbol for target namespace
                // Use ns_define_refer to store unqualified symbol (like Clojure/JVM)
                CljSymbol *unqualified_sym = intern_symbol_global(unqualified_name);
                if (unqualified_sym) {
                    ns_define_refer(target_ns, unqualified_sym, val);
                }
            }
        }
    }
}

/**
 * @brief Process a single require spec (Symbol or Vector)
 * @param spec Require spec (Symbol or Vector [namespace :as alias] or [namespace :refer ...])
 * @param st Evaluation state
 * @return true on success, false on error
 */
static bool process_require_spec(CljObject *spec, EvalState *st) {
    if (!spec || !st) return false;

    const char *ns_name = NULL;
    CljObject *alias_sym = NULL;
    CljObject *refer_syms = NULL;
    bool refer_all = false;

    CljVector *vec = NULL;

    // Handle simple Symbol case: (require 'namespace)
    if (spec && TAG(spec) == CLJ_SYMBOL) {
        CljSymbol *sym = as_symbol(spec);
        if (!sym || !sym->cname) return false;
        ns_name = sym->cname;
    }
    // Handle Vector case: [namespace :as alias] or [namespace :refer [syms]]
    else if (spec && TAG(spec) == CLJ_VECTOR) {
        vec = as_vector(spec);
        if (vector_count(vec) < 1) return false;

        // First element should be namespace name (Symbol or String)
        CljObject *ns_obj = vector_nth(vec, 0);
        if (!ns_obj) return false;

        if (ns_obj && TAG(ns_obj) == CLJ_SYMBOL) {
            CljSymbol *ns_sym = as_symbol(ns_obj);
            if (!ns_sym || !ns_sym->cname) {
                RELEASE(ns_obj);
                return false;
            }
            ns_name = ns_sym->cname;
        } else {
            CljString *ns_str_obj = to_string(ns_obj);
            if (!ns_str_obj) {
                RELEASE(ns_obj);
                return false;
            }
            ns_name = string_data(ns_str_obj);
        }
        // ns_obj lifetime is tied to vector - no release needed

        // Parse keywords: :as, :refer
        int vec_count = vector_count(vec);
        for (int i = 1; i < vec_count; i++) {
            CljObject *elem = vector_nth(vec, i);
            if (!elem) continue;

            // Check if it's a keyword (Symbol starting with :)
            if (elem && TAG(elem) == CLJ_SYMBOL) {
                CljSymbol *kw = as_symbol(elem);
                if (!kw || !kw->cname) {
                    RELEASE(elem);
                    continue;
                }

                // Use pointer comparison for :as and :refer keywords (more efficient and reliable)
                if (kw == SYM_KW_AS) {
                    // :as alias
                    if (i + 1 < vec_count) {
                        alias_sym = vector_nth(vec, i + 1);
                        // Don't release alias_sym - it's stored for later use
                        i++; // Skip next element
                    }
                    RELEASE(elem);
                } else if (kw == SYM_KW_REFER) {
                    // :refer [symbols] or :refer :all
                    if (i + 1 < vec_count) {
                        CljObject *refer_arg = vector_nth(vec, i + 1);
                        if (refer_arg && TAG(refer_arg) == CLJ_SYMBOL) {
                            CljSymbol *refer_sym = as_symbol(refer_arg);
                            if (refer_sym && refer_sym->cname && strcmp(refer_sym->cname, ":all") == 0) {
                                refer_all = true;
                                RELEASE(refer_arg);
                            } else {
                                RELEASE(refer_arg);
                            }
                        } else if (refer_arg && TAG(refer_arg) == CLJ_VECTOR) {
                            refer_syms = refer_arg;
                            // Don't release refer_syms - it's stored for later use
                        } else {
                            RELEASE(refer_arg);
                        }
                        i++; // Skip next element
                    }
                    RELEASE(elem);
                } else {
                    RELEASE(elem);
                }
            } else {
                RELEASE(elem);
            }
        }
    } else {
        return false;
    }

    if (!ns_name) return false;

    // Load namespace (existing logic)
    // CRITICAL: Check if namespace exists, but don't skip loading if it only has native functions
    // A namespace might exist because native functions were registered, but Clojure code hasn't been loaded yet
    CljNamespace *existing = ns_find(ns_name);
    if (existing) {
        // Check if namespace has been fully loaded by checking for a marker function
        // For clojure.core, check if inc exists (core function)
        // For clojure.string, check if blank? exists (first Clojure function defined)
        // OPTIMIZATION: Cache symbol lookups to avoid repeated intern_symbol calls
        // CRITICAL: Namespace mappings use qualified symbols as keys, so we must use a qualified symbol
        bool needs_loading = true;
        if (strcmp(ns_name, "clojure.core") == 0 && existing->mappings) {
            // clojure.core is loaded from C code, so if it has mappings, it's fully loaded
            // Check for a marker function like 'inc' to confirm it's loaded
            static CljSymbol *cached_inc_sym = NULL;
            if (!cached_inc_sym) {
                cached_inc_sym = intern_symbol_global("inc");
            }
            if (cached_inc_sym) {
                // CRITICAL: Use sentinel to distinguish "key not found" from "value is nil"
                ID inc_func = map_get(existing->mappings, cached_inc_sym, NOT_FOUND);
                if (inc_func != NOT_FOUND) {
                    needs_loading = false; // inc found - namespace is fully loaded
                }
            }
        } else if (strcmp(ns_name, "clojure.string") == 0 && existing->mappings) {
            static CljSymbol *cached_blank_sym = NULL;
            if (!cached_blank_sym) {
                CljSymbol *ns_sym = intern_symbol_global("clojure.string");
                if (ns_sym) {
                    cached_blank_sym = intern_symbol(ns_sym, "blank?");
                }
            }
            if (cached_blank_sym) {
                // CRITICAL: Use sentinel to distinguish "key not found" from "value is nil"
                // nil (NULL) is a valid value in Clojure, so we can't use NULL as not_found
                ID blank_func = map_get(existing->mappings, cached_blank_sym, NOT_FOUND);
                if (blank_func != NOT_FOUND) {
                    needs_loading = false; // blank? found - namespace is fully loaded
                }
            }
        }

        if (!needs_loading) {
            // Namespace already fully loaded - just set alias/refer if needed
            // CRITICAL: Ensure st->current_ns is valid before setting alias
            // This is the namespace where the alias should be stored
            if (st && st->current_ns) {
                // Set alias if provided
                // NOTE: alias_sym is extracted from vector in lines 2227-2234
                // It should be set by the time we reach here if :as was in the vector
                // CRITICAL: Use same logic as the working case (line 2412) for consistency
                if (alias_sym && TAG(alias_sym) == CLJ_SYMBOL) {
                    CljObject *ns_name_sym = (CljObject*)intern_symbol_global(ns_name);
                    if (ns_name_sym) {
                        ns_set_alias(st->current_ns, alias_sym, ns_name_sym);
                    }
                }
                // Handle refer
                if (refer_all) {
                    copy_all_symbols_to_namespace(existing, st->current_ns);
                } else if (refer_syms) {
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
    if (!rel) {
        return false;
    }

    // Search order: libs/<rel>, then <rel> (project root)
    char libs_path[512];
    snprintf(libs_path, sizeof(libs_path), "libs/%s", rel);

    char resolved_path[512];
    resolved_path[0] = '\0';
    const char *source_path = NULL;
    char *source = read_file_cstr(libs_path, resolved_path, sizeof(resolved_path));
    if (source) {
        source_path = resolved_path;
    } else {
        source = read_file_cstr(rel, resolved_path, sizeof(resolved_path));
        if (source) {
            source_path = resolved_path;
        }
    }

    if (!source) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
                 "Require failed: namespace '%s' not found (expected file %s or libs/%s)",
                 ns_name, rel, rel);
        throw_exception(EXCEPTION_FILE_NOT_FOUND, error_msg, __FILE__, __LINE__, 0);
        free(rel);
        return false;
    }

    // Evaluate source in current state
    // Save the original namespace pointer (not just the name) to restore it later
    // This ensures aliases are set in the correct namespace
    CljNamespace *orig_ns = NULL;
    if (st && st->current_ns) {
        orig_ns = st->current_ns;
    }

    // CRITICAL: Ensure target namespace exists before loading
    // This ensures that native functions registered before loading are in the correct namespace
    CljNamespace *target_ns = ns_get_or_create(ns_name, NULL);
    if (!target_ns) {
        free(source);
        free(rel);
        return false;
    }

    // Temporarily switch to target namespace
    if (st) {
        st->current_ns = target_ns;
    }
    bool ok = eval_source_in_current_state(source, source_path, st);
    // Restore original namespace
    if (st && orig_ns) {
        st->current_ns = orig_ns;
    }

    free(source);
    free(rel);

    // CRITICAL: Don't fail completely if some expressions failed to load
    // Some functions may have been successfully defined even if others failed
    // This allows partial loading (e.g., if one function has an error, others still work)
    // We only return false if the namespace itself couldn't be created
    if (!ok) {
        // Check if namespace was at least created (even if loading had errors)
        CljNamespace *loaded_ns = ns_find(ns_name);
        if (!loaded_ns) {
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
    if (loaded_ns) {
        if (alias_sym && TAG(alias_sym) == CLJ_SYMBOL) {
            CljObject *ns_name_sym = (CljObject*)intern_symbol_global(ns_name);
            if (ns_name_sym) {
                ns_set_alias(st->current_ns, alias_sym, ns_name_sym);
            }
        }
        if (refer_all) {
            copy_all_symbols_to_namespace(loaded_ns, st->current_ns);
        } else if (refer_syms) {
            copy_symbols_to_namespace(loaded_ns, st->current_ns, refer_syms);
        }
    }

    // Free ns_name if it was allocated
    // ns_name is from autoreleased CljString - no free needed

    return true;
}

static ID normalize_require_spec(ID spec, bool *needs_release) {
    if (needs_release) {
        *needs_release = false;
    }
    if (!spec) {
        return spec;
    }

    unsigned int tag = TAG(spec);
    if (tag == CLJ_SYMBOL || tag == CLJ_VECTOR) {
        return spec;
    }

    if (list_type_matches(tag)) {
        CljList *list = as_list(spec);
        if (!list) return spec;
        
        // Handle (quote symbol) => symbol
        ID first = LIST_FIRST(list);
        if (first && TAG(first) == CLJ_SYMBOL) {
            CljSymbol *first_sym = as_symbol(first);
            if (first_sym == SYM_QUOTE) {
                // (quote x) => x
                ID quoted = LIST_REST(list) ? LIST_FIRST(as_list(LIST_REST(list))) : NULL;
                if (quoted && (TAG(quoted) == CLJ_SYMBOL || TAG(quoted) == CLJ_VECTOR)) {
                    return quoted;
                }
            }
        }
        
        // Convert list to vector for other cases
        CljVector *vec = make_vector(4, CLJ_VECTOR);
        if (!vec) {
            return NULL;
        }
        CljVector *transient_vec = vector_transient(vec);
        RELEASE(vec);
        if (!transient_vec) {
            return NULL;
        }

        CljList *current = list;
        while (current) {
            ID elem = LIST_FIRST(current);
            transient_vec = vector_conj(transient_vec, elem);
            if (!transient_vec) {
                return NULL;
            }
            current = LIST_REST(current) ? as_list(LIST_REST(current)) : NULL;
        }

        CljVector *persistent_vec = vector_persistent(transient_vec);
        RELEASE(transient_vec);
        if (!persistent_vec) {
            return NULL;
        }
        if (needs_release) {
            *needs_release = true;
        }
        return persistent_vec;
    }

    return spec;
}

ID native_require(ID *args, unsigned int argc) {
    if (argc == 0) {
        throw_exception(EXCEPTION_ARITY, "require requires at least 1 argument", __FILE__, __LINE__, 0);
        return NULL;
    }

    ID normalized_specs[argc];
    bool needs_release[argc];
    memset(needs_release, 0, sizeof(needs_release));

    EvalState *st = g_current_eval_state ? g_current_eval_state : get_global_eval_state();

    for (unsigned int i = 0; i < argc; i++) {
        bool release_spec = false;
        ID spec = normalize_require_spec(args[i], &release_spec);
        if (!spec) {
            for (unsigned int j = 0; j < i; j++) {
                if (needs_release[j]) {
                    RELEASE(normalized_specs[j]);
                }
            }
            return NULL;
        }
        normalized_specs[i] = spec;
        needs_release[i] = release_spec;

        if (spec && TAG(spec) != CLJ_SYMBOL && TAG(spec) != CLJ_VECTOR) {
            throw_exception(EXCEPTION_TYPE, "require expects a symbol or vector", __FILE__, __LINE__, 0);
            for (unsigned int j = 0; j <= i; j++) {
                if (needs_release[j]) {
                    RELEASE(normalized_specs[j]);
                }
            }
            return NULL;
        }
    }

    for (unsigned int i = 0; i < argc; i++) {
        if (!process_require_spec((CljObject*)normalized_specs[i], st)) {
            for (unsigned int j = 0; j < argc; j++) {
                if (needs_release[j]) {
                    RELEASE(normalized_specs[j]);
                }
            }
            return NULL;
        }
    }

    for (unsigned int i = 0; i < argc; i++) {
        if (needs_release[i]) {
            RELEASE(normalized_specs[i]);
        }
    }
    return NULL; // Clojure-compatible: require returns nil
}

#endif // ESP32_BUILD

// File I/O: spit - write string to file
#ifndef ESP32_BUILD
ID native_spit(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 2, "spit")) return NULL;

    // Convert first argument (filename) to CljString, then get C-string data
    CljString *filename_str_obj = to_string(args[0]);
    if (!filename_str_obj) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                       "spit requires a string or symbol as first argument (filename)",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    const char *filename_str = string_data(filename_str_obj);

    // Convert second argument (content) to CljString, then get C-string data
    CljString *content_str_obj = to_string(args[1]);
    if (!content_str_obj) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                       "spit requires a string or symbol as second argument (content)",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    const char *content_str = string_data(content_str_obj);

    // Open file for writing (overwrites if exists - Clojure-compatible)
    FILE *fp = fopen(filename_str, "w");
    if (!fp) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
                "Cannot open file '%s' for writing: %s", filename_str, strerror(errno));
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, error_msg,
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    // Write content to file
    size_t content_len = string_length(content_str_obj);
    size_t bytes_written = fwrite(content_str, 1, content_len, fp);

    // Check for write errors
    if (bytes_written != content_len) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
                "Error writing to file '%s': %s", filename_str, strerror(errno));
        fclose(fp);
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, error_msg,
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    // Ensure file is flushed
    if (fflush(fp) != 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
                "Error flushing file '%s': %s", filename_str, strerror(errno));
        fclose(fp);
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, error_msg,
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    // Cleanup
    fclose(fp);

    // Clojure-compatible: spit returns nil
    return NULL;
}
#endif // ESP32_BUILD

// Binary operations (inline for performance)
// Variadische Number-Reducer mit Single-Pass und Float-Promotion
ID native_add_variadic(ID *args, unsigned int argc) {
    if (argc == 0) return create_fixnum_result(0);
    if (argc == 1) return RETAIN(args[0]);

    bool sawFixed = false;
    int acc_i = 0;
    int32_t acc_fixed = 0;

    for (unsigned int i = 0; i < argc; i++) {
        if (!sawFixed) {
            switch (TAG(args[i])) {
                case CLJ_INT: {
                    int new_val = AS_FIXNUM(args[i]);
                    // Check for integer overflow before addition
                    if (acc_i > 0 && new_val > INT_MAX - acc_i) {
                        // Overflow detected - throw exception
                        return throw_arithmetic_overflow(ERR_INTEGER_OVERFLOW_ADDITION, acc_i, new_val);
                    } else if (acc_i < 0 && new_val < INT_MIN - acc_i) {
                        // Underflow detected - throw exception
                        return throw_arithmetic_overflow(ERR_INTEGER_UNDERFLOW_ADDITION, acc_i, new_val);
                    } else {
                        acc_i += new_val;
                    }
                    break;
                }
                case CLJ_FLOAT: {
                    sawFixed = true;
                    // Check for fixed-point overflow before conversion using original values
                    float acc_f = (float)acc_i;
                    float val_f = AS_FIXED(args[i]);
                    float result = acc_f + val_f;
                    if (result > 262144.0f || result < -262144.0f) { // Max fixed-point range
                        return throw_fixed_overflow(ERR_FIXED_OVERFLOW_ADDITION);
                    }
                    acc_fixed = fixnum_to_fixed(acc_i) + extract_fixed_value(args[i]);
                    break;
                }
                default: {
                    // Heap objects or other types - convert to fixed-point
                    sawFixed = true;
                    float acc_f = (float)acc_i;
                    float val_f = as_fixed(args[i]);
                    float result = acc_f + val_f;
                    if (result > 262144.0f || result < -262144.0f) {
                        return throw_fixed_overflow(ERR_FIXED_OVERFLOW_ADDITION);
                    }
                    acc_fixed = fixnum_to_fixed(acc_i) + extract_fixed_value(args[i]);
                    break;
                }
            }
        } else {
            int32_t val;
            switch (TAG(args[i])) {
                case CLJ_INT:
                    val = fixnum_to_fixed(AS_FIXNUM(args[i]));
                    break;
                default:
                    val = extract_fixed_value(args[i]);
                    break;
            }

            // Check for fixed-point addition overflow using original values
            float acc_f = (float)acc_fixed / 8192.0f;
            float val_f;
            switch (TAG(args[i])) {
                case CLJ_INT:
                    val_f = (float)AS_FIXNUM(args[i]);
                    break;
                default:
                    val_f = as_fixed(args[i]);
                    break;
            }
            float result = acc_f + val_f;
            if (result > 262144.0f || result < -262144.0f) { // Max fixed-point range
                return throw_fixed_overflow(ERR_FIXED_OVERFLOW_ADDITION);
            }

            acc_fixed += val;
        }
    }

    return sawFixed ? create_fixed_result(acc_fixed) : create_fixnum_result(acc_i);
}

ID native_mul_variadic(ID *args, unsigned int argc) {
    if (argc == 0) return create_fixnum_result(1);
    if (argc == 1) return RETAIN(args[0]);

    bool sawFixed = false;
    int acc_i = 1;
    int32_t acc_fixed = 0;

    for (unsigned int i = 0; i < argc; i++) {
        if (!sawFixed) {
            switch (TAG(args[i])) {
                case CLJ_INT: {
                    int new_val = AS_FIXNUM(args[i]);
                    // Check for integer overflow before multiplication
                    if (acc_i != 0 && new_val != 0) {
                        bool would_overflow = false;
                        if (new_val > 0) {
                            // Standard overflow check for positive multiplier
                            would_overflow = (acc_i > INT_MAX / new_val || acc_i < INT_MIN / new_val);
                        } else {
                            // Negative multiplier: check based on sign of accumulator
                            // Positive * negative = negative: check if result < INT_MIN
                            // Negative * negative = positive: check if result > INT_MAX
                            // Special case: new_val == -1
                            if (new_val == -1) {
                                // acc_i * -1 = -acc_i
                                // Overflow if: acc_i == INT_MIN (would make -acc_i overflow)
                                // Note: acc_i can't be > INT_MAX since it's an int
                                would_overflow = (acc_i == INT_MIN);
                            } else {
                                // For acc_i > 0 and new_val < 0: check if acc_i * new_val < INT_MIN
                                //   => acc_i > INT_MIN / new_val (since new_val is negative, division rounds toward 0)
                                // For acc_i < 0 and new_val < 0: check if acc_i * new_val > INT_MAX
                                //   => acc_i < INT_MAX / new_val (since both are negative, division rounds toward 0)
                                would_overflow = (acc_i > 0)
                                    ? (acc_i > INT_MIN / new_val)  // acc_i * new_val < INT_MIN if acc_i > INT_MIN / new_val
                                    : (acc_i < INT_MAX / new_val); // acc_i * new_val > INT_MAX if acc_i < INT_MAX / new_val
                            }
                        }
                        if (would_overflow) {
                            return throw_arithmetic_overflow(ERR_INTEGER_OVERFLOW_MULTIPLICATION, acc_i, new_val);
                        }
                        acc_i *= new_val;
                    } else {
                        acc_i *= new_val; // Safe: one operand is 0
                    }
                    break;
                }
                case CLJ_FLOAT: {
                    sawFixed = true;
                    // Check for fixed-point overflow before conversion
                    float acc_f = (float)acc_i;
                    float val_f = AS_FIXED(args[i]);
                    if (acc_f != 0.0f && val_f != 0.0f) {
                        // Check if multiplication would exceed fixed-point range
                        float result = acc_f * val_f;
                        if (result > 262144.0f || result < -262144.0f) { // Max fixed-point range
                            return throw_fixed_overflow(ERR_FIXED_OVERFLOW_MULTIPLICATION);
                        }
                    }
                    acc_fixed = (fixnum_to_fixed(acc_i) * extract_fixed_value(args[i])) >> 13;
                    break;
                }
                default: {
                    // Heap objects or other types - convert to fixed-point
                    sawFixed = true;
                    float acc_f = (float)acc_i;
                    float val_f = as_fixed(args[i]);
                    if (acc_f != 0.0f && val_f != 0.0f) {
                        float result = acc_f * val_f;
                        if (result > 262144.0f || result < -262144.0f) {
                            return throw_fixed_overflow(ERR_FIXED_OVERFLOW_MULTIPLICATION);
                        }
                    }
                    acc_fixed = (fixnum_to_fixed(acc_i) * extract_fixed_value(args[i])) >> 13;
                    break;
                }
            }
        } else {
            int32_t val;
            switch (TAG(args[i])) {
                case CLJ_INT:
                    val = fixnum_to_fixed(AS_FIXNUM(args[i]));
                    break;
                default:
                    val = extract_fixed_value(args[i]);
                    break;
            }

            // Check for fixed-point multiplication overflow
            float acc_f = (float)acc_fixed / 8192.0f;
            float val_f;
            switch (TAG(args[i])) {
                case CLJ_INT:
                    val_f = (float)AS_FIXNUM(args[i]);
                    break;
                default:
                    val_f = as_fixed(args[i]);
                    break;
            }
            if (acc_f != 0.0f && val_f != 0.0f) {
                float result = acc_f * val_f;
                if (result > 262144.0f || result < -262144.0f) { // Max fixed-point range
                    return throw_fixed_overflow(ERR_FIXED_OVERFLOW_MULTIPLICATION);
                }
            }

            acc_fixed = (acc_fixed * val) >> 13; // Fixed-Point Multiplikation mit Shift
        }
    }

    return sawFixed ? create_fixed_result(acc_fixed) : create_fixnum_result(acc_i);
}

ID native_sub_variadic(ID *args, unsigned int argc) {
    CHECK_ARITY_MIN(argc, 1, "sub");
    if (argc == 1) {
        if (!args[0]) {
            throw_exception_formatted(EXCEPTION_TYPE, __FILE__, __LINE__, 0, ERR_EXPECTED_NUMBER);
            return NULL;
        }
        uint16_t tag = TAG(args[0]);
        if (tag != CLJ_INT && tag != CLJ_FLOAT) {
            throw_exception_formatted(EXCEPTION_TYPE, __FILE__, __LINE__, 0, ERR_EXPECTED_NUMBER);
            return NULL;
        }
        switch (tag) {
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

    switch (TAG(args[0])) {
        case CLJ_INT:
            acc_i = AS_FIXNUM(args[0]);
            break;
        case CLJ_FLOAT:
        default:
            sawFixed = true;
            acc_fixed = extract_fixed_value(args[0]);
            break;
    }

        for (unsigned int i = 1; i < argc; i++) {
        if (!sawFixed) {
            switch (TAG(args[i])) {
                case CLJ_INT: {
                    int new_val = AS_FIXNUM(args[i]);
                    // Check for integer overflow/underflow before subtraction
                    if (acc_i > 0 && new_val < acc_i - INT_MAX) {
                        // Overflow detected - throw exception
                        return throw_arithmetic_overflow(ERR_INTEGER_OVERFLOW_SUBTRACTION, acc_i, new_val);
                    } else if (acc_i < 0 && new_val > acc_i - INT_MIN) {
                        // Underflow detected - throw exception
                        return throw_arithmetic_overflow(ERR_INTEGER_UNDERFLOW_SUBTRACTION, acc_i, new_val);
                    } else {
                        acc_i -= new_val;
                    }
                    break;
                }
                case CLJ_FLOAT:
                default: {
                    acc_fixed = fixnum_to_fixed(acc_i);
                    sawFixed = true;
                    int32_t val;
                    switch (TAG(args[i])) {
                        case CLJ_INT:
                            val = fixnum_to_fixed(AS_FIXNUM(args[i]));
                            break;
                        default:
                            val = extract_fixed_value(args[i]);
                            break;
                    }
                    acc_fixed -= val;
                    break;
                }
            }
        } else {
            int32_t val;
            switch (TAG(args[i])) {
                case CLJ_INT:
                    val = fixnum_to_fixed(AS_FIXNUM(args[i]));
                    break;
                default:
                    val = extract_fixed_value(args[i]);
                    break;
            }
            acc_fixed -= val;
        }
    }

    return sawFixed ? create_fixed_result(acc_fixed) : create_fixnum_result(acc_i);
}

ID native_mod(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 2, "mod")) return NULL;

    uint16_t tag_a = TAG(args[0]);
    uint16_t tag_b = TAG(args[1]);
    if (tag_a == CLJ_INT && tag_b == CLJ_INT) {
        int a = AS_FIXNUM(args[0]);
        int b = AS_FIXNUM(args[1]);
        if (b == 0) {
            throw_exception_formatted(EXCEPTION_DIVISION_BY_ZERO, __FILE__, __LINE__, 0,
                "Division by zero: %d %% %d", a, b);
            return NULL;
        }
        return create_fixnum_result(a % b);
    }

    // For fixed-point or mixed types, convert to fixed and compute
    int32_t a_fixed;
    switch (TAG(args[0])) {
        case CLJ_INT:
            a_fixed = fixnum_to_fixed(AS_FIXNUM(args[0]));
            break;
        default:
            a_fixed = extract_fixed_value(args[0]);
            break;
    }
    int32_t b_fixed;
    switch (TAG(args[1])) {
        case CLJ_INT:
            b_fixed = fixnum_to_fixed(AS_FIXNUM(args[1]));
            break;
        default:
            b_fixed = extract_fixed_value(args[1]);
            break;
    }

    if (b_fixed == 0) {
        throw_exception_formatted(EXCEPTION_DIVISION_BY_ZERO, __FILE__, __LINE__, 0,
            "Division by zero in mod");
        return NULL;
    }

    // For fixed-point, we need to compute modulo at the fixed-point scale
    // This is a simplified version - for full precision, we'd need to handle the fixed-point arithmetic
    int a_int = a_fixed >> 13;
    int b_int = b_fixed >> 13;
    if (b_int == 0) {
        throw_exception_formatted(EXCEPTION_DIVISION_BY_ZERO, __FILE__, __LINE__, 0,
            "Division by zero: %d %% %d", a_int, b_int);
        return NULL;
    }
    return create_fixnum_result(a_int % b_int);
}

ID native_quot(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 2, "quot")) return NULL;

    uint16_t tag_a = TAG(args[0]);
    uint16_t tag_b = TAG(args[1]);
    if (tag_a == CLJ_INT && tag_b == CLJ_INT) {
        int a = AS_FIXNUM(args[0]);
        int b = AS_FIXNUM(args[1]);
        if (b == 0) {
            throw_exception_formatted(EXCEPTION_DIVISION_BY_ZERO, __FILE__, __LINE__, 0,
                "Division by zero: %d / %d", a, b);
            return NULL;
        }
        // Clojure quot truncates toward zero (C integer division already does this)
        return create_fixnum_result(a / b);
    }

    // For fixed-point or mixed types, convert to fixed and compute
    int32_t a_fixed;
    switch (TAG(args[0])) {
        case CLJ_INT:
            a_fixed = fixnum_to_fixed(AS_FIXNUM(args[0]));
            break;
        default:
            a_fixed = extract_fixed_value(args[0]);
            break;
    }
    int32_t b_fixed;
    switch (TAG(args[1])) {
        case CLJ_INT:
            b_fixed = fixnum_to_fixed(AS_FIXNUM(args[1]));
            break;
        default:
            b_fixed = extract_fixed_value(args[1]);
            break;
    }

    if (b_fixed == 0) {
        throw_exception_formatted(EXCEPTION_DIVISION_BY_ZERO, __FILE__, __LINE__, 0,
            "Division by zero in quot");
        return NULL;
    }

    // For fixed-point, compute quotient at the fixed-point scale
    int a_int = a_fixed >> 13;
    int b_int = b_fixed >> 13;
    if (b_int == 0) {
        throw_exception_formatted(EXCEPTION_DIVISION_BY_ZERO, __FILE__, __LINE__, 0,
            "Division by zero: %d / %d", a_int, b_int);
        return NULL;
    }
    return create_fixnum_result(a_int / b_int);
}

ID native_bit_shift_left(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 2, "bit-shift-left")) return NULL;

    // Both arguments must be integers
    if (TAG(args[0]) != CLJ_INT || TAG(args[1]) != CLJ_INT) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "bit-shift-left requires integer arguments",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    int a = AS_FIXNUM(args[0]);
    int b = AS_FIXNUM(args[1]);

    // Clojure bit-shift-left: shift left by b bits
    // Note: C left shift is undefined for negative shift amounts or shift >= width
    if (b < 0 || b >= 32) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "bit-shift-left shift amount must be 0-31",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    return create_fixnum_result(a << b);
}

ID native_range(ID *args, unsigned int argc) {
    CHECK_ARITY_RANGE(argc, 1, 3, "range");

    int start = 0, end = 0, step = 1;

    if (argc == 1) {
        // (range end) => [0 1 2 ... end-1]
        end = AS_FIXNUM(args[0]);
        start = 0;
        step = 1;
    } else if (argc == 2) {
        // (range start end) => [start start+1 ... end-1]
        start = AS_FIXNUM(args[0]);
        end = AS_FIXNUM(args[1]);
        step = 1;
    } else {
        // (range start end step) => [start start+step ... end-step]
        start = AS_FIXNUM(args[0]);
        end = AS_FIXNUM(args[1]);
        step = AS_FIXNUM(args[2]);
        if (step == 0) {
            throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "range step cannot be zero",
                           __FILE__, __LINE__, 0);
            return NULL;
        }
    }

    // Calculate size
    int size = 0;
    if (step > 0) {
        if (start >= end) size = 0;
        else size = (end - start + step - 1) / step;
    } else {
        if (start <= end) size = 0;
        else size = (start - end - step - 1) / (-step);
    }

    if (size < 0) size = 0;

    // Return empty vector singleton if size is 0
    if (size == 0) {
        return empty_vector();
    }

    // Create vector with calculated capacity
    ID vec = make_vector(size, CLJ_VECTOR);
    CljVector *v = as_vector(vec);
    if (!v) return NULL;

    // Fill vector
    for (int i = start; (step > 0) ? (i < end) : (i > end); i += step) {
        ID val = create_fixnum_result(i);
        v = vector_conj(v, val);
    }

    return AUTORELEASE(vec);
}

ID native_repeat(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 2, "repeat")) return NULL;

    // First argument must be integer (count)
    if (TAG(args[0]) != CLJ_INT) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "repeat count must be an integer",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    int count = AS_FIXNUM(args[0]);
    if (count < 0) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "repeat count cannot be negative",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    ID value = args[1]; // Second argument is the value to repeat

    // Return empty vector singleton if count is 0
    if (count == 0) {
        return empty_vector();
    }

    // Create vector with exact capacity
    ID vec = make_vector(count, CLJ_VECTOR);
    CljVector *v = as_vector(vec);
    if (!v) return NULL;

    // Fill vector with repeated value
    for (int i = 0; i < count; i++) {
        ID val = value ? RETAIN(value) : NULL;
        v = vector_conj(v, val);
        RELEASE(val); // vector_conj retains, so release our copy - RELEASE handles NULL
    }

    return AUTORELEASE(vec);
}

ID native_math_sqrt(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "Math/sqrt")) return NULL;

    // Extract numeric value
    float val;
    switch (TAG(args[0])) {
        case CLJ_INT:
            val = (float)AS_FIXNUM(args[0]);
            break;
        case CLJ_FLOAT:
            val = as_fixed((CljValue)args[0]);
            break;
        default:
            val = extract_fixed_value(args[0]);
            break;
    }

    if (val < 0) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "Math/sqrt argument cannot be negative",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    double sqrt_result = sqrt((double)val);
    return create_fixed_result((int32_t)round(sqrt_result * (1 << 13)));
}

ID native_format(ID *args, unsigned int argc) {
    CHECK_ARITY_MIN(argc, 1, "format");

    // First argument must be a string (format string)
    if (TAG(args[0]) != CLJ_STRING) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "format first argument must be a string",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    CljString *fmt_str = (CljString*)args[0];
    if (!fmt_str || TAG(args[0]) != CLJ_STRING) return NULL;

    // Allocate buffer for formatted string (start with reasonable size)
    size_t buf_size = 256;
    char *buffer = malloc(buf_size);
    if (!buffer) {
        throw_exception(EXCEPTION_RUNTIME, "format: failed to allocate buffer",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    // Format arguments based on format string
    if (argc == 1) {
        // No arguments, just copy format string
        snprintf(buffer, buf_size, "%s", fmt_str->data);
    } else {
        // We need to handle variadic arguments
        // For simplicity, support common format specifiers: %d, %f, %s
        // This is a simplified version - full implementation would need to parse format string
        const char *fmt = fmt_str->data;
        const char *p = fmt;
        char *out = buffer;
        size_t remaining = buf_size - 1;
        int arg_idx = 1;

        while (*p && arg_idx < (int)argc && remaining > 0) {
            if (*p == '%' && *(p + 1) != '\0') {
                p++; // Skip '%'
                char spec = *p++;

                switch (spec) {
                    case 'd': {
                        // Integer
                        int val = AS_FIXNUM(args[arg_idx]);
                        int n = snprintf(out, remaining, "%d", val);
                        if (n < 0 || n >= (int)remaining) {
                            // Buffer too small, reallocate
                            size_t used = out - buffer;
                            buf_size *= 2;
                            buffer = realloc(buffer, buf_size);
                            if (!buffer) {
                                throw_exception(EXCEPTION_RUNTIME, "format: failed to reallocate buffer",
                                               __FILE__, __LINE__, 0);
                                return NULL;
                            }
                            out = buffer + used;
                            remaining = buf_size - used - 1;
                            n = snprintf(out, remaining, "%d", val);
                        }
                        out += n;
                        remaining -= n;
                        arg_idx++;
                        break;
                    }
                    case 'f': {
                        // Float
                        float val = (TAG(args[arg_idx]) == CLJ_INT) ?
                                   (float)AS_FIXNUM(args[arg_idx]) :
                                   as_fixed((CljValue)args[arg_idx]);
                        int n = snprintf(out, remaining, "%f", val);
                        if (n < 0 || n >= (int)remaining) {
                            size_t used = out - buffer;
                            buf_size *= 2;
                            buffer = realloc(buffer, buf_size);
                            if (!buffer) {
                                throw_exception(EXCEPTION_RUNTIME, "format: failed to reallocate buffer",
                                               __FILE__, __LINE__, 0);
                                return NULL;
                            }
                            out = buffer + used;
                            remaining = buf_size - used - 1;
                            n = snprintf(out, remaining, "%f", val);
                        }
                        out += n;
                        remaining -= n;
                        arg_idx++;
                        break;
                    }
                    case 's': {
                        // String
                        CljString *str = (TAG(args[arg_idx]) == CLJ_STRING) ? (CljString*)args[arg_idx] : NULL;
                        if (!str) {
                            // Try to convert to string
                            CljString *str_repr = print_str(args[arg_idx]);
                            if (str_repr) {
                                int n = snprintf(out, remaining, "%s", string_data(str_repr));
                                if (n < 0 || n >= (int)remaining) {
                                    size_t used = out - buffer;
                                    buf_size *= 2;
                                    buffer = realloc(buffer, buf_size);
                                    if (!buffer) {
                                        throw_exception(EXCEPTION_RUNTIME, "format: failed to reallocate buffer",
                                                       __FILE__, __LINE__, 0);
                                        return NULL;
                                    }
                                    out = buffer + used;
                                    remaining = buf_size - used - 1;
                                    n = snprintf(out, remaining, "%s", string_data(str_repr));
                                }
                                out += n;
                                remaining -= n;
                            }
                        } else {
                            int n = snprintf(out, remaining, "%s", str->data);
                            if (n < 0 || n >= (int)remaining) {
                                size_t used = out - buffer;
                                buf_size *= 2;
                                buffer = realloc(buffer, buf_size);
                                if (!buffer) {
                                    throw_exception(EXCEPTION_RUNTIME, "format: failed to reallocate buffer",
                                                   __FILE__, __LINE__, 0);
                                    return NULL;
                                }
                                out = buffer + used;
                                remaining = buf_size - used - 1;
                                n = snprintf(out, remaining, "%s", str->data);
                            }
                            out += n;
                            remaining -= n;
                        }
                        arg_idx++;
                        break;
                    }
                    case '%': {
                        // Literal %
                        *out++ = '%';
                        remaining--;
                        break;
                    }
                    default: {
                        // Unknown specifier, copy as-is
                        *out++ = '%';
                        *out++ = spec;
                        remaining -= 2;
                        break;
                    }
                }
            } else {
                *out++ = *p++;
                remaining--;
            }
        }
        *out = '\0';
    }

    // Create string object from buffer
    CljString *result = make_string(buffer);
    free(buffer);

    if (!result) {
        throw_exception(EXCEPTION_RUNTIME, "format: failed to create string",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    return AUTORELEASE(result);
}

// Set current EvalState (called by eval_function_call before calling builtins)
void builtin_set_eval_state(EvalState *st) {
    g_current_eval_state = st;
}

ID native_eval(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "eval")) return NULL;

    if (!g_current_eval_state) {
        throw_exception(EXCEPTION_RUNTIME, "eval: EvalState not available",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    // Evaluate the argument (which should be a quoted form)
    // In Clojure, (eval 'form) means the form is already quoted
    // So we just evaluate it directly
    ID form = args[0];

    // Use eval_parsed to evaluate the form
    return eval_parsed(form, g_current_eval_state, NULL);
}

ID native_read_string(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "read-string")) return NULL;

    if (!g_current_eval_state) {
        throw_exception(EXCEPTION_RUNTIME, "read-string: EvalState not available",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    // First argument must be a string
    if (TAG(args[0]) != CLJ_STRING) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "read-string argument must be a string",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    CljString *str = (CljString*)args[0];
    if (!str) return NULL;

    // Parse the string using parse from parser.c
    ID parsed = parse(str->data, g_current_eval_state);

    // parse returns AUTORELEASE objects
    return parsed;
}

ID native_div_variadic(ID *args, unsigned int argc) {
    CHECK_ARITY_MIN(argc, 1, "div");
    if (argc == 1) {
        if (!args[0]) {
            throw_exception_formatted(EXCEPTION_TYPE, __FILE__, __LINE__, 0, ERR_EXPECTED_NUMBER);
            return NULL;
        }
        uint16_t tag = TAG(args[0]);
        if (tag != CLJ_INT && tag != CLJ_FLOAT) {
            throw_exception_formatted(EXCEPTION_TYPE, __FILE__, __LINE__, 0, ERR_EXPECTED_NUMBER);
            return NULL;
        }
        switch (tag) {
            case CLJ_INT: {
                int x = AS_FIXNUM(args[0]);
                if (x == 0) {
                    // Division by zero - throw exception
                    throw_exception_formatted(EXCEPTION_DIVISION_BY_ZERO, __FILE__, __LINE__, 0,
                        "Division by zero: 1 / %d", x);
                    return NULL;
                }
                if (1 % x == 0) return create_fixnum_result(1/x);
                return create_fixed_result(fixnum_to_fixed(1) / x);
            }
            case CLJ_FLOAT:
            default: {
                int32_t x = extract_fixed_value(args[0]);
                if (x == 0) {
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

    switch (TAG(args[0])) {
        case CLJ_INT:
            acc_i = AS_FIXNUM(args[0]);
            break;
        case CLJ_FLOAT:
        default:
            sawFixed = true;
            acc_fixed = extract_fixed_value(args[0]);
            break;
    }

        for (unsigned int i = 1; i < argc; i++) {
        if (!sawFixed) {
            switch (TAG(args[i])) {
                case CLJ_INT: {
                    int d = AS_FIXNUM(args[i]);
                    if (d == 0) {
                        // Division by zero - throw exception
                        throw_exception_formatted(EXCEPTION_DIVISION_BY_ZERO, __FILE__, __LINE__, 0,
                            "Division by zero: %d / %d", acc_i, d);
                        return NULL;
                    }
                    if (acc_i % d == 0) {
                        acc_i /= d;
                    } else {
                        sawFixed = true;
                        acc_fixed = fixnum_to_fixed(acc_i) / d; // Fixnum zu Fixed promoten
                    }
                    break;
                }
                case CLJ_FLOAT:
                default: {
                    if (!sawFixed) {
                        acc_fixed = fixnum_to_fixed(acc_i);
                        sawFixed = true;
                    }
                    int32_t d;
                    switch (TAG(args[i])) {
                        case CLJ_INT:
                            d = fixnum_to_fixed(AS_FIXNUM(args[i]));
                            break;
                        default:
                            d = extract_fixed_value(args[i]);
                            break;
                    }
                    if (d == 0) {
                        // Division by zero - throw exception
                        throw_exception_formatted(EXCEPTION_DIVISION_BY_ZERO, __FILE__, __LINE__, 0,
                            "Division by zero: %d / %d", acc_fixed >> 13, d >> 13);
                        return NULL;
                    } else {
                        acc_fixed = (acc_fixed << 13) / d; // Fixed-Point Division mit Shift
                    }
                    break;
                }
            }
        } else {
            int32_t d;
            switch (TAG(args[i])) {
                case CLJ_INT:
                    d = fixnum_to_fixed(AS_FIXNUM(args[i]));
                    break;
                default:
                    d = extract_fixed_value(args[i]);
                    break;
            }
            if (d == 0) {
                // Division by zero - throw exception
                throw_exception_formatted(EXCEPTION_DIVISION_BY_ZERO, __FILE__, __LINE__, 0,
                    "Division by zero: %d / %d", acc_fixed >> 13, d >> 13);
                return NULL;
            } else {
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

ID native_byte_array(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "byte-array")) return NULL;

    // If argument is a fixnum, create array with that size
    switch (TAG(args[0])) {
        case CLJ_INT: {
            int size = AS_FIXNUM(args[0]);
            if (size < 0) {
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
    if (!seq) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "byte-array argument must be a number or sequence",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    // For now, only support vectors as sequences
    if (seq && TAG(seq) == CLJ_VECTOR) {
        CljVector *vec = as_vector(seq);
        int count = vector_count(vec);
        CljValue arr = (CljValue)make_byte_array(count);

        for (int i = 0; i < count; i++) {
            ID elem = vector_nth(vec, i);
            if (!elem || TAG(elem) != CLJ_INT) {
                RELEASE(arr);
                RELEASE(elem);
                throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "byte-array sequence elements must be numbers",
                               __FILE__, __LINE__, 0);
                return NULL;
            }
            int val = AS_FIXNUM(elem);
            // elem lifetime is tied to vector - no release needed
            if (val < 0 || val > 255) {
                RELEASE(arr);
                throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                        "byte values must be 0-255, got %d", val);
                return NULL;
            }
            byte_array_set(arr, i, (uint8_t)val);
        }

        return arr;
    }

    throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "byte-array currently only supports vectors as sequences",
                   __FILE__, __LINE__, 0);
    return NULL;
}

ID native_aget(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 2, "aget")) return NULL;

    ID arr = args[0];
    if (!arr || TAG(arr) != CLJ_BYTE_ARRAY) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "aget first argument must be a byte-array",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    if (TAG(args[1]) != CLJ_INT) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "aget index must be a number",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    int index = AS_FIXNUM(args[1]);
    uint8_t value = byte_array_get((CljValue)arr, index);
    return fixnum(value);
}

ID native_aset(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 3, "aset")) return NULL;

    ID arr = args[0];
    if (!arr || TAG(arr) != CLJ_BYTE_ARRAY) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "aset first argument must be a byte-array",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    if (TAG(args[1]) != CLJ_INT) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "aset index must be a number",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    if (TAG(args[2]) != CLJ_INT) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "aset value must be a number",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    int index = AS_FIXNUM(args[1]);
    int value = AS_FIXNUM(args[2]);

    if (value < 0 || value > 255) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                "byte value must be 0-255, got %d", value);
        return NULL;
    }

    byte_array_set((CljValue)arr, index, (uint8_t)value);
    return args[2]; // Return the value (Clojure-compatible)
}

ID native_alength(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "alength")) return NULL;

    ID arr = args[0];
    if (!arr || TAG(arr) != CLJ_BYTE_ARRAY) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "alength argument must be a byte-array",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    int length = byte_array_length((CljValue)arr);
    return fixnum(length);
}

ID native_aclone(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "aclone")) return NULL;

    ID arr = args[0];
    if (!arr || TAG(arr) != CLJ_BYTE_ARRAY) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "aclone argument must be a byte-array",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    return byte_array_clone((CljValue)arr);
}

// Comparison operators as native functions
ID native_lt(ID *args, unsigned int argc) {
    (void)argc; // Suppress unused parameter warning
    CompareResult result;
    if (!compare_numeric_values(args[0], args[1], &result)) {
        throw_exception("TypeError", "Expected number for < comparison",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    return (result == COMPARE_LESS) ? clj_true : clj_false;
}

ID native_gt(ID *args, unsigned int argc) {
    (void)argc; // Suppress unused parameter warning
    CompareResult result;
    if (!compare_numeric_values(args[0], args[1], &result)) {
        throw_exception("TypeError", "Expected number for > comparison",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    return (result == COMPARE_GREATER) ? clj_true : clj_false;
}

ID native_le(ID *args, unsigned int argc) {
    (void)argc; // Suppress unused parameter warning
    CompareResult result;
    if (!compare_numeric_values(args[0], args[1], &result)) {
        throw_exception("TypeError", "Expected number for <= comparison",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    return (result == COMPARE_LESS || result == COMPARE_EQUAL) ?
           clj_true : clj_false;
}

ID native_ge(ID *args, unsigned int argc) {
    (void)argc; // Suppress unused parameter warning
    CompareResult result;
    if (!compare_numeric_values(args[0], args[1], &result)) {
        throw_exception("TypeError", "Expected number for >= comparison",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    return (result == COMPARE_GREATER || result == COMPARE_EQUAL) ?
           clj_true : clj_false;
}

ID native_eq(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 2, "=")) return NULL;

    ID a = args[0];
    ID b = args[1];

    if (!a || !b) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "= arguments cannot be null",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    // Try numeric comparison first
    float val_a, val_b;
    switch (TAG(a)) {
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

    switch (TAG(b)) {
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

ID native_not_eq(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 2, "not=")) return NULL;

    ID a = args[0];
    ID b = args[1];

    if (!a || !b) {
        // Both nil: equal, so not= returns false
        if (!a && !b) return clj_false;
        // One nil, one not: not equal, so not= returns true
        return clj_true;
    }

    // Try numeric comparison first
    float val_a, val_b;
    bool a_numeric = false, b_numeric = false;
    switch (TAG(a)) {
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

    switch (TAG(b)) {
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
    if (a_numeric && b_numeric) {
        return val_a != val_b ? clj_true : clj_false;
    }

    // Otherwise use general equality, then invert
    bool equal = clj_equal(a, b);
    return equal ? clj_false : clj_true;
}

ID native_identical(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 2, "identical?")) return clj_false;
    return (args[0] == args[1]) ? clj_true : clj_false;
}

ID native_vector_p(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "vector?")) return clj_false;
    return (args[0] && TAG(args[0]) == CLJ_VECTOR) ? clj_true : clj_false;
}

ID native_map_p(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "map?")) return clj_false;
    return (args[0] && TAG(args[0]) == CLJ_MAP) ? clj_true : clj_false;
}

// ============================================================================
// Type Predicates (new)
// ============================================================================

ID native_number_p(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "number?");
    return (is_fixnum(args[0]) || is_fixed(args[0])) ? clj_true : clj_false;
}

ID native_integer_p(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "integer?");
    return is_fixnum(args[0]) ? clj_true : clj_false;
}

ID native_float_p(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "float?");
    return is_fixed(args[0]) ? clj_true : clj_false;
}

ID native_string_p(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "string?");
    return (args[0] && TAG(args[0]) == CLJ_STRING) ? clj_true : clj_false;
}

ID native_keyword_p(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "keyword?");
    return IS_KEYWORD(args[0]) ? clj_true : clj_false;
}

ID native_symbol_p(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "symbol?");
    return (args[0] && TAG(args[0]) == CLJ_SYMBOL && !IS_KEYWORD(args[0])) ? clj_true : clj_false;
}

ID native_fn_p(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "fn?");
    return (args[0] && (TAG(args[0]) == CLJ_FUNC || TAG(args[0]) == CLJ_CLOSURE)) ? clj_true : clj_false;
}

ID native_char_p(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "char?");
    return is_character(args[0]) ? clj_true : clj_false;
}

ID native_list_p(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "list?");
    // Only CLJ_LIST is a true list (ASTNodes are internal compiler artifacts)
    return (args[0] && TAG(args[0]) == CLJ_LIST) ? clj_true : clj_false;
}

// native_time removed: time is now only a special form (eval_time)
// This ensures time can measure actual evaluation time, not pre-evaluated arguments

// Native sleep implementation
ID native_sleep(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "sleep")) return NULL;

    // Get the sleep duration in seconds
    CljObject *duration_obj = args[0];
    if (TAG(duration_obj) != CLJ_INT) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "sleep duration must be a number",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    // Convert to seconds (assuming the number is in seconds)
    int duration = as_fixnum((CljValue)duration_obj);

    // Use Unix sleep function
    sleep(duration);

    // Return nil
    return NULL;
}

// ============================================================================
// ATOM FUNCTIONS
// ============================================================================

// Native atom implementation
ID native_atom(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "atom")) return NULL;

    ID value = args[0];  // Can be NULL (nil) or immediate
    CljAtom *atom = make_atom(value);

    return (CljObject*)atom;
}

// Native deref implementation
ID native_deref(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "deref")) return NULL;

    ID obj = args[0];
    if (!obj || TAG(obj) != CLJ_ATOM) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "deref requires an atom",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    CljAtom *atom = as_atom(obj);
    ID value = atom_deref(atom);

    return value;  // Can be NULL (nil) or immediate
}

// Native reset! implementation
ID native_reset_bang(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 2, "reset!")) return NULL;

    ID obj = args[0];
    if (!obj || TAG(obj) != CLJ_ATOM) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "reset! requires an atom",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    CljAtom *atom = as_atom(obj);
    ID new_value = args[1];  // Can be NULL (nil) or immediate

    ID result = atom_reset(atom, new_value);

    return result;  // Returns new value (can be NULL/nil or immediate)
}

// Native swap! implementation
ID native_swap_bang(ID *args, unsigned int argc) {
    if (argc < 2) {
        throw_exception(EXCEPTION_ARITY, "swap! requires at least 2 arguments (atom and function)",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    CljObject *obj = args[0];
    if (!obj || TAG(obj) != CLJ_ATOM) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "swap! requires an atom",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    CljAtom *atom = as_atom(obj);
    ID fn = args[1];

    if (!fn) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "swap! requires a function",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    // Prepare additional arguments (if any)
    ID *fn_args = NULL;
    unsigned int fn_argc = 0;

    if (argc > 2) {
        fn_argc = argc - 2;
        // Use malloc instead of calloc - array is immediately filled
        fn_args = (ID*)malloc(fn_argc * sizeof(ID));
        if (!fn_args) {
            throw_oom();
            return NULL;
        }

        for (unsigned int i = 0; i < fn_argc; i++) {
            fn_args[i] = args[i + 2];
        }
    }

    ID result = atom_swap(atom, fn, fn_args, fn_argc);

    if (fn_args) {
        free(fn_args);
    }

    return result;  // Returns new value (can be NULL/nil or immediate)
}

// Note: def and ns are now special forms (not builtins) because they require non-evaluated arguments
// They are handled directly in eval_list() via eval_def() and eval_ns()

// do: Evaluate expressions sequentially, return last value
// Note: As a builtin, arguments are already evaluated, so we just return the last one
ID native_do(ID *args, unsigned int argc) {
    if (argc == 0) {
        // Empty do: (do) returns nil
        return NULL;
    }

    // Arguments are already evaluated by eval_arg, so we just return the last one
    // Note: We need to RETAIN the last argument since it will be released by the caller
    ID last = args[argc - 1];
    // CRITICAL: If last is NULL (nil), return NULL directly without RETAIN/AUTORELEASE
    if (!last) {
        return NULL;
    }
    return AUTORELEASE(RETAIN(last));
}

// dotimes: Execute expression n times with variable bound to 0, 1, ..., n-1
// dotimes is now implemented as a special form, not a builtin

// Helper function to register a builtin in clojure.core namespace (DRY principle)
// Also supports qualified symbols like "Math/sqrt" for other namespaces
static void register_builtin_in_core(const char *cname, BuiltinFn func) {
    // Check if name is a qualified symbol (e.g., "Math/sqrt")
    const char *slash = strchr(cname, '/');
    CljNamespace *target_ns;
    const char *symbol_name;

    if (slash && slash > cname && slash[1] != '\0') {
        // Qualified symbol: split into namespace and name
        size_t ns_len = slash - cname;
        char *ns_name = (char*)malloc(ns_len + 1);
        if (!ns_name) {
            return;
        }
        strncpy(ns_name, cname, ns_len);
        ns_name[ns_len] = '\0';

        symbol_name = slash + 1;
        target_ns = ns_get_or_create(ns_name, NULL);
        free(ns_name);
    } else {
        // Unqualified symbol: register in clojure.core
        target_ns = ns_get_or_create("clojure.core", NULL);
        symbol_name = cname;
    }

    if (!target_ns) {
        return;
    }

    // Namespace is already registered in ns_registry via ns_register
    // No need for special cache handling

    // Register the builtin in target namespace
    CljObject *symbol = (CljObject*)intern_symbol_global(symbol_name);
    CljObject *func_obj = make_named_func(func, NULL, cname);
    if (symbol && func_obj) {
        ns_define(target_ns, symbol, func_obj);

        // Add metadata to native function (Clojure-compatible: :name and :ns)
#ifdef ENABLE_META
        // Ensure special symbols are initialized
        init_special_symbols();

        // Create metadata map with :name and :ns
        CljMap *meta_map = make_map(4);
        if (meta_map) {
            // Add :name (function name as string)
            if (SYM_KW_NAME && symbol_name && symbol_name[0] != '\0') {
                CljString *name_str = make_string(symbol_name);
                if (name_str) {
                    CljMap *updated = map_assoc(meta_map, SYM_KW_NAME, name_str);
                    if (updated != meta_map) {
                        RELEASE(meta_map);
                        meta_map = updated;
                    }
                    RELEASE(name_str);
                }
            }

            // Add :ns (namespace name as symbol)
            if (SYM_KW_NS && target_ns && target_ns->name) {
                CljMap *updated = map_assoc(meta_map, SYM_KW_NS, target_ns->name);
                if (updated != meta_map) {
                    RELEASE(meta_map);
                    meta_map = updated;
                }
            }

            // Set metadata on function object
            meta_set(func_obj, meta_map);
            RELEASE(meta_map);
        }
#endif // ENABLE_META

        // Builtin registered successfully
    } else {
        // Failed to register builtin
    }
}

void register_builtins() {
    // NOTE: Most functions are now registered via :native stubs in clojure.core.clj
    // This allows metadata (docstrings) to be properly attached.
    // Only functions needed in --no-core mode are registered here directly.
    
    // Functions needed before clojure.core.clj is loaded (--no-core mode)
    register_builtin_in_core("eval", native_eval);
    register_builtin_in_core("read-string", native_read_string);
#ifndef ESP32_BUILD
    register_builtin_in_core("require", native_require);
    register_builtin_in_core("load-file", native_load_file);
#endif

    // NOTE: clojure.string functions are NOT registered here as builtins.
    // They are defined in libs/clojure/string.clj and loaded via require.
    // This allows metadata (docstrings) to be properly attached.
    
    // NOTE: clojure.repl/source is registered here because it's in a different namespace
    // and needs to be available before clojure.repl.clj is loaded.
    register_builtin_in_core("clojure.repl/source", native_source);
    register_builtin_in_core("clojure.repl/dir", native_repl_dir);
    register_builtin_in_core("clojure.repl/rt", native_rt);
    
    // Meta functions
    register_builtin_in_core("meta", native_meta);
    register_builtin_in_core("with-meta", native_with_meta);
}
