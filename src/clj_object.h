/*
 * clj_object.h (renamed from CljObject.h)
 */
#ifndef TINY_CLJ_VALUE_H
#define TINY_CLJ_VALUE_H

#include "common.h"
#include "types.h"
#include <string.h>

struct CljNamespace;

#define LAST_PRIMITIVE_TYPE CLJ_SYMBOL
#define IS_PRIMITIVE_TYPE(type) ((type) <= LAST_PRIMITIVE_TYPE)

typedef struct CljObject CljObject;
#define type(object) ((object) ? (object)->type : CLJ_UNKNOWN)

struct CljObject {
    CljType type;
    int rc;
    union {
        int i;
        double f;
        int b;
        void* data;
    } as;
};

#define SYMBOL_NAME_MAX_LEN 32

typedef struct {
    CljObject base;
    struct CljNamespace *ns;
    char name[SYMBOL_NAME_MAX_LEN];
} CljSymbol;

typedef struct {
    CljObject base;
    int count;
    int capacity;
    int mutable_flag;
    CljObject **data;
} CljPersistentVector;

typedef struct {
    CljObject base;
    int count;
    int capacity;
    CljObject **data;
} CljMap;

typedef struct {
    CljObject base;
    CljObject *head;
    CljObject *tail;
} CljList;

typedef struct {
    CljObject base;
    CljObject* (*fn)(CljObject **args, int argc);
    void *env;
} CljFunc;

typedef struct {
    CljObject base;
    CljObject **params;
    int param_count;
    CljObject *body;
    CljObject *closure_env;
    const char *name;
} CljFunction;

typedef struct {
    int rc;
    const char *type;
    const char *message;
    const char *file;
    int line;
    int col;
    CljObject *data;
} CLJException;

CljObject* make_int(int x);
CljObject* make_float(double x);
CljObject* make_string(const char *s);
CljObject* make_symbol(const char *name, const char *ns);
CljObject* make_error(const char *message, const char *file, int line, int col);
CljObject* make_exception(const char *type, const char *message, const char *file, int line, int col, CljObject *data);

void throw_exception(const char *type, const char *message, const char *file, int line, int col);
CljObject* make_function(CljObject **params, int param_count, CljObject *body, CljObject *closure_env, const char *name);
CljObject* make_list();

CljObject* clj_nil();
CljObject* clj_true();
CljObject* clj_false();

void retain(CljObject *v);
void release(CljObject *v);

char* pr_str(CljObject *v);
bool clj_equal(CljObject *a, CljObject *b);

CljObject* map_get(CljObject *map, CljObject *key);
void map_assoc(CljObject *map, CljObject *key, CljObject *value);
CljObject* map_keys(CljObject *map);
CljObject* map_vals(CljObject *map);
int map_count(CljObject *map);
void map_put(CljObject *map, CljObject *key, CljObject *value);
void map_foreach(CljObject *map, void (*func)(CljObject*, CljObject*));
int map_contains(CljObject *map, CljObject *key);
void map_remove(CljObject *map, CljObject *key);

typedef struct SymbolEntry {
    char *ns;
    char *name;
    CljObject *symbol;
    struct SymbolEntry *next;
} SymbolEntry;

extern SymbolEntry *symbol_table;

CljObject* intern_symbol(const char *ns, const char *name);
CljObject* intern_symbol_global(const char *name);
void symbol_table_cleanup();
int symbol_count();

#ifdef ENABLE_META
extern CljObject *meta_registry;
void meta_set(CljObject *v, CljObject *meta);
CljObject* meta_get(CljObject *v);
void meta_clear(CljObject *v);
void meta_registry_init();
void meta_registry_cleanup();
#else
#define meta_set(v, meta) ((void)0)
#define meta_get(v) (NULL)
#define meta_clear(v) ((void)0)
#define meta_registry_init() ((void)0)
#define meta_registry_cleanup() ((void)0)
#endif

typedef struct CljObjectPool CljObjectPool;
CljObject *autorelease(CljObject *v);
CljObjectPool *cljvalue_pool_push();
void cljvalue_pool_pop(CljObjectPool *pool);
void cljvalue_pool_cleanup_all();

#define CLJVALUE_POOL_SCOPE(name) for (CljObjectPool *(name) = cljvalue_pool_push(); (name) != NULL; cljvalue_pool_pop(name), (name) = NULL)

CljObject* clj_call_function(CljObject *fn, int argc, CljObject **argv);
CljObject* clj_apply_function(CljObject *fn, CljObject **args, int argc, CljObject *env);

CljObject* env_extend_stack(CljObject *parent_env, CljObject **params, CljObject **values, int count);
CljObject* env_get_stack(CljObject *env, CljObject *key);
void env_set_stack(CljObject *env, CljObject *key, CljObject *value);

void set_global_eval_state(void *state);

CLJException* create_exception(const char *type, const char *message, const char *file, int line, int col, CljObject *data);
void retain_exception(CLJException *exception);
void release_exception(CLJException *exception);

CljObject* create_object(CljType type);
void retain_object(CljObject *obj);
void release_object(CljObject *obj);
void free_object(CljObject *obj);

static inline CljSymbol* as_symbol(CljObject *obj) { return (type(obj) == CLJ_SYMBOL) ? (CljSymbol*)obj : NULL; }
static inline CljPersistentVector* as_vector(CljObject *obj) { return (type(obj) == CLJ_VECTOR) ? (CljPersistentVector*)obj : NULL; }
static inline CljMap* as_map(CljObject *obj) { return (type(obj) == CLJ_MAP) ? (CljMap*)obj->as.data : NULL; }
static inline CljList* as_list(CljObject *obj) { return (type(obj) == CLJ_LIST) ? (CljList*)obj->as.data : NULL; }
static inline CljFunction* as_function(CljObject *obj) { return (type(obj) == CLJ_FUNC) ? (CljFunction*)obj : NULL; }
static inline int is_native_fn(CljObject *fn) { return type(fn) == CLJ_FUNC && ((CljFunction*)fn)->params == NULL && ((CljFunction*)fn)->body == NULL && ((CljFunction*)fn)->closure_env == NULL; }

#endif

