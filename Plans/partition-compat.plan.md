---
name: partition compat plan
overview: "Clojure/JVM-kompatibles und performantes `partition` in der nativen Runtime planen: mit LazySeq-Außenverhalten, Unterstützung aller relevanten Arities und passenden Regressionstests für Semantik, Typen und Speicherverhalten."
todos:
  - id: partition-regression-tests
    content: "Regressionstests fuer `partition`-Kompatibilitaet zuerst ergaenzen: 2/3/4 Aritys, Laziness, innere Fenstertypen, Fehlerfaelle und Padding/Step-Verhalten"
    status: pending
  - id: partition-lazy-runtime
    content: "`partition` in `src/builtins.c` auf einen einzigen nativen LazySeq-Codepfad mit zustandsbasiertem Thunk-Executor umstellen"
    status: pending
  - id: partition-state-symbols
    content: Noetige Symbol- und Thunk-State-Helfer in `src/symbol.c` und `src/symbol.h` konsistent ergaenzen, falls fuer den neuen Executor benoetigt
    status: pending
  - id: partition-core-surface
    content: Die oeffentliche Core-Definition in `libs/clojure/core.clj` auf die JVM-Aritys und einen praezisen englischen Docstring anpassen
    status: pending
  - id: partition-memory-tests
    content: Speicher- und Teilrealisierungsverhalten mit gezielten LazySeq-/Heap-Regressionstests absichern
    status: pending
  - id: cleanup
    content: Sourcecode aufraeumen – Debug-Code, temporaere Workarounds, tote Codepfade, ueberfluessige Kommentare und nicht mehr benoetigte Hilfsfunktionen entfernen
    status: pending
isProject: true
---

# Plan für `partition`

## Ziel
`partition` soll in tiny-clj semantisch zu Clojure/JVM aufschließen und dabei den bestehenden LazySeq-Mechanismus der Runtime wiederverwenden statt die komplette Eingabe eager zu materialisieren.

## Bestehende Basis
- Die aktuelle 2-Arg-Implementierung in [`/Users/theisen/Projects/tiny-clj/src/builtins.c`](/Users/theisen/Projects/tiny-clj/src/builtins.c) materialisiert die gesamte Eingabe und baut innere Gruppen als Vektoren. Das ist weder lazy noch typkompatibel.
- Die vorhandene LazySeq-Infrastruktur in [`/Users/theisen/Projects/tiny-clj/src/seq.h`](/Users/theisen/Projects/tiny-clj/src/seq.h) und [`/Users/theisen/Projects/tiny-clj/src/seq.c`](/Users/theisen/Projects/tiny-clj/src/seq.c) ist bereits für native Builtins gedacht.
- Gute Vorbilder für ein zustandsbasiertes Lazy-Builtin sind `native_map_thunk_executor` und `native_range_infinite_thunk_executor` in [`/Users/theisen/Projects/tiny-clj/src/builtins.c`](/Users/theisen/Projects/tiny-clj/src/builtins.c): Sie kapseln Zustand in einer Map, hängen ihn als `thunk_state` an eine `CljLazySeq` und produzieren pro Realisierung genau einen Seq-Schritt.
- Die öffentliche Definition in [`/Users/theisen/Projects/tiny-clj/libs/clojure/core.clj`](/Users/theisen/Projects/tiny-clj/libs/clojure/core.clj) unterstützt aktuell nur `[n coll]`, obwohl Clojure/JVM `([n coll] [n step coll] [n step pad coll])` anbietet.

## Zielverhalten
- Unterstützte Arities: `([n coll] [n step coll] [n step pad coll])`.
- `n` und `step` müssen positiv sein; falsche Werte sollen wie bei Clojure/JVM als `IllegalArgumentException` enden.
- Das äußere Ergebnis soll eine LazySeq sein.
- Jedes realisierte Fenster soll als list-artige Seq zurückkommen, nicht als Vektor.
- `(partition n coll)` soll intern auf denselben Codepfad wie `(partition n n coll)` normalisiert werden.
- Das 4-Arg-Verhalten soll Padding linear aus `pad` konsumieren; reicht `pad` nicht, darf das letzte Fenster kürzer als `n` sein.
- Nicht-seqbare Eingaben wie Zahlen dürfen nicht still als leer behandelt werden; sie sollen eine Seq-Konvertierungs-Exception auslösen.

## Umsetzungsansatz
- `partition` in [`/Users/theisen/Projects/tiny-clj/src/builtins.c`](/Users/theisen/Projects/tiny-clj/src/builtins.c) auf einen einzigen nativen Lazy-Pfad umstellen.
- Einen neuen `native_partition_thunk_executor` nach dem Muster von `map`/`range` einführen.
- Den Executor-Zustand als Map halten, z. B. mit normalisierten Feldern für `n`, `step`, `pad-seq`, `coll-seq` und optional `remaining-skip` nur falls die Schritt-Logik das wirklich braucht.
- Pro Realisierung genau ein Fenster aufbauen: erst bis zu `n` Elemente aus `coll-seq` sammeln, dann optional aus `pad-seq` auffüllen, danach den Restzustand für das nächste Fenster vorbereiten.
- Für Performance direkte Seq-APIs wie `make_seq`, `seq_first`, `seq_next_inplace` und `make_list`/Cons-Aufbau verwenden, nicht bestehende Builtins wie `take`, `drop`, `concat` intern aufrufen.
- Die Schrittlogik so bauen, dass nur das Nötige traversiert wird: kein Vollscan der Eingabe, kein Sammeln aller Partitionen in einem Zwischenvektor.
- Falls ein benannter Thunk-Funktionssymbol-Cache nötig ist, die nötigen Symbole konsistent in [`/Users/theisen/Projects/tiny-clj/src/symbol.c`](/Users/theisen/Projects/tiny-clj/src/symbol.c) und [`/Users/theisen/Projects/tiny-clj/src/symbol.h`](/Users/theisen/Projects/tiny-clj/src/symbol.h) ergänzen.
- Die Core-Definition in [`/Users/theisen/Projects/tiny-clj/libs/clojure/core.clj`](/Users/theisen/Projects/tiny-clj/libs/clojure/core.clj) auf eine passende native Mehrfach-Arity bzw. `& args`-Form umstellen und den Docstring an die JVM-Semantik angleichen.

## Teststrategie
- Zuerst Regressionstests ergänzen, bevor die Implementierung geändert wird.
- In [`/Users/theisen/Projects/tiny-clj/src/tests/test_core_functions.c`](/Users/theisen/Projects/tiny-clj/src/tests/test_core_functions.c) Semantiktests für 2-, 3- und 4-Arg-Fälle ergänzen: exakte Partitionierung, überlappende Fenster, Lücken bei `step > n`, Padding, zu kurzes Padding und Trunkierung.
- Dort außerdem Typ-/Laziness-Checks ergänzen: äußeres Ergebnis ist `CLJ_LAZY_SEQ` oder `CLJ_SEQ`, inneres Fenster ist nicht `vector?`, und `(take 3 (partition 2 (range)))` funktioniert.
- Fehlerfälle ergänzen: `n <= 0`, `step <= 0`, nicht-seqbare `coll`.
- Vorhandene `nil`-Element-Tests in [`/Users/theisen/Projects/tiny-clj/src/tests/test_builtins_destructure.c`](/Users/theisen/Projects/tiny-clj/src/tests/test_builtins_destructure.c) nur gezielt erweitern, nicht duplizieren.
- Falls die neue Lazy-Implementierung zusätzliche Retain/Release-Risiken einführt, einen kleinen Heap-/Leak-Regressionstest nach dem Muster anderer LazySeq-Tests ergänzen, damit partielle Realisierung keine unnötigen Referenzen festhält.

## Abnahmekriterien
- Alle drei Clojure-Aritys funktionieren.
- `partition` ist lazy genug für partielle Auswertung auf `range`.
- Innere Fenster verhalten sich seq/list-artig statt vektorartig.
- Fehlerfälle sind näher an Clojure/JVM und nicht mehr stillschweigend leer.
- Bestehende und neue Unit-Tests laufen grün, insbesondere die betroffenen Core-/Builtin-/LazySeq-Tests.
