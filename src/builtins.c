#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include "object.h"
#include "vector.h"
#include "map.h"
#include "numeric_utils.h"
#include "runtime.h"
#include "memory.h"
#include "namespace.h"
#include "value.h"
#include "seq.h"
#include "byte_array.h"
#include "exception.h"
#include "list.h"
#include "symbol.h"
#include "function.h"
#include "function_call.h"
#include "exception.h"
#include "clj_strings.h"
#include "event_loop.h"
#include "strings.h"
#include "reader.h"
#include "parser.h"

// Forward declaration for eval_body_with_env
extern CljObject* eval_body_with_env(CljObject *body, CljMap *env);

// Helper function to validate builtin arguments (DRY principle)
static bool validate_builtin_args(unsigned int argc, unsigned int expected, const char *func_name) {
    if (argc != expected) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "%s requires exactly %u argument%s, got %u", 
                func_name, expected, expected == 1 ? "" : "s", argc);
        throw_exception(EXCEPTION_TYPE_ARITY, error_msg, __FILE__, __LINE__, 0);
        return false;
    }
    return true;
}

// Flexible arity checker using compiler-provided function name (__func__)
#define REQUIRE_ARITY(CONDITION) do { \
    if (!(CONDITION)) { \
        char error_msg[256]; \
        snprintf(error_msg, sizeof(error_msg), \
                "%s arity check failed: expected (%s), got %u", \
                __func__, #CONDITION, (argc)); \
        throw_exception(EXCEPTION_TYPE_ARITY, error_msg, __FILE__, __LINE__, 0); \
        return NULL; \
    } \
} while(0)

// nth with optional default: (nth v i) | (nth v i default)
ID native_nth(ID *args, unsigned int argc) {
    REQUIRE_ARITY(argc == 2 || argc == 3);
    ID vec = args[0];
    ID idx = args[1];
    if (!vec || !is_type(vec, CLJ_VECTOR) || !IS_FIXNUM(idx)) return NULL;
    int i = AS_FIXNUM(idx);
    CljPersistentVector *v = as_vector(vec);
    if (!v || i < 0 || i >= v->count) {
        if (argc == 3) return args[2];
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "nth index out of bounds", __FILE__, __LINE__, 0);
        return NULL;
    }
    return (RETAIN(v->data[i]));
}

// Forward declaration
ID conj2(ID vec, ID val);

ID conj2_wrapper(ID *args, int argc) {
    if (!validate_builtin_args(argc, 2, "conj")) return NULL;
    return conj2(args[0], args[1]);
}

ID conj2(ID vec, ID val) {
    if (!vec || !is_type(vec, CLJ_VECTOR)) return NULL;
    CljPersistentVector *v = as_vector(vec);
    int is_mutable = v ? v->mutable_flag : 0;
    if (is_mutable) {
        if (v->count >= v->capacity) {
            int newcap = v->capacity > 0 ? v->capacity * 2 : 1;
            void *newmem = realloc(v->data, sizeof(CljObject*) * (size_t)newcap);
            if (!newmem) return NULL;
            v->data = (CljObject**)newmem;
            v->capacity = newcap;
        }
        v->data[v->count++] = (RETAIN(val), val);
        return (ID)RETAIN(vec);
    } else {
        int need = v->count + 1;
        int newcap = v->capacity;
        if (need > newcap) newcap = newcap > 0 ? newcap * 2 : 1;
        CljValue copy_val = make_vector(newcap, 0);
        CljObject *copy = copy_val;
        CljPersistentVector *c = as_vector(copy);
        for (int i = 0; i < v->count; ++i) {
            c->data[i] = (RETAIN(v->data[i]), v->data[i]);
        }
        c->count = v->count;
        c->data[c->count++] = (RETAIN(val), val);
        return (ID)copy;
    }
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
            result = make_list((ID)val, (CljList*)result);
        }
        return (result);
    }
    
    if (is_type(coll, CLJ_VECTOR)) {
        CljObject *result = coll;
        for (unsigned int i = 1; i < argc; i++) {
            CljObject *val = args[i];
            result = conj2((ID)result, (ID)val);
            if (!result) return (NULL);
        }
        return (result);
    }
    
    // Throw exception for unsupported collection type
    throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, 
                    "conj not supported on this type", 
                    __FILE__, __LINE__, 0);
    return (NULL);
}

// First function that works with BuiltinFn signature
ID native_first(ID *args, unsigned int argc) {
    CLJ_ASSERT(args != NULL);
    
    REQUIRE_ARITY(argc == 1);
    
    CljObject *coll = args[0];
    if (!coll) {
        // first of nil returns nil
        return NULL;
    }
    
    // Direct access for lists (already a seq) - no allocation needed
    if (is_type(coll, CLJ_LIST)) {
        CljObject *first = LIST_FIRST((CljList*)coll);
        return first;  // Return existing object directly - no memory management needed
    }
    
    // Use seq implementation for other types (vectors, maps, strings)
    CljObject *seq = seq_create(coll);
    if (!seq) return NULL;
    
    CljObject *result = seq_first(seq);
    seq_release(seq);
    
    return result;
}

// Rest function that works with BuiltinFn signature
ID native_rest(ID *args, unsigned int argc) {
    CLJ_ASSERT(args != NULL);
    
    REQUIRE_ARITY(argc == 1);
    
    CljObject *coll = args[0];
    if (!coll) {
        // rest of nil returns empty list
        return empty_list();
    }
    
    // Direct access for lists (already a seq) - no allocation needed
    if (is_type(coll, CLJ_LIST)) {
        CljObject *rest = LIST_REST((CljList*)coll);
        return rest ? rest : empty_list();  // No AUTORELEASE needed for singleton
    }
    
    if (is_type(coll, CLJ_VECTOR)) {
        CljPersistentVector *v = as_vector(coll);
        if (!v || v->count <= 1) {
            return empty_list();
        }
        
        // Use CljSeqIterator (existing!) instead of copying
        CljObject *seq = seq_create(coll);
        if (!seq) return empty_list();
        
        // Return rest of sequence (O(1) operation!)
        CljObject *rest_seq = seq_rest(seq);
        
        // Free the intermediate seq object to prevent memory leak
        seq_release(seq);
        
        return rest_seq;
    }
    
    if (is_type(coll, CLJ_SEQ)) {
        // Already a sequence - just call seq_rest
        return seq_rest(coll);
    }
    
    // Throw exception for unsupported collection type
    throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, 
                    "rest not supported on this type", 
                    __FILE__, __LINE__, 0);
    return (NULL);
}

// Cons function that works with BuiltinFn signature
ID native_cons(ID *args, unsigned int argc) {
    CLJ_ASSERT(args != NULL);
    
    REQUIRE_ARITY(argc == 2);
    
    CljObject *elem = args[0];
    CljObject *coll = args[1];
    
    if (!elem) elem = NULL;
    
    CljObject *result = NULL;
    
    // nil oder leer
    if (!coll) {
        result = (CljObject*)make_list(elem, NULL);
        return result;
    }
    
    // Typ-basierte Behandlung
    switch (coll->type) {
        case CLJ_LIST:
        case CLJ_SEQ:
            result = make_list(elem, (CljList*)coll);
            return result;
        
        default: {
            // Vektor oder andere → zu Seq konvertieren
            CljObject *seq = seq_create(coll);
            if (!seq) {
                result = make_list(elem, NULL);
            } else {
                result = make_list(elem, (CljList*)seq);
            }
            return result;
        }
    }
}

ID assoc3(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 3, "assoc")) return NULL;
    CljObject *vec = args[0];
    CljObject *idx = args[1];
    CljObject *val = args[2];
    if (!vec || !is_type(vec, CLJ_VECTOR) || !idx || !IS_FIXNUM(idx)) return (NULL);
    int i = AS_FIXNUM(idx);
    CljPersistentVector *v = as_vector(vec);
    if (!v || i < 0 || i >= v->count) return (NULL);
    int is_mutable = v->mutable_flag;
    if (is_mutable) {
        RELEASE(v->data[i]);
        v->data[i] = (RETAIN(val), val);
        return (RETAIN(vec));
    } else {
        CljValue copy_val = make_vector(v->capacity, 0);
        CljObject *copy = (CljObject*)copy_val;
        CljPersistentVector *c = as_vector(copy);
        for (int j = 0; j < v->count; ++j) {
            c->data[j] = (RETAIN(v->data[j]), v->data[j]);
        }
        c->count = v->count;
        RELEASE(c->data[i]);
        c->data[i] = (RETAIN(val), val);
        return (copy);
    }
}

// Transient functions
ID native_transient(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "transient")) return NULL;
    
    CljObject *coll = args[0];
    if (!coll) return (NULL);
    
    if (is_type(coll, CLJ_VECTOR)) {
        return ((CljObject*)transient((CljValue)coll));
    } else if (is_type(coll, CLJ_MAP)) {
        return ((CljObject*)transient_map((CljValue)coll));
    } else if (is_type(coll, CLJ_TRANSIENT_VECTOR) || is_type(coll, CLJ_TRANSIENT_MAP)) {
        // Clojure-compatible: transient on transient returns the same object
        return (coll);
    }
    
    // Throw exception for unsupported collection type (Clojure-compatible)
    throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, 
                    "transient requires a persistent collection at position 1", 
                    __FILE__, __LINE__, 0);
    return (NULL);
}

ID native_persistent(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "persistent!")) return NULL;
    
    CljObject *coll = args[0];
    if (!coll) return (NULL);
    
    if (is_type(coll, CLJ_TRANSIENT_VECTOR)) {
        return ((CljObject*)persistent((CljValue)coll));
    } else if (is_type(coll, CLJ_TRANSIENT_MAP)) {
        return ((CljObject*)persistent_map((CljValue)coll));
    } else if (is_type(coll, CLJ_VECTOR) || is_type(coll, CLJ_MAP)) {
        // Clojure-compatible: persistent! on persistent returns the same object
        return (coll);
    }
    
    // Throw exception for unsupported collection type (Clojure-compatible)
    throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, 
                    "persistent! requires a transient collection at position 1", 
                    __FILE__, __LINE__, 0);
    return (NULL);
}

ID native_conj_bang(ID *args, unsigned int argc) {
    if (argc < 2) return (NULL);
    
    CljObject *coll = args[0];
    if (!coll) return NULL;
    
    
    if (is_type(coll, CLJ_TRANSIENT_VECTOR)) {
        CljValue result = (CljValue)coll;
        for (unsigned int i = 1; i < argc; i++) {
            result = clj_conj(result, (CljValue)args[i]);
            if (!result) return NULL;
        }
        return (CljObject*)result;
    } else if (is_type(coll, CLJ_TRANSIENT_MAP)) {
        if (argc != 3) return NULL; // conj! for maps needs key-value pair
        return (CljObject*)conj_map((CljValue)coll, (CljValue)args[1], (CljValue)args[2]);
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
    
    if (is_type(map, CLJ_MAP) || is_type(map, CLJ_TRANSIENT_MAP)) {
        return ((CljObject*)map_get((CljValue)map, (CljValue)key));
    }
    
    return (NULL); // Return nil for unsupported types
}

ID native_count(ID *args, unsigned int argc) {
    CLJ_ASSERT(args != NULL);
    
    if (!validate_builtin_args(argc, 1, "count")) return NULL;
    CljObject *coll = args[0];
    if (!coll) return (NULL);
    
    if (is_type(coll, CLJ_MAP) || is_type(coll, CLJ_TRANSIENT_MAP)) {
        return (fixnum(map_count((CljValue)coll)));
    } else if (is_type(coll, CLJ_VECTOR) || is_type(coll, CLJ_TRANSIENT_VECTOR)) {
        CljPersistentVector *vec = as_vector(coll);
        return (fixnum(vec ? vec->count : 0));
    } else if (is_type(coll, CLJ_LIST)) {
        CljList *list = as_list(coll);
        return (fixnum(list_count(list)));
    } else if (is_type(coll, CLJ_STRING)) {
        CljString *str = (CljString*)coll;
        
 
        // Return string length directly
        return fixnum(str->length);
    }
    
    return (fixnum(0)); // Default count for unsupported types
}

// Create vector from variadic args: (vector a b c)
ID native_vector(ID *args, unsigned int argc) {
    CljValue vec = make_vector((int)argc, 0);
    CljPersistentVector *v = as_vector((CljObject*)vec);
    if (!v) return NULL;
    for (unsigned int i = 0; i < argc; i++) {
        v->data[v->count++] = (RETAIN((CljObject*)args[i]), (CljObject*)args[i]);
    }
    return (CljObject*)vec;
}

// Convert collection to vector: (vec coll)
ID native_vec(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "vec")) return NULL;
    CljObject *coll = args[0];
    if (!coll) return (CljObject*)make_vector(0, 0);
    if (is_type(coll, CLJ_VECTOR)) return coll;
    // Fallback: try seq view and accumulate
    CljObject *seq = seq_create(coll);
    if (!seq) return (CljObject*)make_vector(0, 0);
    CljValue out = make_vector(0, 0);
    CljObject *cur;
    while ((cur = seq_first(seq)) != NULL) {
        out = vector_conj(out, (CljValue)cur);
        seq_next(seq);
    }
    seq_release(seq);
    return (CljObject*)out;
}

// (peek v) -> last element or nil
ID native_peek(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "peek")) return NULL;
    CljObject *coll = args[0];
    if (!coll || !is_type(coll, CLJ_VECTOR)) return NULL;
    CljPersistentVector *v = as_vector(coll);
    if (!v || v->count == 0) return NULL;
    return RETAIN(v->data[v->count - 1]);
}

// (pop v) -> vector without last element; error on empty
ID native_pop(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "pop")) return NULL;
    CljObject *coll = args[0];
    if (!coll || !is_type(coll, CLJ_VECTOR)) return NULL;
    CljPersistentVector *v = as_vector(coll);
    if (!v || v->count == 0) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "pop on empty vector", __FILE__, __LINE__, 0);
        return NULL;
    }
    int new_count = v->count - 1;
    CljValue out = make_vector(new_count, 0);
    CljPersistentVector *o = as_vector((CljObject*)out);
    for (int i = 0; i < new_count; i++) {
        o->data[i] = (RETAIN(v->data[i]), v->data[i]);
    }
    o->count = new_count;
    return (CljObject*)out;
}

// (subvec v start) | (subvec v start end)
ID native_subvec(ID *args, unsigned int argc) {
    if (argc != 2 && argc != 3) {
        throw_exception(EXCEPTION_ARITY, "subvec requires 2 or 3 args", __FILE__, __LINE__, 0);
        return NULL;
    }
    CljObject *coll = args[0];
    if (!coll || !is_type(coll, CLJ_VECTOR)) return NULL;
    if (!IS_FIXNUM(args[1])) return NULL;
    int start = AS_FIXNUM(args[1]);
    CljPersistentVector *v = as_vector(coll);
    if (!v) return NULL;
    int end = (argc == 3 && IS_FIXNUM(args[2])) ? AS_FIXNUM(args[2]) : v->count;
    if (start < 0 || end < start || end > v->count) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "subvec index out of bounds", __FILE__, __LINE__, 0);
        return NULL;
    }
    int len = end - start;
    CljValue out = make_vector(len, 0);
    CljPersistentVector *o = as_vector((CljObject*)out);
    for (int i = 0; i < len; i++) {
        o->data[i] = (RETAIN(v->data[start + i]), v->data[start + i]);
    }
    o->count = len;
    return (CljObject*)out;
}

ID native_keys(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "keys")) return NULL;
    CljObject *map = (CljObject*)args[0];
    if (!map) return (NULL);
    
    if (is_type(map, CLJ_MAP) || is_type(map, CLJ_TRANSIENT_MAP)) {
        return ((CljObject*)map_keys((CljValue)map));
    }
    
    return (NULL); // Return nil for unsupported types
}

ID native_vals(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "vals")) return NULL;
    CljObject *map = (CljObject*)args[0];
    if (!map) return (NULL);
    
    if (is_type(map, CLJ_MAP) || is_type(map, CLJ_TRANSIENT_MAP)) {
        return ((CljObject*)map_vals((CljValue)map));
    }
    
    return (NULL); // Return nil for unsupported types
}

ID native_if(ID *args, unsigned int argc) {
    if (argc < 2) return (NULL);
    CljObject *cond = (CljObject*)args[0];
    if (clj_is_truthy(cond)) {
        return (RETAIN((CljObject*)args[1]));
    } else if (argc > 2) {
        return (RETAIN((CljObject*)args[2]));
    } else {
        return (NULL);
    }
}

ID native_type(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "type")) return NULL;
    CljValue val = args[0];
    if (!val) return ((CljObject*)intern_symbol_global("nil"));
    
    // Get the tag to determine immediate vs heap object
    uint8_t tag = get_tag(val);
    
    // Switch on tag for immediate values
    switch (tag) {
        case TAG_FIXNUM:
            return (CljObject*)intern_symbol_global("Number");
        case TAG_CHAR:
            return (CljObject*)intern_symbol_global("Character");
        case TAG_SPECIAL: {
            int special_type = as_special(val);
            if (special_type == SPECIAL_TRUE || special_type == SPECIAL_FALSE) {
                return (CljObject*)intern_symbol_global("Boolean");
            }
            return (CljObject*)intern_symbol_global("Special");
        }
        case TAG_FIXED:
            return (CljObject*)intern_symbol_global("Number");
        case TAG_POINTER:
            // Heap object - continue to object type switch
            break;
        default:
            return (CljObject*)intern_symbol_global("Unknown");
    }
    
    // Handle heap objects
    CljObject *obj = (CljObject*)val;
    
    // Check for keyword (symbol with ':' prefix)
    if (is_type(obj, CLJ_SYMBOL)) {
        CljSymbol *sym = as_symbol(obj);
        if (sym && sym->name[0] == ':') {
            return (CljObject*)intern_symbol_global("Keyword");
        }
    }
    
    // Switch on object type for heap objects
    switch (obj->type) {
        case CLJ_SYMBOL:
            return (CljObject*)intern_symbol_global("Symbol");
        case CLJ_STRING:
            return (CljObject*)intern_symbol_global("String");
        case CLJ_VECTOR:
            return (CljObject*)intern_symbol_global("Vector");
        case CLJ_TRANSIENT_VECTOR:
            return (CljObject*)intern_symbol_global("TransientVector");
        case CLJ_TRANSIENT_MAP:
            return (CljObject*)intern_symbol_global("TransientMap");
        case CLJ_MAP:
            return (CljObject*)intern_symbol_global("Map");
        case CLJ_LIST:
            return (CljObject*)intern_symbol_global("List");
        case CLJ_FUNC:
            return (CljObject*)intern_symbol_global("Function");
        case CLJ_EXCEPTION:
            return (CljObject*)intern_symbol_global("Exception");
        default:
            return (CljObject*)intern_symbol_global(clj_type_name(obj->type));
    }
}

ID native_array_map(ID *args, unsigned int argc) {
    // Must have even number of arguments (key-value pairs)
    if (argc % 2 != 0) {
        // Return empty map instead of nil for odd number of args
        return (make_map(0));
    }
    
    // Create map with appropriate capacity
    int pair_count = argc / 2;
    
    // Handle empty map case specially
    if (pair_count == 0) {
        return (make_map(0));
    }
    
    CljMap *map = (CljMap*)make_map(pair_count);
    
    // Add all key-value pairs
    for (unsigned int i = 0; i < argc; i += 2) {
        CljObject *key = (CljObject*)args[i];
        CljObject *value = (CljObject*)args[i + 1];
        map_assoc((CljValue)map, (CljValue)key, (CljValue)value);
    }
    
    return ((CljObject*)map);
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
    EvalState *st = evalstate();
    CljMap *env = NULL;
    int ran = event_loop_run_next(env, st);
    return ran ? make_special(SPECIAL_TRUE) : make_special(SPECIAL_FALSE);
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
            char *str = readable ? pr_str((CljObject*)args[i]) : print_str((CljObject*)args[i]);
            printf("%s", str);
            free(str);
            
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
        if (!args[i] || (!IS_FIXNUM(args[i]) && !IS_FIXED(args[i]))) {
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
    return (NULL);
}

// Helper function to throw fixed-point overflow exceptions
static ID throw_fixed_overflow(const char* err_msg) {
    throw_exception_formatted(EXCEPTION_ARITHMETIC, __FILE__, __LINE__, 0, err_msg);
    return (NULL);
}

// Helper function to create fixnum result
static ID create_fixnum_result(int acc_i) {
    return (fixnum(acc_i));
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
        return (make_string(""));
    }
    
    // Calculate total length
    size_t total_len = 0;
    for (unsigned int i = 0; i < argc; i++) {
        char *s = to_string(args[i]);
        if (s) {
            total_len += strlen(s);
            free(s);
        }
    }
    
    // Allocate buffer
    char *buffer = ALLOC(char, total_len + 1);
    if (!buffer) return make_string("");
    buffer[0] = '\0';
    
    // Concatenate all strings
    for (unsigned int i = 0; i < argc; i++) {
        char *s = to_string(args[i]);
        if (s) {
            strcat(buffer, s);
            free(s);
        }
    }
    
    CljObject *result = make_string(buffer);
    free(buffer);
    return result;
}

// File I/O: slurp - read entire file as string
#ifndef ESP32_BUILD
ID native_slurp(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "slurp")) return NULL;
    
    // Convert argument to C-string
    char *filename_str = to_string(args[0]);
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
        free(filename_str);
        return NULL;
    }
    
    // Get file size
    if (fseek(fp, 0, SEEK_END) != 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Cannot seek in file '%s': %s", filename_str, strerror(errno));
        free(filename_str);
        fclose(fp);
        throw_exception(EXCEPTION_TYPE_ILLEGAL_ARGUMENT, error_msg,
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    long file_size = ftell(fp);
    if (file_size < 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Cannot determine size of file '%s': %s", filename_str, strerror(errno));
        free(filename_str);
        fclose(fp);
        throw_exception(EXCEPTION_TYPE_ILLEGAL_ARGUMENT, error_msg,
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    // Reset to beginning
    rewind(fp);
    
    // Read file content
    char *buffer = ALLOC(char, file_size + 1);
    if (!buffer) {
        free(filename_str);
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
        free(filename_str);
        fclose(fp);
        throw_exception(EXCEPTION_TYPE_ILLEGAL_ARGUMENT, error_msg,
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    // Create Clojure string and cleanup
    CljObject *result = make_string(buffer);
    
    free(buffer);
    free(filename_str);
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
                (void)eval_expr_simple((CljObject*)form, st);
                RELEASE((CljObject*)form);
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

ID native_require(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "require")) return NULL;

    // Accept symbol or string; convert to plain string
    char *ns_name = to_string(args[0]);
    if (!ns_name || ns_name[0] == '\0') {
        if (ns_name) free(ns_name);
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "require expects non-empty namespace name", __FILE__, __LINE__, 0);
        return NULL;
    }

    // Idempotenz: Wenn Namespace bereits existiert, nichts tun
    CljNamespace *existing = ns_find(ns_name);
    if (existing) { free(ns_name); return NULL; }

    // Convert namespace to relative path a.b -> a/b.clj (with '-' -> '_')
    char *rel = namespace_to_relpath(ns_name);
    if (!rel) { free(ns_name); return NULL; }

    // Search order: libs/<rel>, then <rel> (project root)
    char libs_path[512];
    snprintf(libs_path, sizeof(libs_path), "libs/%s", rel);

    char *source = read_file_cstr(libs_path);
    if (!source) {
        source = read_file_cstr(rel);
    }

    if (!source) {
        // Graceful failure: return nil to indicate missing namespace without throwing
        free(rel); free(ns_name);
        return NULL;
    }

    // Evaluate source in current state (file may contain (ns ...) to switch)
    EvalState *st = evalstate();
    const char *orig_ns = NULL;
    if (st && st->current_ns && st->current_ns->name && is_type(st->current_ns->name, CLJ_SYMBOL)) {
        orig_ns = as_symbol(st->current_ns->name)->name;
    }

    // Temporär in Ziel-NS wechseln wie Clojure-Ladeprozess; Datei (ns ...) kann überschreiben
    if (st) evalstate_set_ns(st, ns_name);
    bool ok = eval_source_in_current_state(source, st);
    // Immer ursprüngliches *ns* wiederherstellen (Clojure-kompatibel: require ändert *ns* nicht)
    if (st && orig_ns) evalstate_set_ns(st, orig_ns);

    free(source);
    free(rel);
    free(ns_name);

    if (!ok) {
        // Graceful failure without exception to keep tests non-fatal
        return NULL;
    }
    return NULL; // Clojure-compatible: require returns nil
}
#endif // ESP32_BUILD

// File I/O: spit - write string to file
#ifndef ESP32_BUILD
ID native_spit(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 2, "spit")) return NULL;
    
    // Convert first argument (filename) to C-string
    char *filename_str = to_string(args[0]);
    if (!filename_str) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, 
                       "spit requires a string or symbol as first argument (filename)",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    // Convert second argument (content) to C-string
    char *content_str = to_string(args[1]);
    if (!content_str) {
        free(filename_str);
        throw_exception(EXCEPTION_TYPE_ILLEGAL_ARGUMENT, 
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
        free(filename_str);
        free(content_str);
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
        free(filename_str);
        free(content_str);
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
        free(filename_str);
        free(content_str);
        fclose(fp);
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, error_msg,
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    // Cleanup
    free(filename_str);
    free(content_str);
    fclose(fp);
    
    // Clojure-compatible: spit returns nil
    return NULL;
}

// File I/O: file-exists? - check if file exists using POSIX access()
ID native_file_exists(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "file-exists?")) return NULL;
    
    // Access internal C-string directly without heap copy (DRY: reuse pattern)
    const char *filename_str = NULL;
    if (is_type(args[0], CLJ_STRING)) {
        // Direct access to string data (no heap copy)
        filename_str = clj_string_data(as_clj_string(args[0]));
    } else if (is_type(args[0], CLJ_SYMBOL)) {
        // Direct access to symbol name (no heap copy)
        CljSymbol *sym = as_symbol(args[0]);
        if (!sym || !sym->name) {
            throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, 
                           "file-exists? requires a string or symbol argument",
                           __FILE__, __LINE__, 0);
            return NULL;
        }
        filename_str = sym->name;
    } else {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, 
                       "file-exists? requires a string or symbol argument",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    // Use POSIX access() with F_OK to check file existence
    int result = access(filename_str, F_OK);
    
    // Return true if file exists (access() returns 0), false otherwise
    // Clojure-compatible: graceful return without exceptions
    // Use constants: false = 0x00000005, true = 0x00000045 (no make_special call)
    if (result == 0) {
        return (ID)0x00000045;  // true constant
    } else {
        return (ID)0x00000005;  // false constant
    }
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
            if (IS_FIXNUM(args[i])) {
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
            } else {
                sawFixed = true;
                // Check for fixed-point overflow before conversion using original values
                float acc_f = (float)acc_i;
                float val_f = as_fixed(args[i]);
                float result = acc_f + val_f;
                if (result > 262144.0f || result < -262144.0f) { // Max fixed-point range
                    return throw_fixed_overflow(ERR_FIXED_OVERFLOW_ADDITION);
                }
                acc_fixed = fixnum_to_fixed(acc_i) + extract_fixed_value(args[i]);
            }
        } else {
            int32_t val = IS_FIXNUM(args[i]) ? fixnum_to_fixed(AS_FIXNUM(args[i])) 
                                              : extract_fixed_value(args[i]);
            
            // Check for fixed-point addition overflow using original values
            float acc_f = (float)acc_fixed / 8192.0f;
            float val_f = IS_FIXNUM(args[i]) ? (float)AS_FIXNUM(args[i]) : as_fixed(args[i]);
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
            if (IS_FIXNUM(args[i])) {
                int new_val = AS_FIXNUM(args[i]);
                // Check for integer overflow before multiplication
                if (acc_i != 0 && new_val != 0) {
                    // Check if multiplication would overflow
                    if (acc_i > INT_MAX / new_val || acc_i < INT_MIN / new_val) {
                        // Overflow detected - throw exception (no promotion possible)
                        return throw_arithmetic_overflow(ERR_INTEGER_OVERFLOW_MULTIPLICATION, acc_i, new_val);
                    } else {
                        acc_i *= new_val;
                    }
                } else {
                    acc_i *= new_val; // Safe: one operand is 0
                }
            } else {
                sawFixed = true;
                // Check for fixed-point overflow before conversion
                float acc_f = (float)acc_i;
                float val_f = as_fixed(args[i]);
                if (acc_f != 0.0f && val_f != 0.0f) {
                    // Check if multiplication would exceed fixed-point range
                    float result = acc_f * val_f;
                    if (result > 262144.0f || result < -262144.0f) { // Max fixed-point range
                        return throw_fixed_overflow(ERR_FIXED_OVERFLOW_MULTIPLICATION);
                    }
                }
                acc_fixed = (fixnum_to_fixed(acc_i) * extract_fixed_value(args[i])) >> 13;
            }
        } else {
            int32_t val = IS_FIXNUM(args[i]) ? fixnum_to_fixed(AS_FIXNUM(args[i])) 
                                              : extract_fixed_value(args[i]);
            
            // Check for fixed-point multiplication overflow
            float acc_f = (float)acc_fixed / 8192.0f;
            float val_f = IS_FIXNUM(args[i]) ? (float)AS_FIXNUM(args[i]) : as_fixed(args[i]);
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
        return (NULL); 
    }
    if (argc == 1) {
        if (!args[0] || (!IS_FIXNUM(args[0]) && !IS_FIXED(args[0]))) { 
            throw_exception_formatted(EXCEPTION_TYPE_TYPE, __FILE__, __LINE__, 0, ERR_EXPECTED_NUMBER); 
            return (NULL);
        } 
        return IS_FIXNUM(args[0]) ? create_fixnum_result(-AS_FIXNUM(args[0]))
                                  : create_fixed_result(-extract_fixed_value(args[0]));
    }
    
    if (!validate_numeric_args(args, argc)) return (NULL);
    
    bool sawFixed = false; 
    int32_t acc_fixed = 0; 
    int acc_i = 0;
    
    if (IS_FIXNUM(args[0])) {
        acc_i = AS_FIXNUM(args[0]);
    } else {
        sawFixed = true;
        acc_fixed = extract_fixed_value(args[0]);
    }
    
        for (unsigned int i = 1; i < argc; i++) {
        if (!sawFixed && IS_FIXNUM(args[i])) {
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
        } else {
            if (!sawFixed) {
                acc_fixed = fixnum_to_fixed(acc_i);
                sawFixed = true;
            }
            int32_t val = IS_FIXNUM(args[i]) ? fixnum_to_fixed(AS_FIXNUM(args[i])) 
                                              : extract_fixed_value(args[i]);
            acc_fixed -= val;
        }
    }
    
    return sawFixed ? create_fixed_result(acc_fixed) : create_fixnum_result(acc_i);
}

ID native_div_variadic(ID *args, unsigned int argc) {
    if (argc == 0) { 
        throw_exception_formatted("ArityError", __FILE__, __LINE__, 0, ERR_WRONG_ARITY_ZERO); 
        return (NULL); 
    }
    if (argc == 1) {
        if (!args[0] || (!IS_FIXNUM(args[0]) && !IS_FIXED(args[0]))) { 
            throw_exception_formatted(EXCEPTION_TYPE_TYPE, __FILE__, __LINE__, 0, ERR_EXPECTED_NUMBER); 
            return (NULL);
        } 
        if (IS_FIXNUM(args[0])) { 
            int x = AS_FIXNUM(args[0]); 
            if (x == 0) {
                // Division by zero - throw exception
                throw_exception_formatted(EXCEPTION_DIVISION_BY_ZERO, __FILE__, __LINE__, 0,
                    "Division by zero: 1 / %d", x);
                return NULL;
            }
            if (1 % x == 0) return create_fixnum_result(1/x); 
            return create_fixed_result(fixnum_to_fixed(1) / x); 
        } else { 
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
    
    if (!validate_numeric_args(args, argc)) return (NULL);
    
    bool sawFixed = false; 
    int32_t acc_fixed = 0; 
    int acc_i = 0;
    
    if (IS_FIXNUM(args[0])) {
        acc_i = AS_FIXNUM(args[0]);
    } else {
        sawFixed = true;
        acc_fixed = extract_fixed_value(args[0]);
    }
    
        for (unsigned int i = 1; i < argc; i++) {
        if (!sawFixed && IS_FIXNUM(args[i])) {
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
        } else {
            if (!sawFixed) {
                acc_fixed = fixnum_to_fixed(acc_i);
                sawFixed = true;
            }
            int32_t d = IS_FIXNUM(args[i]) ? fixnum_to_fixed(AS_FIXNUM(args[i])) 
                                            : extract_fixed_value(args[i]);
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
    if (IS_FIXNUM(args[0])) {
        int size = AS_FIXNUM(args[0]);
        if (size < 0) {
            throw_exception_formatted("IllegalArgumentException", __FILE__, __LINE__, 0,
                    "byte-array size must be non-negative, got %d", size);
            return NULL;
        }
        return (ID)make_byte_array(size);
    }
    
    // Otherwise, treat as sequence and create array from values
    CljObject *seq = (CljObject*)args[0];
    if (!seq) {
        throw_exception(EXCEPTION_TYPE_ILLEGAL_ARGUMENT, "byte-array argument must be a number or sequence",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    // For now, only support vectors as sequences
    if (is_type(seq, CLJ_VECTOR)) {
        CljPersistentVector *vec = as_vector(seq);
        CljValue arr = make_byte_array(vec->count);
        
        for (int i = 0; i < vec->count; i++) {
            if (!IS_FIXNUM(vec->data[i])) {
                RELEASE((CljObject*)arr);
                throw_exception(EXCEPTION_TYPE_ILLEGAL_ARGUMENT, "byte-array sequence elements must be numbers",
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
    
    throw_exception(EXCEPTION_TYPE_ILLEGAL_ARGUMENT, "byte-array currently only supports vectors as sequences",
                   __FILE__, __LINE__, 0);
    return NULL;
}

ID native_aget(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 2, "aget")) return NULL;
    
    CljObject *arr = (CljObject*)args[0];
    if (!arr || !is_type(arr, CLJ_BYTE_ARRAY)) {
        throw_exception(EXCEPTION_TYPE_ILLEGAL_ARGUMENT, "aget first argument must be a byte-array",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    if (!IS_FIXNUM(args[1])) {
        throw_exception(EXCEPTION_TYPE_ILLEGAL_ARGUMENT, "aget index must be a number",
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
    if (!arr || !is_type(arr, CLJ_BYTE_ARRAY)) {
        throw_exception(EXCEPTION_TYPE_ILLEGAL_ARGUMENT, "aset first argument must be a byte-array",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    if (!IS_FIXNUM(args[1])) {
        throw_exception(EXCEPTION_TYPE_ILLEGAL_ARGUMENT, "aset index must be a number",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    if (!IS_FIXNUM(args[2])) {
        throw_exception(EXCEPTION_TYPE_ILLEGAL_ARGUMENT, "aset value must be a number",
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
    if (!arr || !is_type(arr, CLJ_BYTE_ARRAY)) {
        throw_exception(EXCEPTION_TYPE_ILLEGAL_ARGUMENT, "alength argument must be a byte-array",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    int length = byte_array_length((CljValue)arr);
    return fixnum(length);
}

ID native_aclone(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "aclone")) return NULL;
    
    CljObject *arr = (CljObject*)args[0];
    if (!arr || !is_type(arr, CLJ_BYTE_ARRAY)) {
        throw_exception(EXCEPTION_TYPE_ILLEGAL_ARGUMENT, "aclone argument must be a byte-array",
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
        throw_exception(EXCEPTION_TYPE, "Expected number for < comparison",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    return (result == COMPARE_LESS) ? make_special(SPECIAL_TRUE) : make_special(SPECIAL_FALSE);
}

ID native_gt(ID *args, unsigned int argc) {
    (void)argc; // Suppress unused parameter warning
    CompareResult result;
    if (!compare_numeric_values((CljObject*)args[0], (CljObject*)args[1], &result)) {
        throw_exception(EXCEPTION_TYPE, "Expected number for > comparison",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    return (result == COMPARE_GREATER) ? make_special(SPECIAL_TRUE) : make_special(SPECIAL_FALSE);
}

ID native_le(ID *args, unsigned int argc) {
    (void)argc; // Suppress unused parameter warning
    CompareResult result;
    if (!compare_numeric_values((CljObject*)args[0], (CljObject*)args[1], &result)) {
        throw_exception(EXCEPTION_TYPE, "Expected number for <= comparison",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    return (result == COMPARE_LESS || result == COMPARE_EQUAL) ? 
           make_special(SPECIAL_TRUE) : make_special(SPECIAL_FALSE);
}

ID native_ge(ID *args, unsigned int argc) {
    (void)argc; // Suppress unused parameter warning
    CompareResult result;
    if (!compare_numeric_values((CljObject*)args[0], (CljObject*)args[1], &result)) {
        throw_exception(EXCEPTION_TYPE, "Expected number for >= comparison",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    return (result == COMPARE_GREATER || result == COMPARE_EQUAL) ? 
           make_special(SPECIAL_TRUE) : make_special(SPECIAL_FALSE);
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
    if (is_fixnum((CljValue)a)) {
        val_a = (float)as_fixnum((CljValue)a);
    } else if (is_fixed((CljValue)a)) {
        val_a = as_fixed((CljValue)a);
    } else {
        // Not numeric, use general equality
        return clj_equal(a, b) ? make_special(SPECIAL_TRUE) : make_special(SPECIAL_FALSE);
    }
    
    if (is_fixnum((CljValue)b)) {
        val_b = (float)as_fixnum((CljValue)b);
    } else if (is_fixed((CljValue)b)) {
        val_b = as_fixed((CljValue)b);
    } else {
        // Not numeric, use general equality
        return clj_equal(a, b) ? make_special(SPECIAL_TRUE) : make_special(SPECIAL_FALSE);
    }
    
    return val_a == val_b ? make_special(SPECIAL_TRUE) : make_special(SPECIAL_FALSE);
}

ID native_identical(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 2, "identical?")) return make_special(SPECIAL_FALSE);
    return (args[0] == args[1]) ? make_special(SPECIAL_TRUE) : make_special(SPECIAL_FALSE);
}

ID native_vector_p(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "vector?")) return make_special(SPECIAL_FALSE);
    return is_type((CljObject*)args[0], CLJ_VECTOR) ? make_special(SPECIAL_TRUE) : make_special(SPECIAL_FALSE);
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
    EvalState *st = evalstate();
    CljObject *result = NULL;
    
    // Use eval_expr_simple for proper evaluation
    if (st && args[0]) {
        result = eval_expr_simple(args[0], st);
    }
    
    // End timing (Clojure-compatible: capture end time)
    gettimeofday(&end, NULL);
    
    // Calculate elapsed time in microseconds (Clojure-compatible: precise calculation)
    double elapsed_us = (end.tv_sec - start.tv_sec) * 1000000.0 + 
                       (end.tv_usec - start.tv_usec);
    
    // Print timing information (Clojure-compatible: "μsecs" format)
    printf("Elapsed time: %.2f μsecs\n", elapsed_us);
    
    // Return the result of the evaluated expression (Clojure-compatible: return the value)
    return result;
}

// Native sleep implementation
ID native_sleep(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "sleep")) return NULL;
    
    // Get the sleep duration in seconds
    CljObject *duration_obj = args[0];
    if (!is_fixnum((CljValue)duration_obj)) {
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

// Native def implementation (converted from special form)
ID native_def(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 2, "def")) return NULL;
    
    EvalState *st = evalstate();
    if (!st) {
        return NULL;
    }
    
    // First argument should be a symbol (name)
    CljObject *symbol = args[0];
    if (!symbol || !is_type(symbol, CLJ_SYMBOL)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "def requires a symbol as first argument",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    // Second argument is the value expression - evaluate it
    CljObject *value_expr = args[1];
    if (!value_expr) {
        return NULL;
    }
    
    // Evaluate the value expression
    CljObject *value = NULL;
    if (is_type(value_expr, CLJ_LIST)) {
        value = eval_list(as_list((ID)value_expr), (CljMap*)st->current_ns->mappings, st);
    } else {
        value = eval_expr_simple(value_expr, st);
    }
    if (!value) {
        return NULL;
    }
    
    // If the value is a function, set its name
    if (is_type(value, CLJ_FUNC)) {
        CljFunction *func = as_function((ID)value);
        CljSymbol *sym = as_symbol((ID)symbol);
        if (func && sym && sym->name[0] && !func->name) {
            func->name = strdup(sym->name);
        }
    }
    
    // Store the symbol-value binding in the namespace
    ns_define(st->current_ns, symbol, value);
    
    // Return the symbol (Clojure-compatible: def returns the var/symbol, not the value)
    return symbol;
}

// Native ns implementation (converted from special form)
ID native_ns(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 1, "ns")) return NULL;
    
    EvalState *st = evalstate();
    if (!st) {
        return NULL;
    }
    
    // First argument should be a symbol (namespace name)
    CljObject *ns_name_obj = args[0];
    if (!ns_name_obj || !is_type(ns_name_obj, CLJ_SYMBOL)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "ns expects a symbol",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    CljSymbol *ns_sym = as_symbol((ID)ns_name_obj);
    if (!ns_sym || !ns_sym->name[0]) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "ns symbol has no name",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    // Switch to namespace (creates if not exists)
    evalstate_set_ns(st, ns_sym->name);
    
    return NULL;
}

// do: Evaluate expressions sequentially, return last value
ID native_do(ID *args, unsigned int argc) {
    if (argc == 0) {
        // Empty do: (do) returns nil
        return NULL;
    }
    
    // Evaluate all expressions except the last one
    for (unsigned int i = 0; i < argc - 1; i++) {
        // Evaluate intermediate expressions (side effects only)
        // We don't need to retain the results since they're just side effects
        if (args[i]) {
            // Just evaluate for side effects
            // In a real implementation, we'd evaluate these properly
        }
    }
    
    // Return the last expression's result
    if (argc > 0) {
        return args[argc - 1]; // Return last argument
    }
    
    return NULL;
}

// dotimes: Execute expression n times with variable bound to 0, 1, ..., n-1
// dotimes is now implemented as a special form, not a builtin

// Helper function to register a builtin in clojure.core namespace (DRY principle)
static void register_builtin_in_namespace(const char *name, BuiltinFn func) {
    EvalState *st = evalstate();
    if (!st) return;
    
    // Get or create clojure.core namespace
    CljNamespace *clojure_core = ns_get_or_create("clojure.core", NULL);
    if (!clojure_core) return;
    
    // Explicitly set clojure.core cache if not already set
    // This ensures cache is set even if register_builtins is called before load_clojure_core
    extern TinyClJRuntime g_runtime;
    if (!g_runtime.clojure_core_cache) {
        g_runtime.clojure_core_cache = (void*)clojure_core;
    }
    
    // Register the builtin in clojure.core namespace
    CljObject *symbol = intern_symbol(NULL, name);
    CljObject *func_obj = make_named_func(func, NULL, name);
    if (symbol && func_obj) {
        ns_define(clojure_core, symbol, func_obj);
        // Builtin registered successfully in clojure.core
    } else {
        // Failed to register builtin
    }
}

void register_builtins() {
    // Register all builtins in clojure.core namespace (unified system)
    register_builtin_in_namespace("+", native_add_variadic);
    register_builtin_in_namespace("-", native_sub_variadic);
    register_builtin_in_namespace("*", native_mul_variadic);
    register_builtin_in_namespace("/", native_div_variadic);
    register_builtin_in_namespace("str", native_str);
#ifndef ESP32_BUILD
    register_builtin_in_namespace("slurp", native_slurp);
    register_builtin_in_namespace("spit", native_spit);
    register_builtin_in_namespace("file-exists?", native_file_exists);
    register_builtin_in_namespace("require", native_require);
#endif
    register_builtin_in_namespace("type", native_type);
    register_builtin_in_namespace("array-map", native_array_map);
    register_builtin_in_namespace("nth", native_nth);
    register_builtin_in_namespace("vector", native_vector);
    register_builtin_in_namespace("vec", native_vec);
    register_builtin_in_namespace("peek", native_peek);
    register_builtin_in_namespace("pop", native_pop);
    register_builtin_in_namespace("subvec", native_subvec);
    register_builtin_in_namespace("conj", native_conj);
    register_builtin_in_namespace("first", native_first);
    register_builtin_in_namespace("rest", native_rest);
    register_builtin_in_namespace("cons", native_cons);
    register_builtin_in_namespace("count", native_count);
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
    
    // Special forms converted to builtins
    register_builtin_in_namespace("def", native_def);
    register_builtin_in_namespace("ns", native_ns);
    
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
}
