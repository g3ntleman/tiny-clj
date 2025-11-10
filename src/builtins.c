#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <inttypes.h>
#include "object.h"
#include "vector.h"
#include "map.h"
#include "atom.h"
#include "kv_macros.h"
#include "numeric_utils.h"
#include "runtime.h"
#include "memory.h"
#include "namespace.h"
#include "value.h"
#include "error_messages.h"
#include "seq.h"
#include "byte_array.h"
#include "exception.h"
#include "list.h"
#include "symbol.h"
#include "function.h"
#include "function_call.h"
#include "clj_strings.h"
#include "event_loop.h"
#include "strings.h"
#include "reader.h"
#include "parser.h"

// Forward declaration for eval_body_with_env
extern ID eval_body_with_env(ID body, CljMap *env);

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

    // Validate index
    if (!idx || TAG(idx) != CLJ_INT) {
        throw_exception_formatted("IllegalArgumentException", __FILE__, __LINE__, 0,
                "nth requires an integer index");
        return NULL;
    }
    int i = AS_FIXNUM(idx);
    if (i < 0) {
        return not_found ? RETAIN(not_found) : NULL;
    }

    // Handle nil collection
    if (!coll) {
        return not_found ? RETAIN(not_found) : NULL;
    }

    // Fast path: Vectors (O(1) access)
    if (TAG(coll) == CLJ_VECTOR) {
        CljPersistentVector *v = as_vector(coll);
        if (!v || i >= v->count) {
            return not_found ? RETAIN(not_found) : NULL;
        }
        ID result = v->data[i];
        if (!result || result == SYM_NIL) {
            return NULL;
        }
        return RETAIN(result);
    }

    // Fast path: Lists (O(n) access via list_nth)
    if (TAG(coll) == CLJ_LIST) {
        CljList *list = as_list(coll);
        if (!list) {
            return not_found ? RETAIN(not_found) : NULL;
        }
        ID result = list_nth(list, i);
        if (!result) {
            return not_found ? RETAIN(not_found) : NULL;
        }
        if (result == SYM_NIL) {
            return NULL;
        }
        return RETAIN(result);
    }

    // Slow path: Sequences (O(n) access via iterator)
    if (!is_seqable(coll)) {
        throw_exception_formatted("IllegalArgumentException", __FILE__, __LINE__, 0,
                "nth not supported on this type");
        return NULL;
    }

    SeqIterator iter;
    if (!seq_iter_init(&iter, (CljObject*)coll)) {
        return not_found ? RETAIN(not_found) : NULL;
    }

    // Iterate to index i
    for (int j = 0; j < i; j++) {
        if (seq_iter_empty(&iter)) {
            return not_found ? RETAIN(not_found) : NULL;
        }
        seq_iter_next(&iter);
    }

    if (seq_iter_empty(&iter)) {
        return not_found ? RETAIN(not_found) : NULL;
    }

    ID result = seq_iter_first(&iter);
    if (!result || result == SYM_NIL) {
        return NULL;
    }

    return RETAIN(result);
}

// peek: returns last element of vector, or nil if empty
ID native_peek(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "peek")) return NULL;
    ID vec = args[0];
    if (!vec || TAG(vec) != CLJ_VECTOR) return NULL;
    CljPersistentVector *v = as_vector(vec);
    if (!v || v->count == 0) return NULL;  // nil for empty vector
    return RETAIN(v->data[v->count - 1]);  // Return last element
}

// pop: returns new vector without last element, or empty vector if empty
// Uses Copy-on-Write: RC=1 → in-place mutation (O(1)), RC>1 → COW (O(n))
ID native_pop(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "pop")) return NULL;
    ID vec = args[0];
    if (!vec || TAG(vec) != CLJ_VECTOR) return NULL;
    CljPersistentVector *v = as_vector(vec);
    if (!v || v->count == 0) {
        // Return empty vector singleton (no memory management needed)
        return make_vector(0, false);
    }
    
    // OPTIMIZATION: If RC=1, mutate in-place (O(1))
    // This is the hot path - most common case when vector is not shared
    if (v->base.rc == 1) {
        // Release last element
        if (v->data[v->count - 1]) {
            RELEASE(v->data[v->count - 1]);
        }
        v->count--;
        return AUTORELEASE(vec);  // Return same vector (in-place mutation)
    }
    
    // RC>1: Copy-on-Write (O(n))
    // Original vector is shared, so we must create a new copy
    CljValue new_vec = (CljValue)make_vector(v->count - 1, false);
    CljPersistentVector *new_v = as_vector((CljObject*)new_vec);
    if (!new_v) return NULL;
    
    // Copy all elements except the last one
    for (int i = 0; i < v->count - 1; i++) {
        if (v->data[i]) {
            new_v->data[i] = RETAIN(v->data[i]);
            new_v->count++;
        }
    }
    
    return AUTORELEASE(new_vec);
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
        throw_exception_formatted("IllegalArgumentException", __FILE__, __LINE__, 0,
                "subvec requires a vector as first argument");
        return NULL;
    }
    
    if (!start_idx || TAG(start_idx) != CLJ_INT) {
        throw_exception_formatted("IllegalArgumentException", __FILE__, __LINE__, 0,
                "subvec requires a number as start index");
        return NULL;
    }
    
    CljPersistentVector *v = as_vector(vec);
    if (!v) return NULL;
    
    int start = AS_FIXNUM(start_idx);
    int end;
    
    // Determine end index: if not provided, use vector count
    if (end_idx) {
        if (TAG(end_idx) != CLJ_INT) {
            throw_exception_formatted("IllegalArgumentException", __FILE__, __LINE__, 0,
                    "subvec requires a number as end index");
            return NULL;
        }
        end = AS_FIXNUM(end_idx);
    } else {
        end = v->count;
    }
    
    // Bounds validation
    if (start < 0) {
        throw_exception_formatted("IndexOutOfBoundsException", __FILE__, __LINE__, 0,
                "subvec start index %d is negative", start);
        return NULL;
    }
    
    if (end > v->count) {
        throw_exception_formatted("IndexOutOfBoundsException", __FILE__, __LINE__, 0,
                "subvec end index %d is greater than vector count %d", end, v->count);
        return NULL;
    }
    
    if (start > end) {
        throw_exception_formatted("IndexOutOfBoundsException", __FILE__, __LINE__, 0,
                "subvec start index %d is greater than end index %d", start, end);
        return NULL;
    }
    
    // Calculate sub-vector size
    int subvec_count = end - start;
    
    // Special case: empty sub-vector (start == end)
    if (subvec_count == 0) {
        return make_vector(0, false);  // Returns empty-vector singleton (no memory management needed)
    }
    
    // Create new vector with exact capacity needed
    CljValue new_vec_obj = (CljValue)make_vector(subvec_count, false);
    CljPersistentVector *new_vec = as_vector((CljObject*)new_vec_obj);
    if (!new_vec) return NULL;
    
    // Copy elements from start to end with RETAIN
    for (int i = 0; i < subvec_count; i++) {
        if (v->data[start + i]) {
            new_vec->data[i] = RETAIN(v->data[start + i]);
        } else {
            new_vec->data[i] = NULL;  // nil elements
        }
        new_vec->count++;
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
    CljVector result = vector_conj((CljVector)vec, val);
    if (!result) return NULL;
    return (ID)RETAIN(result);
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
        return args[0];
    }
    
    // For 2+ args, conj all values to the collection
    CljObject *coll = args[0];
    if (!coll) {
        // conj nil with values creates a list
        CljObject *result = NULL;
        for (unsigned int i = argc - 1; i >= 1; i--) {
            CljObject *val = args[i];
            result = (CljObject*)make_list((ID)val, (CljList*)result);
        }
        return (result);
    }
    
    if (coll && TAG(coll) == CLJ_VECTOR) {
        CljObject *result = coll;
        for (unsigned int i = 1; i < argc; i++) {
            CljObject *val = args[i];
            result = conj2((ID)result, (ID)val);
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
            return seq_first((ID)coll);
        }
        
        default: {
            // Use seq implementation for other types (vectors, maps, strings)
            CljSeqIterator *seq = make_seq(coll);
            if (!seq) return NULL;
            
            CljObject *result = seq_first((CljObject*)seq);
            RELEASE((ID)seq);
            
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
    
    CljObject *coll = (CljObject*)coll_id;
    
    // EAT-YOUR-OWN-DOG-FOOD: Use seq_next for all seqable types
    // This consolidates the logic and eliminates duplication
    // For CLJ_LIST, seq_next handles it efficiently (returns CLJ_LIST directly)
    // For CLJ_VECTOR, CLJ_SEQ, and other seqable types, seq_next handles them via seq_rest
    
    // Check if collection is seqable before trying to create seq
    if (!is_seqable((ID)coll)) {
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
    bool seq_is_original = ((ID)seq == (ID)coll);
    
    // Return next of the created seq
    ID result = seq_next((ID)seq);
    
    // seq_next returns new objects (rc=1) or existing objects - need to AUTORELEASE new ones
    // For CLJ_LIST, seq_next returns existing objects directly (no memory management needed)
    // For other types, seq_next returns new CljSeqIterator objects (rc=1) - need AUTORELEASE
    // Note: seq_next never returns immediate values, only NULL or heap objects (CLJ_LIST or CLJ_SEQ)
    if (result) {
        CljObject *obj = (CljObject*)result;
        // If it's a CLJ_LIST, it's an existing object - no AUTORELEASE needed
        // If it's a CLJ_SEQ, it's a new object - need AUTORELEASE
        if (obj->type == CLJ_SEQ) {
            // CRITICAL: AUTORELEASE before releasing the original seq
            // This ensures the result is properly managed before we free the original seq
            // AUTORELEASE already has IS_IMMEDIATE check, so no need to check here
            result = AUTORELEASE(result);
        }
    }
    
    // Only release the seq if we created it (not if it was the original object)
    // If seq_is_original, the caller (eval_and_call_native) will release args[0]
    if (!seq_is_original) {
        RELEASE((ID)seq);
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
        return (ID)empty_list();
    }
    
    // Use make_list_from_stack to create list from arguments
    return AUTORELEASE(make_list_from_stack((CljValue*)args, argc));
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
    if (coll && TAG(coll) == CLJ_LIST) {
        // Safe cast - we already checked is_type
        CljList *list = (CljList*)coll;
        // Use list_count to check if list is empty (handles nil elements correctly)
        if (!list || list_count(list) == 0) {
            return empty_list();
        }
        
        // Build reversed list by traversing and consing
        CljList *result = NULL;
        CljObject *current = coll;
        
        while (current && TAG(current) == CLJ_LIST) {
            // Safe cast - we already checked is_type
            CljList *list = (CljList*)current;
            if (!list) break;
            
            CljObject *first = list->first;
            if (first) {
                CljList *new_result = make_list((ID)first, result);
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
            CljList *new_result = make_list((ID)first, result);
            RELEASE(result);
            result = new_result;
        }
        // Advance iterator to next position
        if (!seq_iter_next(&seq->iter)) {
            break; // End of sequence
        }
    }
    
    // Release seq object
    RELEASE((ID)seq);
    
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
        CljPersistentVector *v = as_vector(coll);
        if (!v || i < 0 || i >= v->count) return NULL;
        // Use COW-based vector_assoc (automatically handles RC=1 in-place, RC>1 COW)
        CljVector result = vector_assoc((CljVector)coll, i, val);
        if (!result) return NULL;
        return (ID)RETAIN(result);
    }
    
    // Handle maps
    if (coll && TAG(coll) == CLJ_MAP) {
        if (!key) return NULL;
        // Use COW-based map_assoc (automatically handles RC=1 in-place, RC>1 COW)
        // map_assoc always returns a map (either the same or a new one), never NULL
        CljMap *result = map_assoc((CljMap*)coll, (ID)key, (ID)val);
        return (ID)RETAIN((CljObject*)result);
    }
    
    // Unsupported collection type
    return NULL;
}

// Transient functions
ID native_transient(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "transient")) return NULL;
    
    CljObject *coll = args[0];
    if (!coll) return (NULL);
    
    if (coll && TAG(coll) == CLJ_VECTOR) {
        return ((CljObject*)transient((CljValue)coll));
    } else if (coll && TAG(coll) == CLJ_MAP) {
        return ((CljObject*)map_transient((CljMap*)coll));
    } else if ((coll && TAG(coll) == CLJ_TRANSIENT_VECTOR) || (coll && TAG(coll) == CLJ_TRANSIENT_MAP)) {
        // Clojure-compatible: transient on transient returns the same object
        return (coll);
    }
    
    // Throw exception for unsupported collection type (Clojure-compatible)
    throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, 
                    "transient requires a persistent collection at position 1", 
                    __FILE__, __LINE__, 0);
    return NULL;
}

ID native_persistent(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "persistent!")) return NULL;
    
    CljObject *coll = args[0];
    if (!coll) return (NULL);
    
    if (coll && TAG(coll) == CLJ_TRANSIENT_VECTOR) {
        return ((CljObject*)persistent((CljValue)coll));
    } else if (coll && TAG(coll) == CLJ_TRANSIENT_MAP) {
        return ((CljObject*)map_persistent((CljMap*)coll));
    } else if ((coll && TAG(coll) == CLJ_VECTOR) || (coll && TAG(coll) == CLJ_MAP)) {
        // Clojure-compatible: persistent! on persistent returns the same object
        return (coll);
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
    
    
    if (coll && TAG(coll) == CLJ_TRANSIENT_VECTOR) {
        CljValue result = (CljValue)coll;
        for (unsigned int i = 1; i < argc; i++) {
            result = clj_conj(result, (CljValue)args[i]);
            if (!result) return NULL;
        }
        return (CljObject*)result;
    } else if (coll && TAG(coll) == CLJ_TRANSIENT_MAP) {
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
    if (!validate_builtin_args(argc, 2, "get")) return NULL;
    CljObject *map = (CljObject*)args[0];
    CljObject *key = (CljObject*)args[1];
    if (!map || !key) return (NULL);
    
    if (map && (TAG(map) == CLJ_MAP || TAG(map) == CLJ_TRANSIENT_MAP)) {
        return map_get((CljMap*)map, (ID)key);
    }
    
    return NULL; // Return nil for unsupported types
}

ID native_count(ID *args, unsigned int argc) {
    CLJ_ASSERT(args != NULL);
    
    if (!validate_builtin_args(argc, 1, "count")) return NULL;
    CljObject *coll = args[0];
    // Clojure behavior: (count nil) throws IllegalArgumentException
    if (!coll) {
        throw_exception_formatted("IllegalArgumentException", __FILE__, __LINE__, 0,
                "Don't know how to count: nil");
        return NULL;
    }
    
    // Handle CLJ_SEQ (sequences from rest, etc.)
    if (coll && TAG(coll) == CLJ_SEQ) {
        return fixnum(seq_count((ID)coll));
    }
    
    if (coll && (TAG(coll) == CLJ_MAP || TAG(coll) == CLJ_TRANSIENT_MAP)) {
        return (fixnum(map_count((CljMap*)coll)));
    } else if (coll && (TAG(coll) == CLJ_VECTOR || TAG(coll) == CLJ_TRANSIENT_VECTOR)) {
        CljPersistentVector *vec = as_vector(coll);
        return (fixnum(vec ? vec->count : 0));
    } else if (coll && TAG(coll) == CLJ_LIST) {
        CljList *list = as_list(coll);
        return (fixnum(list_count(list)));
    } else if (coll && TAG(coll) == CLJ_STRING) {
        CljString *str = (CljString*)coll;
        
 
        // Return string length directly
        return fixnum(str->length);
    }
    
    return (fixnum(0)); // Default count for unsupported types
}

ID native_keys(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "keys")) return NULL;
    CljObject *map = (CljObject*)args[0];
    if (!map) return (NULL);
    
    if (map && (TAG(map) == CLJ_MAP || TAG(map) == CLJ_TRANSIENT_MAP)) {
        return map_keys((ID)map);
    }
    
    return NULL; // Return nil for unsupported types
}

ID native_vals(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "vals")) return NULL;
    CljObject *map = (CljObject*)args[0];
    if (!map) return (NULL);
    
    if (map && (TAG(map) == CLJ_MAP || TAG(map) == CLJ_TRANSIENT_MAP)) {
        return map_vals((ID)map);
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
            return (CljObject*)intern_symbol("clojure.lang", "Long");
        case TAG_CHAR:
            return (CljObject*)intern_symbol("clojure.lang", "Character");
        case TAG_BOOL: {
            int special_type = as_special(val);
            if (special_type == SPECIAL_TRUE || special_type == SPECIAL_FALSE) {
                return (CljObject*)intern_symbol("clojure.lang", "Boolean");
            }
            return (CljObject*)intern_symbol("clojure.lang", "Special");
        }
        case TAG_FIXED:
            return (CljObject*)intern_symbol("clojure.lang", "Double");
        case TAG_POINTER:
            // Heap object - continue to object type switch
            break;
        default:
            return (CljObject*)intern_symbol("clojure.lang", "Unknown");
    }
    
    // Handle heap objects
    CljObject *obj = (CljObject*)val;
    
    // Check for keyword (symbol with ':' prefix)
    if (IS_KEYWORD(obj)) {
        return (CljObject*)intern_symbol("clojure.lang", "Keyword");
    }
    
    // Switch on object type for heap objects
    switch (obj->type) {
        case CLJ_SYMBOL:
            return (CljObject*)intern_symbol("clojure.lang", "Symbol");
        case CLJ_STRING:
            return (CljObject*)intern_symbol("clojure.lang", "String");
        case CLJ_VECTOR:
            return (CljObject*)intern_symbol("clojure.lang", "PersistentVector");
        case CLJ_TRANSIENT_VECTOR:
            return (CljObject*)intern_symbol("clojure.lang", "TransientVector");
        case CLJ_TRANSIENT_MAP:
            return (CljObject*)intern_symbol("clojure.lang", "TransientArrayMap");
        case CLJ_MAP:
            return (CljObject*)intern_symbol("clojure.lang", "PersistentArrayMap");
        case CLJ_LIST:
            return (CljObject*)intern_symbol("clojure.lang", "PersistentList");
        case CLJ_FUNC:
            return (CljObject*)intern_symbol("clojure.lang", "IFn");
        case CLJ_CLOSURE:
            return (CljObject*)intern_symbol("clojure.lang", "IFn");
        case CLJ_EXCEPTION:
            return (CljObject*)intern_symbol("clojure.lang", "Exception");
        default:
            // Fallback: use type name but still in clojure.lang namespace
            return (CljObject*)intern_symbol("clojure.lang", clj_type_name(obj->type));
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
        CljObject *key = (CljObject*)args[i];
        CljObject *value = (CljObject*)args[i + 1];
        CljMap *updated_map = map_assoc(map, (ID)key, (ID)value);
        ASSIGN(map, updated_map);
    }
    
    return AUTORELEASE(map);
}

ID native_vector(ID *args, unsigned int argc) {
    // Clojure-compatible: (vector) returns empty vector singleton
    // This is the same singleton returned by make_vector(0, false)
    if (argc == 0) {
        return make_vector(0, false);  // Returns empty-vector singleton (no memory management needed)
    }
    
    // Create vector with exact capacity (no growth needed)
    CljValue vec = (CljValue)make_vector(argc, false);
    CljPersistentVector *v = as_vector((CljObject*)vec);
    if (!v) return NULL;
    
    // Add all elements with RETAIN (Clojure-compatible: all args are retained)
    for (unsigned int i = 0; i < argc; i++) {
        CljObject *elem = (CljObject*)args[i];
        if (elem) {
            v->data[i] = RETAIN(elem);
        } else {
            v->data[i] = NULL;  // nil is represented as NULL
        }
        v->count++;
    }
    
    return AUTORELEASE(vec);
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
        throw_exception_formatted("IllegalArgumentException", __FILE__, __LINE__, 0,
                "vec requires a seqable collection");
        return NULL;
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
    CljVector vec = (CljVector)make_vector(4, false);
    if (!vec) {
        throw_exception_formatted("RuntimeException", __FILE__, __LINE__, 0,
                "Failed to create vector");
        return NULL;
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

ID make_named_func(BuiltinFn fn, void *env, const char *name) {
    CljFunc *func = ALLOC(CljFunc, 1);
    if (!func) return NULL;
    
    func->base.type = CLJ_FUNC;
    func->base.rc = 1;
    func->fn = (CljObject* (*)(CljObject **, int))fn; // Cast to expected signature
    func->env = env;
    
    // Safely handle name parameter
    if (name && strlen(name) > 0) {
        // Allocate memory for the name to avoid issues with string literals
        func->name = ALLOC(char, strlen(name) + 1);
        if (func->name) {
            strcpy((char*)func->name, name);
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
    EvalState *st = evalstate_new(false);
    CljMap *env = NULL;
    bool ran = event_loop_run_next(env, st);
    evalstate_free(st);
    return ran ? clj_true : clj_false;
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
            const char *str = readable ? pr_str((CljObject*)args[i]) : print_str((CljObject*)args[i]);
            printf("%s", str);
            free((char*)str);
            
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

// Helper function to validate numeric arguments
static bool validate_numeric_args(ID *args, int argc) {
    for (int i = 0; i < argc; i++) {
        // CRITICAL: Check if args[i] is NULL or if it's a valid immediate value
        // Immediate values (fixnums) have odd tags, so they are never NULL
        // But we need to check if args[i] is actually NULL (nil) or if it's a valid value
        if (!args[i]) {
            // ASSERTION: Test thesis that nil is being passed to numeric operations
            // This tests whether nil is being passed to builtin numeric functions
            // This is where the "Cannot use nil as a Number" error originates
            CLJ_ASSERT(args[i] == NULL); // Argument is nil
            
            // NULL argument (nil) - provide better error message
            throw_exception_formatted(EXCEPTION_TYPE, __FILE__, __LINE__, 0, 
                "Cannot use nil as a Number");
            return false;
        }
        // CRITICAL: Check if args[i] is a valid number
        // Immediate values (fixnums) should pass this check
        // But if args[i] is a heap object, it must be a number type
        uint16_t tag = TAG(args[i]);
        if (tag != CLJ_INT && tag != CLJ_FLOAT) {
            throw_exception_formatted(EXCEPTION_TYPE, __FILE__, __LINE__, 0, ERR_EXPECTED_NUMBER);
            return false;
        }
    }
    return true;
}

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
    throw_exception_formatted(EXCEPTION_ARITHMETIC, __FILE__, __LINE__, 0, err_msg, a, b);
    return NULL;
}

// Helper function to throw fixed-point overflow exceptions
static ID throw_fixed_overflow(const char* err_msg) {
    throw_exception_formatted(EXCEPTION_ARITHMETIC, __FILE__, __LINE__, 0, err_msg);
    return NULL;
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
    
    // Calculate total length
    size_t total_len = 0;
    for (unsigned int i = 0; i < argc; i++) {
        const char *s = to_string(args[i]);
        if (s) {
            total_len += strlen(s);
            free((char*)s);
        }
    }
    
    // Allocate buffer
    char *buffer = ALLOC(char, total_len + 1);
    if (!buffer) return make_string("");
    buffer[0] = '\0';
    
    // Concatenate all strings
    for (unsigned int i = 0; i < argc; i++) {
        const char *s = to_string(args[i]);
        if (s) {
            strcat(buffer, s);
            free((char*)s);
        }
    }
    
    CljObject *result = (CljObject*)make_string(buffer);
    free(buffer);
    return result;
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
    const char *name = NULL;
    
    if (argc == 2) {
        // Two arguments: namespace (can be nil) and name
        ID ns_arg = args[0];
        ID name_arg = args[1];
        
        // Namespace can be nil (NULL) or a string
        if (ns_arg && TAG(ns_arg) != CLJ_STRING) {
            throw_exception_formatted("IllegalArgumentException", __FILE__, __LINE__, 0,
                    "symbol namespace must be a string or nil");
            return NULL;
        }
        
        if (!name_arg || TAG(name_arg) != CLJ_STRING) {
            throw_exception_formatted("IllegalArgumentException", __FILE__, __LINE__, 0,
                    "symbol requires a string for name");
            return NULL;
        }
        
        // Extract namespace (can be NULL if nil was passed)
        if (ns_arg) {
            CljString *ns_str = (CljString*)ns_arg;
            ns = clj_string_data(ns_str);
        } else {
            ns = NULL;  // nil namespace
        }
        
        CljString *name_str = (CljString*)name_arg;
        name = clj_string_data(name_str);
    } else {
        // One argument: name only
        ID name_arg = args[0];
        
        if (!name_arg || TAG(name_arg) != CLJ_STRING) {
            throw_exception_formatted("IllegalArgumentException", __FILE__, __LINE__, 0,
                    "symbol requires a string argument");
            return NULL;
        }
        
        CljString *name_str = (CljString*)name_arg;
        name = clj_string_data(name_str);
    }
    
    // Create symbol from string(s)
    CljSymbol *sym = intern_symbol(ns, name);
    if (!sym) {
        throw_exception_formatted("RuntimeException", __FILE__, __LINE__, 0,
                "Failed to create symbol from string");
        return NULL;
    }
    
    // intern_symbol returns a retained symbol, but builtin functions should return AUTORELEASE
    return AUTORELEASE((ID)sym);
}

// File I/O: slurp - read entire file as string
#ifndef ESP32_BUILD
ID native_slurp(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "slurp")) return NULL;
    
    // Convert argument to C-string
    const char *filename_str = to_string(args[0]);
    if (!filename_str) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, 
                       "slurp requires a string or symbol argument",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    // Open file
    FILE *fp = fopen(filename_str, "r");
    if (!fp) {
        // Graceful: return nil on missing file (test expects non-fatal failure)
        free((void*)filename_str);
        return NULL;
    }
    
    // Get file size
    if (fseek(fp, 0, SEEK_END) != 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Cannot seek in file '%s': %s", filename_str, strerror(errno));
        free((void*)filename_str);
        fclose(fp);
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, error_msg,
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    long file_size = ftell(fp);
    if (file_size < 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Cannot determine size of file '%s': %s", filename_str, strerror(errno));
        free((void*)filename_str);
        fclose(fp);
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, error_msg,
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    // Reset to beginning
    rewind(fp);
    
    // Read file content
    char *buffer = ALLOC(char, file_size + 1);
    if (!buffer) {
        free((void*)filename_str);
        fclose(fp);
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, 
                       "Out of memory reading file",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    size_t bytes_read = fread(buffer, 1, (size_t)file_size, fp);
    buffer[bytes_read] = '\0';  // Null-terminate
    
    // Check for read errors
    if (bytes_read != (size_t)file_size && !feof(fp)) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Error reading file '%s': %s", filename_str, strerror(errno));
        free(buffer);
        free((void*)filename_str);
        fclose(fp);
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, error_msg,
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    // Create Clojure string and cleanup
    CljObject *result = (CljObject*)make_string(buffer);
    
    free(buffer);
    free((void*)filename_str);
    fclose(fp);
    
    // make_string returns object with rc=1 - caller takes ownership
    return result;
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

static char* read_file_cstr(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    long sz = ftell(fp);
    if (sz < 0) { fclose(fp); return NULL; }
    rewind(fp);
    char *buffer = (char*)malloc((size_t)sz + 1);
    if (!buffer) { fclose(fp); return NULL; }
    size_t n = fread(buffer, 1, (size_t)sz, fp);
    buffer[n] = '\0';
    fclose(fp);
    return buffer;
}

static bool eval_source_in_current_state(const char *src, EvalState *st) {
    if (!src || !st) return false;
    bool ok = true;
    WITH_AUTORELEASE_POOL({
        Reader reader;
        reader_init(&reader, src);
        while (!reader_is_eof(&reader)) {
            reader_skip_all(&reader);
            if (reader_is_eof(&reader)) break;
            TRY {
                CljValue form = value_by_parsing_expr(&reader, st);
                if (!form) {
                    if (reader_is_eof(&reader)) break; else { ok = false; break; }
                }
                (void)eval_parsed((CljObject*)form, st, NULL);
                // value_by_parsing_expr returns AUTORELEASE object
            } CATCH(ex) {
                ok = false;
                // Skip to next line to avoid infinite loop
                while (!reader_is_eof(&reader) && reader_current(&reader) != '\n') reader_next(&reader);
                if (!reader_is_eof(&reader)) reader_next(&reader);
            } END_TRY
        }
    });
    return ok;
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
    
    CljPersistentVector *vec = as_vector(symbols);
    for (int i = 0; i < vec->count; i++) {
        CljObject *sym = vec->data[i];
        if (!sym || TAG(sym) != CLJ_SYMBOL) continue;
        
        // Look up symbol in source namespace
        CljObject *val = (CljObject*)map_get((CljMap*)source_ns->mappings, (CljValue)sym);
        if (val) {
            // Copy to target namespace
            ns_define(target_ns, sym, val);
        }
    }
}

/**
 * @brief Copy all symbols from source namespace to target namespace
 * @param source_ns Source namespace
 * @param target_ns Target namespace
 */
static void copy_all_symbols_to_namespace(CljNamespace *source_ns, CljNamespace *target_ns) {
    if (!source_ns || !target_ns || !source_ns->mappings) return;
    
    CljMap *map = as_map(source_ns->mappings);
    if (!map) return;
    
    // Iterate through all mappings in source namespace
    for (int i = 0; i < map->count; i++) {
        CljObject *key = KV_KEY(map->data, i);
        CljObject *val = (CljObject*)KV_VALUE(map->data, i);
        if (key && val) {
            // Copy to target namespace
            ns_define(target_ns, key, val);
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
    
    CljPersistentVector *vec = NULL;
    bool ns_name_allocated = false;
    
    // Handle simple Symbol case: (require 'namespace)
    if (spec && TAG(spec) == CLJ_SYMBOL) {
        CljSymbol *sym = as_symbol(spec);
        if (!sym || !sym->name) return false;
        ns_name = sym->name;
    }
    // Handle Vector case: [namespace :as alias] or [namespace :refer [syms]]
    else if (spec && TAG(spec) == CLJ_VECTOR) {
        vec = as_vector(spec);
        if (vec->count < 1) return false;
        
        // First element should be namespace name (Symbol or String)
        CljObject *ns_obj = vec->data[0];
        if (!ns_obj) return false;
        
        if (ns_obj && TAG(ns_obj) == CLJ_SYMBOL) {
            CljSymbol *ns_sym = as_symbol(ns_obj);
            if (!ns_sym || !ns_sym->name) return false;
            ns_name = ns_sym->name;
        } else {
            const char *ns_str = to_string(ns_obj);
            if (!ns_str) return false;
            ns_name = ns_str;
            ns_name_allocated = true;
        }
        
        // Parse keywords: :as, :refer
        for (int i = 1; i < vec->count; i++) {
            CljObject *elem = vec->data[i];
            if (!elem) continue;
            
            // Check if it's a keyword (Symbol starting with :)
            if (elem && TAG(elem) == CLJ_SYMBOL) {
                CljSymbol *kw = as_symbol(elem);
                if (!kw || !kw->name) continue;
                
                if (kw->name[0] == ':' && strcmp(kw->name, ":as") == 0) {
                    // :as alias
                    if (i + 1 < vec->count) {
                        alias_sym = vec->data[i + 1];
                        i++; // Skip next element
                    }
                } else if (kw->name[0] == ':' && strcmp(kw->name, ":refer") == 0) {
                    // :refer [symbols] or :refer :all
                    if (i + 1 < vec->count) {
                        CljObject *refer_arg = vec->data[i + 1];
                        if (refer_arg && TAG(refer_arg) == CLJ_SYMBOL) {
                            CljSymbol *refer_sym = as_symbol(refer_arg);
                            if (refer_sym && refer_sym->name && strcmp(refer_sym->name, ":all") == 0) {
                                refer_all = true;
                            }
                        } else if (refer_arg && TAG(refer_arg) == CLJ_VECTOR) {
                            refer_syms = refer_arg;
                        }
                        i++; // Skip next element
                    }
                }
            }
        }
    } else {
        return false;
    }
    
    if (!ns_name) return false;
    
    // Load namespace (existing logic)
    CljNamespace *existing = ns_find(ns_name);
    if (existing) {
        // Namespace already loaded - just set alias/refer if needed
        if (alias_sym && TAG(alias_sym) == CLJ_SYMBOL) {
            CljObject *ns_name_sym = (CljObject*)intern_symbol(NULL, ns_name);
            if (ns_name_sym) {
                ns_set_alias(st->current_ns, alias_sym, ns_name_sym);
            }
        }
        if (refer_all) {
            copy_all_symbols_to_namespace(existing, st->current_ns);
        } else if (refer_syms) {
            copy_symbols_to_namespace(existing, st->current_ns, refer_syms);
        }
        if (ns_name_allocated) {
            free((char*)ns_name);
        }
        return true;
    }
    
    // Convert namespace to relative path
    char *rel = namespace_to_relpath(ns_name);
    if (!rel) {
        if (ns_name_allocated) {
            free((char*)ns_name);
        }
        return false;
    }
    
    // Search order: libs/<rel>, then <rel> (project root)
    char libs_path[512];
    snprintf(libs_path, sizeof(libs_path), "libs/%s", rel);
    
    char *source = read_file_cstr(libs_path);
    if (!source) {
        source = read_file_cstr(rel);
    }
    
    if (!source) {
        free(rel);
        if (ns_name_allocated) {
            free((char*)ns_name);
        }
        return false;
    }
    
    // Evaluate source in current state
    const char *orig_ns = NULL;
    if (st && st->current_ns && st->current_ns->name && TAG(st->current_ns->name) == CLJ_SYMBOL) {
        orig_ns = as_symbol(st->current_ns->name)->name;
    }
    
    // Temporarily switch to target namespace
    if (st) evalstate_set_ns(st, ns_name);
    bool ok = eval_source_in_current_state(source, st);
    // Restore original namespace
    if (st && orig_ns) evalstate_set_ns(st, orig_ns);
    
    free(source);
    free(rel);
    
    if (!ok) {
        if (ns_name_allocated) {
            free((char*)ns_name);
        }
        return false;
    }
    
    // Now that namespace is loaded, set alias/refer if needed
    CljNamespace *loaded_ns = ns_find(ns_name);
    if (loaded_ns) {
        if (alias_sym && TAG(alias_sym) == CLJ_SYMBOL) {
            CljObject *ns_name_sym = (CljObject*)intern_symbol(NULL, ns_name);
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
    if (ns_name_allocated) {
        free((char*)ns_name);
    }
    
    return true;
}

ID native_require(ID *args, unsigned int argc) {
    if (argc == 0) {
        throw_exception(EXCEPTION_ARITY, "require requires at least 1 argument", __FILE__, __LINE__, 0);
        return NULL;
    }

    EvalState *st = evalstate_new(false);
    if (!st) return NULL;

    // Process each require spec (support multiple specs: (require '[ns1 :as n1] '[ns2 :as n2]))
    for (unsigned int i = 0; i < argc; i++) {
        if (!process_require_spec(args[i], st)) {
            // Graceful failure: continue with next spec instead of throwing
            // This allows partial success (e.g., one namespace loads, another fails)
            continue;
        }
    }

    evalstate_free(st);
    return NULL; // Clojure-compatible: require returns nil
}
#endif // ESP32_BUILD

// File I/O: spit - write string to file
#ifndef ESP32_BUILD
ID native_spit(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 2, "spit")) return NULL;
    
    // Convert first argument (filename) to C-string
    const char *filename_str = to_string(args[0]);
    if (!filename_str) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, 
                       "spit requires a string or symbol as first argument (filename)",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    // Convert second argument (content) to C-string
    const char *content_str = to_string(args[1]);
    if (!content_str) {
        free((void*)filename_str);
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, 
                       "spit requires a string or symbol as second argument (content)",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    // Open file for writing (overwrites if exists - Clojure-compatible)
    FILE *fp = fopen(filename_str, "w");
    if (!fp) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Cannot open file '%s' for writing: %s", filename_str, strerror(errno));
        free((void*)filename_str);
        free((void*)content_str);
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, error_msg,
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    // Write content to file
    size_t content_len = strlen(content_str);
    size_t bytes_written = fwrite(content_str, 1, content_len, fp);
    
    // Check for write errors
    if (bytes_written != content_len) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Error writing to file '%s': %s", filename_str, strerror(errno));
        free((void*)filename_str);
        free((void*)content_str);
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
        free((void*)filename_str);
        free((void*)content_str);
        fclose(fp);
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, error_msg,
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    // Cleanup
    free((void*)filename_str);
    free((void*)content_str);
    fclose(fp);
    
    // Clojure-compatible: spit returns nil
    return NULL;
}
#endif // ESP32_BUILD

// Binary operations (inline for performance)
// Variadische Number-Reducer mit Single-Pass und Float-Promotion
ID native_add_variadic(ID *args, unsigned int argc) {
    if (argc == 0) return create_fixnum_result(0);
    if (argc == 1) return (RETAIN((CljObject*)args[0]));
    
    if (!validate_numeric_args(args, argc)) return (NULL);
    
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
    if (argc == 1) return (RETAIN((CljObject*)args[0]));
    
    if (!validate_numeric_args(args, argc)) return (NULL);
    
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
    if (argc == 0) { 
        throw_exception_formatted("ArityError", __FILE__, __LINE__, 0, ERR_WRONG_ARITY_ZERO); 
        return NULL; 
    }
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
    
    if (!validate_numeric_args(args, argc)) return (NULL);
    
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
    
    if (!validate_numeric_args(args, argc)) return NULL;
    
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

ID native_div_variadic(ID *args, unsigned int argc) {
    if (argc == 0) { 
        throw_exception_formatted("ArityError", __FILE__, __LINE__, 0, ERR_WRONG_ARITY_ZERO); 
        return NULL; 
    }
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
    
    if (!validate_numeric_args(args, argc)) return (NULL);
    
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
                throw_exception_formatted("IllegalArgumentException", __FILE__, __LINE__, 0,
                        "byte-array size must be non-negative, got %d", size);
                return NULL;
            }
            return (ID)make_byte_array(size);
        }
        default:
            break;
    }
    
    // Otherwise, treat as sequence and create array from values
    CljObject *seq = (CljObject*)args[0];
    if (!seq) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "byte-array argument must be a number or sequence",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    // For now, only support vectors as sequences
    if (seq && TAG(seq) == CLJ_VECTOR) {
        CljPersistentVector *vec = as_vector(seq);
        CljValue arr = (CljValue)make_byte_array(vec->count);
        
        for (int i = 0; i < vec->count; i++) {
            if (TAG(vec->data[i]) != CLJ_INT) {
                RELEASE((CljObject*)arr);
                throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "byte-array sequence elements must be numbers",
                               __FILE__, __LINE__, 0);
                return NULL;
            }
            int val = AS_FIXNUM(vec->data[i]);
            if (val < 0 || val > 255) {
                RELEASE((CljObject*)arr);
                throw_exception_formatted("IllegalArgumentException", __FILE__, __LINE__, 0,
                        "byte values must be 0-255, got %d", val);
                return NULL;
            }
            byte_array_set(arr, i, (uint8_t)val);
        }
        
        return (ID)arr;
    }
    
    throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "byte-array currently only supports vectors as sequences",
                   __FILE__, __LINE__, 0);
    return NULL;
}

ID native_aget(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 2, "aget")) return NULL;
    
    CljObject *arr = (CljObject*)args[0];
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
    
    CljObject *arr = (CljObject*)args[0];
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
        throw_exception_formatted("IllegalArgumentException", __FILE__, __LINE__, 0,
                "byte value must be 0-255, got %d", value);
        return NULL;
    }
    
    byte_array_set((CljValue)arr, index, (uint8_t)value);
    return args[2]; // Return the value (Clojure-compatible)
}

ID native_alength(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "alength")) return NULL;
    
    CljObject *arr = (CljObject*)args[0];
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
    
    CljObject *arr = (CljObject*)args[0];
    if (!arr || TAG(arr) != CLJ_BYTE_ARRAY) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "aclone argument must be a byte-array",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    return (ID)byte_array_clone((CljValue)arr);
}

// Comparison operators as native functions
ID native_lt(ID *args, unsigned int argc) {
    (void)argc; // Suppress unused parameter warning
    CompareResult result;
    if (!compare_numeric_values((CljObject*)args[0], (CljObject*)args[1], &result)) {
        throw_exception("TypeError", "Expected number for < comparison",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    return (result == COMPARE_LESS) ? clj_true : clj_false;
}

ID native_gt(ID *args, unsigned int argc) {
    (void)argc; // Suppress unused parameter warning
    CompareResult result;
    if (!compare_numeric_values((CljObject*)args[0], (CljObject*)args[1], &result)) {
        throw_exception("TypeError", "Expected number for > comparison",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    return (result == COMPARE_GREATER) ? clj_true : clj_false;
}

ID native_le(ID *args, unsigned int argc) {
    (void)argc; // Suppress unused parameter warning
    CompareResult result;
    if (!compare_numeric_values((CljObject*)args[0], (CljObject*)args[1], &result)) {
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
    if (!compare_numeric_values((CljObject*)args[0], (CljObject*)args[1], &result)) {
        throw_exception("TypeError", "Expected number for >= comparison",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    return (result == COMPARE_GREATER || result == COMPARE_EQUAL) ? 
           clj_true : clj_false;
}

ID native_eq(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 2, "=")) return NULL;
    
    CljObject *a = (CljObject*)args[0];
    CljObject *b = (CljObject*)args[1];
    
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

ID native_identical(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 2, "identical?")) return clj_false;
    return (args[0] == args[1]) ? clj_true : clj_false;
}

ID native_vector_p(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "vector?")) return clj_false;
    return (args[0] && TAG(args[0]) == CLJ_VECTOR) ? clj_true : clj_false;
}

// native_time removed: time is now only a special form (eval_time)
// This ensures time can measure actual evaluation time, not pre-evaluated arguments

// Native time-micro implementation with microsecond resolution
ID native_time_micro(ID *args, unsigned int argc) {
    if (argc != 1) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "time-micro requires exactly 1 argument",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    // Start timing (Clojure-compatible: capture start time)
    struct timeval start, end;
    gettimeofday(&start, NULL);
    
    // Evaluate the argument (it should be a function call or expression)
    EvalState *st = evalstate_new(false);
    CljObject *result = NULL;
    
    // Use eval_parsed for proper evaluation
    if (st && args[0]) {
        result = (CljObject*)eval_parsed(args[0], st, NULL);
    }
    
    // End timing (Clojure-compatible: capture end time)
    gettimeofday(&end, NULL);
    
    // Calculate elapsed time in microseconds (Clojure-compatible: precise calculation)
    double elapsed_us = (end.tv_sec - start.tv_sec) * 1000000.0 + 
                       (end.tv_usec - start.tv_usec);
    
    // Print timing information (Clojure-compatible: "μsecs" format)
    printf("Elapsed time: %.2f μsecs\n", elapsed_us);
    
    evalstate_free(st);
    // Return the result of the evaluated expression (Clojure-compatible: return the value)
    return result;
}

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
        fn_args = (ID*)calloc(fn_argc, sizeof(ID));
        if (!fn_args) {
            throw_oom(CLJ_ATOM);
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
    CljObject *last = (CljObject*)args[argc - 1];
    return AUTORELEASE(RETAIN(last));
}

// dotimes: Execute expression n times with variable bound to 0, 1, ..., n-1
// dotimes is now implemented as a special form, not a builtin

// Helper function to register a builtin in clojure.core namespace (DRY principle)
static void register_builtin_in_namespace(const char *name, BuiltinFn func) {
    EvalState *st = evalstate_new(false);
    if (!st) return;
    
    // Get or create clojure.core namespace
    CljNamespace *clojure_core = ns_get_or_create("clojure.core", NULL);
    if (!clojure_core) {
        evalstate_free(st);
        return;
    }
    
    // Explicitly set clojure.core cache if not already set
    // This ensures cache is set even if register_builtins is called before load_clojure_core
    extern TinyClJRuntime g_runtime;
    if (!g_runtime.clojure_core_cache) {
        g_runtime.clojure_core_cache = (void*)clojure_core;
    }
    
    // Register the builtin in clojure.core namespace
    CljObject *symbol = (CljObject*)intern_symbol(NULL, name);
    CljObject *func_obj = make_named_func(func, NULL, name);
    if (symbol && func_obj) {
        ns_define(clojure_core, symbol, func_obj);
        // Builtin registered successfully in clojure.core
    } else {
        // Failed to register builtin
    }
    
    evalstate_free(st);
}

void register_builtins() {
    // Register all builtins in clojure.core namespace (unified system)
    register_builtin_in_namespace("+", native_add_variadic);
    register_builtin_in_namespace("-", native_sub_variadic);
    register_builtin_in_namespace("*", native_mul_variadic);
    register_builtin_in_namespace("/", native_div_variadic);
    register_builtin_in_namespace("mod", native_mod);
    register_builtin_in_namespace("str", native_str);
    register_builtin_in_namespace("symbol", native_symbol);
#ifndef ESP32_BUILD
    register_builtin_in_namespace("slurp", native_slurp);
    register_builtin_in_namespace("spit", native_spit);
    register_builtin_in_namespace("require", native_require);
#endif
    register_builtin_in_namespace("type", native_type);
    register_builtin_in_namespace("array-map", native_array_map);
    register_builtin_in_namespace("vector", native_vector);
    register_builtin_in_namespace("vec", native_vec);
    register_builtin_in_namespace("nth", nth2);
    register_builtin_in_namespace("peek", native_peek);
    register_builtin_in_namespace("pop", native_pop);
    register_builtin_in_namespace("subvec", native_subvec);
    register_builtin_in_namespace("conj", native_conj);
    register_builtin_in_namespace("first", native_first);
    register_builtin_in_namespace("rest", native_rest);
    register_builtin_in_namespace("next", native_next);
    register_builtin_in_namespace("cons", native_cons);
    register_builtin_in_namespace("list", native_list);
    register_builtin_in_namespace("count", native_count);
    register_builtin_in_namespace("reverse", native_reverse);
    register_builtin_in_namespace("assoc", assoc3);
    register_builtin_in_namespace("transient", native_transient);
    register_builtin_in_namespace("persistent!", native_persistent);
    register_builtin_in_namespace("conj!", native_conj_bang);
    register_builtin_in_namespace("get", native_get);
    register_builtin_in_namespace("keys", native_keys);
    register_builtin_in_namespace("vals", native_vals);
    register_builtin_in_namespace("println", native_println);
    
    // Register print functions
    register_builtin_in_namespace("print", native_print);
    register_builtin_in_namespace("pr", native_pr);
    register_builtin_in_namespace("prn", native_prn);
    
    // Register comparison operators as normal functions
    register_builtin_in_namespace("<", native_lt);
    register_builtin_in_namespace(">", native_gt);
    register_builtin_in_namespace("<=", native_le);
    register_builtin_in_namespace(">=", native_ge);
    register_builtin_in_namespace("=", native_eq);
    register_builtin_in_namespace("identical?", native_identical);
    register_builtin_in_namespace("vector?", native_vector_p);
    
    // Time function
    // time is now only a special form (eval_time), not a builtin
    // This ensures time can measure actual evaluation time, not pre-evaluated arguments
    register_builtin_in_namespace("time-micro", native_time_micro);
    register_builtin_in_namespace("sleep", native_sleep);
    
    // Note: def and ns are special forms (not builtins) because they require non-evaluated arguments
    // They are handled directly in eval_list() via eval_def() and eval_ns()
    
    // Control flow functions
    register_builtin_in_namespace("do", native_do);
    
    // Loop constructs
    // dotimes is now implemented as a special form, not a builtin
    
    // Byte array functions
    register_builtin_in_namespace("byte-array", native_byte_array);
    register_builtin_in_namespace("aget", native_aget);
    register_builtin_in_namespace("aset", native_aset);
    register_builtin_in_namespace("alength", native_alength);
    register_builtin_in_namespace("aclone", native_aclone);
    // Event-loop builtin
    register_builtin_in_namespace("run-next-task", native_run_next_task);
    
    // Atom functions
    register_builtin_in_namespace("atom", native_atom);
    register_builtin_in_namespace("deref", native_deref);
    register_builtin_in_namespace("reset!", native_reset_bang);
    register_builtin_in_namespace("swap!", native_swap_bang);
}
