---
name: Binary-Trees Benchmark Test-First
overview: "Den binary-trees Benchmark unverändert zum Laufen bringen durch Test-First-Entwicklung: Zuerst Tests für die fehlenden Features schreiben, dann Implementierungen fixen."
todos:
  - id: test-doseq-vector
    content: Test für doseq mit Vector-Bindings schreiben (test_loops.c)
    status: pending
  - id: fix-doseq
    content: "eval_doseq anpassen: Vector-Bindings akzeptieren"
    status: pending
    dependencies:
      - test-doseq-vector
  - id: test-for-vector
    content: Test für for mit Vector-Bindings schreiben (test_loops.c)
    status: pending
  - id: fix-for
    content: "eval_for anpassen: Bindings nicht evaluieren, Vector akzeptieren"
    status: pending
    dependencies:
      - test-for-vector
  - id: test-integration
    content: Integration-Test für vereinfachten binary-trees schreiben
    status: pending
  - id: run-benchmark
    content: Vollständigen binary-trees Benchmark ausführen und validieren
    status: pending
    dependencies:
      - fix-doseq
      - fix-for
---

# Binary-Trees Benchmark Test-First Implementation

## GAP-Analyse

### Aktuelle Situation

Der [`benchmarks/binarytrees.clj`](benchmarks/binarytrees.clj) Benchmark crasht mit **"Cannot call nil as a function"** in der `doseq`-Schleife. Die Tests zeigen:

✅ **Funktioniert:**

- `make-tree` und `check-tree` Funktionen
- `let` mit Vector-Destrukturierung `[item left right]`
- `zero?`, `dec`, `inc`, `max`
- `bit-shift-left`, `quot`, `reduce`
- `range` mit 3 Parametern (start, end, step)
- `map` über `range`

❌ **Broken:**

- **`doseq`**: Erwartet `CLJ_LIST` für binding_list, bekommt aber `CLJ_VECTOR` → keine Iteration
- **`for`**: Verwendet `eval_arg()` das Bindings evaluiert statt zu parsen → "Unable to resolve symbol"

### Root Cause

**Problem 1: `eval_doseq` in [`src/eval.c:2310`](src/eval.c)**

```c
CljObject *binding_list = list_get_element(list, 1);
// ...
if (!binding_list || binding_list->type != CLJ_LIST || !body) {
    return NULL;
}
```

- Erwartet `CLJ_LIST`, aber `(doseq [x coll] ...) `hat einen `CLJ_VECTOR` als Bindung
- Gibt `NULL` zurück → keine Iteration → Body wird nie ausgeführt

**Problem 2: `eval_for` in [`src/eval.c:2239`](src/eval.c)**

```c
CljObject *binding_list = eval_arg(list, 1, env, NULL);
```

- Evaluiert die Bindungen mit `eval_arg` → versucht `x` als Variable zu resolven
- Sollte stattdessen die Bindungs-Vector-Struktur direkt parsen

## Umsetzungsplan (Test-First)

### Phase 1: Test für doseq mit Vector-Bindings

**Ziel**: Test schreiben der den korrekten Zustand beschreibt

1. **Test in `src/tests/test_loops.c` hinzufügen**

   - Test: `(doseq [x [1 2 3]] (println x))` soll jeden Wert ausgeben
   - Test: `(doseq [x (range 3)] (+ x 1))` soll über range iterieren
   - Erwartung: Side-effects werden ausgeführt, Rückgabe ist `nil`

### Phase 2: doseq Fix implementieren

**Ziel**: Test grün bekommen

2. **`eval_doseq` in [`src/eval.c`](src/eval.c) anpassen**

   - Binding-List als `CLJ_VECTOR` akzeptieren (nicht nur `CLJ_LIST`)
   - Check ändern: `|| (binding_list->type != CLJ_LIST && TAG(binding_list) != CLJ_VECTOR)`
   - Vector-Elemente mit `vector_nth()` statt List-Traversal extrahieren

### Phase 3: Test für for mit Vector-Bindings

**Ziel**: Test schreiben der den korrekten Zustand beschreibt

3. **Test in `src/tests/test_loops.c` hinzufügen**

   - Test: `(for [x [1 2 3]] (* x x)) `soll `(1 4 9)` zurückgeben
   - Test: `(for [x (range 5)] x) `soll `(0 1 2 3 4)` zurückgeben
   - Erwartung: Liste von Ergebnissen wird zurückgegeben

### Phase 4: for Fix implementieren

**Ziel**: Test grün bekommen

4. **`eval_for` in [`src/eval.c`](src/eval.c) anpassen**

   - **NICHT** `eval_arg()` für Bindings verwenden
   - Stattdessen `list_get_element(list, 1)` für direkte Struktur
   - Analog zu `eval_doseq`: Vector als Bindings akzeptieren
   - Body-Expression evaluieren (nicht die Bindungen!)

### Phase 5: Integration Test

**Ziel**: Kompletter Benchmark läuft durch

5. **Test für gesamten Benchmark-Ablauf**

   - Vereinfachter binary-trees Test mit depth=4
   - Erwartete Ausgabe überprüfen
   - Keine Memory-Probleme (AutoreleasePool Warnungen sind OK für Tests)

6. **Benchmark ausführen**

   - `./build/tiny-clj-repl -f benchmarks/binarytrees.clj`
   - Erwartete Ausgabe mit stretch tree, iterations, long-lived tree
   - Performance messen (optional)

## Kritische Dateien

- [`src/eval.c`](src/eval.c) - `eval_doseq` (Zeile 2301), `eval_for` (Zeile 2230)
- [`src/tests/test_loops.c`](src/tests/test_loops.c) - Neue Tests hinzufügen
- [`benchmarks/binarytrees.clj`](benchmarks/binarytrees.clj) - Ziel-Benchmark

## Erfolgskriterien

✅ Alle neuen Tests sind grün

✅ Bestehende Loop-Tests bleiben grün

✅ `benchmarks/binarytrees.clj` läuft ohne Crash durch

✅ Ausgabe entspricht dem erwarteten Format
