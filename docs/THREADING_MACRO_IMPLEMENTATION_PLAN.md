# Implementierungsplan: Threading-Macro `->>` für Standard-Clojure-Kompatibilität

## Problem-Analyse

### Aktuelles Problem
Die Standard-Clojure-Implementierung von `->>` kann nicht direkt verwendet werden, weil:

1. **Makro-Expansion zwischen Parsing und Evaluation**: In tiny-clj werden Makros zwischen Parsing und Evaluation expandiert (via `eval_function_call` in `ast_canon.c:307`), genau wie in Clojure "ahead-of-time". Die Makro-Funktion wird jedoch als Clojure-Funktion evaluiert, nicht als Compile-Zeit-Makro.
2. **Rekursive Funktionen**: Die Standard-Implementierung verwendet rekursive Funktionen (`append-last`), die bei langen Listen zu Stack-Overflow führen
3. **Evaluation von `reverse` während Expansion**: `(list 'reverse ...)` erzeugt Code, der während der Makro-Expansion evaluiert werden muss, was zu Problemen führt

### Standard-Clojure-Implementierung (Referenz)
```clojure
(defmacro ->> [x & forms]
  (let [thread-step (fn thread-step [x forms]
                      (if (nil? forms)
                        x
                        (let [form (first forms)
                              threaded (if (list? form)
                                         (let [op (first form)
                                               args (rest form)]
                                           (concat (list op) args (list x)))
                                         (list form x))
                              rest-forms (next forms)]
                          (if (nil? rest-forms)
                            threaded
                            (thread-step threaded rest-forms)))))]
    (thread-step x forms)))
```

**Problem**: `concat` nimmt nur 2 Argumente in tiny-clj, nicht 3 wie in Clojure.

## Anforderungen

### Funktionale Anforderungen
1. ✅ `(->> x)` → `x` (keine Forms)
2. ✅ `(->> x form)` → `(form x)` (eine Form, nicht-Liste)
3. ✅ `(->> x (op))` → `(op x)` (eine Form, Liste ohne Args)
4. ✅ `(->> x (op a b))` → `(op a b x)` (eine Form, Liste mit Args)
5. ✅ `(->> x form1 form2)` → `(form2 (form1 x))` (mehrere Forms)
6. ✅ `(->> x (op1 a) (op2 b))` → `(op2 b (op1 a x))` (mehrere Forms mit Args)

### Nicht-funktionale Anforderungen
1. **Keine tiefe Rekursion**: Vermeide Stack-Overflow bei langen Listen
2. **Ahead-of-Time-Expansion**: Expansion erfolgt zwischen Parsing und Evaluation (während `canonicalize_expr`), genau wie in Clojure
3. **Kompatibilität**: Verhalten identisch zu Standard-Clojure
4. **Performance**: Keine signifikanten Performance-Einbußen

## Lösungsansätze

### Ansatz 1: Iterative Lösung mit `cons` (Aktuell)
**Status**: ❌ Funktioniert nicht - verursacht Stack-Overflow

```clojure
(defmacro ->> [x & forms]
  (let [append-last (fn append-last [lst val]
                       (if (nil? (rest lst))
                         (list (first lst) val)
                         (cons (first lst) (append-last (rest lst) val))))
        ...])
```

**Problem**: Tiefe Rekursion bei langen Listen.

### Ansatz 2: `reverse` zur Laufzeit (Vorgeschlagen)
**Status**: ❌ Funktioniert nicht - `reverse` wird zur Laufzeit evaluiert

```clojure
(list 'reverse (list 'cons x (list 'reverse form)))
```

**Problem**: Erzeugt Code, der während der Makro-Expansion evaluiert werden muss, was zu Problemen führt.

### Ansatz 3: `concat` mit 2 Argumenten (Vorgeschlagen)
**Status**: ⚠️ Teilweise - `concat` nimmt nur 2 Argumente

```clojure
(concat (list op) (concat args (list x)))
```

**Problem**: Verschachtelte `concat`-Aufrufe sind ineffizient.

### Ansatz 4: Native Helper-Funktion (Empfohlen)
**Status**: ✅ Beste Lösung

Erstelle eine native C-Funktion `append-to-end`, die zur Makro-Expansion-Zeit verwendet werden kann.

## Implementierungsplan

### Phase 1: Analyse und Design (✅ Abgeschlossen)
- [x] Problem identifiziert
- [x] Anforderungen definiert
- [x] Lösungsansätze evaluiert

### Phase 2: Native Helper-Funktion implementieren

#### 2.1: C-Funktion `append_to_end` erstellen
**Datei**: `src/builtins.c` oder `src/macro_helpers.c` (neu)

```c
/**
 * @brief Append value to end of list (for macro expansion)
 * @param list List to append to
 * @param val Value to append
 * @return New list with val appended to end
 * @note Used during macro expansion (between parsing and evaluation)
 */
static ID append_to_end(ID list, ID val) {
    if (!list || !list_type_matches(TAG(list))) {
        return make_list(val, NULL);
    }
    
    CljList *lst = as_list(list);
    if (!lst->rest) {
        // Single element: (first) -> (first val)
        return make_list(lst->first, make_list(val, NULL));
    }
    
    // Multiple elements: collect all, then append val
    ID elements[256];
    int count = 0;
    
    CljList *cur = lst;
    while (cur && count < 256) {
        elements[count++] = cur->first;
        cur = cur->rest ? as_list(cur->rest) : NULL;
    }
    
    // Build new list: (first ... last val)
    CljList *result = make_list(val, NULL);
    for (int i = count - 1; i >= 0; i--) {
        result = make_list(elements[i], result);
    }
    
    return result;
}
```

#### 2.2: Makro-Helper-Registry erstellen
**Datei**: `src/macro_helpers.c` (neu)

```c
// Registry für Makro-Helper-Funktionen
typedef ID (*MacroHelperFn)(ID, ID);

static MacroHelperFn g_macro_helpers[16];
static const char* g_macro_helper_names[16];
static int g_macro_helper_count = 0;

void register_macro_helper(const char *name, MacroHelperFn fn) {
    if (g_macro_helper_count < 16) {
        g_macro_helper_names[g_macro_helper_count] = name;
        g_macro_helpers[g_macro_helper_count] = fn;
        g_macro_helper_count++;
    }
}

ID call_macro_helper(const char *name, ID arg1, ID arg2) {
    for (int i = 0; i < g_macro_helper_count; i++) {
        if (strcmp(g_macro_helper_names[i], name) == 0) {
            return g_macro_helpers[i](arg1, arg2);
        }
    }
    return NULL;
}
```

#### 2.3: Clojure-Wrapper-Funktion erstellen
**Datei**: `src/clojure.core.clj`

```clojure
; Helper function for macro expansion (native implementation)
(defn append-to-end [lst val] :native)
```

**Datei**: `src/builtins.c` oder `src/clojure_core.c`

```c
ID native_append_to_end(ID *args, unsigned int argc) {
    if (!validate_builtin_args(argc, 2, "append-to-end")) return NULL;
    return append_to_end(args[0], args[1]);
}
```

### Phase 3: `->>` Makro mit Helper-Funktion implementieren

**Datei**: `src/clojure.core.clj`

```clojure
(defmacro ->> [x & forms]
  (let [thread-step (fn thread-step [x forms]
                      (if (nil? forms)
                        x
                        (let [form (first forms)
                              threaded (if (list? form)
                                         (let [op (first form)
                                               args (rest form)]
                                           (if (nil? args)
                                             (list op x)
                                             (append-to-end form x)))
                                         (list form x))
                              rest-forms (next forms)]
                          (if (nil? rest-forms)
                            threaded
                            (thread-step threaded rest-forms)))))]
    (thread-step x forms)))
```

### Phase 4: Alternative: Direkte C-Implementierung

Falls die Helper-Funktion nicht funktioniert, direkte C-Implementierung:

**Datei**: `src/builtins.c`

```c
ID native_thread_last_macro(ID *args, unsigned int argc) {
    if (argc < 1) return NULL;
    
    ID x = args[0];
    if (argc == 1) return RETAIN(x);
    
    // Process forms from last to first
    ID result = x;
    for (int i = argc - 1; i >= 1; i--) {
        ID form = args[i];
        if (list_type_matches(TAG(form))) {
            CljList *lst = as_list(form);
            if (lst) {
                ID op = lst->first;
                CljList *args_list = lst->rest ? as_list(lst->rest) : NULL;
                if (!args_list) {
                    // (op) -> (op result)
                    result = make_list(op, make_list(result, NULL));
                } else {
                    // (op a b) -> (op a b result)
                    result = append_to_end(form, result);
                }
            }
        } else {
            // Non-list form -> (form result)
            result = make_list(form, make_list(result, NULL));
        }
    }
    
    return result;
}
```

### Phase 5: Testing

#### 5.1: Unit-Tests
- [ ] `(->> 5)` → `5`
- [ ] `(->> 5 inc)` → `6`
- [ ] `(->> 5 (+ 3))` → `8`
- [ ] `(->> [1 2 3] (map inc))` → `(2 3 4)`
- [ ] `(->> [1 2 3] (map inc) (filter even?))` → `(2 4)`
- [ ] `(->> {:a 1} (assoc :b 2) (get :b))` → `2`

#### 5.2: Edge-Cases
- [ ] Leere Liste: `(->> [])`
- [ ] Lange Listen: `(->> [1 2 ... 100] (map inc))`
- [ ] Verschachtelte Threading: `(->> x (-> y))`
- [ ] Makro-Expansion: `(macroexpand '(->> x (op)))`

#### 5.3: Performance-Tests
- [ ] Vergleich mit Standard-Clojure
- [ ] Stack-Overflow-Tests bei langen Listen
- [ ] Memory-Profiling

### Phase 6: Dokumentation

- [ ] Code-Kommentare
- [ ] README-Update
- [ ] API-Dokumentation

## Empfohlene Implementierung

### Option A: Native Helper-Funktion (Bevorzugt)
**Vorteile**:
- ✅ Keine tiefe Rekursion
- ✅ Effizient (C-Implementierung)
- ✅ Kompatibel mit Standard-Clojure-Verhalten
- ✅ Wiederverwendbar für andere Makros

**Nachteile**:
- ⚠️ Zusätzliche C-Funktion erforderlich
- ⚠️ Integration in Makro-Expansion-System

### Option B: Iterative Clojure-Lösung
**Vorteile**:
- ✅ Reine Clojure-Implementierung
- ✅ Keine C-Änderungen erforderlich

**Nachteile**:
- ❌ Schwierig ohne `concat` mit 3+ Argumenten
- ❌ Mögliche Performance-Probleme

## Nächste Schritte

1. **Implementiere `append_to_end` C-Funktion** (Phase 2.1)
2. **Registriere als native Funktion** (Phase 2.3)
3. **Aktualisiere `->>` Makro** (Phase 3)
4. **Teste Implementierung** (Phase 5)
5. **Dokumentiere Änderungen** (Phase 6)

## Risiken und Mitigation

| Risiko | Wahrscheinlichkeit | Impact | Mitigation |
|--------|-------------------|--------|------------|
| Stack-Overflow bei langen Listen | Hoch | Hoch | Native C-Implementierung verwenden |
| Performance-Einbußen | Mittel | Mittel | Profiling durchführen |
| Inkompatibilität mit Standard-Clojure | Niedrig | Hoch | Umfassende Tests durchführen |
| Integration in Makro-System | Mittel | Mittel | Schrittweise Implementierung |

## Erfolgs-Kriterien

- ✅ Alle Unit-Tests bestehen
- ✅ Keine Stack-Overflow-Fehler
- ✅ Verhalten identisch zu Standard-Clojure
- ✅ Performance akzeptabel (< 2x langsamer als Standard-Clojure)
- ✅ Code dokumentiert und wartbar

