# Optimierte for-Expansion (Test-First)

## Overview

for-build so anpassen, dass die innerste Schleife map statt mapcat+(list body) nutzt. Eliminiert O(n) Cons-Allokationen pro for-Ausdruck.

## Kernidee

Statt Fallunterscheidung: **Immer** die innerste Schleife mit `map` statt `mapcat+(list body)`.

**Aktuell** (fuer `(for [x xs y ys] body)`):

```clojure
(mapcat (fn [x]
          (mapcat (fn [y] (list body)) ys))  ;; (list body) pro Element!
        xs)
```

**Optimiert:**

```clojure
(mapcat (fn [x]
          (map (fn [y] body) ys))            ;; map fuer innerste
        xs)
```

Spart O(n*m) `(list body)` Allokationen -> 0.

## Test-Kommando

Nach jedem Schritt:

```bash
cmake --build build -t unit-tests && ./build/unit-tests
```

## Schritt 0: Baseline

Alle Tests muessen gruen sein bevor wir anfangen.

## Schritt 1: Tests anpassen

Datei: [src/tests/test_macros.c](src/tests/test_macros.c)

Aendern (alle einfachen Faelle - innerste Schleife):

- `test_for_macroexpand_simple` (Zeile 496): `mapcat` -> `map`
- `test_for_macroexpand_modifiers` (Zeile 540): `mapcat` -> `map`
- `test_for_macroexpand_destructuring` (Zeile 563): `mapcat` -> `map`
- `test_for_macroexpand_with_cond_else` (Zeile 594): `mapcat` -> `map`

Unveraendert (aeussere Schleife bei mehreren Bindings):

- `test_for_macroexpand_multiple_bindings` (Zeile 518): bleibt `mapcat` (aeusserste Schleife)

Nach diesem Schritt: Tests werden fehlschlagen (erwartet).

## Schritt 2: for-build anpassen

Datei: [src/clojure.core.clj](src/clojure.core.clj)

Zwei Aenderungen in `for-build` (Zeilen 1113-1154):

### Aenderung A: Basis-Fall (Zeile 1115-1116)

```clojure
;; Vorher:
(if (empty? clauses)
  (list 'clojure.core/list body)

;; Nachher:
(if (empty? clauses)
  body
```

### Aenderung B: map vs mapcat Entscheidung (Zeile 1152)

```clojure
;; Vorher:
(list 'clojure.core/mapcat
      (list 'fn (vec [sym]) inner-with-lets)
      coll2)

;; Nachher:
(list (if (empty? rest-clauses)
        'clojure.core/map      ;; innerste Schleife
        'clojure.core/mapcat)  ;; aeussere Schleifen
      (list 'fn (vec [sym]) inner-with-lets)
      coll2)
```

Nach diesem Schritt: Alle Tests sollten gruen sein.

## Beispiel-Expansionen

**Eine Binding:**

```clojure
(for [x (range 6)] (* x x))
;; => (map (fn [x] (* x x)) (seq (range 6)))
```

**Mehrere Bindings:**

```clojure
(for [x [1 2] y [3 4]] [x y])
;; => (mapcat (fn [x]
;;              (map (fn [y] [x y]) (seq [3 4])))
;;            (seq [1 2]))
```

## Kein zusaetzlicher Code noetig

- Keine neuen Hilfsfunktionen
- Nur 2 kleine Aenderungen in `for-build`
- Alle for-Formen profitieren (eine oder mehrere Bindings)
