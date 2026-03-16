# EDN Assets On-Demand: Test-First Plan

## Ziel
Deklarative Schwergewichte aus `tiny-fx.startup`, `tiny-fx.game-demo` und den Demo-Song-Namespaces aus den Namespaces entfernen und in `.edn`-Assets auslagern, die erst bei Bedarf geladen werden.

## Randbedingungen
- Test-first: jede Verhaltensänderung startet mit fehlschlagendem Test.
- Kein Kompatibilitäts-Layer: keine Legacy-Var-Shims für entfernte Top-Level-`def`s.
- Öffentliche API darf bewusst gebrochen und Tests/Callsites direkt auf die neue API migriert werden.
- Testqualität darf nicht sinken (keine zusätzlichen `Ignored`, keine abgeschwächten Assertions).
- Asset-Deployment muss auf einem hardware-agnostischen `tiny-clj.fs`-Image basieren.
- Host-nahe Tests dürfen keinen RAM-Backend-Fallback nutzen.
- Kein Serial-Deployment-Pfad.
- Implementierungsregel (persistent): Bei jeder Umsetzungsetappe wird dieser Plan sofort mit dem aktuellen Stand aktualisiert
  (Status, getroffene Entscheidungen, Abweichungen, nächste konkrete Schritte).

## Scope
- Primär: `libs/tiny-fx/startup.clj`, `libs/tiny-fx/game-demo.clj`.
- Zusätzlich: `libs/tiny-fx/sound-demos*.clj`.
- Neu: deklarative Assets in `.edn` (z. B. `libs/tiny-fx/assets/startup.edn`, `libs/tiny-fx/assets/game-demo.edn`).
- Loader/Builder in Clojure-Code, Records weiterhin im Code erzeugt.
- Neu: minimales Deployment-Tool fuer Verzeichnisse -> `tiny-clj.fs`-Image (cp-artige UX).

## Zielbild (Architektur)
1. `.edn` enthält nur deklarative Daten:
- Styles, Farben, Geometrie, Keyframes, Entities, Regel-Spezifikationen.
- Pro Demo-Song ein eigenes Asset (keine Sammeldatei für alle Songs).

2. `.clj` enthält nur Verhalten:
- Loader (`load-*-spec!`), Validierung, Übersetzung Spec -> Runtime-Records.
- Callbacks, Watcher, side effects.

3. On-Demand:
- `require` lädt nur Code.
- `create-startup-bundle` / `create-demo-bundle` laden/parsen EDN erst beim ersten Aufruf.
- Optionaler Cache (Atom) für wiederholte Aufrufe.

4. Deployment/Runtime-Naehe:
- Assets und Libraries werden in ein gemeinsames `tiny-clj.fs`-Image kopiert.
- Host-REPL und ESP32 nutzen dasselbe Image-Format fuer realitaetsnahe Tests.
- RAM-Backend wird abgeruestet (kein stiller Host-Fallback).
- Deployment nur image-basiert, nicht seriell.

## Test-First Arbeitspakete

### Phase 1: Require-Heap Schutztests (zuerst rot)
1. Neue Tests für Heap bei reinem Namespace-Load:
- `(heap (require 'tiny-fx.startup))`
- `(heap (require 'tiny-fx.game-demo))`

2. Zielwerte als harte Budget-Tests:
- `tiny-fx.startup`: `:total <= 32768`
- `tiny-fx.game-demo`: `:total <= 131072`

3. Tests müssen aktuell fehlschlagen (Baseline ist deutlich höher).

### Phase 2: API-Vertrags-Tests auf neue Form (zuerst rot)
1. Startup:
- `create-startup-bundle` liefert gültige Bundle-Struktur.
- `slot-descriptors` liefern erwartete IDs/Atoms.

2. Game-demo:
- `create-demo-bundle` liefert 3 Szenen mit gültigen Collision-Rules.
- `game-demo-config` liefert funktionsfähige `:slots`, `:spatial-callback`, `:game-scene-atom`.

3. Entfernte Top-Level-`def`-Nutzung aus Tests entfernen (kein Shim).

### Phase 3: EDN-Asset-Loader Tests (zuerst rot)
1. Loader findet und liest Asset-Dateien.
2. Loader validiert Pflichtfelder (harte Fehler bei invaliden Assets).
3. Loader-Cache-Verhalten:
- erster Call lädt/parst,
- weiterer Call verwendet Cache,
- Reset-Hook für Tests verfügbar.

### Phase 3b: Demo-Song-Assets (ein Song = ein Asset) Tests (zuerst rot)
1. Pro Song wird genau ein Asset geladen:
- `tiny-fx.sound-demos/demo :minuet-in-g` lädt nur `minuet-in-g.edn`.
- analoge Tests für `the-entertainer`, `gymnopedie-no-1`, `rondo-alla-turca`, `hall-of-the-mountain-king`, `can-can`, `rocket-launch-sfx`, `laser-sfx`, `william-tell-finale`.

2. Keine unnötige Vorab-Ladung:
- `(heap (require 'tiny-fx.sound-demos))` bleibt niedrig (ohne Songdaten im Namespace-Load).

3. Format-/Schema-Tests pro Song-Asset:
- Pflichtfelder (`:track-id`, `:steps`, `:opts`) vorhanden.
- Typ/Shape-Validierung der Step-Events.

### Phase 3c: Image/Deployment-Vertrag (zuerst rot)
1. Host nutzt image-basiertes `tiny-clj.fs`:
- Start ohne Image-Konfiguration liefert harten Fehler (kein RAM-Fallback).
- Start mit Image funktioniert und kann Assets ueber `resolve_path_to_bytes`/`tiny-clj.fs` lesen.

2. `cp`-artiges Deployment-Tool (nur Verzeichnisse, immer rekursiv):
- CLI-Form: `tinyclj-cp --image <tinydb.bin> [--init-size <size>] [--dry-run] [--verbose] <src_dir>... <dest>`.
- `<src_dir>` muss Verzeichnis sein, `<dest>` hat Form `fs:/...`.
- `--dry-run` macht keine Image-Aenderung, `--verbose` listet kopierte Zielpfade.

3. Kein Serial-Deploy:
- Tests/Docs verankern, dass kein UART-Upload-Pfad Teil der Loesung ist.

### Phase 4: Migration Implementierung
1. EDN-Dateien erstellen.
2. `startup.clj` und `game-demo.clj` auf Spec->Build umstellen.
3. Direkte Top-Level-Konstruktionen entfernen.
4. Callsites/Tests auf neue API migrieren.
5. Demo-Songs in einzelne Assets aufteilen und Loader in `tiny-fx.sound-demos`/`tiny-fx.sound-demos-william` umstellen.
6. RAM-Backend-Pfad im Host entfernen und image-basierten Pfad verpflichtend machen.
7. `tinyclj-cp` mit reduzierter Parameteroberflaeche implementieren.

### Phase 5: Regression/Performance Absicherung
1. Vollsuite muss grün bleiben.
2. `Ignored`-Zahl bleibt unverändert.
3. Require-Heap-Tests bleiben stabil innerhalb Budget.
4. Vergleichsmessung dokumentieren (vorher/nachher):
- `heap(require ns)`
- `heap(require+create-bundle)`

## Konkrete Dateiplanung
- Neu:
  - `libs/tiny-fx/assets/startup.edn`
  - `libs/tiny-fx/assets/game-demo.edn`
  - `libs/tiny-fx/assets/sound-demos/minuet-in-g.edn`
  - `libs/tiny-fx/assets/sound-demos/the-entertainer.edn`
  - `libs/tiny-fx/assets/sound-demos/gymnopedie-no-1.edn`
  - `libs/tiny-fx/assets/sound-demos/rondo-alla-turca.edn`
  - `libs/tiny-fx/assets/sound-demos/hall-of-the-mountain-king.edn`
  - `libs/tiny-fx/assets/sound-demos/can-can.edn`
  - `libs/tiny-fx/assets/sound-demos/rocket-launch-sfx.edn`
  - `libs/tiny-fx/assets/sound-demos/laser-sfx.edn`
  - `libs/tiny-fx/assets/sound-demos/william-tell-finale.edn`
- Anpassungen:
  - `libs/tiny-fx/startup.clj`
  - `libs/tiny-fx/game-demo.clj`
  - `libs/tiny-fx/sound-demos.clj`
  - `libs/tiny-fx/sound-demos-data.clj`
  - `libs/tiny-fx/sound-demos-william.clj`
  - `src/fs_layer.c` (RAM-Backend entfernen, image-basierter Host-Pfad)
  - Embedded-Source/Asset-Registrierung (C-Seite), damit EDN auch im eingebetteten Modus verfügbar ist.
  - neues Deployment-Tool, z. B. `scripts/tinyclj_cp.py`
- Tests:
  - `src/tests/test_vector_scene_graph.c`
  - ggf. neue spezialisierte Loader/Heap-Tests (`test_memory` oder neue Testdatei `test_assets_edn.c`).
  - neue Deploy/Image-Vertragstests (Host ohne RAM-Fallback, CLI-Validierung, dry-run).

## Akzeptanzkriterien
1. `require`-Heap Ziele erfüllt:
- startup <= 32 KB
- game-demo <= 128 KB

2. Vollsuite:
- `0 Failures`
- keine zusätzlichen `Ignored`.

3. Kein Kompat-Layer:
- Keine Legacy-Top-Level-Defs als Weiterleitungen.
- Tests/Callsites direkt auf neue API migriert.

4. Deployment-Vertrag:
- Host-Tests laufen image-basiert (kein RAM-Backend-Fallback).
- `tinyclj-cp` akzeptiert nur:
  - `--image`
  - `--init-size`
  - `--dry-run`
  - `--verbose`
  - `<src_dir>... <dest>`
- Quellen sind nur Verzeichnisse und werden immer rekursiv kopiert.

## Risiken und Gegenmaßnahmen
1. Risiko: EDN-Parsekosten verschieben Latenz auf ersten Aufruf.
- Maßnahme: lazy cache + einmalige Parse-Validierung.

2. Risiko: eingebettete Builds finden EDN nicht.
- Maßnahme: Asset-Embedding als Teil der Source-Registry + eigene Loader-Tests.

3. Risiko: versteckte Abhängigkeit auf entfernte Vars in Tests.
- Maßnahme: API-Vertrags-Tests vor Migration und systematische Refactorings.
