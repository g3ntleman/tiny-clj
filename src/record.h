#ifndef TINY_CLJ_RECORD_H
#define TINY_CLJ_RECORD_H

#include <stddef.h>
#include <subjective-c/record.h>
#include "symbol.h"

// tiny-clj runtime registry APIs
CljRecordDescriptor *record_descriptor_lookup(ID type_symbol);
CljRecordDescriptor *record_register_descriptor(ID type_symbol, ID fields);
CljRecordDescriptor *record_register_descriptor_from_ns_cnames(const char *ns_name,
                                                               const char *type_name,
                                                               size_t field_count,
                                                               ...);
CljRecordDescriptor *record_register_descriptor_from_cnames(const char *type_name,
                                                            size_t field_count,
                                                            ...);

#endif // TINY_CLJ_RECORD_H
