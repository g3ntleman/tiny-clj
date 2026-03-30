---
name: Symbol-Slice Data-Segment
overview: Test-first Slice-`cname` aus Reader/Datensegment. Layout-Annahme: **`uint8_t cname_byte_len`** zusammen mit **`uint8_t` Namespace-Registry-Index** im Platz des bisherigen **`ns_name`-Zeigers** (4 B / Align-4). Offen bleibt u. a. **`unqualified`** und Feinlayout — siehe **Entscheidung**.
todos:
  - id: tests-slice-api
    content: "Neue Tests: intern slice, clj_equal/clj_hash vs NUL-Symbol, UTF-8-Slice + utf8valid_n"
    status: pending
  - id: tests-hashtable-key
    content: "Test: HashSet-Lookup mit Stack-Key (cname_byte_len) findet interniertes Symbol"
    status: pending
  - id: tests-parser-rodata
    content: "Test: parse/eval aus String-Literal → is_pointer_in_data_segment(cname); qualified-Slice"
    status: pending
  - id: adjust-existing-test
    content: test_parser_qualified_core_cname_not_shared_with_sym_if_rodata Kommentar/Erwartung an Slice-Optimierung anpassen
    status: pending
  - id: model-symbol-len
    content: uint8_t cname_byte_len + uint8_t ns_registry_index; symbol_cname_len; ns-Auflösung über Registry; unqualified slice-fähig; Xtensa sizeof; max. Name SYMBOL_NAME_MAX_LEN
    status: pending
  - id: esp32-size-budget
    content: Größenbudget gemäß Abschnitt Entscheidung verifizieren (Rechnung/Messung, Xtensa)
    status: pending
  - id: hash-equal-print
    content: hash.c, equality.c, to_string.c + grep strlen(sym->cname) auf Längen-API
    status: pending
  - id: intern-slice
    content: make_symbol_slice, intern_*_slice, symbol_table_find mit Key-Feld
    status: pending
  - id: parser-offsets
    content: "parse_symbol: Reader-Offsets + slice path; bestehende PARSER_SYMBOL_BUF/SYMBOL_NAME_MAX_LEN-Grenzen; komponierte Namen Buffer beibehalten"
    status: pending
  - id: full-tests-cleanup
    content: unit-tests Gesamtlauf; Code aufräumen (cleanup-Regel)
    status: pending
  - id: cleanup
    content: Sourcecode aufräumen – Debug-Code, temporäre Workarounds, tote Codepfade, überflüssige Kommentare und nicht mehr benötigte Hilfsfunktionen entfernen
    status: pending
isProject: true
---

# Plan: Symbol-Namen aus Reader-/Datensegment (Test-first)

## Voraussetzung: Pointer im „Datensegment“ erkennen

Die Slice-Optimierung entscheidet, ob `cname` direkt auf `Reader.src` zeigen darf. Dafür nutzt das Projekt bereits **`is_pointer_in_data_segment`** ([subjective-c/src/memory.c](subjective-c/src/memory.c)), deklariert in [subjective-c/src/subjective-c/memory.h](subjective-c/src/subjective-c/memory.h).

### ESP32 (`ESP32_BUILD` / `ESP_PLATFORM`)

- **DRAM:** Zeiger in `[ _data_start, _data_end )` oder `[ _bss_start, _bss_end )` (Linker-Symbole) gelten als statisch.
- **DROM / Flash-Rodata:** zusätzlich `esp_ptr_in_drom(ptr)` (wenn `SUBJECTIVE_C_HAVE_ESP_MEMORY_UTILS`) bzw. Adressbereich `SOC_DROM_LOW`…`SOC_DROM_HIGH` (wenn `SUBJECTIVE_C_HAVE_SOC_MEMORY_RANGES`).
- **Nicht** erfasst: Heap, Stack (typisch außerhalb dieser Bereiche).

Damit ist für eingebettete `.clj`-Strings in Flash/Rodata und für statische C-String-Literals die Erkennung **relativ präzise**; für einen Token-Slice `[p, p+n)` sollte die Implementierung sicherstellen, dass **Start und Ende** (mindestens `p` und `p+n-1`) in einem für die Plattform als „statisch/rodata“ geltenden Bereich liegen (DROM/DRAM wie oben).

### Host (ohne ESP-Makros)

- **Heuristik:** 64-Bit: `(uintptr_t)ptr < 0x100000000ULL`; 32-Bit: `< 0x08000000UL`.
- **Bedeutung:** Kein echtes ELF-Segment-Mapping wie auf ESP; niedrige Adressen werden als „vermutlich Literal/statisch“ behandelt, typischer **mmap-Heap** liegt oft darüber → `make_symbol` kopiert weiterhin inline (bestehendes Verhalten).
- **Risiko:** Plattform-/ASLR-abhängig; für Unit-Tests auf macOS mit `parse("foo")` ist das meist ausreichend, um Slice-Pfade zu triggern, aber **keine formale Garantie** für alle Host-Konfigurationen.

### Tests

- Bestehend: [subjective-c/tests/test_memory.c](subjective-c/tests/test_memory.c) (`test_is_pointer_in_data_segment_*`).
- Für den Symbol-Slice-Plan: Parser-Rodata-Tests können **`#include` / Link auf `memory.h`** nutzen und `is_pointer_in_data_segment(sym->cname)` asserten, sobald der Parser-Pfad umgestellt ist.

**Fazit:** Die Voraussetzung „wir wissen, wie wir Pointer erkennen“ ist im Code **bereits erfüllt**; bei der Umsetzung **keine zweite Heuristik erfinden**, sondern `is_pointer_in_data_segment` (und bei Bedarf eine schmale Hilfsfunktion `reader_bytes_in_data_segment(reader, off, len)` darauf aufbauend) verwenden.

## Ziel

Parser erzeugte Symbole sollen, wenn `reader->src` (bzw. der Token-Bytbereich) im **statischen Adressraum** liegt (`is_pointer_in_data_segment`), **`cname` direkt auf diese Bytes** zeigen können (ohne `memcpy` hinter `CljSymbol`), statt über Stack-Buffer und Inline-Kopie. Dafür braucht es **explizite Namenslänge** (heute implizit `strlen`).

## Längenfeld: 8 Bit (`uint8_t`)

- **`cname_byte_len` als `uint8_t`:** **`0`** = klassisch **NUL-terminiert**, Länge per **`strlen(cname)`**; **`1`…`255`** = explizite **Byte-Länge** des Slices (ohne trailing `\0` im Quelltext).
- **Obere Grenze** für gültige Symbolnamen bleibt die bestehende **`SYMBOL_NAME_MAX_LEN`** ([aktuell 64](src/symbol.h)); Slice-Längen >255 braucht es dafür nicht — **`uint8_t` reicht**.
- Keine Erhöhung von `PARSER_SYMBOL_BUF` / `SYMBOL_NAME_MAX_LEN` allein wegen des 8-Bit-Feldes nötig.

## Entscheidung: Speicherlayout & Namespace (**nur Ergebnis**)

**Festgelegte Annahme (Planbasis):** Das zusätzliche **`uint8_t` für die `cname`-Bytelänge** wird **gemeinsam mit einem kleinen, ebenfalls 8-Bit-Index** in den **Namespace** umgesetzt: **`uint8_t ns_registry_index`** verweist auf eine **stabile** numerische Namespace-Referenz (Auflösung z. B. `stable_ns_table[ns_registry_index]` → `CljSymbol*` des Namespace-Namens oder → `CljNamespace*` und dann `->name`). Beide **`uint8_t`-Felder** liegen im **gleichen 4-Byte-ausgerichteten Speicherwort**, das bisher **`CljSymbol *ns_name`** belegte — **restliche 2 Byte** (**Padding** / **reserviert/Flags**), sodass **`sizeof(CljSymbol)` auf ILP32 typisch unverändert 16 Byte** bleiben kann.

**Bezug `g_runtime.ns_registry`:** Es gibt bereits eine **Map aller Namespaces** — **`g_runtime.ns_registry`** (`CljTransientMap` mit Backing **`CljPersistentMap`**, Schlüssel typisch **`CljSymbol*`** des NS-Namens, Wert **`CljNamespace*`**, s. [src/namespace.h](src/namespace.h), [subjective-c/src/subjective-c/map.h](subjective-c/src/subjective-c/map.h)). **Nicht** als stabiler Index für `ns_registry_index` geeignet: der **lineare Index eines Keys im Backing-Array `data[]`** der Map ist bei **`map_assoc` / COW / Kapazitätswachstum** **nicht stabil** (Einrücken, neues Array, andere Belegung) — analog zum Verbot, Hash-Tabellen-Slots zu persistieren. **Optional** kann die Map **weiter** die autoritative Zuordnung Symbol→`CljNamespace` bleiben; die **`uint8_t`-ID** muss dann **zusätzlich** vergeben werden (append-only Seitentabelle / paralleler Vektor), **nicht** „Index aus `MAP_FOR_EACH` übernehmen“.

- **Kapazität:** **`uint8_t`** → höchstens **255** nutzbare Registry-Indizes (plus evtl. **`0` = unqualifiziert / kein NS** — Kodierung in der Implementierung festlegen).
- **`sizeof(CljSymbol)` / 64-Bit:** *(TBD — gleiche Packlogik, ggf. anderes Padding; vor Merge verifizieren.)*
- **`unqualified`-Zeiger:** *(TBD — beibehalten vs. entfernen / Lookup)*
- **Umsetzungsreihenfolge:** *(TBD — Registry + Index + `cname_byte_len` typischerweise **mit** Slice-Pfad; Details der Phasen anpassen.)*

## Technische Randbedingungen (Invarianten, keine Wirtschafts-Abwägung)

- **`cname_byte_len`:** Semantik wie im Abschnitt **Längenfeld**; gültige Namen ≤ **`SYMBOL_NAME_MAX_LEN`** bei Erzeugung/Parser.
- **Kein** persistierter Index **volatiler** Tabellen: weder **Symbol-Interning-`CljHashSet`**-Slots noch **Backing-Slot-Index** in **`CljPersistentMap`** (`g_runtime.ns_registry`) — beide können sich bei Wachstum/Umverteilung ändern. Der **`ns_registry_index`** verweist nur auf eine **explizit stabile** Seitentabelle (append-only o. Ä.), nicht auf `g_runtime.symbol_table` oder Roh-Offsets in `ns_registry->backing->data`.
- **Speicher:** Pro Namespace weiter **mindestens ein Wort** in der **stablen** Tabelle (z. B. `CljSymbol*` oder `CljNamespace*`); **`g_runtime.ns_registry`** trägt ohnehin Einträge — eine **zusätzliche** ID-Tabelle ist **kein** doppelter `CljNamespace*`-Speicher, wenn sie nur **denselben** Zeiger oder den **NS-Namen** spiegelt (Designwahl bei Implementierung).

## Ausgangslage (Code)

- [src/parser.c](src/parser.c) `parse_symbol`: baut `buffer[PARSER_SYMBOL_BUF]`, ruft `intern_symbol(..., symbol_str)` mit Zeiger **in den Stack** auf → [src/symbol.c](src/symbol.c) `make_symbol`: kein Datensegment → **Inline-Kopie** nach `sym+1`.
- Bestehender Test [test_parser_qualified_core_cname_not_shared_with_sym_if_rodata](src/tests/test_qualified_symbol_resolution.c) dokumentiert aktuell: Reader-`if` ≠ `SYM_IF->cname` (Inline vs. rodata). Nach der Optimierung bleibt **Inhalt** gleich; die **Pointer-Story** wird „Substring im Quell-Literal“ vs. separates `SYM_IF`-Literal – der Test muss **semantisch** bleiben, Kommentar/Erwartung ggf. anpassen (weiterhin typischerweise **nicht** pointer-gleich mit `SYM_IF`).

## Phase 1 – Tests zuerst (API & Semantik, noch rot)

Ohne Produktionsänderung zunächst **fehlende Verträge** ergänzen; sie brechen, bis die Implementierung da ist.

1. **Slice-Interning / Gleichheit / Hash (Unit, C)**
   - In [src/tests/test_symbol_clojure_compat.c](src/tests/test_symbol_clojure_compat.c) oder kleiner Erweiterung in [src/tests/test_qualified_symbol_resolution.c](src/tests/test_qualified_symbol_resolution.c):
     - `intern_symbol_global_slice(ptr, len)` bzw. `intern_symbol_slice(ns, ptr, len)` (Namen final wie in Implementierung): zweimal gleicher Slice → **identisches** `CljSymbol*`.
     - `clj_equal(slice_sym, intern_symbol_global("foo"))` → **true** (wenn beide Pfade existieren).
     - `clj_hash_full` gleich für beide.
   - **UTF-8:** Slice mit mehrbyte-Zeichen; optional `utf8valid_n` (neu in [src/utf8.h](src/utf8.h)) vor Interning – Test mit gültigem/ungültigem Slice (Exception oder Fehlerpfad wie heute bei `utf8valid(buffer)`).
2. **Symbol-Tabellen-Lookup mit Slice-Key**
   - Test: HashSet-Lookup mit **Stack-Key** (fake `CljSymbol` mit `cname_byte_len` + `cname` ohne trailing `\0`) findet bereits interniertes Slice-Symbol (entscheidend für `symbol_table_find`).
3. **Parser / Eval-Integration (bestehende Suite erweitern)**
   - Neuer Test: `parse`/`eval_string` mit **String-Literal** in `.rodata` (normales `parse("foo", …)`), danach `is_pointer_in_data_segment(sym->cname)` **true** und `symbol_cname_len(sym)==3` (oder strlen-Modus 0 – je nach gewählter Kodierung `0 = strlen`).
   - Qualifiziert: `'ns/name` aus Literal – **ns-** und **name-**Slice beide im Datensegment erkennbar (oder mindestens `cname` des lokalen Teils).
4. **Bestehenden Test anpassen**
   - [test_parser_qualified_core_cname_not_shared_with_sym_if_rodata](src/tests/test_qualified_symbol_resolution.c): beibehalten: `strcmp`-Gleichheit `"if"`; Pointer-Vergleich zu `SYM_IF` **weiterhin false** (realistisch). Kommentar aktualisieren: nicht mehr „immer Inline-Kopie“, sondern „kein gemeinsamer `cname`-Zeiger mit statischem `SYM_IF`“.
5. **Regression nicht duplizieren**
   - Statt neuer Test-Datei: bestehende Gruppen laufen lassen: [src/tests/test_parser.c](src/tests/test_parser.c), [src/tests/test_namespace.c](src/tests/test_namespace.c), [src/tests/test_equal.c](src/tests/test_equal.c), gesamte [src/tests/test_qualified_symbol_resolution.c](src/tests/test_qualified_symbol_resolution.c), [src/tests/test_keyword_evaluation.c](src/tests/test_keyword_evaluation.c).

## Phase 2 – Minimale Datenmodell-Erweiterung

- [src/symbol.h](src/symbol.h): `CljSymbol` gemäß Abschnitt **Entscheidung** — **`CljSymbol *ns_name`** durch **`uint8_t ns_registry_index` + `uint8_t cname_byte_len` + 2 B Reserve** im selben 4-B-Wort ersetzen; **`cname_byte_len`:** **`0`** = NUL-terminiert, **`strlen`**; **`1`…`255`** = Slice-Länge (≤ **`SYMBOL_NAME_MAX_LEN`**). Statische `SYM_*`: `cname_byte_len` = 0, `ns_registry_index` wie unqualifiziert kodieren. **stabile `ns_id`-Tabelle** (append-only) + `symbol_ns_name(sym)` (Lookup); **`g_runtime.ns_registry`** bleibt Map-basiert — **ID** bei `ns_register` / `ns_get_or_create` vergeben, **nicht** Map-`data[]`-Index. **`sizeof(CljSymbol)`** Xtensa/Host prüfen.
- **Reihenfolge / Layout:** Registry und Index-Zuweisung **vor** breiter Nutzung qualifizierter Symbole; **`unqualified`** gemäß offenem TBD.
- Öffentliche Helfer: `symbol_cname_len(const CljSymbol *)` (in `.c` oder `static inline` + `#include <string.h>` in Header – je nach Stil des Repos).
- [symbol_get_cached_unqualified](src/symbol.h): Lookup muss **längenbewusst** (`symbol_table_lookup_slice` o. Ä.) — entfällt teilweise, wenn `unqualified` entfernt und durch Lookup ersetzt wird.

## Phase 3 – Hash, Equal, to_string

- [src/hash.c](src/hash.c) `hash_symbol`: FNV über **Bytes** mit `symbol_cname_len`, Namespace-Name ebenso.
- [src/equality.c](src/equality.c) `CLJ_SYMBOL` + Symbol-Token-Vergleiche: `memcmp`/`strncmp` mit Längen statt blind `strcmp`.
- [src/to_string.c](src/to_string.c) und weitere `strlen(sym->cname)`-Stellen (grep) auf `symbol_cname_len` umstellen.

## Phase 4 – `make_symbol` / `intern_symbol` Slice-Pfad

- [src/symbol.c](src/symbol.c):
  - `make_symbol_slice` (Signatur je nach NS-Modell: z. B. `ns_registry_index` statt `CljSymbol *ns`): wenn **gesamter** Bereich `[p, p+n)` als „statisch/rodata“ gilt → `sym->cname = p`, `cname_byte_len = n`, **`ns_registry_index`** setzen; sonst **Kopie** nach `sym+1` + NUL + `cname_byte_len = 0`.
  - `intern_symbol_slice` / `intern_symbol_global_slice` + `symbol_table_find` mit Stack-Key (`make_symbol_key` um `cname_byte_len` erweitern).

## Phase 5 – Parser

- [src/parser.c](src/parser.c) `parse_symbol`: **Byte-Offsets** `token_start` / `token_end` im `Reader` mitschreiben (Slash-Offset für qualifizierte Tokens).
- Wenn `reader_slice_in_data_segment(reader, start, end)` (Hilfsfunktion auf Basis von `is_pointer_in_data_segment`): `intern_*_slice` statt `buffer`+`intern_*` für die Pfade, die **keine** zusätzliche Zusammenbau-Buffer brauchen (`keyword_with_colon`, `::alias/...` bleiben bei Stack/Format wie heute).
- `true`/`false`/`nil` per `strncmp` auf Slice statt `strcmp(buffer)`.

## Phase 6 – Verifikation & Aufräumen

- Voll `./build/unit-tests-prof` (oder Projekt-Standard).
- Gezielt: `test_symbol_clojure_compat`, `test_parser`, `test_qualified_symbol_resolution`, `test_equal`, `test_namespace`.
- Debug-Ausgaben entfernen, tote Pfade vermeiden.
- Abschluss: **cleanup** laut Projektregel (überflüssige Kommentare, nur nötige öffentliche API).

## Risiken / bewusste Nicht-Ziele

- **Kein** Schreiben von `\0` in read-only `src` (Flash/rodata).
- Token ohne NUL im Quelltext erfordern **immer** `cname_byte_len` (kein `strlen` auf Slice); praktische Obergrenze **`SYMBOL_NAME_MAX_LEN`** bzw. Parser-Limits; Speicherung der Länge in **`uint8_t`** (1…255).
- Umfang: viele `strlen(sym->cname)`-Call-Sites – ein grep-gestützter, disziplinierter Durchgang ist Pflicht.
- **Host-Heuristik** in `is_pointer_in_data_segment` ist schwächer als ESP; Verhalten bei Slice-Optimierung host-seitig dokumentiert halten (Tests mit typischen `parse("…")`-Literalen).
- **8-Bit `ns_registry_index`:** Hard-Limit **255** nutzbare Registry-Slots (Kodierung von „kein NS“ extra); bei Überschreitung anderes Schema oder Fehlerpfad definieren.
- **ESP32 / ILP32:** Ziel laut **Entscheidung** kein Wachstum gegenüber heute, solange **`ns_name`-Zeiger** durch **`uint8` ns-Index + `uint8` `cname_byte_len` + 2 B Reserve/Padding** im selben 4-B-Wort ersetzt wird; **ohne** diese Packung würde ein **zusätzliches** `uint8_t` allein typisch **+4 B/Symbol** erzwingen.
