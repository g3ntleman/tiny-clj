---
name: Game-Demo nach `libs/` konsolidieren + Host-REPL Flash-FS Sync
overview: "`libs/tiny-fx/game-demo.clj` soll fuer nicht-core Namespaces wie `tiny-fx.game-demo` die alleinige fachliche Quelle werden. Embedded-Sources bleiben weiterhin fuer Bootstrap und Core-Namespaces erhalten, weil sie guenstiger im RAM-Verbrauch sind. Fuer den Host-REPL fehlt ausserdem ein expliziter Sync-Schritt, der die kanonische `libs/`-Datei in das Flash-FS/KV-Override schreibt, damit Deployment und Host-Entwicklung denselben Dateipfad und dieselbe Quelle benutzen."
todos:
  - id: game-demo-source-of-truth
    content: "`libs/tiny-fx/game-demo.clj` als einzige inhaltliche Quelle festlegen und Divergenzen zu `src/tiny-fx.game-demo.clj` aufloesen"
    status: pending
  - id: game-demo-embed-pipeline
    content: Build-/Embed-Pfad fuer `tiny-fx.game-demo` so umbauen, dass keine manuell gepflegte zweite Quellkopie unter `src/` mehr noetig ist
    status: pending
  - id: host-repl-flashfs-sync
    content: Host-REPL um einen expliziten `copy-file`-basierten Sync von `libs/tiny-fx/game-demo.clj` in das Flash-FS/KV-Override erweitern
    status: pending
  - id: game-demo-consolidation-tests
    content: Resolver-, REPL- und Game-Demo-Tests auf die neue Single-Source-/Flash-FS-Strategie ausrichten
    status: pending
isProject: false
---

# Plan: `tiny-fx.game-demo` auf `libs/` konsolidieren

## Ziel

`tiny-fx.game-demo` soll kuenftig aus fachlicher Sicht nur noch unter
`libs/tiny-fx/game-demo.clj` gepflegt werden. Diese Regel gilt fuer
nicht-core-Dateien bzw. flexible Anwendungs-/Demo-Namespaces, nicht fuer die
Bootstrap- und Core-Schicht. Embedded-Sources bleiben dort weiterhin erhalten,
weil sie fuer Startup und RAM-Verbrauch guenstiger sind. Der Host-REPL-Pfad
soll das spaetere Deployment auf das Flash-FS ausdruecklich nachbilden, statt
still auf eine zweite, separat gepflegte Quelle oder auf implizite
Dateisystem-Fallbacks zu vertrauen.

## Ist-Zustand

### 1. Zwei divergierende Quellen fuer denselben Namespace

Aktuell existieren zwei verschiedene Inhalte fuer `tiny-fx.game-demo`:

- `libs/tiny-fx/game-demo.clj`
- `src/tiny-fx.game-demo.clj`

Beide definieren denselben Namespace, sind aber bereits nicht mehr identisch.
Die `src/`-Variante enthaelt zusaetzliche Demo-/Sound-/Rocket-Launch-Logik,
waehrend die `libs/`-Variante noch eine aeltere Demo-Fassung enthaelt. Das ist
ein klassischer Drift-Zustand: unklar, welche Version bei Host, Embedded,
Tests und spaeterem Flash-FS-Deployment wirklich als Wahrheitsquelle gelten
soll.

### 2. Der Resolver spricht bereits die `libs/`-Pfade

`embedded_sources.c` registriert `tiny-fx.game-demo` bereits unter dem
logischen Pfad:

- `/libs/tiny-fx/game-demo.clj`

Auch `source_resolver.c` arbeitet logisch mit `/libs/...`-Pfaden und erlaubt
auf dem Host Dateisystem-Fallbacks fuer solche Quellpfade. Das heisst:

- der logische Runtime-Pfad ist bereits `libs/`
- die physische Pflege findet aber noch doppelt statt

Wichtig dabei: Dieser Plan will die Embedded-Sources nicht generell verdraengen.
Fuer Core-/Bootstrap-Code bleiben sie bewusst bestehen. Konsolidiert werden
soll nur die Quelle fuer nicht-core-Dateien wie `tiny-fx.game-demo`.

### 3. Host-REPL bildet das geplante Flash-FS-Deployment nicht explizit nach

Fuer das spaetere Deployment soll der Inhalt aus `libs/tiny-fx/game-demo.clj`
in das Flash-FS kopiert werden. Genau dieser Sync-Schritt fehlt auf dem Host
derzeit als explizite Aktion. Stattdessen verlaesst sich der Host implizit auf:

- embedded sources
- KV-Overrides, falls vorhanden
- Dateisystem-Fallback in `source_resolver.c`

Dadurch wird der spaetere Deployment-Weg nicht sauber im Host-Workflow
abgebildet und Fehler in der Copy-/Sync-Strecke bleiben leicht unbemerkt.

## Zielzustand

### Single Source of Truth

Fachlicher Inhalt fuer `tiny-fx.game-demo` lebt nur noch in:

- `libs/tiny-fx/game-demo.clj`

### Embedded-Sources bleiben fuer Core/Bootstrap erhalten

Die bestehende Embedded-Source-Strategie bleibt fuer:

- Bootstrap
- Core-Namespaces
- sonstige besonders RAM-kritische Standardbibliothekspfade

ausdruecklich erhalten.

### Nicht-core-Dateien bekommen eine kanonische `libs/`-Quelle

Nicht-core-Dateien wie `tiny-fx.game-demo` werden fachlich nur noch in
`libs/` gepflegt. Fuer diese Klasse von Dateien soll der flexible
Flash-FS-/KV-Override-Pfad die massgebliche Laufzeitquelle fuer Deployment und
Host-Sync werden.

### Abgeleiteter Embed-Artefakt nur wenn technisch weiter noetig

Falls der C-Build fuer `tiny-fx.game-demo` weiterhin eine Raw-String-Include-Datei
benoetigt, darf diese nicht mehr manuell gepflegt werden. Sie muss entweder:

1. aus `libs/tiny-fx/game-demo.clj` generiert werden, oder
2. durch einen Build-/Loader-Pfad ersetzt werden, der direkt `libs/` einliest

Wichtig: `src/tiny-fx.game-demo.clj` darf danach hoechstens noch Build-Artefakt
oder duenne technische Ableitung sein, aber keine zweite fachliche Quelle.
Das ist eine lokale Entscheidung fuer nicht-core-Dateien und keine globale
Absage an Embedded-Sources.

### Host-REPL nutzt denselben logischen Deployment-Pfad

Der Host-REPL soll vor dem Laden/Verwenden von `tiny-fx.game-demo` einen
expliziten Copy-Schritt bekommen, der die Datei aus:

- `libs/tiny-fx/game-demo.clj`

in den Zielpfad fuer das Flash-FS/KV-Override schreibt, also logisch nach:

- `/libs/tiny-fx/game-demo.clj`

Damit testen Host und spaeteres Deployment denselben Pfad, dieselbe Datei und
denselben Override-Mechanismus.

## Betroffene Stellen

- `libs/tiny-fx/game-demo.clj`
- `src/tiny-fx.game-demo.clj`
- `src/embedded_sources.c`
- `src/source_resolver.c`
- `src/repl.c`
- `esp32-idf/main/tinyclj_idf_run.c`
- `libs/tiny-clj/fs.clj`
- `src/tiny-clj.fs.clj`
- relevante Tests in `src/tests/`

## Arbeitspaket 1: Kanonische `game-demo`-Quelle festlegen

### Aufgaben

- Inhalt von `src/tiny-fx.game-demo.clj` und `libs/tiny-fx/game-demo.clj`
vollstaendig vergleichen.
- Entscheiden, welche Unterschiede fachlich gewollt sind und in die
kanonische `libs/`-Datei uebernommen werden muessen.
- `libs/tiny-fx/game-demo.clj` auf den gewuenschten aktuellen Stand bringen.
- Sicherstellen, dass alle bisherigen Call-Sites weiterhin
`tiny-fx.game-demo/...` verwenden und keine direkte Abhaengigkeit an der
physischen `src/`-Datei haben.

### Done when

- Es gibt nur noch eine fachlich gepflegte Version von `tiny-fx.game-demo`.
- Ein Entwickler muss fuer Aenderungen an der Demo nur noch `libs/tiny-fx/game-demo.clj`
anfassen.

## Arbeitspaket 2: Nicht-core-Embed-/Build-Pipeline entkoppeln von manueller Doppelpflege

### Aufgaben

- Den aktuellen Bedarf von `src/tiny-fx.game-demo.clj` fuer `embedded_sources.c`
dokumentieren, ohne die Embedded-Source-Strategie fuer Core-/Bootstrap-Code
in Frage zu stellen.
- Eine robuste Ableitungsstrategie festlegen:
  - bevorzugt generierter Raw-String aus `libs/tiny-fx/game-demo.clj`
  - alternativ anderer Build-Schritt, der direkt `libs/` in ein embedbares
  Format ueberfuehrt
- Dieselbe Entscheidung gegen die bereits existierenden Duplikate spiegeln:
`game-demo` ist hier der erste konkrete Slice fuer nicht-core-Dateien; die
Umsetzung soll aber nicht in eine Sackgasse fuehren, falls spaeter auch
andere flexible `src/*.clj`-Wrapper konsolidiert werden.

### Done when

- `embedded_sources.c` bekommt fuer `tiny-fx.game-demo` keinen manuell
gepflegten Sonderinhalt mehr aus einer zweiten fachlichen Quelle.
- Ein Inhaltsunterschied zwischen `libs/` und Embed-Artefakt kann nicht mehr
unbemerkt entstehen.
- Core-/Bootstrap-Embedded-Sources bleiben unveraendert moeglich.

## Arbeitspaket 3: Host-REPL Flash-FS Sync per `copy-file`

### Motivation

Der Host soll den spaeteren Deployment-Weg nicht nur logisch, sondern konkret
ueben: Datei aus dem Arbeitsbaum nach Flash-FS/KV kopieren und dann von dort
auflosen.

### Aufgaben

- Festlegen, wo im Host-REPL dieser Sync ausgelost wird:
  - expliziter Startup-Hook
  - dedizierter Helper fuer Demo-/Deployment-Workflows
  - oder bewusst manueller, aber getesteter REPL-Befehl
- Falls noch kein passender Clojure-Helper existiert, eine kleine API fuer
`copy-file` definieren, die intern auf `slurp-bytes` + `spit-bytes` basiert.
- Den fehlenden `copy-file`-Aufruf fuer `libs/tiny-fx/game-demo.clj` in den
Host-Pfad integrieren.
- Sicherstellen, dass der Host danach den Inhalt ueber den logischen
`/libs/tiny-fx/game-demo.clj`-Pfad aus dem KV-/Flash-FS-Override beziehen
kann.

### Entscheidungspunkt

Vor der Implementierung festlegen:

1. Ist `copy-file` eine allgemeine `tiny-clj.fs`-API?
2. Oder reicht fuer diesen Slice ein kleiner host-spezifischer Helper?

Bevorzugt wird die allgemeinere API, wenn sie ohne unnötige Sonderpfade
auskommt und spaeter auch fuer weitere Deployments nuetzlich ist.

### Done when

- Der Host-REPL fuehrt einen expliziten Sync-Schritt fuer
`libs/tiny-fx/game-demo.clj` aus.
- `require`/`load-file` fuer `tiny-fx.game-demo` kann anschliessend aus dem
Flash-FS-/KV-Pfad bedient werden, ohne auf implizite Dateisystem-Fallbacks
angewiesen zu sein.

## Arbeitspaket 4: Tests und Regressionen

### Tests

- Resolver-Test: KV-/Flash-FS-Override fuer `/libs/tiny-fx/game-demo.clj`
gewinnt gegen Embed-/Dateisystem-Fallback.
- REPL-/Startup-Test: Host-Sync kopiert die kanonische Datei in den
Override-Pfad und `require` laedt den erwarteten Inhalt.
- Game-Demo-Regressionstests: bestehende Tests fuer
`create-demo-bundle`, `game-demo-config`, Collision-/Watcher-Verhalten laufen
weiter unveraendert gegen die konsolidierte Quelle.
- Optionaler Build-Test: Embed-Artefakt wird aus `libs/` erzeugt oder auf
Konsistenz geprueft.

### Done when

- Ein absichtlicher Inhaltsunterschied zwischen `libs/` und dem geladenen
Runtime-Inhalt wird von Tests entdeckt.
- Host und Embedded laden logisch denselben `/libs/...`-Pfad.

## Risiken

- Die bestehende `src/*.clj`-Raw-String-Struktur ist historisch gewachsen; ein
zu schneller Umbau kann mehr als nur `game-demo` beruehren.
- Wenn die Abgrenzung zwischen Core und nicht-core unscharf bleibt, droht aus
einem lokalen Konsolidierungsslice versehentlich ein globaler Architekturumbau
zu werden.
- Wenn der Host-REPL-Sync nur fuer `game-demo` hart codiert wird, entsteht ein
neuer Sonderpfad. Deshalb sollte die Loesung entweder bewusst generisch sein
oder klar als erster Slice dokumentiert werden.
- Resolver-Fallbacks duerfen fuer lokale Entwicklung nuetzlich bleiben, sollen
aber den neuen Deployment-/Override-Pfad nicht verdecken.

## Ergebnis nach Umsetzung

Erwartetes Ergebnis:

- `libs/tiny-fx/game-demo.clj` ist die kanonische Datei.
- Der Host testet denselben Flash-FS-/KV-Pfad, der spaeter fuer Deployment
benutzt wird.
- Die bisherige Drift zwischen `libs/` und `src/` ist beseitigt.
- Embedded-Sources bleiben fuer Bootstrap und Core-Namespaces bewusst erhalten.

