# AST-Transformation: Listen zu Vektoren (On-the-fly)

## Konzept

**Ziel:** Listen im AST (Funktionsaufrufe) werden nach dem Parsing in Vektoren umgewandelt, damit beim Funktionsaufruf kein Umkopieren nötig ist.

**Zeitpunkt:** Nach dem Parsing, z.B. im Macro-Expander oder beim Evaluieren (on-the-fly)

**Transformation:**
- `(f a b c)` → Liste `(a b c)` wird zu Vektor `[a b c]` transformiert
- Funktionsaufrufe können dann direkt den Vektor verwenden
- Keine Array-Konvertierung mehr nötig

## Aktueller Stand

**Aktuelle Implementierung:**
- Parser erstellt Listen: `(f a b c)` → `CljList` mit Elementen `a, b, c`
- `call_function_with_args()` konvertiert Listen → Stack-Arrays (`alloc_obj_array()`)
- `eval_function_call()` erwartet `ID *args` (Array)
- Builtins erwarten `ID *args, unsigned int argc` (Array)
- Special Forms extrahieren Argumente manuell aus `CljList *list`
- `env_extend_stack()` erwartet `ID *params` und `ID *values` (Arrays)
- Parameter in Funktionen sind bereits Vektoren (`CljFunction->params` ist `CljVector*`)

**Code-Beispiele:**

```c
// Aktuell: call_function_with_args()
CljObject *args_stack[16];
ID *args = (ID*)alloc_obj_array(argc, args_stack);
for (int i = 0; i < argc; i++) {
    args[i] = eval_arg(list, i + 1, env, st);
    args[i] = RETAIN(args[i]);
}
ID result = eval_function_call(fn, args, argc, env, st);
// ... cleanup ...
free_obj_array((ID*)args, args_stack);

// Aktuell: Builtins
ID nth2(ID *args, unsigned int argc) {
    ID coll = args[0];
    ID idx = args[1];
    // ...
}

// Aktuell: Special Forms
ID eval_def(CljList *list, CljMap *env, EvalState *st) {
    CljObject *symbol = list_get_element(list, 1);
    CljObject *value_expr = list_get_element(list, 2);
    // ...
}
```

## Vorschlag: AST-Transformation (Listen → Vektoren)

**Konzept:** Listen in Funktionsaufrufen werden im AST zu Vektoren transformiert.

**Zeitpunkt der Transformation:**
1. **Option A:** Im Macro-Expander (nach Parsing, vor Expansion)
2. **Option B:** Beim Evaluieren (on-the-fly in `eval_list()`)

**Transformation:**
```c
// Vorher: (f a b c) → CljList mit first=a, rest=(b c)
// Nachher: (f a b c) → CljList mit first=f, rest=[a b c] (Vektor)
```

**Implementierung:**
```c
// In macroexpand() oder eval_list() - on-the-fly Transformation
ID transform_list_to_vector(CljList *list) {
    if (!list) return NULL;
    
    // Erstelle Vektor aus List-Elementen
    CljVector *vec = make_vector(0, CLJ_VECTOR);
    CljList *current = list;
    while (current && current->first) {
        vec = vector_conj(vec, RETAIN(current->first));
        current = as_list(current->rest);
    }
    
    // Alte Liste freigeben
    RELEASE(list);
    
    return (ID)vec;
}

// In call_function_with_args() - vereinfacht
static ID call_function_with_args(ID fn, CljList *list, CljMap *env, EvalState *st) {
    // Argumente sind bereits Vektoren (durch AST-Transformation)
    CljVector *args_vec = (CljVector*)list->rest; // rest ist bereits Vektor
    
    // Evaluierte Argumente in neuem Vektor
    CljVector *eval_args = make_vector(vector_count(args_vec), CLJ_VECTOR);
    for (int i = 0; i < vector_count(args_vec); i++) {
        ID arg = vector_nth(args_vec, i);
        ID eval_arg = eval_parsed(arg, st, env);
        eval_args = vector_conj(eval_args, RETAIN(eval_arg));
    }
    
    // Direkt vector_as_array() verwenden (kein Copy nötig)
    ID *args_array = vector_as_array(eval_args);
    int argc = vector_count(eval_args);
    
    ID result = eval_function_call(fn, args_array, argc, env, st);
    
    RELEASE(eval_args);
    return result;
}
```

**Wichtig:** API bleibt unverändert! Nur interne AST-Transformation.

## Vorteile

### 1. Kein Umkopieren beim Funktionsaufruf

**Problem:** Aktuell werden Listen in Arrays kopiert (`alloc_obj_array()`).

**Lösung:** Listen werden im AST zu Vektoren transformiert → direkter Zugriff ohne Copy.

- Keine `alloc_obj_array()` / `free_obj_array()` nötig
- Keine Stack-Array-Allokation
- Direkter `vector_as_array()` Zugriff (O(1), kein Copy)

### 2. Einfacher für Macro-Expansion

**Problem:** Macro-Argumente müssen als AST-Nodes übergeben werden (un-evaluiert).

**Aktuell:**
```c
// In macroexpand_1()
ID *macro_args = alloc_obj_array(argc, args_stack);
for (int i = 0; i < argc; i++) {
    macro_args[i] = list_get_element(list, i + 1); // Un-evaluiert
    RETAIN(macro_args[i]);
}
ID expanded = eval_function_call(macro_fn, macro_args, argc, ...);
free_obj_array(macro_args, args_stack);
```

**Mit AST-Transformation:**
```c
// In macroexpand_1() - Argumente sind bereits Vektoren
CljVector *macro_args = (CljVector*)list->rest; // rest ist bereits Vektor
ID *args_array = vector_as_array(macro_args); // Direkter Zugriff, kein Copy
int argc = vector_count(macro_args);
ID expanded = eval_function_call(macro_fn, args_array, argc, ...);
// Kein free_obj_array() nötig!
```

**Vorteil:** Keine Array-Konvertierung nötig, direkter Vektor-Zugriff.

### 3. Weniger Memory-Management

**Aktuell:**
- Stack-Array-Allokation (`alloc_obj_array()`)
- Heap-Allokation bei >16 Argumenten
- Explizite `free_obj_array()` Aufrufe
- Manuelle `RETAIN()` / `RELEASE()` für jedes Argument

**Mit AST-Transformation:**
- Vektoren werden einmalig im AST erstellt (bei Transformation)
- Keine `alloc_obj_array()` / `free_obj_array()` nötig
- `vector_as_array()` gibt direkten Zugriff (kein Copy)
- Vektoren werden automatisch von GC verwaltet

### 4. Weniger Code

**Entfernt werden können:**
- `alloc_obj_array()` / `free_obj_array()` Aufrufe in `call_function_with_args()`
- Stack-Array-Deklarationen (`CljObject *args_stack[16]`)
- Array-Konvertierungs-Loops

**Geschätzte Code-Reduktion:** ~30-50 Zeilen (in `call_function_with_args()`)

### 5. Performance

**Vorteil:** Kein Umkopieren nötig!

**Aktuell:**
```c
// Listen → Arrays kopieren
CljObject *args_stack[16];
ID *args = alloc_obj_array(argc, args_stack);  // Kopiert Elemente
for (int i = 0; i < argc; i++) {
    args[i] = eval_arg(list, i + 1, env, st);  // Weitere Kopien
    args[i] = RETAIN(args[i]);
}
// ... Funktionsaufruf ...
free_obj_array(args, args_stack);  // Cleanup
```

**Mit AST-Transformation:**
```c
// Vektoren sind bereits im AST → direkter Zugriff
CljVector *args_vec = (CljVector*)list->rest;  // Kein Copy!
ID *args_array = vector_as_array(args_vec);  // Direkter Zugriff, O(1), kein Copy
// ... Funktionsaufruf mit args_array ...
// Kein free_obj_array() nötig!
```

**Fazit:** Deutlich schneller - kein Umkopieren, direkter Zugriff.

### 6. Einfacherer Code in Special Forms

**Aktuell:**
```c
ID eval_let(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    CljObject *bindings_vec = list_get_element(list, 1);
    // ... manuelle List-Traversierung für weitere Argumente
}
```

**Mit AST-Transformation:**
```c
ID eval_let(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    // rest ist bereits Vektor (durch AST-Transformation)
    CljVector *args = (CljVector*)list->rest;
    CljObject *bindings_vec = vector_nth(args, 0);
    // ... direkter Vektor-Zugriff für weitere Argumente
}
```

**Vorteil:** Konsistenter Zugriff, keine List-Traversierung nötig. **Aber:** API bleibt unverändert!

## Nachteile

### 1. AST-Transformation nötig

**Betroffene Stellen:**
- Transformation in `macroexpand()` oder `eval_list()`
- `call_function_with_args()` muss Vektoren statt Listen verwenden
- Special Forms müssen Vektoren statt Listen verwenden

**Geschätzte Anzahl:** ~10-20 Stellen im Code

**Risiko:** Mittleres Risiko - AST-Transformation muss korrekt implementiert werden

### 2. API bleibt unverändert

**Wichtig:** Builtins und Special Forms behalten ihre Signatur!

**Builtins:**
```c
// Signatur bleibt: ID *args, unsigned int argc
ID nth2(ID *args, unsigned int argc) {
    ID coll = args[0];
    ID idx = args[1];
}

// In call_function_with_args():
CljVector *args_vec = (CljVector*)list->rest;
ID *args_array = vector_as_array(args_vec);  // Direkter Zugriff
int argc = vector_count(args_vec);
ID result = eval_function_call(fn, args_array, argc, env, st);
```

**Vorteil:** Keine API-Änderungen nötig, nur interne Transformation!

### 3. Special Forms

**Aktuell:**
- Special Forms erhalten `CljList *list` und extrahieren Argumente manuell
- `list_get_element()` für Zugriff

**Mit AST-Transformation:**
- Special Forms erhalten weiterhin `CljList *list`
- Aber `list->rest` ist bereits Vektor (durch Transformation)
- `vector_nth()` für Zugriff statt `list_get_element()`

**Nachteil:** Special Forms müssen angepasst werden (aber API bleibt gleich)

### 4. Performance-Überlegungen

**Transformation-Overhead:**
- Listen → Vektoren Transformation kostet einmalig (bei AST-Transformation)
- Aber: Später keine Array-Konvertierung mehr nötig
- Netto-Gewinn: Deutlich schneller bei wiederholten Funktionsaufrufen

**Vektor-Zugriff:**
- `vector_as_array()` gibt direkten Array-Zugriff (O(1), kein Copy)
- Kein Performance-Verlust bei korrekter Verwendung

**Fazit:** Netto-Performance-Gewinn durch Wegfall der Array-Konvertierung.

### 5. Kompatibilität

**Problem:** AST-Struktur ändert sich

**Betroffene Bereiche:**
- AST-Transformation muss korrekt implementiert werden
- Special Forms müssen Vektoren statt Listen verwenden
- Tests müssen möglicherweise angepasst werden (wenn sie AST-Struktur prüfen)

**Risiko:** Mittleres Risiko - Transformation muss korrekt sein, aber API bleibt gleich

## Empfehlung

### Option A: Im gleichen Schritt (mit Macro-Expansion)

**Vorteile:**
- Beide Änderungen zusammen testen
- AST-Transformation kann im Macro-Expander erfolgen
- Macro-Expansion profitiert sofort von Vektor-Argumenten
- Keine doppelte AST-Traversierung

**Nachteile:**
- Größere Refaktorierung
- Höheres Risiko
- Längere Entwicklungszeit

**Geeignet für:** Wenn Macro-Expansion sowieso große Änderungen erfordert

### Option B: Vor Macro-Expansion

**Vorteile:**
- Saubere Basis für Macro-Expansion
- Einfachere Macro-Argument-Übergabe
- Schrittweise Refaktorierung möglich

**Nachteile:**
- Zwei große Refaktorierungen nacheinander
- Längere Entwicklungszeit insgesamt

**Geeignet für:** Wenn AST-Transformation als Vorbereitung für Macros sinnvoll ist

### Option C: Nach Macro-Expansion

**Vorteile:**
- Macro-Expansion zuerst stabilisieren
- Dann optimieren (AST-Transformation)
- Klare Trennung der Änderungen

**Nachteile:**
- Macro-Expansion muss mit Listen arbeiten
- Später umstellen (doppelte Arbeit)

**Geeignet für:** Wenn Macro-Expansion Priorität hat und AST-Transformation optional ist

## Technische Umsetzung (wenn implementiert)

### 1. AST-Transformation (Listen → Vektoren)

**Option A: Im Macro-Expander**
```c
// In macroexpand() - nach Parsing, vor Expansion
ID transform_ast_lists_to_vectors(ID expr) {
    if (!expr || TAG(expr) != CLJ_LIST) {
        return expr;
    }
    
    CljList *list = as_list(expr);
    CljObject *op = LIST_FIRST(list);
    
    // Transformiere Argumente (rest) zu Vektor
    if (list->rest && TAG(list->rest) == CLJ_LIST) {
        CljList *args_list = as_list(list->rest);
        CljVector *args_vec = make_vector(0, CLJ_VECTOR);
        
        CljList *current = args_list;
        while (current && current->first) {
            // Rekursiv verschachtelte Listen transformieren
            ID transformed = transform_ast_lists_to_vectors(current->first);
            args_vec = vector_conj(args_vec, RETAIN(transformed));
            current = as_list(current->rest);
        }
        
        // Ersetze rest durch Vektor
        RELEASE(list->rest);
        list->rest = (CljObject*)args_vec;
    }
    
    // Rekursiv Operator transformieren
    if (op && TAG(op) == CLJ_LIST) {
        ID transformed_op = transform_ast_lists_to_vectors(op);
        ASSIGN(list->first, transformed_op);
    }
    
    return expr;
}
```

**Option B: Beim Evaluieren (on-the-fly)**
```c
// In eval_list() - on-the-fly Transformation
ID eval_list(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    if (!list) return NULL;
    
    CljObject *op = LIST_FIRST(list);
    
    // Transformiere Argumente zu Vektor (wenn noch Liste)
    if (list->rest && TAG(list->rest) == CLJ_LIST) {
        CljList *args_list = as_list(list->rest);
        CljVector *args_vec = make_vector(0, CLJ_VECTOR);
        
        CljList *current = args_list;
        while (current && current->first) {
            args_vec = vector_conj(args_vec, RETAIN(current->first));
            current = as_list(current->rest);
        }
        
        RELEASE(list->rest);
        list->rest = (CljObject*)args_vec;
    }
    
    // ... weiter wie bisher
}
```

### 2. call_function_with_args() - Vereinfachung

```c
static ID call_function_with_args(ID fn, CljList *list, CljMap *env, EvalState *st) {
    // Argumente sind bereits Vektor (durch AST-Transformation)
    CljVector *args_vec = (CljVector*)list->rest;
    int argc = vector_count(args_vec);
    
    // Evaluierte Argumente in neuem Vektor
    CljVector *eval_args = make_vector(argc, CLJ_VECTOR);
    for (int i = 0; i < argc; i++) {
        ID arg = vector_nth(args_vec, i);
        ID eval_arg = eval_parsed(arg, st, env);
        eval_args = vector_conj(eval_args, RETAIN(eval_arg));
    }
    
    // Direkter Array-Zugriff (kein Copy!)
    ID *args_array = vector_as_array(eval_args);
    
    // Funktion aufrufen (API bleibt gleich!)
    ID result = eval_function_call(fn, args_array, argc, env, st);
    
    RELEASE(eval_args);
    return result;
}
```

### 3. Builtins - API bleibt unverändert!

```c
// Signatur bleibt: ID *args, unsigned int argc
ID nth2(ID *args, unsigned int argc) {
    ID coll = args[0];
    ID idx = args[1];
    // ... wie bisher
}
```

### 4. Special Forms - Vektoren statt Listen

```c
ID eval_def(CljList *list, CljMap *env, EvalState *st) {
    // rest ist bereits Vektor (durch AST-Transformation)
    CljVector *args = (CljVector*)list->rest;
    
    if (vector_count(args) < 2) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, 
                       "def requires at least 2 arguments", 
                       NULL, 0, 0);
        return NULL;
    }
    
    CljObject *symbol = vector_nth(args, 0);
    CljObject *value_expr = vector_nth(args, 1);
    // ... wie bisher
}
```

### 5. env_extend_stack() - bleibt unverändert

```c
// Signatur bleibt: ID *params, ID *values, int count
CljMap* env_extend_stack(CljMap *parent_env, ID *params, ID *values, int count) {
    // ... wie bisher
}
```

## Fazit

**Vorteile überwiegen:**
- Kein Umkopieren beim Funktionsaufruf
- Einfacher für Macro-Expansion
- Weniger Code
- Deutlich bessere Performance

**Nachteile:**
- AST-Transformation nötig (~10-20 Stellen)
- Mittleres Risiko - Transformation muss korrekt sein

**Empfehlung:**
- **Option A (Im gleichen Schritt mit Macro-Expansion)** ist am sinnvollsten:
  - AST-Transformation kann im Macro-Expander erfolgen
  - Keine doppelte AST-Traversierung
  - Macro-Expansion profitiert sofort
  - Beide Änderungen zusammen testen

**Wichtig:**
- API bleibt unverändert! (Builtins, Special Forms behalten ihre Signatur)
- Nur interne AST-Transformation
- Deutlich einfacher als ursprünglich gedacht

