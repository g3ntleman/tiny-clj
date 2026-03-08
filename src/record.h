#ifndef TINY_CLJ_RECORD_H
#define TINY_CLJ_RECORD_H

#include <subjective-c/record.h>
#include "symbol.h"

// tiny-clj runtime registry APIs
CljRecordDescriptor *record_descriptor_lookup(ID type_symbol);
CljRecordDescriptor *record_register_descriptor(ID type_symbol, ID fields);

// -----------------------------------------------------------------------------
// DEFRECORD_REGISTER macros (tiny-clj runtime registration helpers)
// -----------------------------------------------------------------------------

#define DEFRECORD_REGISTER(Name, ...) \
    RECORD_PP_CAT(DEFRECORD_REGISTER_, RECORD_PP_NARG(__VA_ARGS__))(Name, __VA_ARGS__)

#define DEFRECORD_REGISTER_1(Name, f1) \
    static CljRecordDescriptor *register_##Name(void) { \
        CljPersistentVector *fv = make_vector(1, STRONG); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f1)); \
        CljRecordDescriptor *d = record_register_descriptor(intern_symbol_global(#Name), fv); \
        RELEASE(fv); \
        return d; \
    }

#define DEFRECORD_REGISTER_2(Name, f1, f2) \
    static CljRecordDescriptor *register_##Name(void) { \
        CljPersistentVector *fv = make_vector(2, STRONG); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f1)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f2)); \
        CljRecordDescriptor *d = record_register_descriptor(intern_symbol_global(#Name), fv); \
        RELEASE(fv); \
        return d; \
    }

#define DEFRECORD_REGISTER_3(Name, f1, f2, f3) \
    static CljRecordDescriptor *register_##Name(void) { \
        CljPersistentVector *fv = make_vector(3, STRONG); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f1)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f2)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f3)); \
        CljRecordDescriptor *d = record_register_descriptor(intern_symbol_global(#Name), fv); \
        RELEASE(fv); \
        return d; \
    }

#define DEFRECORD_REGISTER_4(Name, f1, f2, f3, f4) \
    static CljRecordDescriptor *register_##Name(void) { \
        CljPersistentVector *fv = make_vector(4, STRONG); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f1)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f2)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f3)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f4)); \
        CljRecordDescriptor *d = record_register_descriptor(intern_symbol_global(#Name), fv); \
        RELEASE(fv); \
        return d; \
    }

#define DEFRECORD_REGISTER_5(Name, f1, f2, f3, f4, f5) \
    static CljRecordDescriptor *register_##Name(void) { \
        CljPersistentVector *fv = make_vector(5, STRONG); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f1)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f2)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f3)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f4)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f5)); \
        CljRecordDescriptor *d = record_register_descriptor(intern_symbol_global(#Name), fv); \
        RELEASE(fv); \
        return d; \
    }

#define DEFRECORD_REGISTER_6(Name, f1, f2, f3, f4, f5, f6) \
    static CljRecordDescriptor *register_##Name(void) { \
        CljPersistentVector *fv = make_vector(6, STRONG); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f1)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f2)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f3)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f4)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f5)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f6)); \
        CljRecordDescriptor *d = record_register_descriptor(intern_symbol_global(#Name), fv); \
        RELEASE(fv); \
        return d; \
    }

#define DEFRECORD_REGISTER_7(Name, f1, f2, f3, f4, f5, f6, f7) \
    static CljRecordDescriptor *register_##Name(void) { \
        CljPersistentVector *fv = make_vector(7, STRONG); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f1)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f2)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f3)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f4)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f5)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f6)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f7)); \
        CljRecordDescriptor *d = record_register_descriptor(intern_symbol_global(#Name), fv); \
        RELEASE(fv); \
        return d; \
    }

#define DEFRECORD_REGISTER_8(Name, f1, f2, f3, f4, f5, f6, f7, f8) \
    static CljRecordDescriptor *register_##Name(void) { \
        CljPersistentVector *fv = make_vector(8, STRONG); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f1)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f2)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f3)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f4)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f5)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f6)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f7)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f8)); \
        CljRecordDescriptor *d = record_register_descriptor(intern_symbol_global(#Name), fv); \
        RELEASE(fv); \
        return d; \
    }

#define DEFRECORD_REGISTER_9(Name, f1, f2, f3, f4, f5, f6, f7, f8, f9) \
    static CljRecordDescriptor *register_##Name(void) { \
        CljPersistentVector *fv = make_vector(9, STRONG); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f1)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f2)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f3)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f4)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f5)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f6)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f7)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f8)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f9)); \
        CljRecordDescriptor *d = record_register_descriptor(intern_symbol_global(#Name), fv); \
        RELEASE(fv); \
        return d; \
    }

#define DEFRECORD_REGISTER_10(Name, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10) \
    static CljRecordDescriptor *register_##Name(void) { \
        CljPersistentVector *fv = make_vector(10, STRONG); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f1)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f2)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f3)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f4)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f5)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f6)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f7)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f8)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f9)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f10)); \
        CljRecordDescriptor *d = record_register_descriptor(intern_symbol_global(#Name), fv); \
        RELEASE(fv); \
        return d; \
    }

#define DEFRECORD_REGISTER_11(Name, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11) \
    static CljRecordDescriptor *register_##Name(void) { \
        CljPersistentVector *fv = make_vector(11, STRONG); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f1)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f2)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f3)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f4)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f5)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f6)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f7)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f8)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f9)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f10)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f11)); \
        CljRecordDescriptor *d = record_register_descriptor(intern_symbol_global(#Name), fv); \
        RELEASE(fv); \
        return d; \
    }

#define DEFRECORD_REGISTER_12(Name, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12) \
    static CljRecordDescriptor *register_##Name(void) { \
        CljPersistentVector *fv = make_vector(12, STRONG); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f1)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f2)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f3)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f4)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f5)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f6)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f7)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f8)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f9)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f10)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f11)); \
        vector_conj_inplace(&fv, intern_symbol_global(":" #f12)); \
        CljRecordDescriptor *d = record_register_descriptor(intern_symbol_global(#Name), fv); \
        RELEASE(fv); \
        return d; \
    }

#endif // TINY_CLJ_RECORD_H
