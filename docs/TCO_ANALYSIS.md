# Tail Call Optimization (TCO) Analyse

## Vergleich mit anderen Lisp-Interpretern

### ClojureScript
- **Ansatz**: Explizite `recur`-Form, keine automatische TCO
- **Grund**: JavaScript unterstützt keine TCO nativ
- **Vorteil**: Klarheit - Programmierer muss explizit `recur` verwenden
- **Nachteil**: Erfordert manuelle Transformation von rekursiven zu iterativen Funktionen

### Scheme
- **Ansatz**: Garantierte TCO als Teil des Sprachstandards
- **Implementierung**: Alle Tail Calls werden automatisch optimiert
- **Vorteil**: Programmierer muss sich keine Gedanken machen
- **Nachteil**: Komplexere Implementierung erforderlich

### Common Lisp (CLISP)
- **Ansatz**: Optimiert selbst-rekursive Aufrufe von `DEFUN` und `LABELS`
- **Einschränkung**: Tail Calls zwischen separaten Funktionen innerhalb von `LABELS` werden nicht optimiert
- **Vorteil**: Einfache Fälle werden automatisch optimiert
- **Nachteil**: Nicht alle Tail Calls werden erkannt

### Emacs Lisp
- **Ansatz**: Keine TCO (traditionell), neuere Versionen mit lexikalischem Scoping können TCO für byte-kompilierte Funktionen implementieren
- **Implementierung**: Nutzt `setjmp`/`longjmp` für Stack-Frame-Management

## Aktuelle Implementierung in Tiny-CLJ

### Ansatz: Explizites `recur` + Optionaler Preprozess

Die aktuelle Implementierung folgt dem **Clojure-Ansatz**:

1. **Explizites `recur`** (wie in Clojure):
   - `recur` ist explizit implementiert und funktioniert korrekt
   - Programmierer kann `recur` direkt verwenden
   - `recur` wird zur Laufzeit durch TCO-Loop optimiert

2. **Optionaler Preprozess** (`transform_recursive_tail_calls`):
   - Transformiert rekursive Tail Calls automatisch zu `recur` zur Compile-Zeit
   - Erkennt Tail-Positionen in `if`, `when`, `do`
   - Erkennt selbst-rekursive Aufrufe durch Symbol-Vergleich
   - **Optional**: Programmierer kann auch explizit `recur` verwenden

3. **TCO-Loop** (`eval_function_call`):
   - Verwendet `_Thread_local` State (`g_recur_args`, `g_recur_arg_count`)
   - Iteriert über `recur`-Aufrufe statt Stack-Frames zu erstellen
   - Verhindert Stack-Overflow bei tiefer Rekursion

### Identifizierte Probleme

#### 1. Unvollständige Tail-Position-Erkennung (TEILWEISE BEHOBEN)

**Status**: `is_tail_position` und `transform_recursive_tail_calls` wurden erweitert:
- ✅ `if` - then/else Branches
- ✅ `when` - letzte Body-Expression
- ✅ `do` - letzte Expression
- ✅ `let` - letzte Body-Expression (HINZUGEFÜGT)
- ✅ `cond` - Expressions in jeder Branch (HINZUGEFÜGT)
- ❌ `loop` - letzte Expression wird noch nicht erkannt
- ⚠️ Verschachtelte Strukturen - teilweise unterstützt (rekursive Analyse in `is_tail_position`)

**Beispiel**:
```clojure
(defn test [n]
  (let [x (+ n 1)]
    (test x)))  ; ✅ Wird jetzt als Tail Call erkannt und zu recur transformiert
```

#### 2. Problem mit verschachtelten Funktionen

**Problem**: Globales `_Thread_local` State kann bei verschachtelten Funktionen zu Konflikten führen:

```clojure
(defn outer [x]
  (let [inner (fn [y]
                 (if (= y 0)
                   0
                   (recur (- y 1))))]  ; recur für inner
    (inner x)))  ; Was passiert hier?
```

**Aktueller Code** (Zeile 533-548):
```c
// Save recur state before evaluating body (in case nested functions use recur)
int saved_recur_arg_count = g_recur_arg_count;

// Evaluate function body
ID new_result = eval_body_with_params(...);

// Check if recur was triggered in THIS function
if (g_recur_arg_count >= 0 && saved_recur_arg_count == -1) {
    // Tail Call erkannt - recur was used in THIS function
    ...
}
```

**Problem**: Wenn eine verschachtelte Funktion `recur` verwendet, wird `g_recur_arg_count` gesetzt, aber es ist unklar, ob es für die äußere oder innere Funktion ist.

**Lösung**: Verwende einen Stack von Recur-States statt globalem State:
```c
typedef struct RecurState {
    ID args[16];
    int arg_count;
    struct RecurState *next;
} RecurState;

static _Thread_local RecurState *g_recur_stack = NULL;
```

#### 3. AST-Transformation zur Compile-Zeit

**Problem**: Die Transformation findet zur Compile-Zeit statt (`transform_recursive_tail_calls` in `eval_defn`), aber:
- Funktioniert nur für `defn`, nicht für `fn`
- Kann dynamische Funktionen nicht transformieren
- Erfordert, dass der Funktionsname bekannt ist

**Beispiel**:
```clojure
(defn make-counter [start]
  (fn [n]
    (if (= n 0)
      start
      (make-counter (+ start 1)))))  ; ❌ Wird nicht transformiert
```

**Lösung**: 
- Option 1: Lazy Transformation zur Laufzeit (wenn Funktion erstellt wird)
- Option 2: Explizite `recur`-Verwendung (wie ClojureScript)

#### 4. Fehlende Tail-Position-Erkennung in Operatoren

**Problem**: Operatoren wie `+`, `-`, etc. werden nicht als Tail-Position erkannt:

```clojure
(defn test [n]
  (+ 1 (test (- n 1))))  ; ❌ Wird nicht als Tail Call erkannt
```

**Grund**: `is_tail_position` prüft nur auf rekursive Funktionsaufrufe, nicht auf verschachtelte Ausdrücke.

**Lösung**: Erweitere die Analyse um verschachtelte Ausdrücke:
- Wenn ein Ausdruck in Tail-Position ist und nur einen rekursiven Aufruf enthält, ist der Aufruf auch in Tail-Position
- Beispiel: `(+ 1 (f x))` - wenn `(+ 1 ...)` in Tail-Position ist, dann ist `(f x)` auch in Tail-Position

## Empfohlene Verbesserungen

### 1. Erweiterte Tail-Position-Erkennung

```c
static bool is_tail_position(CljObject *expr, CljObject *body) {
    // ... bestehende Implementierung ...
    
    // Erweitere um let
    if (head == SYM_LET) {
        CljList *rest = as_list((ID)body_list->rest);
        if (rest && rest->rest) {
            CljList *body_exprs = as_list((ID)rest->rest);
            // Letzte Expression in let-Body ist in Tail-Position
            CljList *current = body_exprs;
            while (current && current->rest) {
                current = as_list((ID)current->rest);
            }
            if (current && is_tail_position(expr, current->first)) {
                return true;
            }
        }
    }
    
    // Erweitere um cond
    if (head == SYM_COND) {
        CljList *rest = as_list((ID)body_list->rest);
        while (rest) {
            CljList *clause = as_list((ID)rest->first);
            if (clause && clause->rest) {
                CljList *body = as_list((ID)clause->rest);
                if (body && is_tail_position(expr, body->first)) {
                    return true;
                }
            }
            rest = as_list((ID)rest->rest);
        }
    }
    
    // Rekursive Analyse verschachtelter Strukturen
    // ...
}
```

### 2. Recur-State-Stack für verschachtelte Funktionen

```c
typedef struct RecurState {
    ID args[16];
    int arg_count;
    struct RecurState *next;
} RecurState;

static _Thread_local RecurState *g_recur_stack = NULL;

static void push_recur_state(void) {
    RecurState *state = malloc(sizeof(RecurState));
    state->arg_count = -1;
    state->next = g_recur_stack;
    g_recur_stack = state;
}

static void pop_recur_state(void) {
    if (g_recur_stack) {
        RecurState *old = g_recur_stack;
        g_recur_stack = old->next;
        // Cleanup old->args
        free(old);
    }
}
```

### 3. Lazy Transformation zur Laufzeit

```c
static CljObject* transform_if_needed(CljFunction *func) {
    // Prüfe, ob Transformation bereits durchgeführt wurde
    if (func->transformed) {
        return func->body;
    }
    
    // Transformiere zur Laufzeit
    CljObject *transformed = transform_recursive_tail_calls(
        func->body, func->name, func->params, func->param_count, NULL);
    
    if (transformed) {
        func->body = transformed;
        func->transformed = true;
    }
    
    return transformed;
}
```

## Fazit

Die aktuelle Implementierung folgt dem **Clojure-Ansatz** und ist **grundsätzlich korrekt**:

1. ✅ **Explizites `recur`**: Funktioniert korrekt (wie in Clojure)
2. ✅ **Optionaler Preprozess**: Transformiert rekursive Tail Calls automatisch zu `recur`
3. ✅ **Unterstützte Special Forms**: `if`, `when`, `do`, `let`, `cond` (nach Erweiterung)
4. ⚠️ **Noch nicht unterstützt**: `loop` (könnte erweitert werden)
5. ⚠️ **Risiko**: Globales State kann bei verschachtelten Funktionen zu Konflikten führen

**Vergleich mit ClojureScript**:
- ✅ **Gleich**: Explizites `recur` funktioniert
- ✅ **Gleich**: Optionaler Preprozess für automatische Transformation
- ✅ **Besser**: Unterstützt mehr Special Forms (`let`, `cond`) als ursprünglich
- ⚠️ **Unterschied**: Globales State statt Stack für verschachtelte Funktionen

**Empfehlung**: 
- ✅ **Erledigt**: `is_tail_position` und `transform_recursive_tail_calls` um `let` und `cond` erweitert
- ⚠️ **Optional**: Erweitere um `loop`-Unterstützung
- ⚠️ **Optional**: Implementiere Recur-State-Stack für verschachtelte Funktionen (wenn Probleme auftreten)
- ✅ **Klar**: Explizite `recur`-Verwendung bleibt immer möglich (wie in Clojure)

