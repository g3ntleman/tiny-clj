# Beispiel: for-Schleife über einen Range und expandierter Code

## Typisches Beispiel

```clojure
;; User-Code: Quadrate von 0 bis 5
(for [x (range 6)] (* x x))
;; => (0 1 4 9 16 25)  ; lazy seq
```

## Expandierter Code (macroexpand-1)

Der `for`-Macro in tiny-clj expandiert zu **mapcat** über eine **seq** der Collection, mit optionalem **filter** (`:when`) bzw. **take-while** (`:while`) und **let**-Bindungen.

### Einfacher Fall: eine Binding, kein Modifier

**Eingabe:**
```clojure
(for [x (range 6)] (* x x))
```

**Expandiert (strukturell):**
```clojure
(clojure.core/mapcat
  (clojure.core/fn [x]
    (clojure.core/list (* x x)))
  (clojure.core/seq (range 6)))
```

Ablauf:
1. `(range 6)` liefert eine lazy seq `(0 1 2 3 4 5)`.
2. `(seq (range 6))` bleibt eine seq (bereits sequenziell).
3. `mapcat` wendet `(fn [x] (list (* x x)))` auf jedes Element an und hängt die Listen zusammen → `(0 1 4 9 16 25)`.

---

### Mit :when (nur gerade Indizes)

**Eingabe:**
```clojure
(for [x (range 6) :when (even? x)] x)
;; => (0 2 4)
```

**Expandiert:**
```clojure
(clojure.core/mapcat
  (clojure.core/fn [x]
    (clojure.core/list x))
  (clojure.core/filter
    (clojure.core/fn [x] (even? x))
    (clojure.core/seq (range 6))))
```

Zuerst wird die Range mit `filter` auf gerade Zahlen eingeschränkt, dann `mapcat` mit einer Funktion, die jedes Element in eine einelementige Liste packt.

---

### Mit :let (Binding pro Iteration)

**Eingabe:**
```clojure
(for [x (range 4) :let [y (* x 2)]]
  [x y])
;; => ([0 0] [1 2] [2 4] [3 6])
```

**Expandiert:**
```clojure
(clojure.core/mapcat
  (clojure.core/fn [x]
    (let [y (* x 2)]
      (clojure.core/list [x y])))
  (clojure.core/seq (range 4)))
```

`:let [y (* x 2)]` wird zu einem `let` um den Body gewrappt; der Body ist `(list [x y])`.

---

### Mehrere Bindings (verschachtelte Schleifen)

**Eingabe:**
```clojure
(for [x (range 2) y (range 2)]
  [x y])
;; => ([0 0] [0 1] [1 0] [1 1])
```

**Expandiert:**
```clojure
(clojure.core/mapcat
  (clojure.core/fn [x]
    (clojure.core/mapcat
      (clojure.core/fn [y]
        (clojure.core/list [x y]))
      (clojure.core/seq (range 2))))
  (clojure.core/seq (range 2)))
```

Jede weitere Binding erzeugt ein weiteres verschachteltes `mapcat`: zuerst über `(range 2)` für `x`, darin über `(range 2)` für `y`, Body `(list [x y])`.

---

## Kurzüberblick

| User-Code | Expansion |
|-----------|-----------|
| `(for [x coll] body)` | `(mapcat (fn [x] (list body)) (seq coll))` |
| `:when pred` | Äußere Collection wird zu `(filter (fn [sym] pred) (seq coll))` |
| `:while pred` | Zu `(take-while (fn [sym] pred) (seq coll))` |
| `:let [b v]` | Body wird zu `(let [b v] (list body))` |
| Mehrere Bindings | Verschachteltes `mapcat` (innere Binding = inneres mapcat) |

Die Implementierung steht in `src/clojure.core.clj`: `normalize-for-bindings` (inkl. Destructuring) und `for-build` (mapcat/filter/take-while/let).

---

## Embedded / Reference-Counting: suboptimal

Die aktuelle Expansion ist für **Embedded mit Reference-Counting** ungünstig:

- **Pro Element** wird `(list body)` ausgewertet → mindestens **eine Cons-Zelle** Allokation pro Iteration.
- **mapcat** hängt diese Ein-Element-Listen zusammen und erzeugt dabei weitere temporäre Referenzen und Retain/Release-Traffic.
- Folge: O(n) Allokationen für n Elemente, mehr Druck auf den Autorelease-Pool und potenzielle Fragmentierung.

Mögliche Richtungen (siehe Plans):

- **Iterative Runtime** (`for*`-Special-Form): `for` nur als Macro für Normalisierung, Laufzeit als Schleife mit geringerem Allokationsaufwand (z. B. `Plans/iterative-for-macro.plan.md`).
- **Thunk-only / Lazy-for**: Lazy-Seq ohne pro-Element-`list`; ein Thunk pro „Rest der Seq“, Wert on-demand (z. B. `Plans/thunk-only-for-plan.md`, `Plans/lazy-map-concat-for.plan.md`).
- **Range-spezifische Optimierung**: Erkannte `(range n)`-Fälle als einfache Schleife mit reduzierter Allokation ausführen.
