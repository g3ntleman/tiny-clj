# Bewertung: Vereinigung von `body` und `arity_bodies`

## Vorschlag

Statt zwei separater Felder:
```c
ID body;                    // Für feste Arity
CljVector *arity_bodies;    // Für mehrere Aritäten (NULL wenn nicht verwendet)
```

Nur ein Feld:
```c
ID body_or_bodies;  // Entweder AST-Node (feste Arity) oder Vector (mehrere Aritäten)
```

**Dispatch zur Laufzeit:**
```c
if (TAG(body_or_bodies) == CLJ_VECTOR) {
    // Mehrere Aritäten
    CljVector *arity_bodies = (CljVector*)body_or_bodies;
    // ...
} else {
    // Feste Arity
    ID body = body_or_bodies;
    // ...
}
```

## Vorteile

### 1. Speicher-Ersparnis
- **8 Bytes pro Funktion** (64-bit) / **4 Bytes** (32-bit)
- Wichtig für Embedded-Plattformen
- Besonders relevant, da die meisten Funktionen nur eine feste Arity haben

### 2. Einfachheit
- Ein Feld statt zwei
- Keine NULL-Checks für `arity_bodies`
- Konsistente Typisierung (beide sind `ID`)

### 3. Semantik
- Die Struktur spiegelt wider: "entweder ein Body ODER mehrere Bodies"
- Keine Möglichkeit für inkonsistente Zustände (z.B. beide Felder gesetzt)

## Nachteile

### 1. Performance-Overhead
- **Jeder Funktionsaufruf** muss `TAG()` prüfen, auch bei festen Aritäten
- Aktuell: Fast Path für feste Arity = 0 Overhead
- Mit Vereinigung: Fast Path = 1× `TAG()`-Check (klein, aber messbar)

**Messung:**
```c
// TAG() ist sehr schnell (nur Bit-Operationen):
static inline CljType TAG(ID obj) {
    if ((uintptr_t)obj & 0x1) {
        return (CljType)((uintptr_t)obj & 0x7);
    }
    if (!obj) return CLJ_NIL;
    return ((CljObject*)obj)->type;
}
```
- Geschätzt: **~2-3 CPU-Zyklen** pro Check
- Bei `fib(47)` mit Millionen von Aufrufen: **kumulativer Overhead**

### 2. Fallback-Body Problem
**Aktueller Code:**
```c
if (func->arity_bodies) {
    ID body = vector_nth(func->arity_bodies, argc);
    if (!body) {
        // Fallback: verwende Standard-Body (falls vorhanden)
        body = func->body;  // ← Das würde wegfallen!
    }
}
```

**Problem:** Wenn eine Arity nicht im Vector gefunden wird, gibt es aktuell einen Fallback zu `func->body`. Mit der Vereinigung müsste dieser Fallback anders gelöst werden:
- Option A: Im Vector explizit speichern (auch für nicht-definierte Aritäten)
- Option B: Fallback-Body separat speichern (aber dann haben wir wieder 2 Felder)
- Option C: Kein Fallback (strikter, aber möglicherweise gewünscht)

### 3. Type-Safety
- Weniger explizit: Compiler kann nicht prüfen, ob das Feld korrekt verwendet wird
- Potenzielle Runtime-Fehler statt Compile-Time-Fehler
- Dokumentation muss klarstellen: "Dieses Feld ist entweder X oder Y"

### 4. Code-Komplexität
- Dispatch-Logik wird verschachtelter
- Mehr Bedingungen in Hot Path (`eval_function_call`)
- Potenzielle Fehlerquelle bei zukünftigen Änderungen

## Performance-Analyse

### Hot Path: Feste Arity (häufigster Fall)
**Aktuell:**
```c
// 0 Overhead - direkter Zugriff
ID body = func->body;
eval_body_with_params(body, ...);
```

**Mit Vereinigung:**
```c
// 1× TAG-Check pro Aufruf
if (TAG(func->body_or_bodies) == CLJ_VECTOR) {
    // Sollte nie passieren bei festen Aritäten
    CLJ_ASSERT(0);
} else {
    ID body = func->body_or_bodies;
    eval_body_with_params(body, ...);
}
```

**Optimierung möglich:**
```c
// Branch-Prediction: Fast Path ist wahrscheinlich
ID body_or_bodies = func->body_or_bodies;
if (likely(TAG(body_or_bodies) != CLJ_VECTOR)) {
    // Fast Path: feste Arity
    eval_body_with_params(body_or_bodies, ...);
} else {
    // Slow Path: mehrere Aritäten
    // ...
}
```

### Cold Path: Mehrere Aritäten (seltener Fall)
**Aktuell:**
```c
if (func->arity_bodies) {  // Pointer-Check
    // ...
}
```

**Mit Vereinigung:**
```c
if (TAG(func->body_or_bodies) == CLJ_VECTOR) {  // Tag-Check
    // ...
}
```

**Vergleich:** Ähnlicher Overhead, aber Tag-Check ist minimal schneller als Pointer-Check.

## Empfehlung

### ✅ **JA, mit Einschränkungen**

**Gründe:**
1. **Speicher-Ersparnis ist signifikant** für Embedded-Plattformen
2. **Performance-Overhead ist minimal** (2-3 CPU-Zyklen, gut branch-predictable)
3. **Semantik ist klarer** (entweder/oder statt beide optional)

**Aber:**
1. **Fallback-Body muss geklärt werden:**
   - Option A (empfohlen): Im Vector explizit speichern
     ```c
     // Vector: [body0, body1, body2, fallback_body, NULL, ...]
     // Index = Arity, letzter nicht-NULL = Fallback
     ```
   - Option B: Separates Feld `ID fallback_body` (nur wenn nötig)
   - Option C: Kein Fallback (strikter)

2. **Branch-Prediction nutzen:**
   ```c
   #ifdef __GNUC__
   #define likely(x)   __builtin_expect(!!(x), 1)
   #define unlikely(x) __builtin_expect(!!(x), 0)
   #else
   #define likely(x)   (x)
   #define unlikely(x) (x)
   #endif
   ```

3. **Benchmark erforderlich:**
   - Vor/Nach-Vergleich mit `fib(20)` oder ähnlichem
   - Messen des tatsächlichen Overheads

## Implementierungs-Vorschlag

```c
typedef struct {
    CljObject base;
    CljVector *params;
    ID body_or_bodies;  // Entweder AST-Node (feste Arity) oder Vector (mehrere Aritäten)
    CljList *env_stack;
    CljSymbol *name;
    CljNamespace *ns;
    ID varargs_param;  // Symbol für & args Parameter (NULL wenn keine varargs)
} CljFunction;

// Helper: Prüfe ob mehrere Aritäten
static inline bool has_multiple_arities(CljFunction *func) {
    return func->body_or_bodies && TAG(func->body_or_bodies) == CLJ_VECTOR;
}

// Helper: Hole Body für feste Arity
static inline ID get_single_body(CljFunction *func) {
    CLJ_ASSERT(!has_multiple_arities(func));
    return func->body_or_bodies;
}

// Helper: Hole Vector für mehrere Aritäten
static inline CljVector* get_arity_bodies(CljFunction *func) {
    CLJ_ASSERT(has_multiple_arities(func));
    return (CljVector*)func->body_or_bodies;
}

// Dispatch-Logik
ID eval_function_call(ID fn, ID *args, int argc, CljMap *env, EvalState *st) {
    CljFunction *func = as_function(fn);
    
    if (unlikely(has_multiple_arities(func))) {
        // Mehrere Aritäten
        CljVector *arity_bodies = get_arity_bodies(func);
        ID body = NULL;
        if (argc >= 0 && argc < vector_count(arity_bodies)) {
            body = vector_nth(arity_bodies, argc);
        }
        
        if (!body) {
            // Arity nicht gefunden - prüfe varargs
            if (func->varargs_param) {
                // Varargs-Body verwenden
                // ...
            } else {
                throw_exception(EXCEPTION_ARITY, "Arity mismatch", NULL, 0, 0);
                return NULL;
            }
        }
        // ...
    } else {
        // Fast Path: Feste Arity
        ID body = get_single_body(func);
        int param_count = func->params ? vector_count(func->params) : 0;
        if (argc != param_count) {
            throw_exception(EXCEPTION_ARITY, "Arity mismatch", NULL, 0, 0);
            return NULL;
        }
        return eval_body_with_params(body, ...);
    }
}
```

## Zusammenfassung

| Aspekt | Bewertung | Gewichtung |
|--------|-----------|------------|
| Speicher-Ersparnis | ✅ **8 Bytes pro Funktion** | ⭐⭐⭐⭐⭐ (Embedded) |
| Performance-Overhead | ⚠️ **~2-3 Zyklen pro Aufruf** | ⭐⭐ (messbar, aber klein) |
| Code-Klarheit | ✅ **Klarer (entweder/oder)** | ⭐⭐⭐ |
| Type-Safety | ⚠️ **Weniger explizit** | ⭐⭐ |
| Implementierungs-Aufwand | ✅ **Niedrig** | ⭐⭐⭐ |

**Gesamtbewertung: 7/10** - Empfehlung: **Implementieren**, aber Fallback-Body-Strategie zuerst klären.



