#ifndef SUBJECTIVE_C_RECORD_H
#define SUBJECTIVE_C_RECORD_H

#include <stdbool.h>

#include "object.h"
#include "vector.h"
#include "map.h"

typedef struct CljRecordDescriptor {
    CljObject base;                  // type == CLJ_RECORD_DESCRIPTOR
    ID type_symbol;                  // usually a symbol
    CljPersistentVector *field_keys; // vector of keyword symbols
} CljRecordDescriptor;

typedef struct CljPersistentRecord {
    CljObject base; // type == CLJ_RECORD
    CljRecordDescriptor *descriptor;
    ID values[]; // compact, fixed order defined by descriptor
} CljPersistentRecord;

static inline bool is_record(ID obj) {
    return obj && TAG(obj) == CLJ_RECORD;
}

static inline CljPersistentRecord *as_record(ID obj) {
#ifdef DEBUG
    CLJ_ASSERT(!obj || TAG(obj) == CLJ_RECORD);
#endif
    return obj;
}

static inline CljRecordDescriptor *as_record_descriptor(ID obj) {
#ifdef DEBUG
    CLJ_ASSERT(!obj || TAG(obj) == CLJ_RECORD_DESCRIPTOR);
#endif
    return obj;
}

static inline unsigned int record_declared_field_count(const CljPersistentRecord *record) {
    CLJ_ASSERT(record != NULL && "record_declared_field_count expects non-null record");
    CLJ_ASSERT(record->descriptor != NULL && "record_declared_field_count expects record descriptor");
    CLJ_ASSERT(record->descriptor->field_keys != NULL && "record_declared_field_count expects descriptor field_keys");
    return vector_count(record->descriptor->field_keys);
}

// Returns owned descriptor (rc=1). type_symbol must be a symbol.
CljRecordDescriptor *record_descriptor_create(ID type_symbol, CljPersistentVector *field_keys);

// Returns owned record (rc=1).
CljPersistentRecord *make_record_with_descriptor(CljRecordDescriptor *desc, CljPersistentVector *values);
// Returns owned record (rc=1).
CljPersistentRecord *make_record_from_map_with_descriptor(CljRecordDescriptor *desc, ID source_map);

int record_count(ID record_obj);
int record_field_index(ID record_obj, ID key);
// Returns declared key alias, or NULL on bounds/type mismatch.
ID record_key_at_index(ID record_obj, unsigned int index);
// Returns pool-safe alias, or NULL on bounds/type mismatch.
ID record_get_by_index(ID record_obj, unsigned int index);
// Returns pool-safe alias for a found value, otherwise returns not_found.
ID record_get_sentinel(ID record_obj, ID key, ID not_found);
bool record_contains(ID record_obj, ID key);

// MEMORY_POLICY: usable/pool-safe return (descriptor key vector alias).
ID record_keys(ID record_obj);
// MEMORY_POLICY: usable/pool-safe return.
ID record_vals(ID record_obj);

// Closed-record behavior (no extmap support):
// - assoc existing record field => pool-safe record alias (COW: rc==1 in-place, rc>1 copied)
// - assoc unknown key => throws NotImplementedException
ID record_assoc(ID record_obj, ID key, ID value);
// - dissoc known record field => pool-safe persistent map
// - dissoc unknown key => throws NotImplementedException
ID record_dissoc(ID record_obj, ID key);

// -----------------------------------------------------------------------------
// DEFRECORD macros (compile-time struct overlays, no runtime dependencies)
// -----------------------------------------------------------------------------

#define RECORD_PP_CAT_(a, b) a##b
#define RECORD_PP_CAT(a, b) RECORD_PP_CAT_(a, b)

#define RECORD_PP_NARG_( \
         _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, N, ...) N
#define RECORD_PP_NARG(...) \
    RECORD_PP_NARG_(__VA_ARGS__, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1)

#define DEFRECORD(Name, ...) \
    RECORD_PP_CAT(DEFRECORD_, RECORD_PP_NARG(__VA_ARGS__))(Name, __VA_ARGS__)

#define DEFRECORD_1(Name, f1) \
    typedef struct Name { CljObject base; CljRecordDescriptor *descriptor; ID f1; } Name;
#define DEFRECORD_2(Name, f1, f2) \
    typedef struct Name { CljObject base; CljRecordDescriptor *descriptor; ID f1; ID f2; } Name;
#define DEFRECORD_3(Name, f1, f2, f3) \
    typedef struct Name { CljObject base; CljRecordDescriptor *descriptor; ID f1; ID f2; ID f3; } Name;
#define DEFRECORD_4(Name, f1, f2, f3, f4) \
    typedef struct Name { CljObject base; CljRecordDescriptor *descriptor; ID f1; ID f2; ID f3; ID f4; } Name;
#define DEFRECORD_5(Name, f1, f2, f3, f4, f5) \
    typedef struct Name { CljObject base; CljRecordDescriptor *descriptor; ID f1; ID f2; ID f3; ID f4; ID f5; } Name;
#define DEFRECORD_6(Name, f1, f2, f3, f4, f5, f6) \
    typedef struct Name { CljObject base; CljRecordDescriptor *descriptor; ID f1; ID f2; ID f3; ID f4; ID f5; ID f6; } Name;
#define DEFRECORD_7(Name, f1, f2, f3, f4, f5, f6, f7) \
    typedef struct Name { CljObject base; CljRecordDescriptor *descriptor; ID f1; ID f2; ID f3; ID f4; ID f5; ID f6; ID f7; } Name;
#define DEFRECORD_8(Name, f1, f2, f3, f4, f5, f6, f7, f8) \
    typedef struct Name { CljObject base; CljRecordDescriptor *descriptor; ID f1; ID f2; ID f3; ID f4; ID f5; ID f6; ID f7; ID f8; } Name;
#define DEFRECORD_9(Name, f1, f2, f3, f4, f5, f6, f7, f8, f9) \
    typedef struct Name { CljObject base; CljRecordDescriptor *descriptor; ID f1; ID f2; ID f3; ID f4; ID f5; ID f6; ID f7; ID f8; ID f9; } Name;
#define DEFRECORD_10(Name, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10) \
    typedef struct Name { CljObject base; CljRecordDescriptor *descriptor; ID f1; ID f2; ID f3; ID f4; ID f5; ID f6; ID f7; ID f8; ID f9; ID f10; } Name;
#define DEFRECORD_11(Name, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11) \
    typedef struct Name { CljObject base; CljRecordDescriptor *descriptor; ID f1; ID f2; ID f3; ID f4; ID f5; ID f6; ID f7; ID f8; ID f9; ID f10; ID f11; } Name;
#define DEFRECORD_12(Name, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12) \
    typedef struct Name { CljObject base; CljRecordDescriptor *descriptor; ID f1; ID f2; ID f3; ID f4; ID f5; ID f6; ID f7; ID f8; ID f9; ID f10; ID f11; ID f12; } Name;

#endif // SUBJECTIVE_C_RECORD_H
