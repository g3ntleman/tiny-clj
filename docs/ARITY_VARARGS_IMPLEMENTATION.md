# Implementierungsvorschläge: Verschiedene Aritäten und Varargs

**Datum**: 2025-12-XX  
**Status**: Vorschläge (nicht implementiert)

## Übersicht

Beide Features haben **Performance-Overhead**, da sie zur Laufzeit zusätzliche Prüfungen erfordern:

1. **Verschiedene Aritäten**: Arity-Dispatch bei jedem Funktionsaufruf
2. **Varargs**: Argumente müssen in Sequenz/Liste gesammelt werden

## 1. Verschiedene Aritäten

### Problem
Aktuell unterstützen Clojure-Funktionen nur eine feste Parameteranzahl:
```c
// In eval_function_call():
int param_count = func->params ? vector_count(func->params) : 0;
if (argc != param_count) {
    throw_exception(EXCEPTION_ARITY, "Arity mismatch in function call", NULL, 0, 0);
    return NULL;
}
```

### Vorschlag: Arity-Dispatch-Tabelle

**Struktur-Erweiterung**:
```c
typedef struct {
    CljObject base;
    CljVector *params;  // Parameter vector (can be NULL if no parameters)
    ID body;  // Function body (AST to evaluate)
    CljList *env_stack;
    CljSymbol *name;  // Function name symbol (konsistent mit Clojure-Semantik)
    CljNamespace *ns;
    
    // NEU: Arity-Dispatch (nur bei Bedarf allokiert)
    CljVector *arity_bodies;  // Vector: Index = Arity, Value = Body (AST) (NULL = feste Arity)
    ID varargs_param;         // Symbol für & args Parameter (NULL wenn keine varargs)
    // OPTIMIERUNG für Embedded: min_arity/max_arity aus arity_bodies ableitbar (spart 8 Bytes)
    // OPTIMIERUNG für Embedded: fixed_param_count aus params ableitbar (spart 4 Bytes)
    // OPTIMIERUNG: Vector statt Map für O(1) Indexzugriff (spart Speicher + schneller)
} CljFunction;
```

**Hinweis**: `name` sollte `CljSymbol*` sein (nicht `const char*`), da Funktionen in Clojure durch Symbole identifiziert werden. Dies ist konsistenter mit der Clojure-Semantik und ermöglicht bessere Integration mit Namespace-System und Metadata.

**Implementierung**:
```c
// In eval_function_call():
ID eval_function_call(ID fn, ID *args, int argc, CljMap *env, EvalState *st) {
    CljFunction *func = as_function(fn);
    
    // Arity-Dispatch
    if (func->arity_bodies) {
        // Funktion mit mehreren Aritäten
        // Vector: Index = Arity, O(1) Zugriff
        ID body = NULL;
        if (argc >= 0 && argc < vector_count(func->arity_bodies)) {
            body = vector_nth(func->arity_bodies, argc);
        }
        if (!body) {
            // Clojure-Fidelity: keine impliziten Fallbacks
            throw_exception(EXCEPTION_ARITY, "Arity mismatch", NULL, 0, 0);
            return NULL;
        }
        
        // Evaluieren mit gefundenem Body
        // ... (wie bisher)
    } else {
        // Einfache Funktion mit fester Arity (aktuelles Verhalten)
        int param_count = func->params ? vector_count(func->params) : 0;
        if (argc != param_count) {
            throw_exception(EXCEPTION_ARITY, "Arity mismatch", NULL, 0, 0);
            return NULL;
        }
        // ... (wie bisher)
    }
}
```

**Clojure-Fidelity**: Es gibt keinen impliziten Fallback mehr; eine fehlende Arity führt immer zu `EXCEPTION_ARITY`, genau wie in der JVM-Implementierung.

**Performance-Optimierung**:
- **Fast Path**: Funktionen mit fester Arity (kein Overhead)
- **Vector-Indexzugriff**: `vector_nth` für Arity-Lookup (O(1), schneller als Map)
- **Speicher-effizient**: Vector ist kompakter als Map (keine Key-Value-Paare)
- **Caching**: Häufige Aritäten können gecacht werden

**Parser-Änderungen**:
```clojure
;; Mehrere Aritäten:
(defn add
  ([x] x)
  ([x y] (+ x y))
  ([x y z] (+ x y z)))

;; Wird zu:
;; arity_bodies = vector([NULL, body1, body2, body3])  // Index = Arity, count = 4
;; min_arity = 1 (erste nicht-NULL Stelle)
;; max_arity = 3 (count - 1 = 4 - 1)
```

### Helper-Funktionen (für Embedded-Optimierung)

```c
// Berechne min_arity aus arity_bodies Vector (spart 4 Bytes pro Funktion)
static int compute_min_arity(CljVector *arity_bodies) {
    if (!arity_bodies) return 0;
    int count = vector_count(arity_bodies);
    for (int i = 0; i < count; i++) {
        if (vector_nth(arity_bodies, i) != NULL) {
            return i;  // Erste nicht-NULL Stelle = min_arity
        }
    }
    return 0;
}

// Berechne max_arity aus arity_bodies Vector (spart 4 Bytes pro Funktion)
static int compute_max_arity(CljVector *arity_bodies) {
    if (!arity_bodies) return -1;  // -1 = unbounded
    int count = vector_count(arity_bodies);
    return (count > 0) ? (count - 1) : -1;  // max_arity = letzter Index = count - 1
}
```

### Performance-Impact
- **Overhead pro Aufruf**: 
  - 1× `vector_nth` (O(1) Indexzugriff, schneller als `map_get`)
  - Optional: Berechnung von min/max_arity (nur bei Fehlerfall, kann gecacht werden)
- **Speicher**: 
  - Zusätzlicher Vector pro Funktion mit mehreren Aritäten
  - **Ersparnis: 8 Bytes pro Funktion** (min_arity + max_arity)
  - **Zusätzliche Ersparnis**: Vector ist kompakter als Map (keine Key-Value-Paare)
- **Vergleich**: 
  - Native Funktionen haben bereits variadische Unterstützung (kein Overhead für sie)
  - Vector ist effizienter als Map für sequenzielle Indizes (0, 1, 2, ...)

## 2. Varargs (`& args`)

### Problem
Aktuell werden varargs nur für native Funktionen unterstützt. Clojure-Funktionen können keine `& args` verwenden.

### Vorschlag: Varargs-Parameter-Erkennung

**Parser-Änderungen**:
```c
// In parser.c: parse_fn_params()
// Erkennen von & args:
// [x y & rest] → params = [x, y], varargs_param = rest

typedef struct {
    ID *params;
    int param_count;
    ID varargs_param;  // NULL wenn keine varargs
} ParsedParams;

ParsedParams parse_function_params(CljVector *param_vec) {
    ParsedParams result = {NULL, 0, NULL};
    
    // Suche nach & Symbol
    for (int i = 0; i < vector_count(param_vec); i++) {
        ID param = vector_nth(param_vec, i);
        if (TAG(param) == CLJ_SYMBOL && as_symbol(param) == SYM_AMPERSAND) {
            // & gefunden - nächster Parameter ist varargs
            if (i + 1 < vector_count(param_vec)) {
                result.varargs_param = vector_nth(param_vec, i + 1);
                result.param_count = i;  // Anzahl fester Parameter
                break;
            }
        }
    }
    
    // ... Parameter extrahieren
    return result;
}
```

**Struktur-Erweiterung**:
```c
typedef struct {
    // ... (wie bisher)
    ID varargs_param;  // NULL wenn keine varargs
    int fixed_param_count;  // Anzahl fester Parameter (vor & args)
} CljFunction;
```

**Implementierung in eval_function_call()**:
```c
ID eval_function_call(ID fn, ID *args, int argc, CljMap *env, EvalState *st) {
    CljFunction *func = as_function(fn);
    
    // Prüfe feste Parameter (aus params ableiten, spart 4 Bytes)
    int fixed_count = compute_fixed_param_count(func->params, func->varargs_param);
    if (argc < fixed_count) {
        throw_exception(EXCEPTION_ARITY, "Not enough arguments", NULL, 0, 0);
        return NULL;
    }
    
    // Erstelle Call Frame mit festen Parametern
    CallFrame *call_frame = ...;
    frame_set_bindings(call_frame, params_array, args, fixed_count);
    
    // Varargs: Sammle restliche Argumente in Liste
    if (func->varargs_param) {
        CljList *varargs_list = NULL;
        for (int i = fixed_count; i < argc; i++) {
            varargs_list = make_list(args[i], varargs_list);
        }
        
        // Binde varargs-Parameter
        frame_set_binding(call_frame, func->varargs_param, varargs_list);
    }
    
    // ... (wie bisher)
}
```

**Performance-Optimierung**:
- **Fast Path**: Funktionen ohne varargs (kein Overhead)
- **Lazy Evaluation**: Varargs-Liste nur bei Bedarf erstellen
- **Stack-Allokation**: Für kleine Argumentanzahlen Stack-Array verwenden

**Parser-Änderungen**:
```clojure
;; Varargs:
(defn sum [x & rest]
  (apply + x rest))

;; Wird zu:
;; fixed_param_count = 1
;; varargs_param = 'rest
;; params = [x]
```

### Helper-Funktion (für Embedded-Optimierung)

```c
// Berechne fixed_param_count aus params (spart 4 Bytes pro Funktion)
static int compute_fixed_param_count(CljVector *params, ID varargs_param) {
    if (!params) return 0;
    int count = vector_count(params);
    if (varargs_param) {
        // Suche & Symbol in params
        for (int i = 0; i < count; i++) {
            ID param = vector_nth(params, i);
            if (TAG(param) == CLJ_SYMBOL && as_symbol(param) == SYM_AMPERSAND) {
                return i;  // Anzahl Parameter vor &
            }
        }
    }
    return count;  // Keine varargs, alle Parameter sind fest
}
```

### Performance-Impact
- **Overhead pro Aufruf**: 
  - `make_list` für jedes varargs-Argument (N Allokationen für N varargs)
  - Zusätzliche Bindung im Call Frame
  - Optional: Berechnung von fixed_param_count (nur bei varargs, kann gecacht werden)
- **Speicher**: 
  - Zusätzliche Liste pro Funktionsaufruf mit varargs
  - **Ersparnis: 4 Bytes pro Funktion** (fixed_param_count)
- **Vergleich**: Native Funktionen haben bereits varargs (kein Overhead für sie)

## Kombinierte Implementierung

### Beispiel: `+` mit verschiedenen Aritäten und varargs
```clojure
(defn + 
  ([] 0)
  ([x] x)
  ([x y] (native-add x y))
  ([x y & rest] (apply + (native-add x y) rest)))
```

**Struktur** (optimiert für Embedded):
```c
CljFunction *plus_func = {
    .arity_bodies = vector([body0, body1, body2]),  // Vector: Index = Arity, count = 3
    // [0] = body0  // ([] 0)
    // [1] = body1  // ([x] x)
    // [2] = body2  // ([x y] (native-add x y))
    // min_arity = 0 (aus arity_bodies ableitbar: erste nicht-NULL Stelle)
    // max_arity = 2 (aus arity_bodies ableitbar: count - 1 = 3 - 1)
    // ABER: wegen varargs ist max_arity effektiv -1 (unbounded)
    .varargs_param = 'rest,  // != NULL bedeutet varargs vorhanden
    // fixed_param_count = 2 (aus params ableitbar, vor & rest)
};
```

**Dispatch-Logik** (mit Vector, O(1) Zugriff):
```c
if (func->arity_bodies && argc < vector_count(func->arity_bodies)) {
    ID body = vector_nth(func->arity_bodies, argc);
    if (body) {
        // Arity gefunden - verwende Body
        return eval_body(body, ...);
    }
}
if (argc > 2 && func->varargs_param) {
    // Varargs: verwende varargs-Body
    // Sammle args[2..argc-1] in Liste
}
```

## Performance-Vergleich

| Feature | Overhead pro Aufruf | Speicher | Status |
|---------|---------------------|----------|--------|
| Feste Arity (aktuell) | 0 | 0 | ✅ Implementiert |
| Verschiedene Aritäten | 1× vector_nth (O(1)) | +Vector pro Funktion, **-8 Bytes** (min/max_arity), **kompakter als Map** | ⏳ Vorschlag |
| Varargs | N× make_list | +Liste pro Aufruf, **-4 Bytes** (fixed_param_count) | ⏳ Vorschlag |
| Kombiniert | Beide Overheads | Beide, **-12 Bytes gesamt** | ⏳ Vorschlag |

**Embedded-Optimierung**: 
- Durch Ableitung von min_arity, max_arity und fixed_param_count sparen wir **12 Bytes pro Funktion**
- **Vector statt Map**: O(1) Indexzugriff statt Hash-Lookup, kompakterer Speicher (keine Key-Value-Paare)
- Kleiner Performance-Overhead beim ersten Zugriff (kann gecacht werden)

## Empfehlungen

### Minimierung des Performance-Overheads:

1. **Fast Path für feste Arity**:
   - Funktionen ohne `arity_bodies` haben keinen Overhead
   - Nur Funktionen mit mehreren Aritäten zahlen den Preis

2. **Optimierte Varargs**:
   - Stack-Allokation für kleine Argumentanzahlen (< 8)
   - Lazy Evaluation: Liste nur erstellen, wenn im Body verwendet

3. **Caching**:
   - Arity-Dispatch-Ergebnisse können gecacht werden
   - Varargs-Listen können wiederverwendet werden (wenn unverändert)

4. **Native Funktionen bevorzugen**:
   - Für Hot-Paths (z.B. `+`, `-`, `*`, `/`) native Implementierungen verwenden
   - Clojure-Funktionen mit verschiedenen Aritäten nur für seltene Fälle

## Implementierungsreihenfolge

1. **Phase 1**: Verschiedene Aritäten (einfacher, weniger Overhead)
2. **Phase 2**: Varargs (komplexer, mehr Overhead)
3. **Phase 3**: Kombiniert (beide Features zusammen)

## Code-Beispiele

### Verschiedene Aritäten
```clojure
(defn add
  ([x] x)
  ([x y] (+ x y))
  ([x y z] (+ x y z)))

(add 1)      ; → 1
(add 1 2)    ; → 3
(add 1 2 3)  ; → 6
```

### Varargs
```clojure
(defn sum [x & rest]
  (if (empty? rest)
    x
    (sum (+ x (first rest)) (rest rest))))

(sum 1 2 3 4)  ; → 10
```

### Kombiniert
```clojure
(defn concat
  ([] [])
  ([coll] coll)
  ([coll1 coll2] (into coll1 coll2))
  ([coll1 coll2 & rest] (apply concat (concat coll1 coll2) rest)))
```

## Referenzen

- Aktuelle Implementierung: `src/eval.c:118-123` (Arity-Check)
- Native varargs: `src/builtins.c` (native_*_variadic)
- Validation: `src/validation.c` (validate_min_arity)








