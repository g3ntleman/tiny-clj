---
name: when-let + if-let Migration zu defmacro
overview: >
  `when-let` als `defmacro` hinzufügen; `if-let` vom Parser-Level-Hack
  (aus der Zeit vor defmacro) auf `defmacro` in `clojure.core` umstellen.
  Test-First.
todos:
  - id: tests-when-let
    content: "Failing tests in src/tests/test_let.c schreiben: test_when_let_basic, test_when_let_nil_falsy, test_when_let_false_falsy, test_when_let_multi_body, test_when_let_returns_nil_on_falsy"
    status: pending
  - id: implement-when-let
    content: "`defmacro when-let` in libs/clojure/core.clj nach `when-not` einfügen"
    status: pending
  - id: migrate-if-let
    content: "`defmacro if-let` in libs/clojure/core.clj nach `when-let` einfügen; Parser-Level-Expansion in src/parser.c (~Zeile 1282–1374) entfernen"
    status: pending
  - id: run-tests
    content: "Build + ./build/unit-tests -test test_let ausführen, dann volle Suite prüfen"
    status: pending
isProject: false
---

# `when-let` + `if-let` Migration zu `defmacro`

## Hintergrund

`if-let` wurde als Parser-Level-Hack implementiert (~80 Zeilen C in `parser.c:1282`),
weil `defmacro` damals noch nicht existierte. Das ist inzwischen technische Schuld:
`when-not`, `lazy-seq`, `->`, `->>` zeigen, dass `defmacro` vollständig funktioniert.

## Expansion

```clojure
(when-let [x expr] body1 body2...)
;; => (let [x expr] (when x body1 body2...))

(if-let [x expr] then)
(if-let [x expr] then else)
;; => (let [x expr] (if x then else))
```

**Clojure-Semantik:** Falsy = `nil` oder `false`. `when-let` unterstützt mehrere
Body-Formen (via `when`), `if-let` hat optionalen `else`-Branch.

---

## Schritt 1 – Tests für `when-let` (test_let.c)

Nach dem bestehenden `if-let`-Block in `src/tests/test_let.c` einfügen:

```c
// ============================================================================
// Tests for when-let macro
// ============================================================================

TEST(test_when_let_basic) {
    // (when-let [x 42] x) => 42
    CljObject *result = eval_string("(when-let [x 42] x)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(result));
}

TEST(test_when_let_nil_falsy) {
    // (when-let [x nil] x) => nil
    CljObject *result = eval_string("(when-let [x nil] x)", g_test_eval_state);
    TEST_ASSERT_NULL(result);
}

TEST(test_when_let_false_falsy) {
    // (when-let [x false] 99) => nil
    CljObject *result = eval_string("(when-let [x false] 99)", g_test_eval_state);
    TEST_ASSERT_NULL(result);
}

TEST(test_when_let_multi_body) {
    // (when-let [x 10] (+ x 1) (+ x 2)) => 12 (letzter Ausdruck)
    CljObject *result = eval_string("(when-let [x 10] (+ x 1) (+ x 2))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(12, as_fixnum(result));
}

TEST(test_when_let_returns_nil_on_falsy_multi_body) {
    // (when-let [x nil] 1 2 3) => nil (Body wird nicht ausgeführt)
    CljObject *result = eval_string("(when-let [x nil] 1 2 3)", g_test_eval_state);
    TEST_ASSERT_NULL(result);
}
```

Tests in `RUN_TEST_GROUP` eintragen.

---

## Schritt 2 – `defmacro when-let` (clojure/core.clj)

In `libs/clojure/core.clj` direkt nach `when-not` (~Zeile 64) einfügen:

```clojure
^#^{:doc "Evaluates body when binding test is truthy. Returns nil otherwise."}
(defmacro when-let [bindings & body]
  (let [binding (first bindings)
        test    (second bindings)]
    (list 'let bindings
      (cons 'when (cons binding body)))))
```

---

## Schritt 3 – `defmacro if-let` + Parser-Code entfernen

### 3a: `defmacro if-let` in `libs/clojure/core.clj` nach `when-let` einfügen

```clojure
^#^{:doc "Binds name to value of test. Evaluates then if truthy, else (or nil) otherwise."}
(defmacro if-let [bindings then & [else]]
  (let [binding (first bindings)
        test    (second bindings)]
    (list 'let bindings
      (list 'if binding then else))))
```

### 3b: Parser-Level-Expansion entfernen (`src/parser.c`)

Den `if-let`-Block in `parse_list_head` (~Zeile 1282–1374) vollständig entfernen:

```c
// ENTFERNEN: if (sym && sym->cname && strcmp(sym->cname, "if-let") == 0) { ... }
```

Der verbleibende Code (`parse_list_rest` etc.) bleibt unverändert.

---

## Betroffene Dateien

| Datei | Änderung |
|---|---|
| `src/tests/test_let.c` | 5 `when-let`-Tests + `RUN_TEST_GROUP`-Einträge |
| `libs/clojure/core.clj` | `defmacro when-let` + `defmacro if-let` nach `when-not` |
| `src/parser.c` | `if-let`-Parser-Block (~80 Zeilen) entfernen |

## Risiko

Die bestehenden `if-let`-Tests in `test_let.c` sichern die Korrektheit der Migration ab —
sie müssen nach dem Umbau weiterhin grün sein.
