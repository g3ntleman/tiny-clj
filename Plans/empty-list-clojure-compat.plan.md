# Plan: () als leere Liste (Clojure-kompatibel)

## Ziel

In Clojure JVM gilt:
- `()` ist die **leere Liste** (eigener Wert, nicht nil).
- `(list)` ist dieselbe leere Liste: `(identical? () (list))` => true.
- `(= () nil)` => false.
- `(seq ())` => nil.

Tiny-CLJ hat bisher `()` als nil (NULL) geparst. Dieser Plan stellt auf Clojure-Semantik um.

## Schritte

1. **Parser** (`src/parser.c`)
   - In `parse_list`: Bei `()` nicht `NULL` zurückgeben, sondern `(ID)empty_list()`.
   - In `parse_anon_fn`: Bei leerem Body `(fn [] ())` den Body als `(ID)empty_list()` bauen (statt `NULL`).

2. **Tests anpassen**
   - `test_parser.c`: `parse("()")` soll leere Liste liefern (nicht nil); Typ CLJ_LIST, `list_empty()`.
   - `test_basics.c`: `()` ist nicht nil; `(= () (list))` soll true sein; `(seq ())` => nil.
   - `test_seq.c`: `(seq ())` bleibt nil (Bereits abgedeckt).
   - Ggf. neuer Test: `(identical? () (list))` bzw. Pointer-Gleichheit von `()` und `(list)`.

3. **Abhängigkeiten prüfen**
   - Eval: Auswertung von `()` als Form ergibt `empty_list()` (kein nil). Keine Änderung nötig, wenn Parser bereits die leere Liste liefert.
   - `native_seq`: Erhält leere Liste, `list_empty(list_data)` => true => `result = NULL`. Keine Änderung nötig.
   - Alle Stellen, die `form == NULL` als „keine Form“ interpretieren, bleiben korrekt; „leere Liste“ ist eine gültige Form.

## Abgeschlossen

- [x] Plan dokumentiert
- [x] Parser umgestellt: `()` => `empty_list()`, `(fn [] ())` Body => `empty_list()`
- [x] Tests angepasst: test_parse_empty_list, test_basics (parse), test_empty_list_literal_clojure_compat
