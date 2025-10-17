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

## Übersicht

Einführung einer klaren Unterscheidung zwischen:

- **`CljValue`**: 32-bit Tagged Pointer für Immediate-Werte (Fixnum, Char, Bool, Nil, Float16)
- **`CljObject`**: Vereinfachter Header (type, rc) + Pointer für Heap-allokierte Objekte

## Architektur

### CljValue (32-bit Tagged Pointer)

- **Immediate Types** (kein Heap):
  - Fixnum: 29-bit signed integer (Tag 0)
  - Char: 21-bit Unicode character (Tag 1)
  - Special: nil, true, false (Tag 2, kodiert als Tagged Values)
  - Float16: 16-bit half-precision float (Tag 3)

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

### To-dos

- [ ] Implementiere Transient Maps (Phase 2)
- [ ] Füge transient() und persistent() API für Maps hinzu
- [ ] Implementiere conj!() für Transient Maps
- [ ] Füge Tests für Transient Maps hinzu
- [ ] Erstelle Benchmarks für Transient Performance
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
  - [ ] Finale Code-Review und Performance-Tests
