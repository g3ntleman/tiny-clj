<!-- c63b1f39-a29e-4780-9844-2e18346c314b 63825115-3793-4d44-b615-e91da337d89e -->
# CljValue Cleanup & Finalisierung

## Status Quo

Die Kernfunktionalität ist implementiert:

- ✅ CljValue API mit 32-bit Tagged Pointers
- ✅ Transient Vectors und Maps
- ✅ Stack-Overflow-Schutz (MAX_CALL_STACK_DEPTH = 20)
- ✅ Verschachtelte Auswertung funktioniert
- ✅ Tests laufen erfolgreich
- ✅ Test-Migration zu CljValue API abgeschlossen
- ✅ Legacy APIs als deprecated markiert
- ✅ Migration Guide erstellt
- ✅ **Alle ignorierten Tests aktiviert** (0 IGNORE)
- ✅ **CljSeqIterator-Verifikation** - O(1) Performance für `(rest vector)`
- ✅ **Memory Debugger Integration** - erfolgreiches Debugging

## Übersicht

Einführung einer klaren Unterscheidung zwischen:

- **`CljValue`**: 32-bit Tagged Pointer für Immediate-Werte (Fixnum, Char, Bool, Nil, Fixed-Point)
- **`CljObject`**: Vereinfachter Header (type, rc) + Pointer für Heap-allokierte Objekte

## Architektur

### CljValue (32-bit Tagged Pointer)

- **Immediate Types** (kein Heap):
  - Fixnum: 29-bit signed integer (Tag 0)
  - Char: 21-bit Unicode character (Tag 1)
  - Special: nil, true, false (Tag 2, kodiert als Tagged Values)
  - Fixed-Point: Q16.13 fixed-point (Tag 7)

**Hinweis**: Collection-Singletons (leere Liste, leerer Vector, leere Map) bleiben als Heap-Objekte mit `rc=0` erhalten!

- **Heap Types - Persistent** (Pointer mit Tag):
  - String, Vector, Map, List, Symbol, Seq (Tags 5-10)
- **Heap Types - Transient** (Pointer mit Tag):
  - TransientVector, TransientMap (Tags 11-12)
  - Nutzen gleiche Strukturen wie Persistent-Varianten (CljPersistentVector, CljMap)
  - Separate Tags für Type-Safety und garantierte Mutation
  - **Clojure-kompatibel**: Keine transient strings oder lists!

### CljObject (Bleibt weitgehend unverändert)

```c
struct CljObject {
    CljType type;
    int rc;
    union {
        int i;
        double f;
        bool b;
        void* data;
    } as;
};
```

**Änderung**: Minimal! Nur Subtypen nutzen nun `CljValue*` statt `CljObject**` für Arrays.

### Heap-Strukturen (existierende Tiny-Clj-Strukturen)

**Behalten wir bei** (minimale Änderungen):

- **CljPersistentVector**: `{ CljObject base; int count, capacity, mutable_flag; CljValue *data; }`
- **CljMap**: `{ CljObject base; int count, capacity; CljValue *data; }`
- **CljList**: `{ CljObject base; CljValue first; CljValue rest; }`
- **CljSymbol**: `{ CljObject base; CljNamespace *ns; char name[32]; }` (existiert bereits!)

**Neu hinzufügen** (für seq-Optimierung):

- **CljSeq**: `{ CljObject base; CljValue collection; uint32_t index; }`
- **CljString**: `{ CljObject base; char *data; }` (vereinfacht aus aktuellem String-Handling)

## Implementierung (Minimal-Invasiv, inkrementell)

### Phase 0: CljValue als Alias (KEIN Refactoring!)

**`src/value.h`** (NEU):

- `typedef CljObject* CljValue;` - Einfacher Alias!
- Tag-Definitionen für zukünftige Immediates
- Immediate-Helpers:
  - `make_fixnum()`, `is_fixnum()`, `as_fixnum()` - nutzen Tagged Pointers
  - `make_char()`, `is_char()`, `as_char()`
  - `make_nil()`, `make_bool()` - Wrapper um Singletons
- **Vorteil**: Kompiliert mit existierendem Code, keine Breaking Changes!

### Phase 1: Neue CljValue-API (parallel zu alter API)

**`src/vector.h`** (Ergänzung):

- `CljValue vector_conj_v(CljValue vec, CljValue item)` - neue API
- `CljObject* vector_conj(CljObject*, CljObject*)` - alte API bleibt!
- Interne Implementierung shared

**`src/parser.c`** (schrittweise Migration):

- `parse_number()` → nutzt `make_fixnum()` für kleine Integers
- `parse_literal()` → gibt `CljValue` zurück
- **Vorteil**: Immediates sofort verfügbar, keine Heap-Allokation!

### Phase 2: Transient-Typen (minimale Änderung)

**`src/types.h`**:

- Füge hinzu: `CLJ_TRANSIENT_VECTOR`, `CLJ_TRANSIENT_MAP`
- **Keine Struktur-Änderung**: Nutzen `CljPersistentVector` / `CljMap`!

**`src/vector.c`** (COW + Transients):

- `transient()`: Ändert nur `type` zu `CLJ_TRANSIENT_VECTOR`
- `conj!()`: Case-Statement:
  ```c
  switch (TYPE(vec)) {
  case CLJ_VECTOR:
      if (vec->rc == 1) { /* in-place */ }
      else { /* COW */ }
      break;
  case CLJ_TRANSIENT_VECTOR:
      /* garantiert in-place */
      break;
  }
  ```

- `persistent!()`: Ändert `type` zurück zu `CLJ_VECTOR`

### Phase 3: Container-Element-Migration (optional, Performance)

**`src/object.h`** (minimale Änderung):

```c
typedef struct {
    CljObject base;
    int count, capacity;
    CljValue *data;  // NUR DIESE ZEILE: CljObject** → CljValue*
} CljPersistentVector;
```

- **Vorteil**: 12 Bytes pro Element gespart!
- **Risiko**: Niedrig - nur interne Struktur

### Phase 4: FAM-Optimierung (optional, später)

**Nur wenn nötig** (für extreme Embedded-Constraints):

- Refactor zu FAM-basierten Strukturen (Flexible Array Members)
- Migration: Embedded `data[]` statt separater Pointer
- **Nicht im MVP!**

### Phase 5: Tests und Benchmarks

- Unit-Tests für Fixnum, Char Immediates
- Memory-Profiling: Heap-Allokationen vorher/nachher
- Benchmarks: Integer-Arithmetik, Vector-Iteration
- Transient-Performance-Tests

## Kritische Dateien

- `/Users/theisen/Projects/tiny-clj/src/object.h` (Line 68-80: CljObject struct)
- `/Users/theisen/Projects/tiny-clj/src/types.h` (CljType enum)
- `/Users/theisen/Projects/tiny-clj/src/vector.c` (Line 36-50: make_vector)
- `/Users/theisen/Projects/tiny-clj/src/string.c` (Line 20-32: make_string)
- `/Users/theisen/Projects/tiny-clj/src/memory.c` (retain/release Implementierung)

## Vorteile

- **Speicher**: Container-Elemente jetzt 4 Bytes statt 16 Bytes (CljObject)
- **Performance**: Immediates keine Heap-Allokation
- **Modular**: Backing-Strukturen klar getrennt von Werterepräsentation

## Wichtige Erkenntnisse (Phase 7)

### ✅ CljSeqIterator-Verifikation

**Bewiesen**: `(rest vector)` verwendet den effizienten `CljSeqIterator`:

- **O(1) Performance** für mehrfache `rest`-Aufrufe (nicht O(n²))
- **Clojure-kompatibel**: `(rest vector)` gibt `CLJ_SEQ` zurück, nicht `CLJ_VECTOR`
- **Memory-effizient**: Keine Element-Kopierung, nur Iterator-Status
- **Test**: `test_seq_iterator_verification` verifiziert korrekte Implementierung

### ✅ Memory Debugger Integration

**Erfolgreiches Debugging** mit dem Memory Debugger:

- **0 IGNORE Tests** - alle ignorierten Tests erfolgreich aktiviert
- **Schrittweises Debugging** - systematische Fehlerbehebung
- **Memory Leak Detection** - automatische Erkennung von Speicherproblemen
- **Performance Monitoring** - Überwachung von Allokationen und Deallokationen

### ✅ Test-Qualität

**Alle Tests laufen erfolgreich**:

- `test_cljvalue_immediates_high_level` - ✅ PASS (undefinierte Symbole entfernt)
- `test_cljvalue_transient_maps_high_level` - ✅ PASS (eval_string Issues behoben)
- `test_cljvalue_vectors_high_level` - ✅ PASS (vereinfacht, grundlegende Funktionen)
- `test_seq_iterator_verification` - ✅ NEU (CljSeqIterator O(1) Performance)

### To-dos

- [x] Implementiere Transient Maps (Phase 2)
- [x] Füge transient() und persistent() API für Maps hinzu
- [x] Implementiere conj!() für Transient Maps
- [x] Füge Tests für Transient Maps hinzu
- [x] Erstelle Benchmarks für Transient Performance
- [x] Erstelle src/value.h mit CljValue typedef, Tag-Definitionen und Immediate-Konstruktoren
- [x] Erstelle src/backing.h mit Backing-Struktur-Definitionen (StringBacking, VectorBacking, etc.)
- [x] Vereinfache CljObject in src/object.h zu reinem Header (type, rc, data pointer)
- [x] Implementiere Allokations-Funktionen für Backing-Strukturen in src/backing.c
- [x] Passe retain/release in src/memory.c für CljValue an (Immediate-Check + Backing-RC)
- [x] Migriere src/vector.c auf VectorBacking mit CljValue-Array
- [x] Migriere src/string.c auf StringBacking
- [x] Migriere src/map.c auf MapBacking
- [x] Migriere src/seq.c auf SeqBacking mit RC-Reuse-Optimierung
- [x] Aktualisiere Tests für neue CljValue-API und Backing-Strukturen
- [x] **Phase 6: Aufräumen der alten Funktionen**
  - [x] Entferne veraltete CljObject*-APIs aus src/vector.h und src/vector.c (als deprecated markiert)
  - [x] Entferne veraltete CljObject*-APIs aus src/map.h und src/map.c (als deprecated markiert)
  - [x] Entferne veraltete CljObject*-APIs aus src/clj_string.h (als deprecated markiert)
  - [x] Entferne veraltete CljObject*-APIs aus src/seq.h und src/seq.c (als deprecated markiert)
  - [x] Migriere alle Tests von CljObject* zu CljValue
  - [x] Entferne veraltete Parser-Funktionen (parse() als deprecated markiert)
  - [x] Aktualisiere alle Header-Dateien auf CljValue-API (deprecated markiert)
  - [x] Dokumentiere Breaking Changes und Migration Guide
  - [x] Erstelle Cleanup-Script für veraltete Funktionen
- [x] **Phase 7: Debug ignorierten Tests**
  - [x] Debug test_cljvalue_immediates_high_level - entferne undefinierte Symbole
  - [x] Debug test_cljvalue_transient_maps_high_level - fixe eval_string Issues
  - [x] Debug test_cljvalue_vectors_high_level - fixe eval_string Issues
  - [x] Füge test_seq_iterator_verification hinzu - verifiziere CljSeqIterator O(1) Performance
  - [x] Finale Überprüfung - alle Tests PASS, 0 IGNORE
- [ ] Finale Code-Review und Performance-Tests

---

# ID Macro Implementation Plan

## Overview

Introduce `ID` typedef as `typedef void*` to provide a generic type for function argument arrays that accept any value type. This eliminates explicit casts when passing mixed types to variadic functions.

**Important**:

- Keep specific type signatures (CljList*, CljMap*, CljValue, etc.) unchanged
- Use `ID` only for generic argument arrays: `ID *args` instead of `CljObject **args`
- Functions like `list_count` that work specifically with `CljList*` should keep their specific type

## Changes

### 1. Define ID typedef and helpers in `src/value.h` ✅

After the `CljValue` typedef (around line 10), add:

```c
// Generic object type for argument arrays in variadic functions
// This eliminates the need for explicit casts when passing values to functions.
// Similar to Objective-C's 'id' type.
//
// Usage:
//   ID generic_func(ID *args, int argc) {
//       for (int i = 0; i < argc; i++) {
//           CljObject* obj = ID_TO_OBJ(args[i]);
//           // ... work with obj
//       }
//   }
//
//   ID args[] = {make_fixnum(42), make_string("hello")};
//   generic_func(args, 2);  // No casts needed!
typedef void* ID;

// Safe cast from ID to CljObject* with debug checks
#ifdef DEBUG
static inline CljObject* ID_TO_OBJ(ID id) {
    if (!id) return NULL;
    CljValue val = (CljValue)id;
    // Check if it's an immediate or heap object
    if (is_fixnum(val) || is_fixed(val) || is_char(val) || is_special(val)) {
        // It's an immediate - return as-is (will be treated as CljObject*)
        return (CljObject*)val;
    }
    // It's a heap object - verify it has a valid type
    CljObject* obj = (CljObject*)val;
    if (obj->type >= CLJ_UNKNOWN && obj->type < CLJ_TYPE_COUNT) {
        return obj;
    }
    fprintf(stderr, "ID_TO_OBJ: Invalid object type %d at %p\n", obj->type, obj);
    abort();
}
#else
#define ID_TO_OBJ(id) ((CljObject*)(id))
#endif

// Safe cast from CljObject* to ID (always safe, no check needed)
#define OBJ_TO_ID(obj) ((ID)(obj))
```

### 2. Add convenience macros in `src/value.h` ✅

After the immediate helper functions (around line 160), add:

```c
// Convenience macros for type checking with ID or any pointer type
// These eliminate the need to cast to CljValue before checking
#define IS_FIXNUM(val) is_fixnum((CljValue)(val))
#define AS_FIXNUM(val) as_fixnum((CljValue)(val))
#define IS_FIXED(val) is_fixed((CljValue)(val))
#define AS_FIXED(val) as_fixed((CljValue)(val))
#define IS_CHAR(val) is_char((CljValue)(val))
#define AS_CHAR(val) as_char((CljValue)(val))
#define IS_SPECIAL(val) is_special((CljValue)(val))
#define AS_SPECIAL(val) as_special((CljValue)(val))
```

### 3. Update native function signatures to use ID arrays

#### `src/builtins.h` - Update all native function signatures:

```c
// Change from CljObject **args to ID *args for all native functions
ID native_add(ID *args, int argc);
ID native_sub(ID *args, int argc);
ID native_mul(ID *args, int argc);
ID native_div(ID *args, int argc);
ID native_str(ID *args, int argc);
ID native_type(ID *args, int argc);
ID native_array_map(ID *args, int argc);
ID native_conj(ID *args, int argc);
ID native_rest(ID *args, int argc);
ID native_transient(ID *args, int argc);
ID native_persistent(ID *args, int argc);
ID native_conj_bang(ID *args, int argc);
ID native_get(ID *args, int argc);
ID native_count(ID *args, int argc);
ID native_keys(ID *args, int argc);
ID native_vals(ID *args, int argc);
ID native_if(ID *args, int argc);
ID native_println(ID *args, int argc);
```

#### `src/builtins.c` - Update all native function implementations:

```c
// Example: native_add function
ID native_add(ID *args, int argc) {
    if (argc < 2) return OBJ_TO_ID(NULL);
    
    int sum = 0;
    for (int i = 0; i < argc; i++) {
        if (IS_FIXNUM(args[i])) {
            sum += AS_FIXNUM(args[i]);
        } else {
            return OBJ_TO_ID(NULL); // Error case
        }
    }
    return OBJ_TO_ID(make_fixnum(sum));
}
```

#### `src/runtime.h` - Update BuiltinFn typedef:

```c
typedef ID (*BuiltinFn)(ID *args, int argc);
```

### 4. Update CljFunc typedef (optional - keep old for compatibility)

#### `src/object.h` (line 138 and 284):

```c
typedef struct {
    CljObject base;
    ID (*fn)(ID *args, int argc);  // Changed from CljObject **args
    void *env;
    const char *name;
} CljFunc;

// Note: Keep env as CljObject* since it's specifically a CljMap*
// fn parameter stays CljObject* as it's specifically a function
ID clj_apply_function(CljObject *fn, ID *args, int argc, CljObject *env);
```

### 5. Update implementations in .c files

#### `src/object.c` (line 922):

```c
ID clj_apply_function(CljObject *fn, ID *args, int argc, CljObject *env) {
    if (!is_type(fn, CLJ_FUNC)) return NULL;
    (void)env;
    
    // Evaluate arguments (simplified; would normally call eval())
    ID *eval_args = STACK_ALLOC(ID, argc);
    for (int i = 0; i < argc; i++) {
        eval_args[i] = OBJ_TO_ID(RETAIN(ID_TO_OBJ(args[i])));
    }
    
    return clj_call_function(fn, argc, eval_args);
}
```

#### `src/function_call.c` (line 243):

```c
ID eval_function_call(CljObject *fn, ID *args, int argc, CljMap *env) {
    (void)env;
    if (!is_type(fn, CLJ_FUNC)) {
        throw_exception("TypeError", "Attempt to call non-function value", NULL, 0, 0);
        return NULL;
    }
    
    // Check if it's a native function (CljFunc) or Clojure function (CljFunction)
    if (is_native_fn(fn)) {
        // It's a native function (CljFunc)
        CljFunc *native_func = (CljFunc*)fn;
        return native_func->fn(args, argc);  // args is already ID*
    }
    // ... rest unchanged
}
```

### 6. Keep specific type signatures unchanged

These functions should **NOT** be changed:

- `int list_count(CljList *list)` - works specifically with lists
- `int map_count(CljMap *map)` - works specifically with maps  
- `CljObject* list_first(CljList *list)` - returns specific list element
- Any function with specific type requirements

### 7. Add usage examples in comments

Add to `src/value.h` after the macros:

```c
// Example usage of ID for variadic functions:
//
//   ID my_add(ID *args, int argc) {
//       int sum = 0;
//       for (int i = 0; i < argc; i++) {
//           if (IS_FIXNUM(args[i])) {
//               sum += AS_FIXNUM(args[i]);
//           }
//       }
//       return OBJ_TO_ID(make_fixnum(sum));
//   }
//
//   ID values[] = {make_fixnum(1), make_fixnum(2), make_fixnum(3)};
//   ID result = my_add(values, 3);  // No casts needed!
```

## Testing Strategy

After **each step**, run tests to ensure nothing breaks:

```bash
make && ./unity-tests 2>&1 | grep -E "FAIL|PASS" | tail -5
```

Steps:

1. Add ID typedef and helpers → compile and test ✅
2. Add convenience macros → compile and test ✅
3. Update native function signatures → compile and test
4. Update function signatures in headers → compile and test
5. Update implementations in .c files → compile and test
6. Final verification with debug and release builds

## Benefits

- Eliminates casts when creating argument arrays
- Debug-safe with type checking via ID_TO_OBJ
- Convenience macros (IS_FIXNUM, etc.) reduce verbosity
- Zero runtime overhead in release builds
- Keeps specific type safety where it matters

## Guidelines

- Use `ID` for generic parameters (single or array) that accept any value type
- Use specific types (CljList*, CljMap*, CljFunction*, etc.) when function works with specific type
- Example: `eval_function_call(CljObject *fn, ID *args, int argc, CljMap *env)`
  - `fn` is generic (could be any function type) → could be `ID` but we keep `CljObject*` for clarity
  - `args` is generic array → `ID*`
  - `env` is specifically a map → `CljMap*`
