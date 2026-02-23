#include "record.h"

#include <string.h>

#include "runtime.h"
#include "value.h"
#include "symbol.h"
#include "symbol_token.h"
#include "seq.h"
#include "strings.h"
#include "memory.h"
#include "exception.h"
#include "mini_format.h"

// Normalize a field designator to a keyword (e.g. x, "x", :x -> :x).
static ID keyword_from_field(ID field) {
  if (!field)
    return NULL;
  if (IS_KEYWORD(field))
    return field;

  const char *name = NULL;
  if (TAG(field) == CLJ_SYMBOL) {
    CljSymbol *sym = as_symbol(field);
    name = sym ? sym->cname : NULL;
  } else if (TAG(field) == CLJ_SYMBOL_TOKEN) {
    name = symbol_token_data((CljSymbolToken *)field);
  } else if (TAG(field) == CLJ_STRING) {
    name = string_data(field);
  }

  if (!name || !name[0])
    return NULL;
  while (*name == ':')
    name++;
  if (!name[0])
    return NULL;

  char kw_name[256] = {0};
  size_t pos = format_append_char(kw_name, 0, sizeof(kw_name), ':');
  format_append(kw_name, pos, sizeof(kw_name), name);
  return intern_symbol_global(kw_name);
}

// Convert an arbitrary seqable field collection to a keyword vector.
// Returns owned vector (rc=1), or NULL on invalid input/allocation failure.
static CljPersistentVector *coerce_fields_to_vector(ID fields) {
  if (!fields)
    return NULL;

  CljPersistentVector *out = make_vector(4, STRONG);
  if (!out)
    return NULL;

  CljType tag = TAG(fields);
  if (tag == CLJ_VECTOR_PERSISTENT || tag == CLJ_VECTOR_TRANSIENT) {
    CljPersistentVector *vec = as_vector(fields);
    unsigned int count = vector_count(vec);
    for (unsigned int i = 0; i < count; i++) {
      ID key = keyword_from_field(vector_nth(vec, i));
      if (!key) {
        RELEASE(out);
        return NULL;
      }
      vector_conj_inplace(&out, key);
    }
    return out;
  }

  if (!is_seqable(fields)) {
    RELEASE(out);
    return NULL;
  }

  SeqIterator iter;
  if (!seq_iter_init(&iter, fields)) {
    RELEASE(out);
    return NULL;
  }

  while (!seq_iter_empty(&iter)) {
    ID key = keyword_from_field(seq_iter_first(&iter));
    if (!key) {
      RELEASE(out);
      return NULL;
    }
    vector_conj_inplace(&out, key);
    seq_iter_next(&iter);
  }

  return out;
}

// Allocate and initialize a record descriptor.
// Returns owned descriptor (rc=1).
static CljRecordDescriptor *record_descriptor_create(ID type_symbol, CljPersistentVector *field_keys) {
  if (!type_symbol || TAG(type_symbol) != CLJ_SYMBOL || !field_keys)
    return NULL;

  CljRecordDescriptor *desc = (CljRecordDescriptor *)alloc(sizeof(CljRecordDescriptor), 1, CLJ_RECORD_DESCRIPTOR);
  if (!desc)
    return NULL;

  desc->type_symbol = RETAIN(type_symbol);
  desc->field_keys = RETAIN(field_keys);

  return desc;
}

// Allocate a record instance with descriptor and nil-initialized slots.
// Returns owned record (rc=1).
static CljPersistentRecord *record_alloc(CljRecordDescriptor *desc) {
  if (!desc)
    return NULL;

  unsigned int field_count = vector_count(desc->field_keys);
  size_t total_size = sizeof(CljPersistentRecord) + ((size_t)field_count * sizeof(ID));
  CljPersistentRecord *record = (CljPersistentRecord *)alloc(total_size, 1, CLJ_RECORD);
  if (!record)
    return NULL;

  record->descriptor = RETAIN(desc);
  for (unsigned int i = 0; i < field_count; i++) {
    record->values[i] = NULL;
  }

  return record;
}

// Closed-records currently do not support extmap behavior.
static void throw_record_extmap_not_implemented(void) {
  throw_exception(EXCEPTION_NOT_IMPLEMENTED,
                  "additional keys not declared in defrecord are not implemented",
                  __FILE__, __LINE__, 0);
}

// Resolve a descriptor key to field index; returns -1 when absent.
static inline int descriptor_field_index(CljRecordDescriptor *desc, ID key) {
  if (!desc || !desc->field_keys || !key)
    return -1;

  unsigned int field_count = vector_count(desc->field_keys);
  for (unsigned int i = 0; i < field_count; i++) {
    ID candidate = vector_nth(desc->field_keys, i);
    if (candidate == key || clj_equal(candidate, key)) {
      return (int)i;
    }
  }
  return -1;
}

// Check whether a descriptor declares a given key.
static bool descriptor_contains_key(CljRecordDescriptor *desc, ID key) {
  return descriptor_field_index(desc, key) >= 0;
}

// Validate constructor source keys against descriptor fields.
// Throws NotImplementedException if unknown keys are present.
static bool validate_source_keys_known(CljRecordDescriptor *desc, ID source_map) {
  if (!source_map)
    return true;

  CljType source_tag = TAG(source_map);
  if (source_tag == CLJ_MAP_PERSISTENT || source_tag == CLJ_MAP_TRANSIENT) {
    CljPersistentMap *source = map_backing(source_map);
    if (!source)
      return true;
    MAP_FOR_EACH(source, key, value) {
      (void)value;
      if (!descriptor_contains_key(desc, key)) {
        throw_record_extmap_not_implemented();
        return false;
      }
    }
    return true;
  }

  if (source_tag == CLJ_RECORD) {
    CljPersistentRecord *source_rec = as_record(source_map);
    if (!source_rec || !source_rec->descriptor)
      return true;
    unsigned int source_count = record_declared_field_count(source_rec);
    for (unsigned int i = 0; i < source_count; i++) {
      ID key = vector_nth(source_rec->descriptor->field_keys, i);
      if (!descriptor_contains_key(desc, key)) {
        throw_record_extmap_not_implemented();
        return false;
      }
    }
    return true;
  }

  return true;
}

// Look up a record descriptor by type symbol in the runtime registry.
// Returns borrowed descriptor pointer, or NULL if not found.
CljRecordDescriptor *record_descriptor_lookup(ID type_symbol) {
  if (!type_symbol || TAG(type_symbol) != CLJ_SYMBOL)
    return NULL;
  if (!g_runtime.record_registry)
    return NULL;

  ID desc = hashmap_get_sentinel(g_runtime.record_registry, type_symbol, NULL);
  if (!desc || TAG(desc) != CLJ_RECORD_DESCRIPTOR)
    return NULL;
  return (CljRecordDescriptor *)desc;
}

// Register or reuse a descriptor for a record type.
// Returns borrowed descriptor pointer. Throws on invalid args or mismatched redefinition.
CljRecordDescriptor *record_register_descriptor(ID type_symbol, ID fields) {
  if (!type_symbol || TAG(type_symbol) != CLJ_SYMBOL) {
    throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                    "record-register requires a symbol type name",
                    __FILE__, __LINE__, 0);
    return NULL;
  }

  CljPersistentVector *field_keys = coerce_fields_to_vector(fields);
  if (!field_keys) {
    throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                    "record-register requires a seqable collection of fields",
                    __FILE__, __LINE__, 0);
    return NULL;
  }

  CljRecordDescriptor *existing = record_descriptor_lookup(type_symbol);
  if (existing) {
    if (!clj_equal(existing->field_keys, field_keys)) {
      RELEASE(field_keys);
      throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                      "record type already registered with different fields",
                      __FILE__, __LINE__, 0);
      return NULL;
    }
    RELEASE(field_keys);
    return existing;
  }

  if (!g_runtime.record_registry) {
    g_runtime.record_registry = make_hashmap(64);
    if (!g_runtime.record_registry) {
      RELEASE(field_keys);
      return NULL;
    }
  }

  CljRecordDescriptor *desc = record_descriptor_create(type_symbol, field_keys);
  RELEASE(field_keys);
  if (!desc)
    return NULL;

  hashmap_assoc_inplace(&g_runtime.record_registry, type_symbol, (ID)desc);
  RELEASE(desc);
  return record_descriptor_lookup(type_symbol);
}

// Return the type symbol for a record instance (borrowed), or NULL for non-records.
ID record_type_symbol(ID record_obj) {
  if (!is_record(record_obj))
    return NULL;
  CljPersistentRecord *record = as_record(record_obj);
  return record->descriptor ? record->descriptor->type_symbol : NULL;
}

// Create a record instance from ordered field values.
// Missing values default to nil. Returns owned record (rc=1).
CljPersistentRecord *record_create(ID type_symbol, CljPersistentVector *values) {
  CljRecordDescriptor *desc = record_descriptor_lookup(type_symbol);
  if (!desc) {
    throw_exception(EXCEPTION_RUNTIME,
                    "record type not registered",
                    __FILE__, __LINE__, 0);
    return NULL;
  }

  CljPersistentRecord *record = record_alloc(desc);
  if (!record)
    return NULL;

  if (!values)
    return record;

  unsigned int provided = vector_count(values);
  unsigned int field_count = record_declared_field_count(record);
  unsigned int limit = provided < field_count ? provided : field_count;
  for (unsigned int i = 0; i < limit; i++) {
    ID value = vector_nth(values, i);
    if (value) {
      record->values[i] = RETAIN(value);
    }
  }

  return record;
}

// Create a record instance from a map/record source.
// Unknown keys are rejected (NotImplementedException). Returns owned record (rc=1).
CljPersistentRecord *record_create_from_map(ID type_symbol, ID source_map) {
  CljRecordDescriptor *desc = record_descriptor_lookup(type_symbol);
  if (!desc) {
    throw_exception(EXCEPTION_RUNTIME,
                    "record type not registered",
                    __FILE__, __LINE__, 0);
    return NULL;
  }

  if (!validate_source_keys_known(desc, source_map)) {
    return NULL;
  }

  CljPersistentRecord *record = record_alloc(desc);
  if (!record)
    return NULL;

  if (!source_map)
    return record;
  unsigned int target_count = record_declared_field_count(record);

  CljType source_tag = TAG(source_map);
  if (source_tag == CLJ_MAP_PERSISTENT || source_tag == CLJ_MAP_TRANSIENT) {
    int source_count = map_count(source_map);
    if (source_count > 0 && (unsigned int)source_count < target_count) {
      CljPersistentMap *source = map_backing(source_map);
      if (source) {
        MAP_FOR_EACH(source, key, value) {
          int index = descriptor_field_index(desc, key);
          if (index >= 0 && value) {
            record->values[(unsigned int)index] = RETAIN(value);
          }
        }
      }
    } else {
      for (unsigned int i = 0; i < target_count; i++) {
        ID key = vector_nth(desc->field_keys, i);
        ID value = map_get_sentinel(source_map, key, NULL);
        if (value) {
          record->values[i] = RETAIN(value);
        }
      }
    }
    return record;
  }

  if (source_tag == CLJ_RECORD) {
    CljPersistentRecord *source_rec = as_record(source_map);
    unsigned int source_count = source_rec ? record_declared_field_count(source_rec) : 0;
    if (source_rec &&
        source_rec->descriptor == desc &&
        source_count == target_count) {
      for (unsigned int i = 0; i < target_count; i++) {
        ID value = source_rec->values[i];
        if (value) {
          record->values[i] = RETAIN(value);
        }
      }
      return record;
    }

    if (source_rec && source_count < target_count) {
      for (unsigned int i = 0; i < source_count; i++) {
        ID key = vector_nth(source_rec->descriptor->field_keys, i);
        int index = descriptor_field_index(desc, key);
        if (index >= 0) {
          ID value = source_rec->values[i];
          if (value) {
            record->values[(unsigned int)index] = RETAIN(value);
          }
        }
      }
    } else {
      for (unsigned int i = 0; i < target_count; i++) {
        ID key = vector_nth(desc->field_keys, i);
        ID value = record_get_sentinel(source_map, key, NULL);
        if (value) {
          record->values[i] = RETAIN(value);
        }
      }
    }
    return record;
  }

  return record;
}

// Return number of declared fields for a record instance.
int record_count(ID record_obj) {
  if (!is_record(record_obj))
    return 0;
  return (int)record_declared_field_count(as_record(record_obj));
}

// Return descriptor index for a record key, or -1 when absent.
int record_field_index(ID record_obj, ID key) {
  if (!is_record(record_obj))
    return -1;
  CljPersistentRecord *record = as_record(record_obj);
  return descriptor_field_index(record->descriptor, key);
}

// Return the declared key at descriptor index (borrowed), or NULL on bounds/type mismatch.
ID record_key_at_index(ID record_obj, unsigned int index) {
  if (!is_record(record_obj))
    return NULL;
  CljPersistentRecord *record = as_record(record_obj);
  if (!record->descriptor || index >= record_declared_field_count(record))
    return NULL;
  return vector_nth(record->descriptor->field_keys, index);
}

// Return the value at descriptor index (borrowed), or NULL on bounds/type mismatch.
ID record_get_by_index(ID record_obj, unsigned int index) {
  if (!is_record(record_obj))
    return NULL;
  CljPersistentRecord *record = as_record(record_obj);
  if (index >= record_declared_field_count(record))
    return NULL;
  return record->values[index];
}

// Key lookup with caller-provided not-found sentinel.
ID record_get_sentinel(ID record_obj, ID key, ID not_found) {
  if (!is_record(record_obj))
    return not_found;
  CljPersistentRecord *record = as_record(record_obj);
  int index = descriptor_field_index(record->descriptor, key);
  if (index < 0)
    return not_found;
  return record->values[(unsigned int)index];
}

// Predicate: true when key is declared on the record descriptor.
bool record_contains(ID record_obj, ID key) {
  return record_field_index(record_obj, key) >= 0;
}

// Return a vector of record keys in descriptor order.
// MEMORY_POLICY: returns a usable/pool-safe alias of the descriptor key vector.
ID record_keys(ID record_obj) {
  if (!is_record(record_obj))
    return NULL;
  CljPersistentRecord *record = as_record(record_obj);
  return record->descriptor->field_keys;
}

// Return a vector of non-nil record values in descriptor order.
// MEMORY_POLICY: usable/pool-safe return.
ID record_vals(ID record_obj) {
  if (!is_record(record_obj))
    return NULL;
  CljPersistentRecord *record = as_record(record_obj);
  unsigned int field_count = record_declared_field_count(record);

  CljPersistentVector *vals_vec = make_vector(field_count, STRONG);
  if (!vals_vec)
    return NULL;

  for (unsigned int i = 0; i < field_count; i++) {
    ID value = record->values[i];
    if (!value)
      continue;
    CljPersistentVector *next = vector_conj_owned(vals_vec, RETAIN(value));
    if (next != vals_vec) {
      RELEASE(vals_vec);
      vals_vec = next;
    }
  }

  return AUTORELEASE(vals_vec);
}

// Materialize a persistent map view of record fields.
// Returns owned map (rc=1).
CljPersistentMap *record_to_map(ID record_obj) {
  if (!is_record(record_obj))
    return NULL;
  CljPersistentRecord *record = as_record(record_obj);
  unsigned int field_count = record_declared_field_count(record);

  CljPersistentMap *result = make_map((int)field_count);
  if (!result)
    return NULL;
  for (unsigned int i = 0; i < field_count; i++) {
    ID key = vector_nth(record->descriptor->field_keys, i);
    ID val = record->values[i];
    map_assoc_inplace(&result, key, val);
  }

  return result;
}

// Internal assoc helper with COW semantics:
// - rc==1: mutate in-place and return original record
// - rc>1: allocate and return a copied record with updated slot
static CljPersistentRecord *record_assoc_core(CljPersistentRecord *record, unsigned int index, ID value) {
  unsigned int field_count = record_declared_field_count(record);
  if (!record || index >= field_count)
    return NULL;

  if (record->base.rc == 1) {
    ASSIGN(record->values[index], value);
    return record;
  }

  CljPersistentRecord *updated = record_alloc(record->descriptor);
  if (!updated)
    return NULL;

  for (unsigned int i = 0; i < field_count; i++) {
    ID chosen = (i == index) ? value : record->values[i];
    updated->values[i] = RETAIN(chosen);
  }
  return updated;
}

// Associative update for closed records.
// Known keys use COW semantics (rc==1 in-place, rc>1 copy);
// unknown keys throw NotImplementedException.
ID record_assoc(ID record_obj, ID key, ID value) {
  if (!is_record(record_obj))
    return NULL;
  CljPersistentRecord *record = as_record(record_obj);

  int index = descriptor_field_index(record->descriptor, key);
  if (index >= 0) {
    return (ID)record_assoc_core(record, (unsigned int)index, value);
  }

  (void)value;
  throw_record_extmap_not_implemented();
  return NULL;
}

// Remove a known field by materializing a map without that field.
// Unknown keys throw NotImplementedException.
ID record_dissoc(ID record_obj, ID key) {
  if (!is_record(record_obj))
    return NULL;

  int index = record_field_index(record_obj, key);
  if (index < 0) {
    throw_record_extmap_not_implemented();
    return NULL;
  }

  CljPersistentRecord *record = as_record(record_obj);
  unsigned int field_count = record_declared_field_count(record);
  int out_capacity = (field_count > 0) ? ((int)field_count - 1) : 0;
  CljPersistentMap *result = make_map(out_capacity);
  if (!result)
    return NULL;

  for (unsigned int i = 0; i < field_count; i++) {
    if ((int)i == index)
      continue;
    ID field_key = vector_nth(record->descriptor->field_keys, i);
    ID field_value = record->values[i];
    map_assoc_inplace(&result, field_key, field_value);
  }

  return result;
}
