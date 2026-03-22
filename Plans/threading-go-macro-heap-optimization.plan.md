# Threading- und Go-Macro Heap-Optimierung

## Overview

Threading-Macros (`some->`, `cond->`, `some->>`, `cond->>`) und `go`-Blöcke erzeugen
unverhältnismäßig viel Heap. Empirische Messungen zeigen 2–3x Peak-Overhead gegenüber
direktem Code. Die Ursachen sind identifiziert; dieser Plan beschreibt die Optimierungen
nach Impact sortiert.

## Messergebnisse (Baseline)

Jeweils `(defn f [] ...)` mit 5 Pipeline-Schritten, gemessen via `(heap (eval (read-string ...)))`:

| Macro     | Permanent (Bytes) | Peak (Bytes) | ASTCall (Bytes) |
|-----------|------------------:|-------------:|----------------:|
| `->` (Referenz) | 464         | 2,256        | 160             |
| `->>`     | 464               | 2,672        | 160             |
| `as->`    | 736               | 3,088        | 192             |
| `some->`  | **1,840**         | **7,296**    | **512**         |
| `cond->`  | ~1,840            | ~7,300       | ~512            |
| `go` (pro Block) | **352**   | 576          | 64              |

## Ursachenanalyse

### Problem 1: Doppelte Macro-Expansion in `some->` / `cond->`

`some->` expandiert zu verschachtelten `(-> G__ step)` Aufrufen:

```clojure
;; (some-> x (a 1) (b 2)) expandiert zu:
(let [G__2 x
      G__2 (if (nil? G__2) nil (-> G__2 (a 1)))   ;; innerer -> Macro!
      G__2 (if (nil? G__2) nil (-> G__2 (b 2)))]   ;; nochmal!
  G__2)
```

Jeder `(-> G__ step)` durchläuft die gesamte Macro-Expansion-Pipeline erneut:
- Macro-Lookup + `eval_function_call`
- ~3–4 temporäre List-Objekte pro innerer Expansion
- Extra: `if` + `nil?` AST-Knoten (permanent)

**Overhead pro Schritt:** ~275 Bytes permanent (vs. ~32 Bytes bei `->`)

### Problem 2: `->>` rekursives `append-last`

`->>` nutzt eine rekursive `append-last` Hilfsfunktion, die O(M) Cons-Zellen
pro Form mit M Argumenten erzeugt. JVM-Clojure nutzt Syntax-Quote (`~@`),
was effizienter ist.

### Problem 3: `go`-Block Overhead

Jeder `go`-Block erzeugt permanent:
- Closure 48 B + Map 160 B + TransientMap 16 B + ASTCall 64 B + Vector 64 B = **352 B/Block**

## Todos

### A – `some->` / `cond->`: Inline-Threading (Quick-Win, ~50% Peak-Ersparnis)

Statt `(-> G__ (step args...))` direkt `(step G__ args...)` in der Expansion ausgeben.
Eliminiert die doppelte Macro-Expansion komplett.

Betrifft: `libs/clojure/core.clj` Zeilen 299–355 (`some->`, `some->>`, `cond->`, `cond->>`)

```yaml
- id: a-inline-some-arrow
  content: "some->: (-> G__ step) durch direktes Inlining ersetzen – (list 'if (list 'nil? g) 'nil threaded) wobei threaded direkt (cons op (cons g args)) ist statt (list '-> g step)"
  status: pending

- id: a-inline-some-arrow-last
  content: "some->>: Analog – statt (list '->> g step) direkt (append-last (step) g) oder concat-basiert"
  status: pending

- id: a-inline-cond-arrow
  content: "cond->: (list '-> g step) durch direkte Konstruktion ersetzen"
  status: pending

- id: a-inline-cond-arrow-last
  content: "cond->>: Analog zu cond->"
  status: pending

- id: a-test-threading-heap
  content: "Heap-Regressions-Tests: (heap (eval (read-string ...))) für some->/cond-> – Peak muss unter alter Baseline liegen"
  status: pending
```

### B – `->>` append-last eliminieren (kleiner Gewinn, ~400 B Peak)

`append-last` durch `concat` oder direkten Vektor-Aufbau ersetzen.

```yaml
- id: b-thread-last-append
  content: "->> append-last durch (concat (butlast form) (list x (last form))) oder äquivalent ersetzen"
  status: pending
```

### C – `->` / `->>` als C-Builtins / Special Forms (maximaler Effekt, invasiv)

Die Macro-Expansion-Maschinerie komplett umgehen, indem `->` / `->>` direkt im
Canonicalizer (`ast_canon.c`) als Transformation behandelt werden.

```yaml
- id: c-thread-first-special-form
  content: "-> als Canonicalizer-Transformation in ast_canon.c implementieren (kein eval_function_call nötig)"
  status: pending

- id: c-thread-last-special-form
  content: "->> analog als Canonicalizer-Transformation"
  status: pending

- id: c-remove-clj-macros
  content: "Clojure-Macro-Definitionen für ->/->> entfernen (werden von C ersetzt)"
  status: pending
```

### D – `go`-Block Result-Channel leichtgewichtiger

Result-Channel von `CljPersistentMap` (160 B) auf eine spezialisierte
leichtere Struktur umstellen (z.B. struct mit value + closed Flag).

```yaml
- id: d-lightweight-result-channel
  content: "Leichtgewichtige Result-Channel-Struktur statt CljPersistentMap (Ziel: <48 B statt 160 B)"
  status: pending
```

### Cleanup

```yaml
- id: cleanup
  content: "Sourcecode aufräumen – Debug-Code, temporäre Workarounds, tote Codepfade, überflüssige Kommentare und nicht mehr benötigte Hilfsfunktionen entfernen"
  status: pending
```

## Test-Kommandos

```bash
# Threading-Macro-Tests
./build/unit-tests --test "test_threading_macros/*"

# Go-Block-Tests
./build/unit-tests --test "test_go_blocks/*"

# Heap-Regressions-Test für macroexpand
./build/unit-tests --test "test_runtime_stats/runtime_stats_heap_eval_read_string_macroexpand_1_thread_first*"

# Breakout-Tests (Hauptverbraucher)
./build/unit-tests --test "test_breakout_contract/*"

# REPL-Einzelmessungen
./build/tiny-clj-repl -e '(heap (eval (read-string "(defn f [] (some-> {:a 1} (assoc :b 2) (assoc :c 3) (assoc :d 4) (assoc :e 5) (assoc :f 6)))")))'
```

## Priorität

A > B > C > D (nach Aufwand/Nutzen-Verhältnis). A allein senkt den Peak von
`some->`/`cond->` um geschätzt 2–3 KB pro Aufruf und ist risikoarm.
