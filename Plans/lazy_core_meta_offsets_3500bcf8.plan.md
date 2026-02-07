---
name: Lazy core meta offsets
overview: Metadata aus `clojure.core` soll beim Laden nicht mehr als Map/String materialisiert werden. Stattdessen wird für Core-Forms ein Fixnum-Offset in `clojure_core_code` gespeichert und erst bei `(meta ...)` on-demand geparst (ohne Cache). Später geladener Code bleibt unverändert und legt normale Meta-Maps an.
todos:
  - id: lazy-meta-skip-parse
    content: "Parser: Core-Meta als Fixnum-Offset speichern und Meta-Map überspringen"
    status: pending
  - id: propagate-offset-on-def
    content: "Eval: Fixnum-Offset-Meta von Def-Form auf Value übertragen"
    status: pending
  - id: meta-builtin-materialize
    content: "Builtin `(meta ...)`: Fixnum-Offset on-demand zu Map parsen (ohne Cache)"
    status: pending
  - id: tests-core-meta
    content: "Tests: `(meta 'map)` liefert Map mit `:doc` trotz lazy Meta"
    status: pending
isProject: false
---

## Ziel

- **Beim Laden von `clojure.core` keine Meta-Maps/Docstrings allokieren** (drückt `String bytes-current`).
- **Offsets statt Maps**: Meta-Registry speichert für Core-Objekte statt `CljPersistentMap*` ein **Fixnum Offset** in `clojure_core_code`.
- **On-demand**: `(meta x)` materialisiert die Meta-Map durch Parsen ab Offset. **Kein Cache** (deine Wahl 2B).
- **Nicht-Core-Code**: bleibt wie heute (Meta-Maps werden normal gebaut).

## Design-Entscheidungen (aus deiner Vorgabe)

- **Scope**: Nur `clojure.core` bekommt Offset-Meta; später geladener Code materialisiert Meta-Maps normal.
- **Return-Policy**: `(meta ...)` parst jedes Mal neu; das Ergebnis ist nur über den aktuellen Autorelease-Pool gültig.
- **Location-Meta**: Für Core **nicht** zusätzlich speichern; ggf. nur vorhanden, wenn es in der Meta-Map selbst steht (deine Wahl 1A/keep_offset_only).

## Umsetzungsschritte

- In `[src/clojure_core.c](/Users/theisen/Projects/Work/tiny-clj-feature/src/clojure_core.c)` ist `clojure_core_code` ein globaler `const char*`. Wir machen ihn für andere Module sichtbar via kleinem Header (z.B. `src/clojure_core.h` mit `extern const char *clojure_core_code;`).
- In `[src/parser.c](/Users/theisen/Projects/Work/tiny-clj-feature/src/parser.c)` (Funktion `parse_meta`) Core-spezifisch:
  - Wenn die Meta-Form **eine Map** ist (bei Core typischerweise `^#^{...}`), dann **nicht** `parse_map()` bauen, sondern:
    - Offset des `{` speichern (`size_t off = reader_offset(reader);`).
    - Map-Inhalt schnell **balanciert überspringen** (ähnlich dem map-skip Fastpath bei `parser_in_meta`, aber jetzt auch unter `META_ENABLED`).
    - Zielobjekt normal parsen.
    - **Meta setzen als Fixnum-Offset**: `meta_set(obj, fixnum(off))`.
  - Für nicht-Map Meta (`^:kw` etc.) bleibt Verhalten unverändert (oder optional: nur Core-Map lazy, sonst eager).
- In `[src/eval.c](/Users/theisen/Projects/Work/tiny-clj-feature/src/eval.c)` in `eval_def` Meta-Propagation:
  - Wenn `form_meta` ein Fixnum-Offset ist, dann **dieses Offset direkt auf `value` übertragen** (`meta_set(value, form_meta)`), damit `(meta 'map)` später am Funktionsobjekt hängt.
  - Wenn `form_meta` eine echte Map ist, bleibt der bestehende `:name`/`:ns` Merge-Pfad.
- In `[src/builtins.c](/Users/theisen/Projects/Work/tiny-clj-feature/src/builtins.c)` in `native_meta`:
  - Nach `meta_get(target_obj)` prüfen:
    - **Map** → wie bisher zurückgeben.
    - **Fixnum** → als Offset interpretieren, `Reader` auf `clojure_core_code + off` initialisieren und **genau eine Form** parsen (erwartet Map), dann diese Map zurückgeben.
  - Sicherheitschecks: Offset muss in Range sein und an der Position sollte `{` stehen; sonst Exception `EXCEPTION_RUNTIME`.
- In `[src/parser.c](/Users/theisen/Projects/Work/tiny-clj-feature/src/parser.c)` zusätzlich Robustheit:
  - Stellen, die `meta_get()` nehmen und sofort `as_map()` machen (z.B. `merge_metadata_with_object`) so anpassen, dass sie **nur bei `is_map(meta)**` mergen, damit Fixnum-Offsets nicht fälschlich als Map behandelt werden.

## Tests

- Update bestehender Tests (keine neuen Test-Dateien):
  - `[src/tests/test_clojure_core_loading.c](/Users/theisen/Projects/Work/tiny-clj-feature/src/tests/test_clojure_core_loading.c)` oder `[src/tests/test_list_resolution.c](/Users/theisen/Projects/Work/tiny-clj-feature/src/tests/test_list_resolution.c)` erweitern:
    - Core laden.
    - `(meta 'map)` bzw. `(meta map)` muss **eine Map** liefern.
    - `(:doc (meta 'map))` (oder map-get auf Keyword) muss **String** liefern und nicht `nil`.
  - Optional: ein Test, der sicherstellt, dass ein später geladenes `(defn ...)` weiterhin eager Meta-Map erhält (also `meta_get` liefert Map, nicht Fixnum).

## Performance/Memory Notes

- Core-Load wird deutlich billiger: keine Docstring-Strings im `bytes-current`.
- `(meta ...)` wird teurer (parst), aber ist selten.
- Kein Cache bedeutet: wiederholtes `meta` erzeugt temporäre Strings jedes Mal (werden nach Pool-Drain freigegeben).

## Dateien, die voraussichtlich geändert werden

- `[src/parser.c](/Users/theisen/Projects/Work/tiny-clj-feature/src/parser.c)`
- `[src/eval.c](/Users/theisen/Projects/Work/tiny-clj-feature/src/eval.c)`
- `[src/builtins.c](/Users/theisen/Projects/Work/tiny-clj-feature/src/builtins.c)`
- `[src/clojure_core.c](/Users/theisen/Projects/Work/tiny-clj-feature/src/clojure_core.c)` (+ neuer Header z.B. `src/clojure_core.h`)
- Tests: `[src/tests/test_clojure_core_loading.c](/Users/theisen/Projects/Work/tiny-clj-feature/src/tests/test_clojure_core_loading.c)` (oder vorhandene Core-Load Tests)

