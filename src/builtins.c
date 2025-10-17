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
#include "seq.h"

CljObject* nth2(CljObject *vec, CljObject *idx) {
    if (!vec || !idx || vec->type != CLJ_VECTOR || idx->type != CLJ_INT) return NULL;
    int i = idx->as.i;
    CljPersistentVector *v = as_vector(vec);
    if (!v || i < 0 || i >= v->count) return NULL;
    return RETAIN(v->data[i]);
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
CljObject* native_conj(CljObject **args, int argc) {
    if (argc != 2) return NULL;
    CljObject *coll = args[0];
    CljObject *val = args[1];
    if (!coll || !val) return NULL;
    
    if (coll->type == CLJ_VECTOR) {
        return conj2(coll, val);
    }
    
    return NULL; // Unsupported collection type
}

// Rest function that works with BuiltinFn signature
CljObject* native_rest(CljObject **args, int argc) {
    if (argc != 1) return NULL;
    CljObject *coll = args[0];
    if (!coll) return NULL;
    
    if (coll->type == CLJ_VECTOR) {
        CljPersistentVector *v = as_vector(coll);
        if (!v || v->count <= 1) {
            return make_list(NULL, NULL);
        }
        
        // Use CljSeqIterator (existing!) instead of copying
        CljObject *seq = seq_create(coll);
        if (!seq) return make_list(NULL, NULL);
        
        // Return rest of sequence (O(1) operation!)
        return seq_rest(seq);
    }
    
    if (coll->type == CLJ_SEQ) {
        // Already a sequence - just call seq_rest
        return seq_rest(coll);
    }
    
    return NULL; // Unsupported collection type
}

CljObject* assoc3(CljObject *vec, CljObject *idx, CljObject *val) {
    if (!vec || vec->type != CLJ_VECTOR || !idx || idx->type != CLJ_INT) return NULL;
    int i = idx->as.i;
    CljPersistentVector *v = as_vector(vec);
    if (!v || i < 0 || i >= v->count) return NULL;
    int is_mutable = v->mutable_flag;
    if (is_mutable) {
        RELEASE(v->data[i]);
        v->data[i] = (RETAIN(val), val);
        return RETAIN(vec);
    } else {
        CljValue copy_val = make_vector_v(v->capacity, 0);
        CljObject *copy = (CljObject*)copy_val;
        if (!copy) return NULL;
        CljPersistentVector *c = as_vector(copy);
        for (int j = 0; j < v->count; ++j) {
            c->data[j] = (RETAIN(v->data[j]), v->data[j]);
        }
        c->count = v->count;
        RELEASE(c->data[i]);
        c->data[i] = (RETAIN(val), val);
        return copy;
    }
}

// Transient functions
CljObject* native_transient(CljObject **args, int argc) {
    if (argc != 1) return NULL;
    
    CljObject *coll = args[0];
    if (!coll) return NULL;
    
    if (coll->type == CLJ_VECTOR) {
        return (CljObject*)transient((CljValue)coll);
    } else if (coll->type == CLJ_MAP) {
        return (CljObject*)transient_map((CljValue)coll);
    } else if (coll->type == CLJ_TRANSIENT_VECTOR || coll->type == CLJ_TRANSIENT_MAP) {
        // Clojure-compatible: transient on transient returns the same object
        return coll;
    }
    
    // Throw exception for unsupported collection type (Clojure-compatible)
    throw_exception("IllegalArgumentException", 
                    "transient requires a persistent collection at position 1", 
                    __FILE__, __LINE__, 0);
    return NULL;
}

CljObject* native_persistent(CljObject **args, int argc) {
    if (argc != 1) return NULL;
    
    CljObject *coll = args[0];
    if (!coll) return NULL;
    
    if (coll->type == CLJ_TRANSIENT_VECTOR) {
        return (CljObject*)persistent_v((CljValue)coll);
    } else if (coll->type == CLJ_TRANSIENT_MAP) {
        return (CljObject*)persistent_map_v((CljValue)coll);
    } else if (coll->type == CLJ_VECTOR || coll->type == CLJ_MAP) {
        // Clojure-compatible: persistent! on persistent returns the same object
        return coll;
    }
    
    // Throw exception for unsupported collection type (Clojure-compatible)
    throw_exception("IllegalArgumentException", 
                    "persistent! requires a transient collection at position 1", 
                    __FILE__, __LINE__, 0);
    return NULL;
}

CljObject* native_conj_bang(CljObject **args, int argc) {
    if (argc < 2) return NULL;
    
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

CljObject* native_get(CljObject **args, int argc) {
    if (argc != 2) return NULL;
    CljObject *map = args[0];
    CljObject *key = args[1];
    if (!map || !key) return NULL;
    
    if (map->type == CLJ_MAP || map->type == CLJ_TRANSIENT_MAP) {
        return (CljObject*)map_get_v((CljValue)map, (CljValue)key);
    }
    
    return NULL; // Return nil for unsupported types
}

CljObject* native_count(CljObject **args, int argc) {
    if (argc != 1) return NULL;
    CljObject *coll = args[0];
    if (!coll) return NULL;
    
    if (coll->type == CLJ_MAP || coll->type == CLJ_TRANSIENT_MAP) {
        return (CljObject*)make_int(map_count_v((CljValue)coll));
    } else if (coll->type == CLJ_VECTOR || coll->type == CLJ_TRANSIENT_VECTOR) {
        CljPersistentVector *vec = as_vector(coll);
        return (CljObject*)make_int(vec ? vec->count : 0);
    }
    
    return (CljObject*)make_int(0); // Default count for unsupported types
}

CljObject* native_keys(CljObject **args, int argc) {
    if (argc != 1) return NULL;
    CljObject *map = args[0];
    if (!map) return NULL;
    
    if (map->type == CLJ_MAP || map->type == CLJ_TRANSIENT_MAP) {
        return (CljObject*)map_keys_v((CljValue)map);
    }
    
    return NULL; // Return nil for unsupported types
}

CljObject* native_vals(CljObject **args, int argc) {
    if (argc != 1) return NULL;
    CljObject *map = args[0];
    if (!map) return NULL;
    
    if (map->type == CLJ_MAP || map->type == CLJ_TRANSIENT_MAP) {
        return (CljObject*)map_vals_v((CljValue)map);
    }
    
    return NULL; // Return nil for unsupported types
}

CljObject* native_if(CljObject **args, int argc) {
    if (argc < 2) return clj_nil();
    CljObject *cond = args[0];
    if (clj_is_truthy(cond)) {
        return (RETAIN(args[1]), args[1]);
    } else if (argc > 2) {
        return (RETAIN(args[2]), args[2]);
    } else {
        return clj_nil();
    }
}

CljObject* native_type(CljObject **args, int argc) {
    if (argc != 1) return NULL;
    CljObject *obj = args[0];
    if (!obj) return (CljObject*)intern_symbol_global("nil");
    
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
        case CLJ_INT:
            return (CljObject*)intern_symbol_global("Long");
        case CLJ_FLOAT:
            return (CljObject*)intern_symbol_global("Double");
        case CLJ_STRING:
            return (CljObject*)intern_symbol_global("String");
        case CLJ_BOOL:
            return (CljObject*)intern_symbol_global("Boolean");
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

CljObject* native_array_map(CljObject **args, int argc) {
    // Must have even number of arguments (key-value pairs)
    if (argc % 2 != 0) {
        return NULL; // or throw exception for odd number of args
    }
    
    // Create map with appropriate capacity
    int pair_count = argc / 2;
    
    // Handle empty map case specially
    if (pair_count == 0) {
        return (CljObject*)make_map(0);
    }
    
    CljMap *map = make_map(pair_count);
    if (!map) {
        return NULL;
    }
    
    // Add all key-value pairs
    for (int i = 0; i < argc; i += 2) {
        CljObject *key = args[i];
        CljObject *value = args[i + 1];
        map_assoc((CljObject*)map, key, value);
    }
    
    return (CljObject*)map;
}

CljObject* make_func(CljObject* (*fn)(CljObject **args, int argc), void *env) {
    return make_named_func(fn, env, NULL);
}

CljObject* make_named_func(CljObject* (*fn)(CljObject **args, int argc), void *env, const char *name) {
    CljFunc *func = ALLOC(CljFunc, 1);
    if (!func) return NULL;
    
    func->base.type = CLJ_FUNC;
    func->base.rc = 1;
    func->fn = fn;
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
    {"nth", FN_ARITY2, .u.fn2 = nth2},
    {"conj", FN_ARITY2, .u.fn2 = conj2},
    {"assoc", FN_ARITY3, .u.fn3 = assoc3},
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

CljObject* apply_builtin(const BuiltinEntry *entry, CljObject **args, int argc) {
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

// Wrapper functions for arithmetic operations
CljObject* native_add(CljObject **args, int argc) {
    if (argc == 0) return make_int(0);  // (+) → 0
    if (argc == 1) return RETAIN(args[0]);  // (+ x) → x
    
    int sum = 0;
    for (int i = 0; i < argc; i++) {
        if (!args[i] || args[i]->type != CLJ_INT) {
            return clj_nil();
        }
        sum += args[i]->as.i;
    }
    return make_int(sum);
}

CljObject* native_sub(CljObject **args, int argc) {
    if (argc == 0) {
        throw_exception_formatted("ArityError", __FILE__, __LINE__, 0, "Wrong number of args: 0");
        return NULL;
    }
    if (argc == 1) return make_int(-args[0]->as.i);  // (- x) → -x
    
    int result = args[0]->as.i;
    for (int i = 1; i < argc; i++) {
        if (!args[i] || args[i]->type != CLJ_INT) {
            return clj_nil();
        }
        result -= args[i]->as.i;
    }
    return make_int(result);
}

CljObject* native_mul(CljObject **args, int argc) {
    if (argc == 0) return make_int(1);  // (*) → 1
    if (argc == 1) return RETAIN(args[0]);  // (* x) → x
    
    int product = 1;
    for (int i = 0; i < argc; i++) {
        if (!args[i] || args[i]->type != CLJ_INT) {
            return clj_nil();
        }
        product *= args[i]->as.i;
    }
    return make_int(product);
}

CljObject* native_div(CljObject **args, int argc) {
    if (argc == 0) {
        throw_exception_formatted("ArityError", __FILE__, __LINE__, 0, "Wrong number of args: 0");
        return NULL;
    }
    if (argc == 1) return make_int(1 / args[0]->as.i);  // (/ x) → 1/x
    
    int result = args[0]->as.i;
    for (int i = 1; i < argc; i++) {
        if (!args[i] || args[i]->type != CLJ_INT) {
            return clj_nil();
        }
        if (args[i]->as.i == 0) {
            throw_exception_formatted("ArithmeticError", __FILE__, __LINE__, 0, "Division by zero");
            return NULL;
        }
        result /= args[i]->as.i;
    }
    return make_int(result);
}

CljObject* native_println(CljObject **args, int argc) {
    if (argc < 1) return clj_nil();
    if (args[0]) {
        char *str = pr_str(args[0]);
        printf("%s\n", str);
        free(str);
    }
    return clj_nil();
}

// ============================================================================
// VARIADIC FUNCTIONS (Phase 1)
// ============================================================================

// String concatenation (variadic)
CljObject* native_str(CljObject **args, int argc) {
    if (argc == 0) {
        return make_string("");
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
static inline int add_op(int a, int b) { return a + b; }
static inline int sub_op(int a, int b) { return a - b; }
static inline int mul_op(int a, int b) { return a * b; }
static inline int div_op(int a, int b) { return a / b; }

// Generic reducer with Mutable Result Pattern
static CljObject* variadic_reduce_int(CljObject **args, int argc, 
                                      int identity_val,
                                      int (*op)(int, int),
                                      bool needs_at_least_one,
                                      bool is_division) {
    // argc == 0: return identity or Exception
    if (argc == 0) {
        if (needs_at_least_one) {
            throw_exception_formatted("ArityError", __FILE__, __LINE__, 0,
                "Wrong number of args: 0");
            return NULL;
        }
        return make_int(identity_val);
    }
    
    // argc == 1: return arg or unary operation (- or /)
    if (argc == 1) {
        // Validation
        if (!args[0] || args[0]->type != CLJ_INT) {
            throw_exception_formatted("TypeError", __FILE__, __LINE__, 0,
                "Expected integer");
            return NULL;
        }
        if (needs_at_least_one) {
            // (- 5) → -5,  (/ 8) → 0 (integer division 1/8)
            return make_int(op(identity_val, args[0]->as.i));
        }
        // For addition and multiplication: (+ 5) → 5, (* 5) → 5
        return RETAIN(args[0]);
    }
    
    // Validation: first argument
    if (!args[0] || args[0]->type != CLJ_INT) {
        throw_exception_formatted("TypeError", __FILE__, __LINE__, 0,
            "Expected integer");
        return NULL;
    }
    
    // ONE allocation for the result
    CljObject *result = make_int(args[0]->as.i);
    if (!result) return NULL;
    
    // Happy Path: Mutate result in-place (no further allocations!)
    for (int i = 1; i < argc; i++) {
        // Type validation
        if (!args[i] || args[i]->type != CLJ_INT) {
            RELEASE(result);
            throw_exception_formatted("TypeError", __FILE__, __LINE__, 0,
                "Expected integer");
            return NULL;
        }
        
        // Division-by-zero pre-check
        if (is_division && args[i]->as.i == 0) {
            RELEASE(result);
            throw_exception_formatted("ArithmeticException", __FILE__, __LINE__, 0,
                "Divide by zero");
            return NULL;
        }
        
        // Operation (mutates result in-place)
        result->as.i = op(result->as.i, args[i]->as.i);
    }
    
    return result;  // rc=1, caller takes ownership
}

// Public API
CljObject* native_add_variadic(CljObject **args, int argc) {
    return variadic_reduce_int(args, argc, 0, add_op, false, false);
}

CljObject* native_mul_variadic(CljObject **args, int argc) {
    return variadic_reduce_int(args, argc, 1, mul_op, false, false);
}

CljObject* native_sub_variadic(CljObject **args, int argc) {
    return variadic_reduce_int(args, argc, 0, sub_op, true, false);
}

CljObject* native_div_variadic(CljObject **args, int argc) {
    return variadic_reduce_int(args, argc, 1, div_op, true, true);
}

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
    register_builtin_in_namespace("nth", (BuiltinFn)nth2);
    register_builtin_in_namespace("conj", native_conj);
    register_builtin_in_namespace("rest", native_rest);
    register_builtin_in_namespace("assoc", (BuiltinFn)assoc3);
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
