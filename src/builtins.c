#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <sys/time.h>
#include "object.h"
#include "vector.h"
#include "map.h"
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
#include "exception.h"
#include "clj_strings.h"
#include "strings.h"
#include "parser.h"


ID nth2(ID *args, unsigned int argc) {
    if (argc != 2) return NULL;
    ID vec = args[0];
    ID idx = args[1];
    if (!vec || !idx || !is_type(vec, CLJ_VECTOR) || !IS_FIXNUM(idx)) return (NULL);
    int i = AS_FIXNUM(idx);
    CljPersistentVector *v = as_vector(vec);
    if (!v || i < 0 || i >= v->count) return (NULL);
    return (RETAIN(v->data[i]));
}

// Forward declaration
ID conj2(ID vec, ID val);

ID conj2_wrapper(ID *args, int argc) {
    if (argc != 2) return NULL;
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
    throw_exception(EXCEPTION_TYPE_ILLEGAL_ARGUMENT, 
                    "conj not supported on this type", 
                    __FILE__, __LINE__, 0);
    return (NULL);
}

// First function that works with BuiltinFn signature
ID native_first(ID *args, unsigned int argc) {
    CLJ_ASSERT(args != NULL);
    
    if (argc != 1) {
        throw_exception(EXCEPTION_TYPE_ARITY, 
                        "Wrong number of args passed to: first", 
                        __FILE__, __LINE__, 0);
        return NULL;
    }
    
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
    
    if (argc != 1) {
        throw_exception(EXCEPTION_TYPE_ARITY, 
                        "Wrong number of args (0) passed to: rest", 
                        __FILE__, __LINE__, 0);
        return (NULL);
    }
    
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
        return seq_rest(seq);
    }
    
    if (is_type(coll, CLJ_SEQ)) {
        // Already a sequence - just call seq_rest
        return seq_rest(coll);
    }
    
    // Throw exception for unsupported collection type
    throw_exception(EXCEPTION_TYPE_ILLEGAL_ARGUMENT, 
                    "rest not supported on this type", 
                    __FILE__, __LINE__, 0);
    return (NULL);
}

// Cons function that works with BuiltinFn signature
ID native_cons(ID *args, unsigned int argc) {
    CLJ_ASSERT(args != NULL);
    
    if (argc != 2) {
        throw_exception("ArityException",
                        "Wrong number of args passed to: cons",
                        __FILE__, __LINE__, 0);
        return NULL;
    }
    
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
    if (argc != 3) return (NULL);
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
    if (argc != 1) return (NULL);
    
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
    throw_exception(EXCEPTION_TYPE_ILLEGAL_ARGUMENT, 
                    "transient requires a persistent collection at position 1", 
                    __FILE__, __LINE__, 0);
    return (NULL);
}

ID native_persistent(ID *args, unsigned int argc) {
    if (argc != 1) return (NULL);
    
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
    throw_exception(EXCEPTION_TYPE_ILLEGAL_ARGUMENT, 
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
    throw_exception(EXCEPTION_TYPE_ILLEGAL_ARGUMENT, 
                    "conj! requires a transient collection at position 1", 
                    __FILE__, __LINE__, 0);
    return NULL;
}

ID native_get(ID *args, unsigned int argc) {
    if (argc != 2) return NULL;
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
    
    if (argc != 1) return (NULL);
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

ID native_keys(ID *args, unsigned int argc) {
    if (argc != 1) return (NULL);
    CljObject *map = (CljObject*)args[0];
    if (!map) return (NULL);
    
    if (is_type(map, CLJ_MAP) || is_type(map, CLJ_TRANSIENT_MAP)) {
        return ((CljObject*)map_keys((CljValue)map));
    }
    
    return (NULL); // Return nil for unsupported types
}

ID native_vals(ID *args, unsigned int argc) {
    if (argc != 1) return (NULL);
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
    if (argc != 1) return (NULL);
    CljObject *obj = (CljObject*)args[0];
    if (!obj) return ((CljObject*)intern_symbol_global("nil"));
    
    // Check for keyword (symbol with ':' prefix)
    if (is_type(obj, CLJ_SYMBOL)) {
        CljSymbol *sym = as_symbol(obj);
        if (sym && sym->name[0] == ':') {
            return (CljObject*)intern_symbol_global("Keyword");
        }
    }
    
    // Return type name for other types
    switch (obj->type) {
        case CLJ_SYMBOL:
            return (CljObject*)intern_symbol_global("Symbol");
        // CLJ_INT, CLJ_FLOAT, CLJ_BOOL removed - handled as immediates
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

// Legacy builtin table and apply_builtin removed - all builtins now use namespace registration

// Arithmetic functions - native_*_variadic implement operations directly (no wrappers)

ID native_println(ID *args, unsigned int argc) {
    if (argc < 1) return (NULL);
    if (args[0]) {
        char *str = pr_str((CljObject*)args[0]);
        printf("%s\n", str);
        free(str);
    }
    return (NULL);
}

// ============================================================================
// HELPER FUNCTIONS (DRY Principle)
// ============================================================================

// Helper function to validate numeric arguments
static bool validate_numeric_args(ID *args, int argc) {
    for (unsigned int i = 0; i < argc; i++) {
        if (!args[i] || (!IS_FIXNUM(args[i]) && !IS_FIXED(args[i]))) {
            throw_exception_formatted(EXCEPTION_TYPE_TYPE, __FILE__, __LINE__, 0, ERR_EXPECTED_NUMBER);
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
        return (make_string_impl(""));
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
    if (!buffer) return make_string_impl("");
    buffer[0] = '\0';
    
    // Concatenate all strings
    for (unsigned int i = 0; i < argc; i++) {
        char *s = to_string(args[i]);
        if (s) {
            strcat(buffer, s);
            free(s);
        }
    }
    
    CljObject *result = make_string_impl(buffer);
    free(buffer);
    return result;
}

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
                throw_exception_formatted(EXCEPTION_TYPE_DIVISION_BY_ZERO, __FILE__, __LINE__, 0, 
                    "Division by zero: 1 / %d", x);
                return NULL;
            }
            if (1 % x == 0) return create_fixnum_result(1/x); 
            return create_fixed_result(fixnum_to_fixed(1) / x); 
        } else { 
            int32_t x = extract_fixed_value(args[0]);
            if (x == 0) {
                // Division by zero - throw exception
                throw_exception_formatted(EXCEPTION_TYPE_DIVISION_BY_ZERO, __FILE__, __LINE__, 0, 
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
                throw_exception_formatted(EXCEPTION_TYPE_DIVISION_BY_ZERO, __FILE__, __LINE__, 0, 
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
                throw_exception_formatted(EXCEPTION_TYPE_DIVISION_BY_ZERO, __FILE__, __LINE__, 0, 
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
    if (argc != 1) {
        throw_exception(EXCEPTION_TYPE_ILLEGAL_ARGUMENT, "byte-array requires exactly 1 argument",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
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
    if (argc != 2) {
        throw_exception(EXCEPTION_TYPE_ILLEGAL_ARGUMENT, "aget requires exactly 2 arguments",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
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
    if (argc != 3) {
        throw_exception(EXCEPTION_TYPE_ILLEGAL_ARGUMENT, "aset requires exactly 3 arguments",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
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
    if (argc != 1) {
        throw_exception(EXCEPTION_TYPE_ILLEGAL_ARGUMENT, "alength requires exactly 1 argument",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
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
    if (argc != 1) {
        throw_exception(EXCEPTION_TYPE_ILLEGAL_ARGUMENT, "aclone requires exactly 1 argument",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
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
    CompareResult result;
    if (!compare_numeric_values((CljObject*)args[0], (CljObject*)args[1], &result)) {
        throw_exception("TypeError", "Expected number for < comparison",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    return (result == COMPARE_LESS) ? make_special(SPECIAL_TRUE) : make_special(SPECIAL_FALSE);
}

ID native_gt(ID *args, unsigned int argc) {
    CompareResult result;
    if (!compare_numeric_values((CljObject*)args[0], (CljObject*)args[1], &result)) {
        throw_exception("TypeError", "Expected number for > comparison",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    return (result == COMPARE_GREATER) ? make_special(SPECIAL_TRUE) : make_special(SPECIAL_FALSE);
}

ID native_le(ID *args, unsigned int argc) {
    CompareResult result;
    if (!compare_numeric_values((CljObject*)args[0], (CljObject*)args[1], &result)) {
        throw_exception("TypeError", "Expected number for <= comparison",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    return (result == COMPARE_LESS || result == COMPARE_EQUAL) ? 
           make_special(SPECIAL_TRUE) : make_special(SPECIAL_FALSE);
}

ID native_ge(ID *args, unsigned int argc) {
    CompareResult result;
    if (!compare_numeric_values((CljObject*)args[0], (CljObject*)args[1], &result)) {
        throw_exception("TypeError", "Expected number for >= comparison",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    return (result == COMPARE_GREATER || result == COMPARE_EQUAL) ? 
           make_special(SPECIAL_TRUE) : make_special(SPECIAL_FALSE);
}

ID native_eq(ID *args, unsigned int argc) {
    if (argc != 2) {
        throw_exception(EXCEPTION_TYPE_ILLEGAL_ARGUMENT, "= requires exactly 2 arguments",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    CljObject *a = (CljObject*)args[0];
    CljObject *b = (CljObject*)args[1];
    
    if (!a || !b) {
        throw_exception(EXCEPTION_TYPE_ILLEGAL_ARGUMENT, "= arguments cannot be null",
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

ID native_time(ID *args, unsigned int argc) {
    if (argc != 1) {
        throw_exception(EXCEPTION_TYPE_ILLEGAL_ARGUMENT, "time requires exactly 1 argument",
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
    
    // Calculate elapsed time in milliseconds (Clojure-compatible: precise calculation)
    double elapsed_ms = (end.tv_sec - start.tv_sec) * 1000.0 + 
                       (end.tv_usec - start.tv_usec) / 1000.0;
    
    // Print timing information (Clojure-compatible: "msecs" format)
    printf("Elapsed time: %.2f msecs\n", elapsed_ms);
    
    // Return the result of the evaluated expression (Clojure-compatible: return the value)
    return result;
}

// Native time-micro implementation with microsecond resolution
ID native_time_micro(ID *args, unsigned int argc) {
    if (argc != 1) {
        throw_exception(EXCEPTION_TYPE_ILLEGAL_ARGUMENT, "time-micro requires exactly 1 argument",
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
    if (argc != 1) {
        throw_exception(EXCEPTION_TYPE_ILLEGAL_ARGUMENT, "sleep requires exactly 1 argument",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    // Get the sleep duration in seconds
    CljObject *duration_obj = args[0];
    if (!is_fixnum((CljValue)duration_obj)) {
        throw_exception(EXCEPTION_TYPE_ILLEGAL_ARGUMENT, "sleep duration must be a number",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    // Convert to seconds (assuming the number is in seconds)
    int duration = as_fixnum((CljValue)duration_obj);
    
    // Simple busy wait for testing (not a real sleep)
    for (int i = 0; i < duration * 1000000; i++) {
        // Busy wait - this will definitely take time
        volatile int dummy = i;
        (void)dummy; // Suppress unused variable warning
    }
    
    // Return nil (but not NULL to avoid assertion failure)
    return NULL;
}

// Native def implementation (converted from special form)
ID native_def(ID *args, unsigned int argc) {
    if (argc != 2) {
        throw_exception(EXCEPTION_TYPE_ILLEGAL_ARGUMENT, "def requires exactly 2 arguments",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    EvalState *st = evalstate();
    if (!st) {
        return NULL;
    }
    
    // First argument should be a symbol (name)
    CljObject *symbol = args[0];
    if (!symbol || !is_type(symbol, CLJ_SYMBOL)) {
        throw_exception(EXCEPTION_TYPE_ILLEGAL_ARGUMENT, "def requires a symbol as first argument",
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
        value = eval_list(as_list((ID)value_expr), st->current_ns->mappings, st);
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
    if (argc != 1) {
        throw_exception(EXCEPTION_TYPE_ILLEGAL_ARGUMENT, "ns requires exactly 1 argument",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    EvalState *st = evalstate();
    if (!st) {
        return NULL;
    }
    
    // First argument should be a symbol (namespace name)
    CljObject *ns_name_obj = args[0];
    if (!ns_name_obj || !is_type(ns_name_obj, CLJ_SYMBOL)) {
        throw_exception(EXCEPTION_TYPE_ILLEGAL_ARGUMENT, "ns expects a symbol",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    CljSymbol *ns_sym = as_symbol((ID)ns_name_obj);
    if (!ns_sym || !ns_sym->name[0]) {
        throw_exception(EXCEPTION_TYPE_ILLEGAL_ARGUMENT, "ns symbol has no name",
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

// Helper function to register a builtin in clojure.core namespace (DRY principle)
static void register_builtin_in_namespace(const char *name, BuiltinFn func) {
    EvalState *st = evalstate();
    if (!st) return;
    
    // Get or create clojure.core namespace
    CljNamespace *clojure_core = ns_get_or_create("clojure.core", NULL);
    if (!clojure_core) return;
    
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
    register_builtin_in_namespace("type", native_type);
    register_builtin_in_namespace("array-map", native_array_map);
    register_builtin_in_namespace("nth", nth2);
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
    
    // Register comparison operators as normal functions
    register_builtin_in_namespace("<", native_lt);
    register_builtin_in_namespace(">", native_gt);
    register_builtin_in_namespace("<=", native_le);
    register_builtin_in_namespace(">=", native_ge);
    register_builtin_in_namespace("=", native_eq);
    
    // Time function
    register_builtin_in_namespace("time", native_time);
    register_builtin_in_namespace("time-micro", native_time_micro);
    register_builtin_in_namespace("sleep", native_sleep);
    
    // Special forms converted to builtins
    register_builtin_in_namespace("def", native_def);
    register_builtin_in_namespace("ns", native_ns);
    
    // Control flow functions
    register_builtin_in_namespace("do", native_do);
    
    // Byte array functions
    register_builtin_in_namespace("byte-array", native_byte_array);
    register_builtin_in_namespace("aget", native_aget);
    register_builtin_in_namespace("aset", native_aset);
    register_builtin_in_namespace("alength", native_alength);
    register_builtin_in_namespace("aclone", native_aclone);
}
