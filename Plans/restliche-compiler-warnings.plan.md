---
name: Restliche Compiler-Warnings (ESP32-Build)
overview: "Behebung der im ESP32-IDF-Build verbleibenden Warnungen: Kconfig SPIFFS, CLJ_DEBUG_ASSERT Redefinition, longjmp-Clobber (clojure_core, atom, memory.h RETAIN/RELEASE), unsigned-Vergleiche (reader, exception), unbenutzte Variable (to_string)."
todos:
  - id: sdkconfig-spiffs
    content: "esp32-idf/sdkconfig.defaults: unbekannte SPIFFS-Kconfig-Symbole entfernen"
    status: pending
  - id: clj-debug-assert
    content: "common.h: CLJ_DEBUG_ASSERT nur definieren wenn nicht bereits durch object.h gesetzt"
    status: pending
  - id: memory-retain-release
    content: "memory.h: RETAIN/RELEASE/AUTORELEASE Makros – _id als volatile deklarieren"
    status: pending
  - id: clobbered-clojure-core
    content: "clojure_core.c: target_ns, form, parse_ok, source_name volatile/volatile-Kopie"
    status: pending
  - id: clobbered-atom
    content: "atom.c: Parameter fn volatile-Kopie für longjmp-Sicherheit"
    status: pending
  - id: reader-unsigned
    content: "reader.c: unsinnige unsigned-Vergleiche (cp < 0, cp >= 0) entfernen oder anpassen"
    status: pending
  - id: exception-unsigned
    content: "exception.h: CHECK_ARITY_RANGE – unsigned-Vergleich mit 0 anpassen"
    status: pending
  - id: to-string-unused
    content: "to_string.c: unbenutzte Variable ref in CLJ_SLOT_REF-Zweigen (z.B. (void)ref)"
    status: pending
isProject: false
---

# Restliche Compiler-Warnings beheben (ESP32-IDF)

Nach der ersten Runde (namespace, fs_layer, eval_special_forms, esp_spi_flash) verbleiben im ESP32-Build folgende Warnungstypen. Dieser Plan beschreibt konkrete Fixes.

---

## 1. Kconfig: unbekannte SPIFFS-Symbole (sdkconfig.defaults)

**Meldung:**  
`warning: unknown kconfig symbol 'SPIFFS_*' assigned to ... in esp32-idf/sdkconfig.defaults`

**Betroffen:**  
[esp32-idf/sdkconfig.defaults](esp32-idf/sdkconfig.defaults) – Zeilen mit `CONFIG_SPIFFS_CACHE`, `CONFIG_SPIFFS_PAGE_CHECK`, `CONFIG_SPIFFS_USE_MAGIC`, `CONFIG_SPIFFS_USE_MAGIC_LENGTH`, `CONFIG_SPIFFS_USE_MTIME`, `CONFIG_SPIFFS_MAX_PARTITIONS`.

**Ursache:**  
SPIFFS ist in der verwendeten IDF-Version evtl. ausgeschlossen oder die Optionen umbenannt/entfernt; die Symbole sind dem Kconfig-System unbekannt.

**Vorschlag:**  
Die genannten SPIFFS-Zeilen aus `sdkconfig.defaults` entfernen. Den Kommentar „Disable SPIFFS …“ beibehalten oder kürzen. Wenn SPIFFS per `EXCLUDE_COMPONENTS` ohnehin nicht gebaut wird, reicht das; andernfalls ggf. die aktuellen Kconfig-Namen der IDF-Dokumentation entnehmen und nur gültige setzen.

---

## 2. CLJ_DEBUG_ASSERT redefined (common.h / object.h)

**Meldung:**  
`warning: "CLJ_DEBUG_ASSERT" redefined` (subjective-c/common.h:70, vorher object.h:44).

**Ursache:**  
[subjective-c/src/subjective-c/object.h](subjective-c/src/subjective-c/object.h) definiert `CLJ_DEBUG_ASSERT` unter `#ifndef CLJ_DEBUG_ASSERT`. [subjective-c/src/subjective-c/common.h](subjective-c/src/subjective-c/common.h) definiert es erneut (einmal für `#ifdef DEBUG`, einmal für `#else`) ohne Guard – bei Include-Reihenfolge object.h → common.h führt das zur Redefinition.

**Vorschlag:**  
In common.h die Definition von `CLJ_DEBUG_ASSERT` mit Guard versehen:

- Vor der bestehenden `#ifdef DEBUG`-Gruppe: `#ifndef CLJ_DEBUG_ASSERT` … `#endif` um beide Zweige (DEBUG und #else), sodass nur definiert wird, wenn object.h es noch nicht getan hat.

Damit gibt es genau eine Definition (entweder aus object.h oder aus common.h).

---

## 3. memory.h: RETAIN/RELEASE/AUTORELEASE – _id might be clobbered by longjmp

**Meldung:**  
`variable '_id' might be clobbered by 'longjmp' or 'vfork'` in [subjective-c/src/subjective-c/memory.h](subjective-c/src/subjective-c/memory.h) Zeilen 321, 329 (und ggf. im AUTORELEASE-Macro).

**Ursache:**  
Die Makros verwenden eine lokale Variable `_id`; in Aufrufern mit TRY/CATCH (setjmp/longjmp) kann der Compiler -Wclobbered auslösen.

**Vorschlag:**  
In allen betroffenen Makros (RETAIN, RELEASE, AUTORELEASE) die Variable als `volatile` deklarieren, z.B. `ID volatile _id = (obj);`. Analog zum bereits umgesetzten ASSIGN-Macro.

---

## 4. clojure_core.c: Variablen/Parameter might be clobbered by longjmp

**Meldung:**  

- `variable 'target_ns' might be clobbered` (Zeile 220)  
- `variable 'form' might be clobbered` (Zeile 301)  
- `variable 'parse_ok' might be clobbered` (Zeile 302)  
- `argument 'source_name' might be clobbered` (Zeile 206)

**Ursache:**  
In [src/clojure_core.c](src/clojure_core.c) werden in `eval_core_source` und in der TRY/CATCH-Schleife Variablen gesetzt und nach longjmp (CATCH) oder danach verwendet; der Compiler warnt zu Recht.

**Vorschlag:**  

- Parameter `source_name`: Am Funktionsanfang eine volatile Kopie anlegen, z.B. `const char *volatile source_name_vol = source_name;` und im Rest der Funktion nur diese verwenden (bzw. nur vor TRY nutzen, wo nötig).  
- Lokale Variablen `target_ns`, `form`, `parse_ok`: Entweder als `volatile` deklarieren (z.B. `CljNamespace *volatile target_ns`, `CljValue volatile form`, `bool volatile parse_ok`) oder zu Beginn des TRY-Blocks eine volatile Kopie anlegen und nur diese nach dem TRY/CATCH verwenden.  
Konkrete Stellen: Zeilen um 206 (Parameter), 220 (target_ns), 299–302 (form, parse_ok).

---

## 5. atom.c: Parameter fn might be clobbered by longjmp

**Meldung:**  
`argument 'fn' might be clobbered by 'longjmp' or 'vfork'` in [src/atom.c](src/atom.c) Zeile 61 (`atom_swap`).

**Vorschlag:**  
Zu Beginn von `atom_swap` eine volatile Kopie des Parameters anlegen, z.B. `ID volatile fn_vol = fn;` und im weiteren Verlauf (insbesondere vor/nach TRY/CATCH oder Aufrufen, die longjmp können) `fn_vol` verwenden.

---

## 6. reader.c: comparison of unsigned expression in '< 0' / '>= 0'

**Meldung:**  

- Zeile 88 und 155: `comparison of unsigned expression in '< 0' is always false`  
- Zeile 227: `comparison of unsigned expression in '>= 0' is always true`

**Ursache:**  
[src/reader.c](src/reader.c): `reader_peek_codepoint` liefert einen unsigned Typ (z.B. `uint32_t`). Vergleiche `cp < 0` und `cp >= 0` sind daher unsinnig.

**Vorschlag:**  

- Zeilen 88 und 155: Die Bedingung `if (cp < 0) break;` entfernen (toter Code). Falls ein Fehlerwert benötigt wird, stattdessen einen definierten Sentinel (z.B. festes `uint32_t`-Wert) prüfen und in der API dokumentieren.  
- Zeile 227: `return cp >= 0 && check_func(cp);` zu `return check_func((int)cp);` (oder nur `check_func(cp)`, je nach Signatur von `check_func`) vereinfachen und die immer wahre Bedingung `cp >= 0` weglassen.

---

## 7. exception.h: comparison of unsigned expression in '< 0' always false

**Meldung:**  
[subjective-c/src/subjective-c/exception.h](subjective-c/src/subjective-c/exception.h) Zeile 265: `comparison of unsigned expression in '< 0' is always false` (im Makro `CHECK_ARITY_RANGE`).

**Ursache:**  
Das Makro verwendet `(argc) < (min)` bzw. `(argc) > (max)`. Wenn `argc` unsigned ist und `min` 0, dann ist `(argc) < 0` immer false.

**Vorschlag:**  
Im Makro für die Vergleiche signed Kontext erzwingen, z.B. `(int)(argc) < (int)(min)` und `(int)(argc) > (int)(max)`, oder die Parameter/Min/Max als signed typisieren, sofern semantisch vertretbar. So verschwindet die Warnung ohne Verhaltensänderung bei gültigen Arity-Werten.

---

## 8. to_string.c: unused variable 'ref'

**Meldung:**  
[src/to_string.c](src/to_string.c) Zeilen 218 und 547: `unused variable 'ref'` im `CLJ_SLOT_REF`-Zweig.

**Ursache:**  
`CljSlotRef *ref = (CljSlotRef*)v;` wird nur unter `#ifdef DEBUG` verwendet; ohne DEBUG ist `ref` unbenutzt.

**Vorschlag:**  
Entweder im nicht-DEBUG-Zweig `(void)ref;` direkt nach der Zuweisung einfügen oder die Zuweisung in den `#ifdef DEBUG`-Block verschieben und außerhalb nur `(CljSlotRef*)v` verwenden, wo nötig. Einfachste Variante: eine Zeile `(void)ref;` nach der Deklaration, wenn `ref` außerhalb des #ifdef nicht genutzt wird.

---

## Reihenfolge und Abhängigkeiten


| Nr. | Thema              | Datei(en)                    | Abhängigkeit |
| --- | ------------------ | ---------------------------- | ------------ |
| 1   | SPIFFS Kconfig     | esp32-idf/sdkconfig.defaults | keine        |
| 2   | CLJ_DEBUG_ASSERT   | subjective-c/common.h        | keine        |
| 3   | RETAIN/RELEASE     | subjective-c/memory.h        | keine        |
| 4   | clojure_core       | src/clojure_core.c           | keine        |
| 5   | atom_swap          | src/atom.c                   | keine        |
| 6   | reader unsigned    | src/reader.c                 | keine        |
| 7   | exception unsigned | subjective-c/exception.h     | keine        |
| 8   | to_string unused   | src/to_string.c              | keine        |


Nach den Änderungen: ESP32-IDF-Build erneut ausführen (`./build_idf.sh`) und prüfen, dass diese Warnungen weg sind; Unit-Tests und ggf. REPL-Verhalten stichprobenartig prüfen.