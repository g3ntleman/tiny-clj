#include <stdlib.h>
#include <string.h>
#include "object.h"
#include "vector.h"
#include "map.h"
#include "builtins.h"
#include "runtime.h"
#include "memory.h"
#include "namespace.h"
#include "clj_string.h"
#include "value.h"
#include "error_messages.h"
#include "seq.h"

ID nth2(ID *args, int argc) {
    if (argc != 2) return OBJ_TO_ID(NULL);
    CljObject *vec = ID_TO_OBJ(args[0]);
    CljObject *idx = ID_TO_OBJ(args[1]);
    if (!vec || !idx || vec->type != CLJ_VECTOR || !IS_FIXNUM(idx)) return OBJ_TO_ID(NULL);
    int i = AS_FIXNUM(idx);
    CljPersistentVector *v = as_vector(vec);
    if (!v || i < 0 || i >= v->count) return OBJ_TO_ID(NULL);
    return OBJ_TO_ID(RETAIN(v->data[i]));
}

CljObject* conj2(CljObject *vec, CljObject *val) {
    if (!vec || vec->type != CLJ_VECTOR) return NULL;
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
        return RETAIN(vec);
    } else {
        int need = v->count + 1;
        int newcap = v->capacity;
        if (need > newcap) newcap = newcap > 0 ? newcap * 2 : 1;
        CljValue copy_val = make_vector_v(newcap, 0);
        CljObject *copy = (CljObject*)copy_val;
        if (!copy) return NULL;
        CljPersistentVector *c = as_vector(copy);
        for (int i = 0; i < v->count; ++i) {
            c->data[i] = (RETAIN(v->data[i]), v->data[i]);
        }
        c->count = v->count;
        c->data[c->count++] = (RETAIN(val), val);
        return copy;
    }
}

// Generic conj function that works with BuiltinFn signature
ID native_conj(ID *args, int argc) {
    if (argc != 2) return OBJ_TO_ID(NULL);
    CljObject *coll = ID_TO_OBJ(args[0]);
    CljObject *val = ID_TO_OBJ(args[1]);
    if (!coll || !val) return OBJ_TO_ID(NULL);
    
    if (coll->type == CLJ_VECTOR) {
        return OBJ_TO_ID(conj2(coll, val));
    }
    
    return OBJ_TO_ID(NULL); // Unsupported collection type
}

// Rest function that works with BuiltinFn signature
ID native_rest(ID *args, int argc) {
    if (argc != 1) return OBJ_TO_ID(NULL);
    CljObject *coll = ID_TO_OBJ(args[0]);
    if (!coll) return OBJ_TO_ID(NULL);
    
    if (coll->type == CLJ_VECTOR) {
        CljPersistentVector *v = as_vector(coll);
        if (!v || v->count <= 1) {
            return OBJ_TO_ID(make_list(NULL, NULL));
        }
        
        // Use CljSeqIterator (existing!) instead of copying
        CljObject *seq = seq_create(coll);
        if (!seq) return OBJ_TO_ID(make_list(NULL, NULL));
        
        // Return rest of sequence (O(1) operation!)
        return OBJ_TO_ID(seq_rest(seq));
    }
    
    if (coll->type == CLJ_SEQ) {
        // Already a sequence - just call seq_rest
        return OBJ_TO_ID(seq_rest(coll));
    }
    
    return OBJ_TO_ID(NULL); // Unsupported collection type
}

ID assoc3(ID *args, int argc) {
    if (argc != 3) return OBJ_TO_ID(NULL);
    CljObject *vec = ID_TO_OBJ(args[0]);
    CljObject *idx = ID_TO_OBJ(args[1]);
    CljObject *val = ID_TO_OBJ(args[2]);
    if (!vec || vec->type != CLJ_VECTOR || !idx || !IS_FIXNUM(idx)) return OBJ_TO_ID(NULL);
    int i = AS_FIXNUM(idx);
    CljPersistentVector *v = as_vector(vec);
    if (!v || i < 0 || i >= v->count) return OBJ_TO_ID(NULL);
    int is_mutable = v->mutable_flag;
    if (is_mutable) {
        RELEASE(v->data[i]);
        v->data[i] = (RETAIN(val), val);
        return OBJ_TO_ID(RETAIN(vec));
    } else {
        CljValue copy_val = make_vector_v(v->capacity, 0);
        CljObject *copy = (CljObject*)copy_val;
        if (!copy) return OBJ_TO_ID(NULL);
        CljPersistentVector *c = as_vector(copy);
        for (int j = 0; j < v->count; ++j) {
            c->data[j] = (RETAIN(v->data[j]), v->data[j]);
        }
        c->count = v->count;
        RELEASE(c->data[i]);
        c->data[i] = (RETAIN(val), val);
        return OBJ_TO_ID(copy);
    }
}

// Transient functions
ID native_transient(ID *args, int argc) {
    if (argc != 1) return OBJ_TO_ID(NULL);
    
    CljObject *coll = ID_TO_OBJ(args[0]);
    if (!coll) return OBJ_TO_ID(NULL);
    
    if (coll->type == CLJ_VECTOR) {
        return OBJ_TO_ID((CljObject*)transient((CljValue)coll));
    } else if (coll->type == CLJ_MAP) {
        return OBJ_TO_ID((CljObject*)transient_map((CljValue)coll));
    } else if (coll->type == CLJ_TRANSIENT_VECTOR || coll->type == CLJ_TRANSIENT_MAP) {
        // Clojure-compatible: transient on transient returns the same object
        return OBJ_TO_ID(coll);
    }
    
    // Throw exception for unsupported collection type (Clojure-compatible)
    throw_exception("IllegalArgumentException", 
                    "transient requires a persistent collection at position 1", 
                    __FILE__, __LINE__, 0);
    return OBJ_TO_ID(NULL);
}

ID native_persistent(ID *args, int argc) {
    if (argc != 1) return OBJ_TO_ID(NULL);
    
    CljObject *coll = ID_TO_OBJ(args[0]);
    if (!coll) return OBJ_TO_ID(NULL);
    
    if (coll->type == CLJ_TRANSIENT_VECTOR) {
        return OBJ_TO_ID((CljObject*)persistent_v((CljValue)coll));
    } else if (coll->type == CLJ_TRANSIENT_MAP) {
        return OBJ_TO_ID((CljObject*)persistent_map_v((CljValue)coll));
    } else if (coll->type == CLJ_VECTOR || coll->type == CLJ_MAP) {
        // Clojure-compatible: persistent! on persistent returns the same object
        return OBJ_TO_ID(coll);
    }
    
    // Throw exception for unsupported collection type (Clojure-compatible)
    throw_exception("IllegalArgumentException", 
                    "persistent! requires a transient collection at position 1", 
                    __FILE__, __LINE__, 0);
    return OBJ_TO_ID(NULL);
}

ID native_conj_bang(ID *args, int argc) {
    if (argc < 2) return OBJ_TO_ID(NULL);
    
    CljObject *coll = args[0];
    if (!coll) return NULL;
    
    
    if (coll->type == CLJ_TRANSIENT_VECTOR) {
        CljValue result = (CljValue)coll;
        for (int i = 1; i < argc; i++) {
            result = conj_v(result, (CljValue)args[i]);
            if (!result) return NULL;
        }
        return (CljObject*)result;
    } else if (coll->type == CLJ_TRANSIENT_MAP) {
        if (argc != 3) return NULL; // conj! for maps needs key-value pair
        return (CljObject*)conj_map_v((CljValue)coll, (CljValue)args[1], (CljValue)args[2]);
    }
    
    // Throw exception for unsupported collection type (Clojure-compatible)
    throw_exception("IllegalArgumentException", 
                    "conj! requires a transient collection at position 1", 
                    __FILE__, __LINE__, 0);
    return NULL;
}

ID native_get(ID *args, int argc) {
    if (argc != 2) return OBJ_TO_ID(NULL);
    CljObject *map = ID_TO_OBJ(args[0]);
    CljObject *key = ID_TO_OBJ(args[1]);
    if (!map || !key) return OBJ_TO_ID(NULL);
    
    if (map->type == CLJ_MAP || map->type == CLJ_TRANSIENT_MAP) {
        return OBJ_TO_ID((CljObject*)map_get_v((CljValue)map, (CljValue)key));
    }
    
    return OBJ_TO_ID(NULL); // Return nil for unsupported types
}

ID native_count(ID *args, int argc) {
    if (argc != 1) return OBJ_TO_ID(NULL);
    CljObject *coll = ID_TO_OBJ(args[0]);
    if (!coll) return OBJ_TO_ID(NULL);
    
    if (coll->type == CLJ_MAP || coll->type == CLJ_TRANSIENT_MAP) {
        return OBJ_TO_ID((CljObject*)make_fixnum(map_count_v((CljValue)coll)));
    } else if (coll->type == CLJ_VECTOR || coll->type == CLJ_TRANSIENT_VECTOR) {
        CljPersistentVector *vec = as_vector(coll);
        return OBJ_TO_ID((CljObject*)make_fixnum(vec ? vec->count : 0));
    }
    
    return OBJ_TO_ID((CljObject*)make_fixnum(0)); // Default count for unsupported types
}

ID native_keys(ID *args, int argc) {
    if (argc != 1) return OBJ_TO_ID(NULL);
    CljObject *map = ID_TO_OBJ(args[0]);
    if (!map) return OBJ_TO_ID(NULL);
    
    if (map->type == CLJ_MAP || map->type == CLJ_TRANSIENT_MAP) {
        return OBJ_TO_ID((CljObject*)map_keys_v((CljValue)map));
    }
    
    return OBJ_TO_ID(NULL); // Return nil for unsupported types
}

ID native_vals(ID *args, int argc) {
    if (argc != 1) return OBJ_TO_ID(NULL);
    CljObject *map = ID_TO_OBJ(args[0]);
    if (!map) return OBJ_TO_ID(NULL);
    
    if (map->type == CLJ_MAP || map->type == CLJ_TRANSIENT_MAP) {
        return OBJ_TO_ID((CljObject*)map_vals_v((CljValue)map));
    }
    
    return OBJ_TO_ID(NULL); // Return nil for unsupported types
}

ID native_if(ID *args, int argc) {
    if (argc < 2) return OBJ_TO_ID(NULL);
    CljObject *cond = ID_TO_OBJ(args[0]);
    if (clj_is_truthy(cond)) {
        return OBJ_TO_ID(RETAIN(ID_TO_OBJ(args[1])));
    } else if (argc > 2) {
        return OBJ_TO_ID(RETAIN(ID_TO_OBJ(args[2])));
    } else {
        return OBJ_TO_ID(NULL);
    }
}

ID native_type(ID *args, int argc) {
    if (argc != 1) return OBJ_TO_ID(NULL);
    CljObject *obj = ID_TO_OBJ(args[0]);
    if (!obj) return OBJ_TO_ID((CljObject*)intern_symbol_global("nil"));
    
    // Check for keyword (symbol with ':' prefix)
    if (obj->type == CLJ_SYMBOL) {
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
        case CLJ_NIL:
            return (CljObject*)intern_symbol_global("nil");
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

ID native_array_map(ID *args, int argc) {
    // Must have even number of arguments (key-value pairs)
    if (argc % 2 != 0) {
        // Return empty map instead of nil for odd number of args
        return OBJ_TO_ID((CljObject*)make_map_v(0));
    }
    
    // Create map with appropriate capacity
    int pair_count = argc / 2;
    
    // Handle empty map case specially
    if (pair_count == 0) {
        return OBJ_TO_ID((CljObject*)make_map_v(0));
    }
    
    CljMap *map = (CljMap*)make_map_v(pair_count);
    if (!map) {
        // Return empty map instead of nil on allocation failure
        return OBJ_TO_ID((CljObject*)make_map_v(0));
    }
    
    // Add all key-value pairs
    for (int i = 0; i < argc; i += 2) {
        CljObject *key = ID_TO_OBJ(args[i]);
        CljObject *value = ID_TO_OBJ(args[i + 1]);
        map_assoc_v((CljValue)map, (CljValue)key, (CljValue)value);
    }
    
    return OBJ_TO_ID((CljObject*)map);
}

CljObject* make_func(BuiltinFn fn, void *env) {
    return make_named_func(fn, env, NULL);
}

CljObject* make_named_func(BuiltinFn fn, void *env, const char *name) {
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
    
    return (CljObject*)func;
}

static const BuiltinEntry builtins[] = {
    {"nth", FN_GENERIC, .u.generic = nth2},
    {"conj", FN_ARITY2, .u.fn2 = conj2},
    {"assoc", FN_GENERIC, .u.generic = assoc3},
    {"array-map", FN_GENERIC, .u.generic = native_array_map},
    {"transient", FN_GENERIC, .u.generic = native_transient},
    {"persistent!", FN_GENERIC, .u.generic = native_persistent},
    {"conj!", FN_GENERIC, .u.generic = native_conj_bang},
    {"if", FN_GENERIC, .u.generic = native_if},
    {"type", FN_GENERIC, .u.generic = native_type},
    {"+", FN_GENERIC, .u.generic = native_add},
    {"-", FN_GENERIC, .u.generic = native_sub},
    {"*", FN_GENERIC, .u.generic = native_mul},
    {"/", FN_GENERIC, .u.generic = native_div},
    {"str", FN_GENERIC, .u.generic = native_str},
};

ID apply_builtin(const BuiltinEntry *entry, ID *args, int argc) {
    switch (entry->kind) {
        case FN_ARITY1:
            if (argc == 1) return entry->u.fn1(args[0]);
            break;
        case FN_ARITY2:
            if (argc == 2) return entry->u.fn2(args[0], args[1]);
            break;
        case FN_ARITY3:
            if (argc == 3) return entry->u.fn3(args[0], args[1], args[2]);
            break;
        case FN_GENERIC:
            return entry->u.generic(args, argc);
    }
    return NULL;
}

// Wrapper functions for arithmetic operations (delegate to variadic versions)
ID native_add(ID *args, int argc) { return native_add_variadic(args, argc); }

ID native_sub(ID *args, int argc) { return native_sub_variadic(args, argc); }

ID native_mul(ID *args, int argc) { return native_mul_variadic(args, argc); }

ID native_div(ID *args, int argc) { return native_div_variadic(args, argc); }

ID native_println(ID *args, int argc) {
    if (argc < 1) return OBJ_TO_ID(NULL);
    if (args[0]) {
        char *str = pr_str(ID_TO_OBJ(args[0]));
        printf("%s\n", str);
        free(str);
    }
    return OBJ_TO_ID(NULL);
}

// ============================================================================
// VARIADIC FUNCTIONS (Phase 1)
// ============================================================================

// String concatenation (variadic)
ID native_str(ID *args, int argc) {
    if (argc == 0) {
        return OBJ_TO_ID(make_string(""));
    }
    
    // Calculate total length
    size_t total_len = 0;
    for (int i = 0; i < argc; i++) {
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
    for (int i = 0; i < argc; i++) {
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

// Binary operations (inline for performance)
// Variadische Number-Reducer mit Single-Pass und Float-Promotion
static ID reduce_add(ID *args, int argc) {
    if (argc == 0) return OBJ_TO_ID((CljObject*)make_fixnum(0));
    if (argc == 1) return OBJ_TO_ID(RETAIN(ID_TO_OBJ(args[0])));
    bool sawFloat = false; int acc_i = 0; float acc_f = 0.0f;
    for (int i = 0; i < argc; i++) {
        if (!args[i] || (!IS_FIXNUM(args[i]) && !IS_FLOAT16(args[i]))) {
            throw_exception_formatted("TypeError", __FILE__, __LINE__, 0, ERR_EXPECTED_NUMBER);
            return OBJ_TO_ID(NULL);
        }
        if (!sawFloat) {
            if (IS_FIXNUM(args[i])) acc_i += AS_FIXNUM(args[i]);
            else { sawFloat = true; acc_f = (float)acc_i + AS_FLOAT16(args[i]); }
        } else {
            acc_f += IS_FIXNUM(args[i]) ? (float)AS_FIXNUM(args[i]) : AS_FLOAT16(args[i]);
        }
    }
    return sawFloat ? OBJ_TO_ID((CljObject*)make_float16(acc_f))
                    : OBJ_TO_ID((CljObject*)make_fixnum(acc_i));
}

static ID reduce_mul(ID *args, int argc) {
    if (argc == 0) return OBJ_TO_ID((CljObject*)make_fixnum(1));
    if (argc == 1) return OBJ_TO_ID(RETAIN(ID_TO_OBJ(args[0])));
    bool sawFloat = false; int acc_i = 1; float acc_f = 1.0f;
    for (int i = 0; i < argc; i++) {
        if (!args[i] || (!IS_FIXNUM(args[i]) && !IS_FLOAT16(args[i]))) {
            throw_exception_formatted("TypeError", __FILE__, __LINE__, 0, ERR_EXPECTED_NUMBER);
            return OBJ_TO_ID(NULL);
        }
        if (!sawFloat) {
            if (IS_FIXNUM(args[i])) acc_i *= AS_FIXNUM(args[i]);
            else { sawFloat = true; acc_f = (float)acc_i * AS_FLOAT16(args[i]); }
        } else {
            acc_f *= IS_FIXNUM(args[i]) ? (float)AS_FIXNUM(args[i]) : AS_FLOAT16(args[i]);
        }
    }
    return sawFloat ? OBJ_TO_ID((CljObject*)make_float16(acc_f))
                    : OBJ_TO_ID((CljObject*)make_fixnum(acc_i));
}

static ID reduce_sub(ID *args, int argc) {
    if (argc == 0) { throw_exception_formatted("ArityError", __FILE__, __LINE__, 0, ERR_WRONG_ARITY_ZERO); return OBJ_TO_ID(NULL); }
    if (argc == 1) {
        if (!args[0] || (!IS_FIXNUM(args[0]) && !IS_FLOAT16(args[0]))) { throw_exception_formatted("TypeError", __FILE__, __LINE__, 0, ERR_EXPECTED_NUMBER); return OBJ_TO_ID(NULL);} 
        return IS_FIXNUM(args[0]) ? OBJ_TO_ID((CljObject*)make_fixnum(-AS_FIXNUM(args[0])))
                                  : OBJ_TO_ID((CljObject*)make_float16(-AS_FLOAT16(args[0])));
    }
    bool sawFloat = false; float acc_f = 0.0f; int acc_i = 0;
    if (!args[0] || (!IS_FIXNUM(args[0]) && !IS_FLOAT16(args[0]))) { throw_exception_formatted("TypeError", __FILE__, __LINE__, 0, ERR_EXPECTED_NUMBER); return OBJ_TO_ID(NULL);} 
    if (IS_FIXNUM(args[0])) acc_i = AS_FIXNUM(args[0]); else { sawFloat = true; acc_f = AS_FLOAT16(args[0]); }
    for (int i = 1; i < argc; i++) {
        if (!args[i] || (!IS_FIXNUM(args[i]) && !IS_FLOAT16(args[i]))) { throw_exception_formatted("TypeError", __FILE__, __LINE__, 0, ERR_EXPECTED_NUMBER); return OBJ_TO_ID(NULL);} 
        if (!sawFloat && IS_FIXNUM(args[i])) acc_i -= AS_FIXNUM(args[i]);
        else { if (!sawFloat) { acc_f = (float)acc_i; sawFloat = true; } acc_f -= IS_FIXNUM(args[i]) ? (float)AS_FIXNUM(args[i]) : AS_FLOAT16(args[i]); }
    }
    return sawFloat ? OBJ_TO_ID((CljObject*)make_float16(acc_f))
                    : OBJ_TO_ID((CljObject*)make_fixnum(acc_i));
}

static ID reduce_div(ID *args, int argc) {
    if (argc == 0) { throw_exception_formatted("ArityError", __FILE__, __LINE__, 0, ERR_WRONG_ARITY_ZERO); return OBJ_TO_ID(NULL); }
    if (argc == 1) {
        if (!args[0] || (!IS_FIXNUM(args[0]) && !IS_FLOAT16(args[0]))) { throw_exception_formatted("TypeError", __FILE__, __LINE__, 0, ERR_EXPECTED_NUMBER); return OBJ_TO_ID(NULL);} 
        if (IS_FIXNUM(args[0])) { int x = AS_FIXNUM(args[0]); if (x == 0) return OBJ_TO_ID((CljObject*)make_float16(1.0f/0.0f)); if (1 % x == 0) return OBJ_TO_ID((CljObject*)make_fixnum(1/x)); return OBJ_TO_ID((CljObject*)make_float16(1.0f/(float)x)); }
        else { return OBJ_TO_ID((CljObject*)make_float16(1.0f/AS_FLOAT16(args[0]))); }
    }
    if (!args[0] || (!IS_FIXNUM(args[0]) && !IS_FLOAT16(args[0]))) { throw_exception_formatted("TypeError", __FILE__, __LINE__, 0, ERR_EXPECTED_NUMBER); return OBJ_TO_ID(NULL);} 
    bool sawFloat = false; int acc_i = 0; float acc_f = 0.0f;
    if (IS_FIXNUM(args[0])) acc_i = AS_FIXNUM(args[0]); else { sawFloat = true; acc_f = AS_FLOAT16(args[0]); }
    for (int i = 1; i < argc; i++) {
        if (!args[i] || (!IS_FIXNUM(args[i]) && !IS_FLOAT16(args[i]))) { throw_exception_formatted("TypeError", __FILE__, __LINE__, 0, ERR_EXPECTED_NUMBER); return OBJ_TO_ID(NULL);} 
        if (!sawFloat && IS_FIXNUM(args[i])) { int d = AS_FIXNUM(args[i]); if (d == 0) { sawFloat = true; acc_f = (float)acc_i/0.0f; continue; } if (acc_i % d == 0) acc_i /= d; else { sawFloat = true; acc_f = (float)acc_i/(float)d; } }
        else { if (!sawFloat) { acc_f = (float)acc_i; sawFloat = true; } float d = IS_FIXNUM(args[i]) ? (float)AS_FIXNUM(args[i]) : AS_FLOAT16(args[i]); acc_f /= d; }
    }
    return sawFloat ? OBJ_TO_ID((CljObject*)make_float16(acc_f))
                    : OBJ_TO_ID((CljObject*)make_fixnum(acc_i));
}

// Public API (variadisch)
ID native_add_variadic(ID *args, int argc) { return reduce_add(args, argc); }
ID native_mul_variadic(ID *args, int argc) { return reduce_mul(args, argc); }
ID native_sub_variadic(ID *args, int argc) { return reduce_sub(args, argc); }
ID native_div_variadic(ID *args, int argc) { return reduce_div(args, argc); }

// Helper function to register a builtin in current namespace (DRY principle)
static void register_builtin_in_namespace(const char *name, BuiltinFn func) {
    EvalState *st = evalstate();
    if (!st) return;
    
    // Register the builtin in current namespace (user)
    CljObject *symbol = intern_symbol(NULL, name);
    CljObject *func_obj = make_named_func(func, NULL, name);
    if (symbol && func_obj) {
        ns_define(st, symbol, func_obj);
        // Builtin registered successfully
    } else {
        // Failed to register builtin
    }
}

void register_builtins() {
    for (size_t i = 0; i < sizeof(builtins)/sizeof(builtins[0]); ++i) {
        register_builtin(builtins[i].name, (BuiltinFn)builtins[i].u.generic /* placeholder */);
    }
    
    // Register a test native function to demonstrate the difference
    register_builtin("test-native", (BuiltinFn)native_if);
    
    // Register all builtins in clojure.core namespace (DRY principle)
    register_builtin_in_namespace("+", native_add);
    register_builtin_in_namespace("-", native_sub);
    register_builtin_in_namespace("*", native_mul);
    register_builtin_in_namespace("/", native_div);
    register_builtin_in_namespace("str", native_str);
    register_builtin_in_namespace("type", native_type);
    register_builtin_in_namespace("array-map", native_array_map);
    register_builtin_in_namespace("nth", nth2);
    register_builtin_in_namespace("conj", native_conj);
    register_builtin_in_namespace("rest", native_rest);
    register_builtin_in_namespace("assoc", assoc3);
    register_builtin_in_namespace("transient", native_transient);
    register_builtin_in_namespace("persistent!", native_persistent);
    register_builtin_in_namespace("conj!", native_conj_bang);
    register_builtin_in_namespace("get", native_get);
    register_builtin_in_namespace("count", native_count);
    register_builtin_in_namespace("keys", native_keys);
    register_builtin_in_namespace("vals", native_vals);
    register_builtin_in_namespace("test-native", native_if);
    register_builtin_in_namespace("println", native_println);
}
