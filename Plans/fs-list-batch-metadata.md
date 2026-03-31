# Plan: Directory-Listing liefert immer gemergte Metadaten-Map

**Stand: 31.03.2026**

## Ziel

Das Directory-Listing (`list-batch`) soll für jeden Eintrag immer eine Map mit formalen Metadaten (z. B. Länge, Änderungsdatum) und – falls vorhanden – benutzerdefinierten Metadaten liefern. Fehlen Metadaten, wird eine leere Map (Singleton) zurückgegeben. Die Clojure-API gibt dann eine Sequenz von Maps `{ :path ... :meta ... }` zurück.

## Aktueller Stand
- `tiny-clj.fs/list` liefert bereits `{:path ... :meta ...}`-Eintraege ueber das native `list-batch`.
- `meta-set!` ist in `libs/tiny-clj/fs.clj` implementiert und speichert benutzerdefinierte EDN-Metadaten unter `<path>.meta`.
- Wenn `meta-set!` einen `:size`-Eintrag sieht, ruft die Funktion aktuell explizit `set-size!` auf.
- `(spit-bytes path nil)` loescht bereits ueber die oeffentliche Clojure-API.
- `libs/tiny-clj/fs_test.clj` deckt `meta-set!`/Listing-Basisfaelle ab; in diesem Audit blieb ausserdem `./build/unit-tests --test 'test_file_io/*' --quiet` gruen (`38 Tests, 0 Failures`).

## Ziel-API
- `list-batch` gibt zurück:
  ```clojure
  {:entries [{:path "foo.txt" :meta {...}}
             {:path "bar.txt" :meta {...}}]
   :last-key ...}
  ```
- Die `:meta`-Map enthält gemergte formale und optionale Metadaten.
- Falls keine Metadaten vorhanden: leere Map (Singleton).


## Erweiterung: meta-set! Funktion

### Ist-Stand
`meta-set!` ist bereits vorhanden. Die aktuelle Implementierung serialisiert die Map per `pr-str`, schreibt sie via `tiny-db.kv/put-bytes` nach `<path>.meta` und delegiert `:size`-Aenderungen an `set-size!`.

### Ziel
Eine Clojure-Funktion `meta-set!` soll es ermöglichen, benutzerdefinierte Metadaten für einen Pfad zu setzen. Diese werden beim nächsten Listing automatisch mit ausgegeben und mit den formalen Metadaten gemerged.

### API
```clojure
(meta-set! path meta-map)
```
Setzt die Metadaten-Map für den angegebenen Pfad. Überschreibt nur die benutzerdefinierten Felder, formale Felder bleiben erhalten.

### Implementierung (Clojure)
```clojure
(defn meta-set! [path meta-map]
  ;; serialisiert meta-map und speichert sie als speziellen KV-Eintrag hinter dem Pfad
  (tiny-db.kv/put-bytes (str path ".meta") (serialize-meta meta-map)))
```
`serialize-meta` serialisiert die Map als Byte-Array (EDN oder eigenes Format).

### Tests
- Test: Setze Metadaten für einen Pfad, prüfe, dass sie beim Listing erscheinen und korrekt gemerged werden.
- Test: Setze leere Map, prüfe, dass nur formale Metadaten erscheinen.
- Test: Überschreibe bestehende Metadaten, prüfe Merge-Strategie.

## Änderung: Dateilängen-Korrektur via `meta-set!`

### Ziel
Dateilaengen-Anpassungen erfolgen aktuell ueber die separate API `set-size!`; `meta-set!` ruft sie optional auf, wenn ein `:size`-Eintrag vorhanden ist. Das weicht vom urspruenglichen Plan ab, `:size` ausschliesslich implizit ueber Metadaten anzuwenden.

### Implementierung (Konzept)
- `meta-set!` serialisiert die Map als EDN und speichert sie in `<path>.meta` mittels `tiny-db.kv/put-bytes`.
- Aktuell ruft `meta-set!` direkt `set-size!` auf; eine separate `fs_apply_meta(...)`-Hilfsfunktion existiert im aktuellen Baum nicht.
- Verhalten bei `:size`:
  - Wenn `:size` > aktuelle Größe: reserviere zusätzliche Chunks (mit Nullbytes initialisieren) und aktualisiere die `FsFileMeta`-Einträge.
  - Wenn `:size` < aktuelle Größe: lösche überzählige Chunks und aktualisiere `FsFileMeta`.

### Fehlerfälle
- Negative `:size`: Exception (prüfbar in Clojure vor dem Write oder nativ).
- `:size` auf nicht-existierende Datei: Datei wird angelegt und nötige Chunks reserviert.

### Tests
- Verwende `meta-set!` mit `{:size N}` und prüfe, dass die Datei entsprechend verlängert/gekürzt wird.
- Prüfe, dass vorhandene Daten in überschneidenden Bereichen erhalten bleiben.
- Prüfe `:modified` / `:created` Metadaten-Verhalten beim Setzen durch `meta-set!`.

### Doku
- Ergänze in `fs.clj` und der Plan-Datei die Beschreibung, dass `meta-set!` mit dem Schlüssel `:size` Datei-Längen-Anpassungen auslöst und die native Schicht Blöcke reserviert/freigibt.

## Änderung: Löschen über spit-bytes

## API- und Implementierungsplan: Löschvorgang über spit-bytes

### Ist-Stand
Die oeffentliche Clojure-API nutzt bereits `(spit-bytes path nil)` zum Loeschen, und `libs/tiny-clj/fs.clj` weist explizit darauf hin. Ein legacy-nativer Entry-Point `tiny-clj.fs/delete!` existiert in `src/builtins_tiny_db.c` allerdings noch.

### Ziel
Das Löschen von Dateien/Verzeichnissen erfolgt ausschließlich über `(spit-bytes path nil)`. Die Funktion `delete!` wird entfernt. Die API bleibt dadurch minimal und konsistent.

### API-Design
- `(spit-bytes path bytes)`
  - Schreibt die angegebenen Bytes an den Pfad.
  - Wenn `bytes` nil ist, wird die Datei/der Eintrag gelöscht.
- `delete!` entfällt vollständig.

### Native Implementierung
- In `fs_layer.c` prüft die native Funktion für `spit-bytes`, ob das Argument `bytes` nil ist.
  - Falls ja: Lösche Datei, Metadaten und reservierte Blöcke.
  - Falls nein: Schreibe/überschreibe Datei wie bisher.
- Löschvorgang entfernt alle zugehörigen Chunk-Keys und Metadaten.

### Fehlerfälle
- Löschen eines nicht existierenden Pfads ist ein No-Op.
- Nach Löschvorgang ist der Pfad im Directory-Listing nicht mehr enthalten.

### Tests
- `(spit-bytes path nil)` entfernt Datei und Metadaten.
- Nach Löschvorgang ist Datei nicht mehr im Listing.
- Schreiben nach Löschvorgang legt Datei neu an.

### Dokumentation
- :doc-String von `spit-bytes` beschreibt das Verhalten für nil.
- Alle Beispiele und Doku werden auf `(spit-bytes path nil)` umgestellt.

### Nächste Schritte
- [x] `delete!` aus der oeffentlichen `tiny-clj.fs`-API entfernen und Doku auf `(spit-bytes path nil)` umstellen
- [x] Native Implementierung von `spit-bytes` auf `nil`-Delete umstellen
- [ ] Legacy-native Funktion `tiny-clj.fs/delete!` entfernen oder explizit als Kompatibilitaets-API dokumentieren
- [ ] Tests fuer direktes `spit-bytes nil` aktualisieren/ergaenzen

## Nächste Schritte
1. C-Implementierung von `list-batch` mit gemergten Metadaten ist vorhanden. **[fertig]**
2. Clojure-Wrapper `tiny-clj.fs/list` nutzt die neue Struktur bereits. **[fertig]**
3. `meta-set!` ist in Clojure implementiert. **[fertig]**
4. Basis-Tests fuer Listing und `meta-set!` sind vorhanden (`libs/tiny-clj/fs_test.clj`). **[teilweise]**
5. Legacy-native `tiny-clj.fs/delete!` entfernen oder als Kompatibilitaets-API markieren.
6. Entscheiden, ob `set-size!` langfristig oeffentlich bleibt oder wieder hinter `meta-set!` verschwindet.
7. Edge-Case-Tests fuer `spit-bytes nil`, `:size` und Merge-Konflikte ergaenzen.
8. Performance und Speicherbedarf auf Embedded-Zielen pruefen.

## Offene Fragen
- Merge-Strategie: Optionale Metadaten überschreiben keine formalen Felder.
- Wie werden Fehlerfälle (z. B. inkonsistente Einträge) behandelt?

## Fortschritt
- [21.01.2026] Plan angelegt und Anforderungen dokumentiert.
- [31.03.2026] Codeabgleich: `list-batch`, `meta-set!`, `set-size!` und `spit-bytes nil` sind im aktuellen Baum implementiert; offene Punkte auf Legacy-API, Edge-Case-Tests und Embedded-Validierung reduziert.

---
Diese Datei wird bei jedem Fortschritt aktualisiert.