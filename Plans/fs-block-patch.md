# Plan: Lesen und Schreiben von (kleinen) Blöcken (Patch) innerhalb eines Files

## Ziel
- Ermögliche das gezielte Lesen und Schreiben (Patchen) von Teilbereichen (Blöcken) innerhalb einer Datei im Tiny-CLJ-Filesystem.
- Unterstütze effiziente, blockweise Updates ohne das gesamte File neu zu schreiben.
- API: `(fs/read-block path offset length)` und `(fs/write-block path offset bytes)`

## Stand 31.03.2026
- `tiny-clj.fs/read-block` und `tiny-clj.fs/write-block` sind in `libs/tiny-clj/fs.clj` bereits als oeffentliche API vorhanden.
- Die nativen Handler existieren in `src/builtins_tiny_db.c`.
- Die aktuelle Implementierung ist funktional, aber noch nicht blockorientiert optimiert: sie materialisiert die komplette Datei via `fs_read_bytes(...)`, patcht/schneidet im Speicher und schreibt anschliessend wieder vollstaendig zurueck.
- In diesem Audit blieb `./build/unit-tests --test 'test_file_io/*' --quiet` gruen (`38 Tests, 0 Failures`), direkte Regressionstests fuer `read-block` und `write-block` wurden im aktuellen Baum aber nicht gefunden.

## Anforderungen
- **Lesen:**
  - Lese einen Block ab `offset` mit maximaler Länge `length` aus einer Datei.
  - Rückgabe: Byte-Array der gelesenen Daten (kürzer, falls Datei-Ende erreicht).
- **Schreiben:**
  - Schreibe ein Byte-Array ab `offset` in eine bestehende Datei (patch).
  - Datei wird ggf. verlängert, aber nicht gekürzt.
  - Nur der betroffene Block wird überschrieben, Rest bleibt erhalten.
- **Blockgröße:**
  - Unterstütze beliebige Offsets und Längen (keine feste Blockgröße).
  - Für große Dateien: Implementierung kann intern auf Chunk/Block-Ebene optimieren.
- **Fehlerfälle:**
  - Offset außerhalb der Datei: Rückgabe leeres Array (read) bzw. Datei wird ggf. aufgefüllt (write).
  - Negative Offsets/Längen: Exception.

## Umsetzungsschritte
1. **API-Design:**
   - Clojure-API: `read-block`, `write-block` in `fs.clj`
   - Native Bindings: `fs_read_block`, `fs_write_block` in `fs_layer.c`
2. **Native Implementierung:**
   - Implementiere effizientes Lesen/Schreiben von Teilbereichen in `fs_layer.c`.
   - Nutze bestehende Chunk-Logik für große Dateien.
3. **Clojure-Bindings:**
   - Binde die nativen Funktionen in `fs.clj` als `(defn read-block ...)` und `(defn write-block ...)`.
4. **Tests:**
   - Schreibe Unit-Tests für verschiedene Blockgrößen, Offsets und Fehlerfälle.
   - Teste Patch-Operationen (Teilüberschreibungen).
5. **Dokumentation:**
   - Ergänze API-Doku und Beispiele in `fs.clj` und im Plan.

## Testfälle
- Lese Block am Anfang, in der Mitte, am Ende einer Datei
- Schreibe Block am Anfang, in der Mitte, am Ende einer Datei
- Schreibe Block, der Datei verlängert
- Lese/Schreibe mit Offset außerhalb der Datei
- Negative Offsets/Längen werfen Exception

## Offene Fragen
- Sollen Block-Operationen atomar sein? (z.B. für parallele Zugriffe)
- Wie werden sehr große Dateien (>1MB) behandelt? (Chunk-Handling)

---
**Naechste Schritte:**
- [x] API-Entwurf und Clojure-Bindings
- [x] Native Implementierung fuer `read-block` und `write-block`
- [ ] Direkte Regressionstests fuer `read-block`, `write-block` und Randfaelle ergaenzen
- [ ] Whole-file-Read/Modify/Write auf echte chunk-orientierte Blockpfade umstellen
- [ ] Doku um aktuelle Performance- und Speichercharakteristik ergaenzen
