#ifndef TINY_CLJ_RECORD_H
#define TINY_CLJ_RECORD_H

#include <stdbool.h>
#include "object.h"
#include "symbol.h"
#include "vector.h"
#include "map.h"
#include "hashmap.h"

typedef struct CljRecordDescriptor {
    CljObject base;                 // type == CLJ_RECORD_DESCRIPTOR
    CljSymbol *type_symbol;         // e.g. Point
    CljPersistentVector *field_keys;  // vector of keyword symbols
    CljHashMap *key_to_index;       // keyword -> fixnum(index)
} CljRecordDescriptor;

typedef struct CljPersistentRecord {
    CljObject base;                 // type == CLJ_RECORD
    CljRecordDescriptor *descriptor;
    uint16_t field_count;
    ID values[];                    // compact, fixed order defined by descriptor
} CljPersistentRecord;

static inline bool is_record(ID obj) {
    return obj && TAG(obj) == CLJ_RECORD;
}

static inline CljPersistentRecord *as_record(ID obj) {
#ifdef DEBUG
    CLJ_ASSERT(!obj || TAG(obj) == CLJ_RECORD);
#endif
    return (CljPersistentRecord *)obj;
}

static inline CljRecordDescriptor *as_record_descriptor(ID obj) {
#ifdef DEBUG
    CLJ_ASSERT(!obj || TAG(obj) == CLJ_RECORD_DESCRIPTOR);
#endif
    return (CljRecordDescriptor *)obj;
}

CljRecordDescriptor *record_descriptor_lookup(ID type_symbol);
CljRecordDescriptor *record_register_descriptor(ID type_symbol, ID fields);

CljPersistentRecord *record_create(ID type_symbol, CljPersistentVector *values);
CljPersistentRecord *record_create_from_map(ID type_symbol, ID source_map);

int record_count(ID record_obj);
int record_field_index(ID record_obj, ID key);
ID record_key_at_index(ID record_obj, unsigned int index);
ID record_get_by_index(ID record_obj, unsigned int index);
ID record_get_sentinel(ID record_obj, ID key, ID not_found);
bool record_contains(ID record_obj, ID key);
ID record_keys(ID record_obj);
ID record_vals(ID record_obj);

// Returns owned map (rc=1). Caller owns and must release/autorelease it.
CljPersistentMap *record_to_map(ID record_obj);

// Closed-record behavior (no extmap support):
// - assoc existing record field => record (COW: rc==1 in-place, rc>1 copied)
// - assoc unknown key => throws NotImplementedException
ID record_assoc(ID record_obj, ID key, ID value);
// - dissoc known record field => persistent map
// - dissoc unknown key => throws NotImplementedException
ID record_dissoc(ID record_obj, ID key);

ID record_type_symbol(ID record_obj);

#endif // TINY_CLJ_RECORD_H
