---
name: Threading-Makros implementieren
overview: Implementierung aller Clojure-kompatiblen Threading-Makros (->, ->>, as->, some->, some->>, cond->, cond->>) mit Test-First-Ansatz und DRY-Prinzip. Tests verwenden High-Level-Verhaltenstests mit (source) zur Verifikation.
todos:
  - id: write_tests
    content: High-Level-Tests für alle Threading-Makros in test_threading_macros.c schreiben (->, ->>, as->, some->, some->>, cond->, cond->>)
    status: pending
  - id: helper_functions
    content: Gemeinsame Hilfsfunktionen für Threading-Logik in clojure.core.clj implementieren (DRY-Prinzip)
    status: pending
    dependencies:
      - write_tests
  - id: implement_basic
    content: Basis-Threading-Makros -> und ->> implementieren
    status: pending
    dependencies:
      - helper_functions
  - id: implement_as
    content: as-> Makro implementieren
    status: pending
    dependencies:
      - helper_functions
  - id: implement_some
    content: some-> und some->> Makros implementieren
    status: pending
    dependencies:
      - implement_basic
  - id: implement_cond
    content: cond-> und cond->> Makros implementieren
    status: pending
    dependencies:
      - implement_basic
  - id: verify_tests
    content: Alle Tests ausführen und Clojure-Kompatibilität verifizieren
    status: pending
    dependencies:
      - implement_basic
      - implement_as
      - implement_some
      - implement_cond
---

# Threading-Makros Implementierung

## Übersicht

Implementierung aller Clojure-kompatiblen Threading-Makros mit Test-First-Ansatz und DRY-Prinzip. Die Makros werden in `clojure.core.clj` definiert und verwenden gemeinsame Hilfsfunktionen zur Vermeidung von Code-Duplikation.

## Zu implementierende Makros

1. **`->`** (thread-first): Fügt das erste Argument als erstes Argument in jeden nachfolgenden Funktionsaufruf ein
2. **`->>`** (thread-last): Fügt das erste Argument als letztes Argument in jeden nachfolgenden Funktionsaufruf ein
3. **`as->`**: Benennt das erste Argument um und fügt es an einer spezifischen Position ein
4. **`some->`**: Wie `->`, aber stoppt bei `nil`
5. **`some->>`**: Wie `->>`, aber stoppt bei `nil`
6. **`cond->`**: Bedingtes Threading mit thread-first Semantik
7. **`cond->>`**: Bedingtes Threading mit thread-last Semantik

## Clojure-Referenz-Implementierungen

### `->` (thread-first)
```clojure
(defmacro ->
  "Threads the expr through the forms. Inserts x as the
  second item in the first form, making a list of it if it is not a
  list already. If there are more forms, inserts the first form as the
  second item in second form, etc."
  {:added "1.0"}
  [x & forms]
  (loop [x x, forms forms]
    (if forms
      (let [form (first forms)
            threaded (if (seq? form)
                       (with-meta `(~(first form) ~x ~@(next form)) (meta form))
                       (list form x))]
        (recur threaded (next forms)))
      x)))
```

**Wichtig:** 
- Verwendet `seq?` um zu prüfen ob `form` eine Sequenz ist
- Erhält Metadata mit `with-meta`
- Wenn `form` keine Sequenz ist, erstellt `(list form x)`
- `~x` wird als zweites Element eingefügt (nach `first form`)

### `->>` (thread-last)
```clojure
(defmacro ->>
  "Threads the expr through the forms. Inserts x as the
  last item in the first form, making a list of it if it is not a
  list already. If there are more forms, inserts the first form as the
  last item in second form, etc."
  {:added "1.1"}
  [x & forms]
  (loop [x x, forms forms]
    (if forms
      (let [form (first forms)
            threaded (if (seq? form)
              (with-meta `(~(first form) ~@(next form)  ~x) (meta form))
              (list form x))]
        (recur threaded (next forms)))
      x)))
```

**Wichtig:**
- Gleiche Struktur wie `->`, aber `~x` wird am Ende eingefügt
- `~@(next form)` expandiert die restlichen Argumente vor `~x`

### `as->`
```clojure
(defmacro as->
  "Binds name to expr, evaluates the first form in the lexical context
  of that binding, then binds name to that result, repeating for each
  successive form, returning the result of the last form."
  {:added "1.5"}
  [expr name & forms]
  `(let [~name ~expr
         ~@(interleave (repeat name) (butlast forms))]
     ~(if (empty? forms)
        name
        (last forms))))
```

**Wichtig:**
- Verwendet `let` mit `interleave` um Bindungen zu erstellen
- `interleave (repeat name) (butlast forms)` erstellt Paare: `[name form1, name form2, ...]`
- Wenn keine `forms`, gibt `name` zurück, sonst `(last forms)`

### `some->`
```clojure
(defmacro some->
  "When expr is not nil, threads it into the first form (via ->),
  and when that result is not nil, through the next etc"
  {:added "1.5"}
  [expr & forms]
  (let [g (gensym)
        steps (map (fn [step] `(if (nil? ~g) nil (-> ~g ~step)))
                   forms)]
    `(let [~g ~expr
           ~@(interleave (repeat g) (butlast steps))]
       ~(if (empty? steps)
          g
          (last steps)))))
```

**Wichtig:**
- Verwendet `gensym` für temporäre Variable
- Jeder Schritt prüft `(nil? ~g)` und gibt `nil` zurück wenn nil
- Nutzt `->` für Threading innerhalb jedes Schritts
- `interleave` erstellt Bindungen: `[g step1, g step2, ...]`

### `some->>`
```clojure
(defmacro some->>
  "When expr is not nil, threads it into the first form (via ->>),
  and when that result is not nil, through the next etc"
  {:added "1.5"}
  [expr & forms]
  (let [g (gensym)
        steps (map (fn [step] `(if (nil? ~g) nil (->> ~g ~step)))
                   forms)]
    `(let [~g ~expr
           ~@(interleave (repeat g) (butlast steps))]
       ~(if (empty? steps)
          g
          (last steps)))))
```

**Wichtig:**
- Gleiche Struktur wie `some->`, aber verwendet `->>` statt `->`

### `cond->`
```clojure
(defmacro cond->
  "Takes an expression and a set of test/form pairs. Threads expr (via ->)
  through each form for which the corresponding test
  expression is true. Note that, unlike cond branching, cond-> threading does
  not short circuit after the first true test expression."
  {:added "1.5"}
  [expr & clauses]
  (assert (even? (count clauses)))
  (let [g (gensym)
        steps (map (fn [[test step]] `(if ~test (-> ~g ~step) ~g))
                   (partition 2 clauses))]
    `(let [~g ~expr
           ~@(interleave (repeat g) (butlast steps))]
       ~(if (empty? steps)
          g
          (last steps)))))
```

**Wichtig:**
- Prüft dass `clauses` gerade Anzahl hat mit `assert`
- Verwendet `partition 2` um test/form Paare zu erstellen
- Jeder Schritt prüft `test` und wendet `->` an wenn true, sonst gibt `g` zurück
- Threaded durch ALLE true tests (kein Short-Circuit)

### `cond->>`
```clojure
(defmacro cond->>
  "Takes an expression and a set of test/form pairs. Threads expr (via ->>)
  through each form for which the corresponding test expression
  is true.  Note that, unlike cond branching, cond->> threading does not short circuit
  after the first true test expression."
  {:added "1.5"}
  [expr & clauses]
  (assert (even? (count clauses)))
  (let [g (gensym)
        steps (map (fn [[test step]] `(if ~test (->> ~g ~step) ~g))
                   (partition 2 clauses))]
    `(let [~g ~expr
           ~@(interleave (repeat g) (butlast steps))]
       ~(if (empty? steps)
          g
          (last steps)))))
```

**Wichtig:**
- Gleiche Struktur wie `cond->`, aber verwendet `->>` statt `->`

## Implementierungsstrategie

### Phase 1: Tests schreiben (Test-First)

**Datei:** `src/tests/test_threading_macros.c`

High-Level-Tests die:
- Die Makros direkt verwenden und Ergebnisse verifizieren
- `(source '->)` verwenden um Makro-Definitionen zu verifizieren
- `(macroexpand '(-> x (f)))` verwenden um Expansion zu testen
- Edge Cases und Clojure-Kompatibilität testen

**Test-Struktur:**
```c
// Tests für jedes Makro:
// - Basic functionality
// - Nested forms
// - Edge cases (nil, empty forms, non-seq forms)
// - Clojure compatibility
// - Source verification mit (source)
// - Macroexpansion verification
```

### Phase 2: Gemeinsame Hilfsfunktionen (DRY)

**Datei:** `src/clojure.core.clj`

Extrahiere gemeinsame Logik in Hilfsfunktionen:
- `thread-first-helper`: Gemeinsame Logik für `->` und `some->`
- `thread-last-helper`: Gemeinsame Logik für `->>` und `some->>`
- Wiederverwendung von `->`/`->>` in `some->`/`some->>` und `cond->`/`cond->>`

### Phase 3: Makro-Implementierungen

**Datei:** `src/clojure.core.clj`

Implementiere alle Makros basierend auf den Clojure-Referenzen:

1. **`->`** und **`->>`**: Basis-Threading-Makros mit `loop`/`recur` oder rekursiver Implementierung
2. **`as->`**: Named threading mit `let` und `interleave`
3. **`some->`** und **`some->>`**: Nil-safe Varianten die `->`/`->>` verwenden
4. **`cond->`** und **`cond->>`**: Bedingtes Threading mit `partition` und `->`/`->>`

## Dateien

- **[src/tests/test_threading_macros.c](src/tests/test_threading_macros.c)**: Neue Test-Datei für Threading-Makros
- **[src/clojure.core.clj](src/clojure.core.clj)**: Makro-Definitionen und Hilfsfunktionen

## Clojure-Kompatibilität

Alle Makros müssen exakt das Clojure-Verhalten nachbilden:
- Argument-Positionierung (zweites Element bei `->`, letztes bei `->>`)
- Behandlung von Listen vs. Vektoren (`seq?` Prüfung)
- Nil-Handling (bei `some->`/`some->>`)
- Verschachtelte Formen
- Metadata-Erhaltung (`with-meta`)
- `gensym` für temporäre Variablen
- `interleave` für Bindungs-Erstellung
- `partition` für Paar-Verarbeitung
- `assert` für Validierung bei `cond->`/`cond->>`

## Test-Beispiele

```clojure
; -> (thread-first)
(-> 5 (+ 3) (* 2))  ; => 16
(-> {:a 1} (assoc :b 2) (get :b))  ; => 2
(-> 5 inc)  ; => 6 (single form)
(-> 5)  ; => 5 (no forms)

; ->> (thread-last)  
(->> [1 2 3] (map inc) (filter even?))  ; => (2 4)
(->> [1 2 3] (map inc))  ; => (2 3 4)

; as->
(as-> 5 x (+ x 3) (* x 2))  ; => 16
(as-> 5 x)  ; => 5 (no forms)

; some->
(some-> {:a 1} :b inc)  ; => nil (stops at nil)
(some-> {:a 1} :a inc)  ; => 2
(some-> nil inc)  ; => nil

; some->>
(some->> [1 2 3] (map inc) (filter even?))  ; => (2 4)
(some->> nil (map inc))  ; => nil

; cond->
(cond-> 1 true inc false dec)  ; => 2
(cond-> 1 false inc true dec)  ; => 0
(cond-> 1)  ; => 1 (no clauses)

; cond->>
(cond->> [1 2 3] true (map inc) false (filter even?))  ; => (2 3 4)
```

## DRY-Prinzip

- `some->` und `some->>` verwenden `->` und `->>` intern
- `cond->` und `cond->>` verwenden `->` und `->>` intern
- Gemeinsame Patterns (`gensym`, `interleave`, `let`) wiederverwenden
- Einheitliche Argument-Verarbeitung

