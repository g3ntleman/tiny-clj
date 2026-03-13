---
name: embedded clojure source generation
overview: Ersetzt die doppelt gepflegten eingebetteten Clojure-Quellen durch eine Build-Pipeline, die aus Clojure-Dateien unter /libs generierte Embedded-Sources im Build-Verzeichnis erzeugt. Ziel ist eine einzige Source of Truth für Clojure-Code, bessere Tool-Unterstützung wie clj-kondo und weniger Verwirrung im Repo.
todos:
  - id: source-of-truth
    content: /libs als einzige Quelle für eingebettete Clojure-Dateien festlegen
    status: pending
  - id: generator-script
    content: Deployment-Helfer-Script implementieren, das aus einer Clojure-Datei einen embeddbaren C-Source-Snippet erzeugt
    status: pending
  - id: build-pipeline
    content: CMake so erweitern, dass die Embedded-Sources im Build-Verzeichnis generiert und von embedded_sources.c konsumiert werden
    status: pending
  - id: duplicate-removal
    content: doppelte eingebettete Clojure-Dateien unter src/ entfernen oder auf generierte Artefakte umstellen
    status: pending
  - id: debug-and-feature-gating
    content: DEBUG- und Feature-abhängige Embedded-Quellen weiterhin korrekt in der Generierung abbilden
    status: pending
  - id: tests-and-tooling
    content: Build-, Resolver- und Embedded-Source-Tests sowie clj-kondo für /libs in den neuen Ablauf einpassen
    status: pending
isProject: false
---

# Plan für generierte Embedded-Clojure-Sources

## Ziel

Die aktuell doppelt gepflegte Struktur mit:

- echten Clojure-Dateien unter `/libs`
- zusätzlich eingebetteten Raw-String-Dateien unter `/src`

soll durch einen sauberen Generierungsweg ersetzt werden.

Zielbild:

- Clojure-Quellen leben nur noch unter `/libs`
- ein Script erzeugt daraus embeddbaren C-Source im Build-Verzeichnis
- die Build-Pipeline bindet nur die generierten Artefakte ein
- `resolve_path_to_bytes(...)` liefert weiter Pfade wie `"/libs/tiny-fx/sound.clj"`
- Tooling wie `clj-kondo` arbeitet direkt auf den echten `/libs`-Dateien
- Doppelpflege zwischen `/src/*.clj` und `/libs/**/*.clj` entfällt

## Problem heute

Aktuell ist mindestens ein Teil der Embedded-Clojure-Quellen gespiegelt:

- `[libs/tiny-fx/sound.clj](/Users/theisen/Projects/tiny-clj/libs/tiny-fx/sound.clj)`
- `[src/tiny-fx.sound.clj](/Users/theisen/Projects/tiny-clj/src/tiny-fx.sound.clj)`

Die eingebettete Variante wird in `[src/embedded_sources.c](/Users/theisen/Projects/tiny-clj/src/embedded_sources.c)` per `#include` aufgenommen und unter einem `/libs/...`-Pfad registriert. Das bedeutet:

- zwei Dateien mit gleichem Inhalt
- unklare Source of Truth
- unnötige Edit-Risiken
- schlechte Tool-Unterstützung für die `src`-Raw-String-Variante

## Zielarchitektur

### 1. `/libs` wird Source of Truth

Alle eingebetteten Clojure-Dateien liegen nur noch in `/libs`, zum Beispiel:

- `/libs/tiny-fx/sound.clj`
- `/libs/tiny-fx/sound-demos.clj`
- `/libs/clojure/core.clj`

Es soll keine manuell gepflegte Kopie derselben Quelle unter `/src` mehr geben.

### 2. Generator-Script erzeugt embeddbaren C-Code

Ein neues Script, z. B. unter:

- `[scripts/generate_embedded_clojure_source.py](/Users/theisen/Projects/tiny-clj/scripts/generate_embedded_clojure_source.py)`

übernimmt genau eine Aufgabe:

- Input: eine Clojure-Datei
- Output: ein generierter C-Snippet oder eine generierte Include-Datei im Build-Verzeichnis

Der Output soll von `embedded_sources.c` direkt konsumierbar sein, ohne Handarbeit.

Minimaler Vertrag des Scripts:

- Eingabe-Dateipfad
- Symbolname oder abgeleiteter Identifier
- Ausgabe-Dateipfad
- bytegenaue Erhaltung des Quelltexts
- korrektes Escaping für C-String oder Byte-Array-Ausgabe

Wichtige Eigenschaft:

- Das Script ist deterministisch, damit Builds reproduzierbar bleiben und CMake nur bei echten Änderungen neu baut.

### 3. Generierte Artefakte landen nur im Build-Verzeichnis

Die generierten Dateien sollen nicht ins Repo, sondern z. B. nach:

- `/build/generated/embedded_clojure/...`

geschrieben werden.

Damit bleibt das Repo sauber:

- echte Quelltexte in `/libs`
- generierte C-Artefakte nur im Build-Output

### 4. CMake orchestriert die Generierung

`[CMakeLists.txt](/Users/theisen/Projects/tiny-clj/CMakeLists.txt)` soll pro eingebetteter Clojure-Datei einen `add_custom_command(...)` oder einen äquivalenten Generierungsschritt anlegen.

Das Build-System soll:

- die Eingabedatei aus `/libs` kennen
- das Generator-Script aufrufen
- die generierte Datei in `build/generated/...` erzeugen
- diese generierte Datei in `embedded_sources.c` oder einen generierten Nachfolger einbinden

Wichtig:

- Änderungen in `/libs/**/*.clj` müssen den relevanten Build-Schritt triggern
- reine C-Änderungen sollen nicht unnötig alle Embedded-Sources neu generieren

## Technische Richtungsentscheidung

Es gibt zwei brauchbare Varianten.

### Variante A: Pro Datei ein generiertes Include-Snippet

Für jede `/libs/.../*.clj`-Datei erzeugt das Script eine Datei wie:

- `build/generated/embedded_clojure/tiny_fx_sound.inc`

die dann in einer zentralen C-Datei per `#include` eingebunden wird.

Vorteile:

- nah am heutigen Modell
- einfach in `embedded_sources.c` integrierbar
- kleine, lokale Änderungen

Nachteil:

- `embedded_sources.c` bleibt weiter eine manuell gepflegte Registry-Datei

### Variante B: Gesamte Embedded-Registry generieren

Das Script oder ein zweiter Aggregationsschritt erzeugt direkt:

- eine komplette `embedded_sources_generated.c`
- optional `embedded_sources_generated.h`

inklusive Registry-Tabelle.

Vorteile:

- weniger Handpflege
- mittelfristig die sauberere Architektur

Nachteile:

- größerer Umbau
- etwas mehr CMake-Komplexität

Empfehlung:

- zuerst Variante A als pragmatischen Migrationsschritt
- optional später auf Variante B vereinheitlichen

## Migrationsschritte

### Schritt 1: Generator für eine einzelne Datei bauen

Testfall mit einer bestehenden Datei, z. B.:

- `/libs/tiny-fx/sound.clj`

Erwartung:

- generierter Output entspricht funktional dem heutigen `src/tiny-fx.sound.clj`
- `resolve_path_to_bytes("/libs/tiny-fx/sound.clj")` funktioniert unverändert

### Schritt 2: CMake für diese eine Datei anschließen

Nur einen Pfad in der Build-Pipeline umstellen, bevor die Lösung verallgemeinert wird.

Ziel:

- Proof of Concept ohne Repo-weite Umstellung

### Schritt 3: `embedded_sources.c` auf generierte Includes umstellen

Die `#include "tiny-fx.sound.clj"`-artige Handverdrahtung wird schrittweise durch generierte Includes aus dem Build-Verzeichnis ersetzt.

### Schritt 4: Doppelte Dateien unter `/src` entfernen

Sobald ein Pfad zuverlässig aus `/libs` generiert wird, fällt die gespiegelte Quelldatei unter `/src` weg.

Wichtig:

- nur entfernen, wenn der Build auf allen relevanten Targets stabil läuft

### Schritt 5: Restliche eingebettete Clojure-Dateien migrieren

Danach folgen weitere Bibliotheken wie:

- `tiny-fx`
- `clojure`
- `tiny-db`

## Debug- und Feature-Fälle

Die Generierung muss weiterhin mit bestehenden Build-Bedingungen kompatibel bleiben, insbesondere:

- `#ifdef DEBUG`
- `TINYCLJ_WITH_TINY_FX`

Das betrifft z. B. Debug-only-Quellen wie:

- `[src/tiny-fx.sound-debug.clj](/Users/theisen/Projects/tiny-clj/src/tiny-fx.sound-debug.clj)`

Hier gibt es zwei Optionen:

- Debug-only-Clojure-Dateien ebenfalls nach `/libs` verschieben und nur in Debug-Builds generieren
- oder den Debug-Pfad vorerst separat lassen und erst später vereinheitlichen

Empfehlung:

- Produktionspfad zuerst umstellen
- Debug-only-Quellen danach in einem zweiten, klar abgegrenzten Schritt migrieren

## Tests und Verifikation

### Build-Tests

- normaler Build erzeugt die generierten Embedded-Sources im Build-Verzeichnis
- Änderungen an `/libs/...` triggern Regeneration
- Clean Build funktioniert ohne vorab eingecheckte generierte Artefakte

### Resolver-Tests

Bestehende Tests rund um `resolve_path_to_bytes(...)` und Embedded-Sources sollen weiter grün bleiben, insbesondere:

- `[src/tests/test_embedded_sources.c](/Users/theisen/Projects/tiny-clj/src/tests/test_embedded_sources.c)`

### Tooling-Tests

`clj-kondo` soll direkt auf `/libs`-Dateien laufen können, ohne Raw-String-C-Wrapping.

### Migrations-Sicherungen

Sinnvolle zusätzliche Regressionen:

- generierter Output enthält exakt denselben Quelltext wie die Eingabedatei
- Pfadregistrierung bleibt stabil
- Debug-/Feature-gated Quellen tauchen nur in den richtigen Builds auf

## Risiken

### 1. Build-Abhängigkeiten werden unvollständig

Wenn CMake Eingabe-/Ausgabedateien nicht sauber kennt, werden Änderungen an `/libs` nicht zuverlässig regeneriert.

### 2. Escaping-Fehler im Generator

Wenn der Generator Zeilenumbrüche, Backslashes oder Quotes nicht exakt behandelt, können subtile Runtime-Fehler entstehen.

### 3. Debug-Quellen werden versehentlich immer eingebettet

Deshalb müssen `DEBUG`- und Feature-Bedingungen explizit Teil des Plans bleiben.

### 4. Schrittweise Migration erzeugt einen Mischzustand

Während der Umstellung kann kurzfristig ein Hybrid aus manuellen und generierten Embedded-Sources entstehen. Dieser Zustand muss bewusst klein und kurz gehalten werden.

## Empfohlene Umsetzung

Reihenfolge:

1. Generator-Script für eine einzelne Datei
2. CMake-Integration für genau einen Pfad
3. Resolver-/Embedded-Tests grün halten
4. gespiegelte Datei entfernen
5. weitere Bibliotheken schrittweise migrieren

## Ergebnisbild

Am Ende soll gelten:

- Clojure-Dateien leben in `/libs`
- `clj-kondo` und ähnliche Tools arbeiten direkt auf echten Quellen
- Embedded-C-Quellen werden nur im Build-Verzeichnis erzeugt
- `embedded_sources` konsumiert generierte Artefakte statt gepflegter Duplikate
- Produktions- und Debug-Pfade bleiben klar getrennt
