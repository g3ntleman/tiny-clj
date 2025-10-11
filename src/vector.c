#include "vector.h"
#include "runtime.h"
#include "memory_hooks.h"
#include <stdlib.h>
#include <stdbool.h>

// Empty-vector singleton: actual CljVector instance with rc=0
static CljPersistentVector clj_empty_vector_singleton;
/** @brief Initialize empty vector singleton once */
static void init_empty_vector_singleton_once(void) {
  static bool initialized = false;
  if (initialized)
    return;
  clj_empty_vector_singleton.base.type = CLJ_VECTOR;
  clj_empty_vector_singleton.base.rc =
      0; // Singletons have no reference counting
  clj_empty_vector_singleton.count = 0;
  clj_empty_vector_singleton.capacity = 0;
  clj_empty_vector_singleton.mutable_flag = 0;
  clj_empty_vector_singleton.data = NULL;
    initialized = true;
}

// Creates a CljVector.
// Notes:
// - When capacity <= 0, returns empty-vector singleton (rc=0, data=NULL); do
// not retain/release.
// - When capacity > 0, returns heap vector (rc=1) with zero-initialized backing
// store.
/** @brief Create a new vector with specified capacity and mutability */
CljObject *make_vector(int capacity, int is_mutable) {
  if (capacity <= 0) {
    init_empty_vector_singleton_once();
    return (CljObject *)&clj_empty_vector_singleton;
  }
  CljPersistentVector *vec = ALLOC(CljPersistentVector, 1);
  if (!vec)
    return NULL;

  vec->base.type = CLJ_VECTOR;
  vec->base.rc = 1;
  vec->count = 0;
  vec->capacity = capacity;
  vec->mutable_flag = is_mutable ? 1 : 0;
  vec->data = capacity > 0
                  ? (CljObject **)calloc((size_t)capacity, sizeof(CljObject *))
                  : NULL;

  CREATE((CljObject *)vec);
  return (CljObject *)vec;
}

/** @brief Create a new weak vector for temporary storage */
CljObject *make_weak_vector(int capacity) {
  // Weak vector: same layout as CljVector, aber Push ohne retain
  CljObject *o = make_vector(capacity, 1);
  if (!o)
    return NULL;
  o->type = CLJ_WEAK_VECTOR;
  return o;
}

static inline int is_weak_vec(CljObject *o) {
  return o && is_type(o, CLJ_WEAK_VECTOR);
}

int vector_push_inplace(CljObject *vec, CljObject *item) {
  if (!vec || !item)
    return 0;
  CljPersistentVector *v = as_vector(vec);
  if (!v)
    return 0;
  if (v->count >= v->capacity) {
    int newcap = v->capacity ? v->capacity * 2 : 4;
    void *p = realloc(v->data, (size_t)newcap * sizeof(CljObject *));
    if (!p)
      return 0;
    v->data = (CljObject **)p;
    for (int i = v->capacity; i < newcap; ++i)
      v->data[i] = NULL;
    v->capacity = newcap;
  }
  v->data[v->count++] = item;
  if (!is_weak_vec(vec)) RETAIN(item);
  return 1;
}

CljObject *vector_conj(CljObject *vec, CljObject *item) {
  if (!vec || vec->type != CLJ_VECTOR || !item)
    return NULL;

  CljPersistentVector *old_vec = as_vector(vec);
  if (!old_vec)
    return NULL;

  int new_capacity = old_vec->capacity + 1;
  if (new_capacity < 4)
    new_capacity = 4;

  CljObject *new_vec_obj = make_vector(new_capacity, 0);
  CljPersistentVector *new_vec = as_vector(new_vec_obj);
  if (!new_vec)
    return NULL;

  for (int i = 0; i < old_vec->count; i++) {
    if (old_vec->data[i]) {
      new_vec->data[i] = old_vec->data[i];
      RETAIN(old_vec->data[i]);
    }
  }
  // set count for existing elements (including possible NULL holes)
  new_vec->count = old_vec->count;
  // append the new item using push helper (handles retain and growth)
  vector_push_inplace(new_vec_obj, item);

  return AUTORELEASE(new_vec_obj);
}

CljObject *vector_from_items(CljObject **items, int count) {
  if (count <= 0)
    return make_vector(0, 0);
  CljObject *vec = make_vector(count, 0);
  CljPersistentVector *vd = as_vector(vec);
  if (!vd)
    return vec;
  vd->count = 0;
  for (int i = 0; i < count; ++i) {
    if (items[i]) {
      // use push helper for non-NULL (retains item and manages capacity)
      vector_push_inplace(vec, items[i]);
    } else {
      // preserve NULL holes at the current logical index
      vd->data[vd->count] = NULL;
      vd->count++;
    }
  }
  return vec;
}

CljObject* vector_from_stack(CljObject **stack, int count) {
    CljObject *vec_obj = make_vector(count, 0);
    CljPersistentVector *vec = as_vector(vec_obj);
    if (!vec) return NULL;
    for (int i = 0; i < count; i++) {
        vec->data[i] = stack[i];
        if (stack[i]) RETAIN(stack[i]);
    }
    vec->count = count;
    return vec_obj;
}
