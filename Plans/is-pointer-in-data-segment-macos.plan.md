---
name: is-pointer-in-data-segment macOS
overview: Auf Apple zuerst prüfen, ob **malloc_size** für die Heap-Erkennung in `is_pointer_in_data_segment` ausreicht. Wenn ja, Implementierung ohne **`mach_vm_region`**-Fallback für die Positiv-Erkennung vereinfachen, die bisherigen **zusätzlichen** Absicherungstests entfernen und genau **zwei** gezielte Tests für `is_pointer_in_data_segment` behalten.
todos:
  - id: verify-malloc-size
    content: "Host: ./build/unit-tests-prof --test test_memory/is_pointer_in_data_segment_heap_false (Registry: Gruppe/Name ohne test_-Präfix im zweiten Segment) — malloc_size reicht"
    status: completed
  - id: simplify-apple-impl
    content: "Wenn malloc_size zuverlässig Heap ausschließt: Apple-Zweig auf malloc_size (+ bestehender is_pointer_on_stack) reduzieren; apple_vm_region_is_readable_mapped und Hilfsfunktion entfernen, falls Positivfälle weiterhin ohne VM-Region abgedeckt werden können — sonst malloc_size behalten und VM-Region nur bei nachgewiesenem malloc_size==0 ergänzen (siehe Risiko)"
    status: pending
  - id: trim-tests-two
    content: "src/tests/test_memory.c und subjective-c/tests/test_memory.c auf genau 2 Tests für is_pointer_in_data_segment reduziert (heap_false + static_literal_true)"
    status: completed
  - id: full-tests-cleanup
    content: "./build/unit-tests vollständig; bei Grün Code aufräumen"
    status: pending
  - id: cleanup
    content: Sourcecode aufräumen – Debug-Code, temporäre Workarounds, tote Codepfade, überflüssige Kommentare und nicht mehr benötigte Hilfsfunktionen entfernen
    status: pending
isProject: true
---

# Plan: `malloc_size` ausprobieren, Tests auf 2 reduzieren

## Begriffsklärung („malloc_region“)

Im Repo gibt es **keine** Tests mit dem Namen `malloc_region`. Gemeint ist hier die **`mach_vm_region`/`apple_vm_region_is_readable_mapped`-Logik** in [`subjective-c/src/memory.c`](subjective-c/src/memory.c) und die **Apple-Positivtests**, die diese Kette indirekt mit absichern ([`src/tests/test_memory.c`](src/tests/test_memory.c): `literal_true_apple`, `static_data_true_apple`).

## Schritt 1 — Ausprobieren, ob `malloc_size` ausreicht

- **Ziel:** Sicherstellen, dass für echte `malloc`-Zeiger im **unit-tests**-Binary **`malloc_size(ptr) > 0`** gilt, sodass `is_pointer_in_data_segment` **sofort `false`** liefert (Heap ist kein „Datensegment“ im Sinne der Slice-Optimierung).
- **Vorgehen:** Relevanten Test ausführen; bei Fehlschlag kurz mit lldb an `is_pointer_in_data_segment` `malloc_size(ptr)` auswerten (Args wie in AGENTS.md/Runner: `--test` korrekt übergeben).

## Schritt 2 — Wenn ja: `mach_vm_region` entfernen?

- **Nur dann**, wenn ohne `mach_vm_region` weiterhin **`true`** für **repräsentative statische Zeiger** (z. B. String-Literal in `.rodata` oder `static` in `.data/.bss`) möglich ist — z. B. über **Linker-Symbole** (`getsectbyname` / Segment-Grenzen) oder eine andere **eine** klare Host-Regel, die mit Clojure/JVM-Nutzung nicht kollidiert.
- **Risiko:** Reines **`malloc_size` + `is_pointer_on_stack`** ohne weitere Positiv-Quelle klassifiziert **viele** gültige Literale fälschlich als **`false`** → bricht die Voraussetzung für [`Plans/symbol-slice-data-segment.plan.md`](Plans/symbol-slice-data-segment.plan.md). Dann **`mach_vm_region` beibehalten**; trotzdem können die **Tests** auf zwei sinnvolle Fälle reduziert werden (Heap negativ + ein Positiv).

**Kurzfassung für die Umsetzung:** Zuerst **`malloc_size` für Heap** verifizieren/fixen. **`mach_vm_region` nur entfernen**, wenn ein Ersatz für „statisch/rodata“ definiert und grün getestet ist — nicht nur, weil der Heap-Test grün ist.

## Schritt 3 — Genau **2** Tests für `is_pointer_in_data_segment`

Gemeint: **pro ausgeführtem Plattform-Block** (z. B. unter `#elif defined(__APPLE__) && defined(__MACH__)`) **zwei** Tests, die das Verhalten **end-to-end** absichern:

1. **Negativ / Heap:** `malloc`-Zeiger → `TEST_ASSERT_FALSE(is_pointer_in_data_segment(ptr))` (deckt **`malloc_size`-Pfad**).
2. **Positiv / statischer Speicher:** ein Test mit **einem** stabilen statischen Zeiger → `TEST_ASSERT_TRUE(...)` (z. B. **nur** String-Literal **oder** nur `static int`; nicht beides, um Duplikate zu vermeiden).

**Entfernen (Apple-Host-Block):** die separaten Tests für **Stack**, **NULL** und den **zweiten** Positiv-Duplikat (`literal_true_apple` vs `static_data_true_apple`), sobald die Zwei-Test-Strategie gilt.

**ESP32:** Der bestehende `#if defined(ESP32_BUILD)...`-Block kann analog auf **zwei** Tests reduziert werden (Heap negativ + ein Positiv), damit die Struktur zum Host passt — oder unverändert lassen, wenn ESP32-CI explizit beide Positivvarianten braucht (bei Umsetzung entscheiden und kurz im Commit-Kommentar festhalten).

**Spiegelung:** [`subjective-c/tests/test_memory.c`](subjective-c/tests/test_memory.c) wie `src/tests/test_memory.c` halten, damit keine auseinanderlaufenden Erwartungen entstehen.

## Schritt 4 — Verifikation

- `./build/unit-tests` (bzw. `unit-tests-prof`) vollständig grün.
- Kein Debug-Logging im Production-Pfad; Regel „Report erst nach Test“ beachten.

## Abhängigkeit

- [`Plans/symbol-slice-data-segment.plan.md`](Plans/symbol-slice-data-segment.plan.md): braucht weiterhin zuverlässiges `is_pointer_in_data_segment` für `cname`-Slices.
