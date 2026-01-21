# Plan: Directory-Listing liefert immer gemergte Metadaten-Map

**Stand: 21.01.2026**

## Ziel

Das Directory-Listing (`list-batch`) soll für jeden Eintrag immer eine Map mit formalen Metadaten (z. B. Länge, Änderungsdatum) und – falls vorhanden – benutzerdefinierten Metadaten liefern. Fehlen Metadaten, wird eine leere Map (Singleton) zurückgegeben. Die Clojure-API gibt dann eine Sequenz von Maps `{ :path ... :meta ... }` zurück.

## Aktueller Stand
- `list-batch` liefert aktuell nur Pfade/Keys, keine Metadaten.
- Metadaten müssen für jeden Eintrag separat per `stat` geholt werden (ineffizient).

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

## Erweiterung: Dateilängen-Korrektur und Block-Reservierung

### Ziel
Wenn beim Schreiben/Setzen einer Datei die übergebene Dateilänge (`size`) von der im B-Baum (KV-Meta) gespeicherten Länge abweicht, wird die Dateilänge auf den angegebenen Wert gesetzt. Fehlende Blöcke werden reserviert (angelegt, ggf. mit Nullbytes gefüllt), überzählige Blöcke werden freigegeben (gelöscht).

### API-Erweiterung
- Neue native Funktion: `(fs-set-size! path size)`
  - Setzt die Dateilänge auf den gewünschten Wert.
  - Reserviert fehlende Blöcke, gibt überzählige frei.
  - Rückgabe: neue Metadaten-Map (inkl. aktualisierter Länge und Chunk-Anzahl).

### Implementierung (C)
- Prüfe aktuelle Länge und Chunk-Anzahl im B-Baum/KV-Meta.
- Wenn neue Länge > aktuelle Länge:
  - Reserviere zusätzliche Blöcke (Chunk-Keys anlegen, mit Nullbytes füllen).
- Wenn neue Länge < aktuelle Länge:
  - Lösche überzählige Blöcke (Chunk-Keys entfernen).
- Aktualisiere Metadaten (Größe, Chunk-Anzahl) im KV-Meta.

### Fehlerfälle
- Negative Länge: Exception.
- Nicht existierende Datei: legt leere Datei mit angegebener Länge an.

### Tests
- Setze Länge auf größeren Wert: Blöcke werden angelegt, Datei wird verlängert.
- Setze Länge auf kleineren Wert: Blöcke werden gelöscht, Datei wird gekürzt.
- Setze Länge auf gleichen Wert: keine Änderung.
- Negative Länge: Exception.

### Doku
- Ergänze API-Doku und Beispiele für `fs-set-size!`.

## Änderung: Löschen über spit-bytes

## API- und Implementierungsplan: Löschvorgang über spit-bytes

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
- [ ] delete! aus API und Doku entfernen
- [ ] Native Implementierung von spit-bytes anpassen
- [ ] Tests aktualisieren

## Nächste Schritte
1. C-Implementierung von `list-batch` anpassen, sodass sie Metadaten mitliest und merged. **[fertig]**
2. Clojure-Wrapper anpassen, sodass die neue Struktur genutzt wird. **[offen]**
3. Funktion `meta-set!` in Clojure implementieren. **[offen]**
4. Tests für Listing und meta-set! ergänzen. **[offen]**
5. Performance und Speicherbedarf auf Embedded-Zielen prüfen.
6. Native Funktion in fs_layer.c implementieren
7. Clojure-Binding in fs.clj ergänzen
8. Tests für alle Fälle

## Offene Fragen
- Merge-Strategie: Optionale Metadaten überschreiben keine formalen Felder.
- Wie werden Fehlerfälle (z. B. inkonsistente Einträge) behandelt?

## Fortschritt
- [21.01.2026] Plan angelegt und Anforderungen dokumentiert.
- [offen] C-Implementierung anpassen
- [offen] Clojure-API anpassen
- [offen] Tests ergänzen

---
Diese Datei wird bei jedem Fortschritt aktualisiert.