#include "vector.h"
#include "memory.h"
#include "types.h"      // For SINGLETON_RC
#include "common.h"     // For CLJ_ASSERT
#include "exception.h"  // For throw_exception_formatted
#include "validation.h" // For throw_index_out_of_bounds
#include <stdlib.h>
#include <stdbool.h>
#include <string.h> // For memset

// ---------------------------------------------------------------------------
// Persistent vectors (TAG: CLJ_VECTOR_PERSISTENT)
// ---------------------------------------------------------------------------

// Empty-vector singleton: CLJ_VECTOR_PERSISTENT with rc=SINGLETON_RC, statically initialized.
// Note: Flexible array member cannot be initialized, so we use a struct with no data array.
// The field widths must match CljPersistentVector exactly (conditional on platform).
#if defined(ESP_PLATFORM) || defined(ESP32_BUILD)
static struct {
  CljObject base;
  uint16_t count;
  uint16_t capacity;
} clj_empty_vector_singleton_data = {
    .base = {.type = CLJ_VECTOR_PERSISTENT, .rc = SINGLETON_RC},
    .count = 0,
    .capacity = 0};
#else
static struct {
  CljObject base;
  unsigned int count;
  int capacity;
} clj_empty_vector_singleton_data = {
    .base = {.type = CLJ_VECTOR_PERSISTENT, .rc = SINGLETON_RC},
    .count = 0,
    .capacity = 0};
#endif

static CljPersistentVector *clj_empty_vector_singleton = (CljPersistentVector *)&clj_empty_vector_singleton_data;
CljPersistentVector *vector_empty_singleton = (CljPersistentVector *)&clj_empty_vector_singleton_data;

/** Return empty vector singleton (rc=SINGLETON_RC, do not retain/release). */
CljPersistentVector *empty_vector(void) {
  return clj_empty_vector_singleton;
}

/** Get vector capacity. Returns 0 if vec is NULL. */
unsigned int vector_capacity(CljPersistentVector *vec) {
  return vec ? (unsigned int)vec->capacity : 0u;
}

ID vector_nth(CljPersistentVector *vec, unsigned int index) {
  if (!vec) {
    throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                              "vector_nth: vector is NULL");
    return NULL;
  }
  if (index < vec->count) {
    return vec->data[index];
  }
  throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
                            "vector_nth: index %u is out of bounds for vector with %u elements",
                            index, vec->count);
  return NULL;
}

int vector_index_of(CljPersistentVector *vec, ID value) {
  if (!vec)
    return INDEX_NOT_FOUND;
  VECTOR_FOR_EACH(vec, elem) {
    if (clj_equal(elem, value)) {
      return _i;
    }
  }
  return INDEX_NOT_FOUND;
}

ID *vector_as_array(CljPersistentVector *vec) {
  CLJ_ASSERT(vec != NULL && "vector_as_array called with NULL");
  return vec->data;
}

void vector_clear(CljPersistentVector *vec) {
  CLJ_ASSERT(vec != NULL);
  if (!has_weak_elements((const CljObject *)vec)) {
    VECTOR_FOR_EACH(vec, elem) { RELEASE(elem); }
  }
  vec->count = 0;
}

void vector_truncate(CljPersistentVector *vec, unsigned int n) {
  CLJ_ASSERT(vec && n <= vec->count);
  if (!has_weak_elements((const CljObject *)vec)) {
    unsigned int old_count = vec->count;
    for (unsigned int i = n; i < old_count; i++) {
      RELEASE(vec->data[i]);
      vec->data[i] = NULL;
    }
  }
  vec->count = n;
}

static size_t g_make_vector_copy_count = 0;

size_t vector_make_copy_count(void) {
  return g_make_vector_copy_count;
}

void vector_make_copy_count_reset(void) {
  g_make_vector_copy_count = 0;
}

CljPersistentVector *make_vector_copy(CljPersistentVector *vec, unsigned capacity) {
  g_make_vector_copy_count++;
  if (!vec)
    return NULL;
  unsigned count = MIN(capacity, vec->count);
  bool is_weak = has_weak_elements((const CljObject *)vec);
  CljPersistentVector *vec_copy = make_vector(capacity,
                                              is_weak ? WEAK : STRONG);
  vec_copy->count = count;
  for (unsigned i = 0; i < count; i++) {
    ID src = vec->data[i];
    vec_copy->data[i] = is_weak ? src : RETAIN(src);
  }
  return vec_copy;
}

static CljPersistentVector *vector_pop_core(CljPersistentVector *vec, int autorelease_new) {
  if (!vec)
    return NULL;
  if (vec->count == 0)
    return vec;

  // Fast path: in-place pop if we have unique ownership (rc==1).
  if (((CljObject *)vec)->rc == 1) {
    if (!has_weak_elements((const CljObject *)vec)) {
      ID last = vec->data[vec->count - 1];
      RELEASE(last);
    }
    vec->count--;
    return vec;
  }

  // COW: create a copy with count-1
  unsigned original_count = vec->count;
  CljPersistentVector *tmp = vec;
  tmp->count = original_count - 1;
  CljPersistentVector *new_vec = make_vector_copy(tmp, original_count - 1);
  tmp->count = original_count;
  if (autorelease_new)
    new_vec = AUTORELEASE(new_vec);
  return new_vec;
}

CljPersistentVector *vector_popped(CljPersistentVector *vec) {
  return vector_pop_core(vec, 1);
}

CljPersistentVector *vector_popped_owned(CljPersistentVector *vec) {
  return vector_pop_core(vec, 0);
}

static CljPersistentVector *vector_insert_at_core(CljPersistentVector *vec, unsigned int index, ID item) {
  if (!vec)
    return NULL;
  if (index > vec->count)
    return vec;

  bool is_weak = has_weak_elements((const CljObject *)vec);
  bool needs_growth = (vec->count >= (unsigned int)vec->capacity);

  if (((CljObject *)vec)->rc == 1) {
    if (needs_growth) {
      int nc = vec->capacity * 2;
      if (nc < 4)
        nc = 4;
      CljPersistentVector *grown = make_vector_copy(vec, (unsigned)nc);
      if (!grown)
        return vec;
      vec = grown;
    }
    for (unsigned int i = vec->count; i > index; i--)
      vec->data[i] = vec->data[i - 1];
    vec->data[index] = is_weak ? item : RETAIN(item);
    vec->count++;
    return vec;
  }

  int nc = vec->capacity;
  if (needs_growth) {
    nc = vec->capacity * 2;
    if (nc < 4)
      nc = 4;
  }
  CljPersistentVector *new_vec = make_vector((unsigned)nc,
                                             is_weak ? WEAK : STRONG);
  if (!new_vec)
    return vec;
  new_vec->count = vec->count + 1;
  for (unsigned int i = 0; i < index; i++) {
    ID src = vec->data[i];
    new_vec->data[i] = is_weak ? src : RETAIN(src);
  }
  new_vec->data[index] = is_weak ? item : RETAIN(item);
  for (unsigned int i = index; i < vec->count; i++) {
    ID src = vec->data[i];
    new_vec->data[i + 1] = is_weak ? src : RETAIN(src);
  }
  return new_vec;
}

CljPersistentVector *vector_by_inserting_at(CljPersistentVector *vec, unsigned int index, ID item) {
  CljPersistentVector *result = vector_insert_at_core(vec, index, item);
  if (result != vec)
    return AUTORELEASE(result);
  return result;
}

static CljPersistentVector *vector_remove_at_core(CljPersistentVector *vec, unsigned int index, int autorelease_new) {
  if (!vec)
    return NULL;
  if (index >= vec->count)
    return vec;

  bool is_weak = has_weak_elements((const CljObject *)vec);

  if (((CljObject *)vec)->rc == 1) {
    if (!is_weak) {
      RELEASE(vec->data[index]);
    }
    for (unsigned int i = index + 1; i < vec->count; i++)
      vec->data[i - 1] = vec->data[i];
    vec->count--;
    return vec;
  }

  unsigned new_count = vec->count - 1;
  CljPersistentVector *new_vec = make_vector((unsigned)vec->capacity,
                                             is_weak ? WEAK : STRONG);
  if (!new_vec)
    return vec;
  if (autorelease_new)
    new_vec = AUTORELEASE(new_vec);
  new_vec->count = new_count;

  for (unsigned int i = 0, j = 0; i < vec->count; i++) {
    if (i == index)
      continue;
    ID src = vec->data[i];
    new_vec->data[j++] = is_weak ? src : RETAIN(src);
  }
  return new_vec;
}

CljPersistentVector *vector_by_removing_at(CljPersistentVector *vec, unsigned int index) {
  return vector_remove_at_core(vec, index, 1);
}

static CljPersistentVector *vector_remove_at_owned(CljPersistentVector *vec, unsigned int index) {
  return vector_remove_at_core(vec, index, 0);
}

static size_t g_make_vector_count = 0;

size_t vector_requested_allocation_size(unsigned int capacity) {
  return sizeof(CljPersistentVector) + (size_t)capacity * sizeof(ID);
}

CljPersistentVector *make_vector(unsigned int capacity, ElementRetention retention) {
  bool weakElements = (retention == WEAK);
  if (capacity > CLJ_VECTOR_CAPACITY_MAX) {
    throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                              "make_vector: capacity %u exceeds maximum allowed (%u)",
                              capacity, CLJ_VECTOR_CAPACITY_MAX);
    return NULL;
  }
  if (capacity == 0 && !weakElements) {
    return clj_empty_vector_singleton;
  }
  g_make_vector_count++;
  CljType type = CLJ_VECTOR_PERSISTENT;
  size_t total = sizeof(CljPersistentVector) + (size_t)capacity * sizeof(ID);
  CljPersistentVector *vec = (CljPersistentVector *)alloc(total, 1, type);
  vec->base.type = type;
  vec->base.flags = weakElements ? CLJ_FLAG_WEAK_ELEMENTS : 0;
  vec->count = 0;
  vec->capacity = (int)capacity;
  // data[] is zeroed by alloc(...,1,...) ? alloc uses malloc, not calloc. So we must memset.
  if (capacity > 0) {
    memset(vec->data, 0, (size_t)capacity * sizeof(ID));
  }
  return vec;
}

/** @brief Create a persistent vector from contiguous item storage.
 * @param items Pointer to contiguous items (NULL is only valid when count is 0)
 * @param count Number of items to copy
 * @return New vector with exact capacity/count, or empty singleton when count is 0
 */
CljPersistentVector *make_vector_from_stack(ID *items, unsigned int count) {
  if (count == 0) {
    return empty_vector();
  }
  if (!items) {
    throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                              "make_vector_from_stack: items is NULL for non-zero count");
    return NULL;
  }

  CljPersistentVector *vec = make_vector(count, STRONG);
  if (!vec) {
    return NULL;
  }
  vec->count = count;
  for (unsigned int i = 0; i < count; i++) {
    vec->data[i] = RETAIN(items[i]);
  }
  return vec;
}

CljPersistentVector *vector_conj_owned(CljPersistentVector *vec, ID item) {
  if (!vec)
    return NULL;

  bool is_weak = has_weak_elements((const CljObject *)vec);

  // Empty singleton: create new vector (owned).
  if (is_singleton((CljObject *)vec)) {
    CljPersistentVector *new_vec = make_vector(4, STRONG);
    if (!new_vec)
      return vec;
    new_vec->data[0] = is_weak ? item : RETAIN(item);
    new_vec->count = 1;
    return new_vec;
  }

  // In-place append if unique and room.
  if (((CljObject *)vec)->rc == 1 && vec->count < (unsigned int)vec->capacity) {
    vec->data[vec->count++] = is_weak ? item : RETAIN(item);
    return vec;
  }

  int new_capacity = vec->capacity;
  if (vec->count >= (unsigned int)vec->capacity) {
    new_capacity = vec->capacity * 2;
    if (new_capacity < 4)
      new_capacity = 4;
  }
  CljPersistentVector *new_vec = make_vector_copy(vec, (unsigned)new_capacity);
  if (!new_vec)
    return vec;
  new_vec->data[new_vec->count++] = is_weak ? item : RETAIN(item);
  return new_vec;
}

CljPersistentVector *vector_conj(CljPersistentVector *vec, ID item) {
  CljPersistentVector *result = vector_conj_owned(vec, item);
  if (result != vec)
    return AUTORELEASE(result);
  return result;
}

// Core assoc implementation: RC==1 → in-place, RC>1 → COW.
static CljPersistentVector *vector_assoc_core(CljPersistentVector *vec, unsigned int index, ID value) {
  if (!vec) {
    throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                              "vector_assoc: vector is NULL");
    return NULL;
    return NULL;
  }

  bool is_weak = has_weak_elements((const CljObject *)vec);

  if (index >= vec->count) {
    // For weak vectors we allow append via index == count (matches previous behavior).
    if (!(is_weak && index == vec->count)) {
      throw_index_out_of_bounds("vector_assoc", index, vec->count, "vector");
      return NULL;
    }
  }

  // Empty singleton cannot be modified in place.
  if (is_singleton((CljObject *)vec)) {
    throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                              "vector_assoc: cannot modify empty vector singleton");
    return NULL;
    return NULL;
  }

  if (((CljObject *)vec)->rc == 1) {
    // Append for weak at index==count: ensure capacity and increment count.
    if (is_weak && index == vec->count) {
      if (vec->capacity <= (int)index) {
        int newcap = MAX(MAX(vec->capacity * 2, (int)index + 1), 4);
        CljPersistentVector *grown = make_vector_copy(vec, (unsigned)newcap);
        vec = grown;
      }
      vec->count++;
    }

    CLJ_ASSERT(index < (unsigned int)vec->capacity);
    if (index < vec->count && !is_weak) {
      // Use ASSIGN to avoid releasing when old == value.
      ASSIGN(vec->data[index], value);
    } else {
      vec->data[index] = is_weak ? value : RETAIN(value);
    }
    return vec;
  }

  int newcap = vec->capacity;
  if (is_weak && index == vec->count) {
    newcap = MAX(MAX(vec->capacity * 2, (int)index + 1), 4);
  }
  CljPersistentVector *new_vec = make_vector_copy(vec, (unsigned)newcap);
  if (is_weak && index == vec->count) {
    new_vec->count++;
  }
  CLJ_ASSERT(index < (unsigned int)new_vec->capacity);
  if (index < vec->count && !is_weak) {
    // Use ASSIGN to avoid releasing when old == value.
    ASSIGN(new_vec->data[index], value);
  } else {
    new_vec->data[index] = is_weak ? value : RETAIN(value);
  }
  return new_vec;
}

CljPersistentVector *vector_assoc(CljPersistentVector *vec, unsigned int index, ID value) {
  CljPersistentVector *result = vector_assoc_core(vec, index, value);
  if (result != vec)
    return AUTORELEASE(result);
  return result;
}

CljPersistentVector *vector_set_nth(CljPersistentVector *vec, unsigned int index, ID value) {
  (void)index;
  (void)value;
  if (!vec)
    return NULL;
  throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                            "set-nth on persistent vector is not allowed (immutable); use assoc or vector_assoc");
  return NULL;
}

// === _owned functions (for internal use) ===
static CljPersistentVector *vector_assoc_owned(CljPersistentVector *vec, unsigned int index, ID value) {
  return vector_assoc_core(vec, index, value);
}
static CljPersistentVector *vector_insert_at_owned(CljPersistentVector *vec, unsigned int index, ID item) {
  return vector_insert_at_core(vec, index, item);
}

void vector_conj_inplace(CljPersistentVector **vec_slot, ID item) {
  if (!vec_slot || !*vec_slot)
    return;
  CljPersistentVector *current = *vec_slot;
  CljPersistentVector *updated = vector_conj_owned(current, item);
  if (updated && updated != current) {
    RELEASE(current);
    *vec_slot = updated;
  }
}

void vector_assoc_inplace(CljPersistentVector **vec_slot, unsigned int index, ID value) {
  if (!vec_slot || !*vec_slot)
    return;
  CljPersistentVector *current = *vec_slot;
  CljPersistentVector *updated = vector_assoc_owned(current, index, value);
  if (updated && updated != current) {
    RELEASE(current);
    *vec_slot = updated;
  }
}

void vector_insert_at_inplace(CljPersistentVector **vec_slot, unsigned int index, ID item) {
  if (!vec_slot || !*vec_slot)
    return;
  CljPersistentVector *current = *vec_slot;
  CljPersistentVector *updated = vector_insert_at_owned(current, index, item);
  if (updated && updated != current) {
    RELEASE(current);
    *vec_slot = updated;
  }
}

void vector_remove_at_inplace(CljPersistentVector **vec_slot, unsigned int index) {
  if (!vec_slot || !*vec_slot)
    return;
  CljPersistentVector *current = *vec_slot;
  CljPersistentVector *updated = vector_remove_at_owned(current, index);
  if (updated && updated != current) {
    RELEASE(current);
    *vec_slot = updated;
  }
}

void vector_pop_inplace(CljPersistentVector **vec_slot) {
  if (!vec_slot || !*vec_slot)
    return;
  CljPersistentVector *current = *vec_slot;
  CljPersistentVector *updated = vector_popped_owned(current);
  if (updated && updated != current) {
    RELEASE(current);
    *vec_slot = updated;
  }
}

// ---------------------------------------------------------------------------
// Transient vectors (TAG: CLJ_VECTOR_TRANSIENT) - wrapper around persistent backing
// ---------------------------------------------------------------------------

/**
 * @brief Create a transient wrapper around a persistent vector.
 * @param vec Source persistent vector
 * @return New transient vector with rc=1; caller must RELEASE/AUTORELEASE it
 */
CljTransientVector *make_vector_transient(CljPersistentVector *vec) {
  if (!vec)
    return NULL;
  CLJ_ASSERT(TAG((ID)vec) == CLJ_VECTOR_PERSISTENT);

  CljTransientVector *tvec = (CljTransientVector *)alloc(sizeof(CljTransientVector), 1, CLJ_VECTOR_TRANSIENT);
  tvec->base.type = CLJ_VECTOR_TRANSIENT;
  tvec->head = 0;
  tvec->backing = RETAIN(vec);
  return tvec;
}

CljPersistentVector *vector_persistent(CljTransientVector *tvec) {
  if (!tvec)
    return NULL;
  CLJ_ASSERT(tvec->base.type == CLJ_VECTOR_TRANSIENT);
  CljPersistentVector *b = tvec->backing;
  if (tvec->head == 0) {
    // Fast path: elements are in logical order already; return borrowed backing.
    return b;
  }
  // Ringbuffer wrap-around: allocate a normalised persistent copy in logical order.
  // Returned as AUTORELEASE so callers don't need to change (still borrowed-style).
  unsigned int cnt = b->count;
  unsigned int cap = (unsigned int)b->capacity;
  bool is_weak = has_weak_elements((const CljObject *)b);
  CljPersistentVector *copy = make_vector(cnt, is_weak ? WEAK : STRONG);
  if (!copy)
    return b; // fallback: return stale backing rather than NULL
  copy->count = cnt;
  for (unsigned int i = 0; i < cnt; i++) {
    unsigned int phys = (tvec->head + i) % cap;
    ID src = b->data[phys];
    copy->data[i] = is_weak ? src : RETAIN(src);
  }
  return AUTORELEASE(copy);
}

static void transient_vector_set_backing(CljTransientVector *tvec, CljPersistentVector *new_backing) {
  if (!tvec)
    return;
  CLJ_ASSERT(tvec->base.type == CLJ_VECTOR_TRANSIENT);
  CLJ_ASSERT(new_backing != NULL);
  // Use ASSIGN to keep RC semantics consistent. Caller provides an *owned* new_backing,
  // so we must RELEASE it after ASSIGN's RETAIN to avoid leaking a ref.
  CljPersistentVector *old_backing = tvec->backing;
  ASSIGN(tvec->backing, new_backing);
  if (new_backing != old_backing) {
    RELEASE(new_backing);
  }
}

void vector_push(CljTransientVector *tvec, ID item) {
  if (!tvec) {
    throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                              "vector_push: vector is NULL");
    return;
  }
  CLJ_ASSERT(tvec->base.type == CLJ_VECTOR_TRANSIENT);
  CLJ_ASSERT(tvec->backing != NULL);

  CljPersistentVector *b = tvec->backing;
  unsigned int cnt = b->count;
  unsigned int cap = (unsigned int)b->capacity;
  bool is_weak = has_weak_elements((const CljObject *)b);

  if (cnt < cap) {
    // In-place write at the ringbuffer tail (O(1)).
    unsigned int phys = (tvec->head + cnt) % cap;
    b->data[phys] = is_weak ? item : RETAIN(item);
    b->count++;
    return;
  }

  // Backing is full: grow.  Copy elements in logical order → head resets to 0.
  unsigned int new_cap = cap * 2;
  if (new_cap < 4) new_cap = 4;
  if (new_cap > CLJ_VECTOR_CAPACITY_MAX) {
    throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                              "vector_push: vector would exceed maximum capacity (%u)",
                              CLJ_VECTOR_CAPACITY_MAX);
    return;
  }
  CljPersistentVector *new_b = make_vector(new_cap, is_weak ? WEAK : STRONG);
  if (!new_b) return;
  new_b->count = cnt;
  for (unsigned int i = 0; i < cnt; i++) {
    unsigned int phys = (tvec->head + i) % cap;
    ID src = b->data[phys];
    new_b->data[i] = is_weak ? src : RETAIN(src);
  }
  // Append the new item at logical position cnt
  new_b->data[cnt] = is_weak ? item : RETAIN(item);
  new_b->count = cnt + 1;
  tvec->head = 0;  // Growth invariant: head resets to 0
  transient_vector_set_backing(tvec, new_b);
}

void vector_pop(CljTransientVector *tvec) {
  if (!tvec) {
    throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                              "vector_pop: vector is NULL");
    return;
  }
  CLJ_ASSERT(tvec->base.type == CLJ_VECTOR_TRANSIENT);
  CLJ_ASSERT(tvec->backing != NULL);

  CljPersistentVector *b = tvec->backing;
  unsigned int cnt = b->count;
  if (cnt == 0) {
    throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                              "vector_pop: cannot pop from empty vector");
    return;
  }

  // O(1) tail removal: release the last logical element.
  unsigned int cap = (unsigned int)b->capacity;
  unsigned int tail_phys = (tvec->head + cnt - 1) % cap;
  if (!has_weak_elements((const CljObject *)b)) {
    RELEASE(b->data[tail_phys]);
    b->data[tail_phys] = NULL;
  }
  b->count--;
}

void vector_set_nth_transient(CljTransientVector *tvec, unsigned int index, ID value) {
  if (!tvec) {
    throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                              "vector_set_nth_transient: vector is NULL");
    return;
  }
  CLJ_ASSERT(tvec->base.type == CLJ_VECTOR_TRANSIENT);
  CLJ_ASSERT(tvec->backing != NULL);

  CljPersistentVector *b = tvec->backing;
  unsigned int cnt = b->count;
  if (index >= cnt) {
    throw_index_out_of_bounds("vector_set_nth_transient", index, cnt, "vector");
    return;
  }

  bool is_weak = has_weak_elements((const CljObject *)b);
  unsigned int cap = (unsigned int)b->capacity;
  unsigned int phys = (tvec->head + index) % cap;

  if (!is_weak) {
    ASSIGN(b->data[phys], value);
  } else {
    b->data[phys] = value;
  }
}

void vector_remove_at(CljTransientVector *tvec, unsigned int index) {
  if (!tvec) {
    throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                              "vector_remove_at: vector is NULL");
    return;
  }
  CLJ_ASSERT(tvec->base.type == CLJ_VECTOR_TRANSIENT);
  CLJ_ASSERT(tvec->backing != NULL);

  CljPersistentVector *b = tvec->backing;
  unsigned int cnt = b->count;
  if (index >= cnt) {
    throw_index_out_of_bounds("vector_remove_at", index, cnt, "vector");
    return;
  }

  unsigned int cap = (unsigned int)b->capacity;
  bool is_weak = has_weak_elements((const CljObject *)b);

  if (index == 0) {
    // O(1) front removal: release element and advance head.
    unsigned int phys = tvec->head % cap;
    if (!is_weak) {
      RELEASE(b->data[phys]);
      b->data[phys] = NULL;
    }
    tvec->head = (tvec->head + 1) % cap;
    b->count--;
    return;
  }

  // Arbitrary index: O(n) wrap-aware shift.
  // Release the element being removed first.
  unsigned int phys_remove = (tvec->head + index) % cap;
  if (!is_weak) {
    RELEASE(b->data[phys_remove]);
  }
  // Shift elements logically after `index` one position toward the front.
  for (unsigned int i = index; i < cnt - 1; i++) {
    unsigned int dst = (tvec->head + i) % cap;
    unsigned int src = (tvec->head + i + 1) % cap;
    b->data[dst] = b->data[src];
  }
  // Clear the vacated tail slot.
  unsigned int tail = (tvec->head + cnt - 1) % cap;
  b->data[tail] = NULL;
  b->count--;
}

void vector_insert_at(CljTransientVector *tvec, unsigned int index, ID item) {
  if (!tvec) {
    throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                              "vector_insert_at: vector is NULL");
    return;
  }
  CLJ_ASSERT(tvec->base.type == CLJ_VECTOR_TRANSIENT);
  CLJ_ASSERT(tvec->backing != NULL);

  CljPersistentVector *b = tvec->backing;
  unsigned int cnt = b->count;
  if (index > cnt) {
    throw_index_out_of_bounds("vector_insert_at", index, cnt, "vector");
    return;
  }

  unsigned int cap = (unsigned int)b->capacity;
  bool is_weak = has_weak_elements((const CljObject *)b);

  if (cnt < cap) {
    // In-place insert with wrap-aware shift (O(n), tail shifts right).
    for (unsigned int i = cnt; i > index; i--) {
      unsigned int dst = (tvec->head + i) % cap;
      unsigned int src = (tvec->head + i - 1) % cap;
      b->data[dst] = b->data[src];
    }
    unsigned int phys = (tvec->head + index) % cap;
    b->data[phys] = is_weak ? item : RETAIN(item);
    b->count++;
    return;
  }

  // Need to grow: copy logical order into new backing, head resets to 0.
  unsigned int new_cap = cap * 2;
  if (new_cap < 4) new_cap = 4;
  if (new_cap > CLJ_VECTOR_CAPACITY_MAX) {
    throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                              "vector_insert_at: vector would exceed maximum capacity (%u)",
                              CLJ_VECTOR_CAPACITY_MAX);
    return;
  }
  CljPersistentVector *new_b = make_vector(new_cap, is_weak ? WEAK : STRONG);
  if (!new_b) return;
  new_b->count = cnt + 1;
  for (unsigned int i = 0; i < index; i++) {
    unsigned int phys = (tvec->head + i) % cap;
    ID src = b->data[phys];
    new_b->data[i] = is_weak ? src : RETAIN(src);
  }
  new_b->data[index] = is_weak ? item : RETAIN(item);
  for (unsigned int i = index; i < cnt; i++) {
    unsigned int phys = (tvec->head + i) % cap;
    ID src = b->data[phys];
    new_b->data[i + 1] = is_weak ? src : RETAIN(src);
  }
  tvec->head = 0;
  transient_vector_set_backing(tvec, new_b);
}

void vector_truncate_transient(CljTransientVector *tvec, unsigned int new_count) {
  if (!tvec) {
    throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                              "vector_truncate_transient: vector is NULL");
    return;
  }
  CLJ_ASSERT(tvec->base.type == CLJ_VECTOR_TRANSIENT);
  CLJ_ASSERT(tvec->backing != NULL);

  CljPersistentVector *b = tvec->backing;
  unsigned int cnt = b->count;

  if (new_count >= cnt) {
    return;
  }

  unsigned int cap = (unsigned int)b->capacity;
  bool is_weak = has_weak_elements((const CljObject *)b);

  // Release tail elements that are being truncated (wrap-aware).
  if (!is_weak) {
    for (unsigned int i = new_count; i < cnt; i++) {
      unsigned int phys = (tvec->head + i) % cap;
      RELEASE(b->data[phys]);
      b->data[phys] = NULL;
    }
  }
  b->count = new_count;
}
