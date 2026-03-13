---
name: sound articulation semantics
overview: Stellt den Sound-Pfad auf einen deklarativen, per-Track gesetzten Envelope mit normalisierten Pegeln 0.0..1.0 um. Der Compiler skaliert diese Werte zuverlässig in Byte-Level 0..255; die Runtime verteilt sie gleichmäßig über die Notendauer und optimiert gleiche Stufen zu minimalen PWM-Änderungen. Künstliche Mini-Pausen zwischen gleichen Tönen sind entfernt; aktuell bleibt der Vertrag bewusst bei 1..8 Envelope-Stützpunkten.
todos:
  - id: envelope-format
    content: Deklarativen per-Track-Envelope als normalisierten Vektor 0.0..1.0 festlegen
    status: completed
  - id: envelope-tests
    content: Test-first Fälle für Compiler-Emission und Runtime-Tail-Lautstärke ergänzen
    status: completed
  - id: envelope-runtime
    content: Runtime und Backends auf per-Note-Envelope-Stufen und Boundary-Updates erweitern
    status: completed
  - id: envelope-demo
    content: William-Tell-Demo auf den neuen normalisierten Envelope-Vertrag umstellen
    status: completed
  - id: envelope-limit
    content: Envelope-Vertrag bewusst auf 1..8 Stützpunkte begrenzen und die Runtime schlank halten
    status: completed
  - id: buzzer-tuning
    content: Weichere Envelope-Kandidaten für Buzzer/ESP32 experimentell vergleichen und einen Default auswählen
    status: pending
isProject: false
---

# Plan für deklarative Sound-Envelopes

## Ziel

`[libs/tiny-fx/sound.clj](/Users/theisen/Projects/tiny-clj/libs/tiny-fx/sound.clj)` und `[src/tiny-fx.sound.clj](/Users/theisen/Projects/tiny-clj/src/tiny-fx.sound.clj)` sollen einen einfachen, deklarativen Envelope pro Track unterstützen, damit

- gleiche Folgetöne auch ohne Compiler-Sonderfälle hörbar getrennt werden,
- das Timing der Noten unverändert gleichmäßig bleibt,
- Host und ESP32 dieselbe Semantik direkt aus dem Track lesen.

Das aktuelle Startmodell ist bewusst klein:

- `:envelope` wird einmal pro Track gesetzt,
- die Werte sind normalisierte Zahlen `0.0..1.0`,
- der Vektor ist bewusst auf `1..8` Stützpunkte begrenzt,
- der Compiler skaliert sie zuverlässig in Byte-Level `0..255`,
- die Runtime verteilt die Punkte gleichmäßig über die Gate-Zeit,
- gleiche aufeinanderfolgende Levels werden intern zu wenigen Stufen zusammengefasst,
- dadurch genügen bei `[1.0 1.0 1.0 1.0 0.1]` effektiv zwei PWM-Level pro Note.

Die frühere Idee, gleiche Töne über künstliche Mini-Pausen zu trennen, ist nicht Teil des aktuellen Modells. Trennung kommt jetzt nur noch aus Gate-Verhalten, optionalem Retrigger und vor allem dem Track-Envelope.

## Umgesetztes Modell

### 1. Deklarative DSL auf normalisierte Pegel festgelegt

In `[libs/tiny-fx/sound.clj](/Users/theisen/Projects/tiny-clj/libs/tiny-fx/sound.clj)` und `[src/tiny-fx.sound.clj](/Users/theisen/Projects/tiny-clj/src/tiny-fx.sound.clj)` akzeptiert `compile-track*` jetzt ein optionales `:envelope`-Feld direkt in den bestehenden Optionen:

- Vektor mit `1..8` Einträgen
- Zahlen im Bereich `0.0..1.0`
- Umrechnung nach `0..255` direkt im Compiler über normale Arithmetik
- keine zusätzliche öffentliche Hilfsfunktion nötig
- weiterhin deklarativ auf Clojure-Seite

Der Compiler emittiert dafür genau ein `TRK1_EVT_SET_ENV` am Track-Anfang.

Explizit nicht mehr enthalten:

- compilerseitig eingefügte Mini-Pausen zwischen gleichen Folgetönen
- zusätzliche öffentliche Clojure-Hilfsfunktionen nur für Envelope-Skalierung

### 2. Runtime verteilt den Envelope pro Note

In `[src/sound_engine.h](/Users/theisen/Projects/tiny-clj/src/sound_engine.h)` und `[src/sound_engine.c](/Users/theisen/Projects/tiny-clj/src/sound_engine.c)` wurde das Stream-/Voice-Modell erweitert:

- Track speichert ein aktives Envelope-Profil,
- jede Note übernimmt dieses Profil beim Start,
- die Gate-Dauer wird proportional auf Envelope-Segmente verteilt,
- gleiche benachbarte Pegel werden zu einem Segment zusammengezogen,
- die Tick-Engine wechselt nur an Segmentgrenzen die Lautstärke.

Dadurch bleibt das Notenraster gleich, aber die Note fällt im Tail hörbar ab.

Die Runtime bleibt dabei absichtlich klein:

- keine dynamischen Envelope-Puffer
- keine variable Envelope-Länge im Track-Format
- keine zusätzlichen Sonderpfade für künstliche Gap-Erzeugung

### 3. Backends bleiben konsistent

Die Host-/ESP32-Backends mussten nicht mit neuer DSL-Logik aufgebläht werden. Sie profitieren direkt davon, dass die Runtime nur an relevanten Boundaries neue Lautstärken ausgibt:

- `[src/sound_backend_host.c](/Users/theisen/Projects/tiny-clj/src/sound_backend_host.c)`
- `[src/sound_backend_esp32.c](/Users/theisen/Projects/tiny-clj/src/sound_backend_esp32.c)`

Das Ziel „möglichst wenige PWM-Änderungen pro Note“ wird durch die Segment-Komprimierung in der Runtime erfüllt.

### 4. Demo auf den neuen Vertrag gezogen

`William Tell` verwendet jetzt den deklarativen Envelope direkt in `[libs/tiny-fx/sound-demos.clj](/Users/theisen/Projects/tiny-clj/libs/tiny-fx/sound-demos.clj)`:

- `:envelope [1.0 1.0 1.0 1.0 0.1]`
- `:gate-percent 100`

Damit kommt die Trennung wiederholter Töne aus dem Envelope statt aus Demo-spezifischer Sondermarkierung.

Zwischenstand aus dem Hörtest:

- `[1.0 1.0 1.0 1.0 0.1]` klingt deutlich „retro“ bzw. C64-artig
- weichere Kandidaten wie `[1.0 1.0 0.85 0.7 0.55]` sind als Gegenprobe sinnvoll
- für Buzzer/ESP32 ist der finale Default noch offen und soll experimentell ermittelt werden

## Teststrategie und Ergebnis

Die Änderung wurde test-first auf isolierten Fällen abgesichert in `[src/tests/test_sound_engine.c](/Users/theisen/Projects/tiny-clj/src/tests/test_sound_engine.c)`:

- Compiler emittiert `TRK1_EVT_SET_ENV` genau einmal pro Track
- Tail-Lautstärke fällt im letzten Segment ab
- gleiche Folgetöne starten wieder mit voller Anfangslautstärke, auch ohne Retrigger-Flag
- das Tick-basierte Boundary-Verhalten bleibt explizit getestet

Verifikation:

- `cmake --build build --target unit-tests tiny-clj-repl`
- `./build/unit-tests --test 'test_sound_*'`
- REPL-Smoke-Test für `William Tell`

Stand nach Umsetzung:

- Sound-Tests grün
- REPL-Compilerpfad grün
- Plan auf normalisierten Envelope aktualisiert

## Nächster Schritt

Als separater Folgeplan sinnvoll:

- weiche Envelope-Kandidaten für Buzzer systematisch vergleichen,
- daraus einen Default-Envelope ableiten,
- Legato später separat verfeinern, falls der musikalische Bedarf bleibt.
