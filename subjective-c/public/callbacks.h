#ifndef SUBJECTIVE_C_CALLBACKS_H
#define SUBJECTIVE_C_CALLBACKS_H

#include "object.h"
#include "strings.h"
#include <stdint.h>
#include <stdbool.h>

// Callback-Typen
typedef uint32_t (*CljHashFn)(ID value);
typedef bool (*CljEqualFn)(ID a, ID b);
typedef CljString* (*CljToStringFn)(ID value);

// Callback-Struct für alle tiny-clj Funktionen
typedef struct {
    CljHashFn hash;
    CljEqualFn equal;
    CljToStringFn to_string;
} CljCallbacks;

// Globaler Callback-Struct
extern CljCallbacks g_clj_callbacks;

// Wrapper-Funktionen (rufen Callbacks auf)
uint32_t clj_hash(ID value);
bool clj_equal(ID a, ID b);
CljString* clj_to_string(ID value);

// Default-Implementierungen (nur für subjective-c Typen)
uint32_t clj_hash_default(ID value);
bool clj_equal_default(ID a, ID b);
CljString* clj_to_string_default(ID value);

// Gemeinsamer Setter (von tiny-clj runtime_init aufgerufen)
void clj_set_callbacks(CljCallbacks callbacks);

// Legacy einzelne Setter (für Abwärtskompatibilität)
void clj_set_hash_fn(CljHashFn fn);
void clj_set_equal_fn(CljEqualFn fn);
void clj_set_to_string_fn(CljToStringFn fn);

#endif // SUBJECTIVE_C_CALLBACKS_H

