#include <stdlib.h>
#include <string.h>
#include "object.h"
#include "vector.h"
#include "builtins.h"
#include "runtime.h"
#include "memory_hooks.h"

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
        CljObject *copy = make_vector(newcap, 0);
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

CljObject* assoc3(CljObject *vec, CljObject *idx, CljObject *val) {
    if (!vec || vec->type != CLJ_VECTOR || !idx || idx->type != CLJ_INT) return NULL;
    int i = idx->as.i;
    CljPersistentVector *v = as_vector(vec);
    if (!v || i < 0 || i >= v->count) return NULL;
    int is_mutable = v->mutable_flag;
    if (is_mutable) {
        if (v->data[i]) RELEASE(v->data[i]);
        v->data[i] = (RETAIN(val), val);
        return RETAIN(vec);
    } else {
        CljObject *copy = make_vector(v->capacity, 0);
        if (!copy) return NULL;
        CljPersistentVector *c = as_vector(copy);
        for (int j = 0; j < v->count; ++j) {
            c->data[j] = (RETAIN(v->data[j]), v->data[j]);
        }
        c->count = v->count;
        if (c->data[i]) RELEASE(c->data[i]);
        c->data[i] = (RETAIN(val), val);
        return copy;
    }
}

CljObject* native_if(CljObject **args, int argc) {
    if (argc < 2) return clj_nil();
    CljObject *cond = args[0];
    if (cond == clj_true()) {
        return (RETAIN(args[1]), args[1]);
    } else if (argc > 2) {
        return (RETAIN(args[2]), args[2]);
    } else {
        return clj_nil();
    }
}

CljObject* make_func(CljObject* (*fn)(CljObject **args, int argc), void *env) {
    CljObject *v = ALLOC(CljObject, 1);
    if (!v) return NULL;
    v->type = CLJ_FUNC;
    v->rc = 1;
    ((CljFunc*)v->as.data)->fn = fn;
    ((CljFunc*)v->as.data)->env = env;
    return v;
}

static const BuiltinEntry builtins[] = {
    {"nth", FN_ARITY2, .u.fn2 = nth2},
    {"conj", FN_ARITY2, .u.fn2 = conj2},
    {"assoc", FN_ARITY3, .u.fn3 = assoc3},
    {"if", FN_GENERIC, .u.generic = native_if},
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

void register_builtins() {
    for (size_t i = 0; i < sizeof(builtins)/sizeof(builtins[0]); ++i) {
        register_builtin(builtins[i].name, (BuiltinFn)builtins[i].u.generic /* placeholder */);
    }
}
