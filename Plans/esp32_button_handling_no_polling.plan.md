---
name: ESP32 Button Handling
overview: GPIO-Buttons sollen auf ESP32 ohne periodisches Polling laufen. Die Umsetzung erfolgt test-first und schrittweise: zuerst ein gemeinsamer, host-testbarer Button-Kern, dann Host- und ESP32-Quellen, danach der Abbau des digitalen Polling-Timers und abschliessende Breakout-/Runloop-Regressionen.
todos:
  - id: baseline
    content: Bestehenden Button-, GPIO-, Timer-, Breakout- und Runloop-Pfad einlesen und aktuelle Crash-/Polling-Stellen dokumentieren
    status: pending
  - id: test-button-core
    content: Zuerst Kontrakt-Tests fuer eine gemeinsame Button-State-Machine anlegen (debounce, press, release, hold, repeat optional, callback-dispatch)
    status: pending
  - id: implement-button-core
    content: Plattformneutrale Button-State-Machine in C implementieren, ohne GPIO- oder IRQ-Abhaengigkeiten
    status: pending
  - id: test-host-button-source
    content: Host-Tests fuer simulierte Edge-Events gegen dieselbe Button-State-Machine schreiben
    status: pending
  - id: implement-host-button-source
    content: Host-Pfad auf Edge-getriebene Button-Events statt periodischen Button-Polling-Timer umstellen
    status: pending
  - id: test-esp-button-source
    content: ESP32-nahe Tests fuer GPIO-IRQ-Anbindung, Debounce-Weitergabe und Callback-Dispatch definieren
    status: pending
  - id: implement-esp-button-source
    content: ESP32-Button-Pfad mit GPIO-Interrupts und entkoppelter Weitergabe an die gemeinsame State-Machine anbinden
    status: pending
  - id: remove-button-polling
    content: Periodischen Button-Polling-Timer fuer digitale GPIO-Buttons entfernen; Polling nur fuer analoge Sensorpfade beibehalten
    status: pending
  - id: regression-breakout
    content: Breakout-, Timer- und Runloop-Regressionstests fuer Wandkontakt, Eingaben und Langzeitstabilitaet ergaenzen
    status: pending
  - id: cleanup
    content: Sourcecode aufräumen – Debug-Code, temporäre Workarounds, tote Codepfade, überflüssige Kommentare und nicht mehr benötigte Hilfsfunktionen entfernen
    status: pending
isProject: true
---

# ESP32 Button-Handling ohne Polling

## Arbeitsweise

- Die Umsetzung erfolgt test-first und in kleinen, voneinander getrennten Schritten.
- Vor jeder Implementierung werden zuerst die passenden Tests oder Regressionen ergänzt.
- Nach jedem Implementierungsschritt wird der komplette `unit-tests`-Satz ausgeführt.
- Jeder Schritt hat ein klares Gate; erst bei grünem Gate beginnt der nächste Schritt.

## Ziel

- GPIO-Buttons sollen auf ESP32 event-getrieben ueber Interrupts statt ueber periodisches Polling laufen.
- Host und ESP32 sollen dieselbe Button-Semantik nutzen, damit Debounce-, Hold- und Press/Release-Logik unter macOS testbar bleibt.
- Clojure-Callbacks duerfen nicht im IRQ-Kontext laufen; sie bleiben ueber den bestehenden Ingress-/Runloop-Pfad entkoppelt.
- Analoge Sensorpfade duerfen Polling behalten; nur digitale Buttons werden aus dem Polling-Timer herausgezogen.

## Architekturentscheidung

- Variante A: gemeinsamer Button-Kern in C plus plattformspezifische Event-Quelle.
- Input fuer den Kern:
  - Button-ID bzw. logischer Control-ID
  - Rohlevel / Edge
  - Timestamp
- Output des Kerns:
  - press/down
  - release/up
  - hold
  - optional repeat
- ESP32 liefert Edge-Events aus GPIO-Interrupts.
- Der Host liefert simulierte Edge-Events mit derselben API.
- Debounce-/Hold-Entscheidungen leben im gemeinsamen Kern, nicht in separaten Host-/ESP-Sonderpfaden.

## Relevante Dateien

- GPIO-Runtime und Watcher: `/Users/theisen/Projects/tiny-clj/src/gpio.c`
- ESP32-spezifische GPIO-Anbindung: `/Users/theisen/Projects/tiny-clj/src/gpio_esp32.c`, `/Users/theisen/Projects/tiny-clj/src/gpio_esp32.h`
- Host-GPIO-Anbindung: `/Users/theisen/Projects/tiny-clj/src/gpio_host.c`, `/Users/theisen/Projects/tiny-clj/src/gpio_host.h`
- Clojure-Button-API: `/Users/theisen/Projects/tiny-clj/libs/tiny-clj/button.clj`
- Event-Routing: `/Users/theisen/Projects/tiny-clj/libs/tiny-clj/event.clj`
- Breakout-Runtime: `/Users/theisen/Projects/tiny-clj/libs/tiny-breakout/runtime.clj`
- Runloop/Event-Loop: `/Users/theisen/Projects/tiny-clj/src/event_loop.c`, `/Users/theisen/Projects/tiny-clj/src/viewer_host_runloop.c`
- Bestehende Tests: `/Users/theisen/Projects/tiny-clj/src/tests/test_breakout_runtime_startup.c`, `/Users/theisen/Projects/tiny-clj/src/tests/test_event_loop_latency.c`, `/Users/theisen/Projects/tiny-clj/src/tests/test_timer.c`, `/Users/theisen/Projects/tiny-clj/src/tests/test_gpio_write.c`

## Aktueller Problemstand

- Der aktuelle digitale Button-Pfad nutzt bereits platformnahe GPIO-Ereignisse, haengt aber fuer Debounce/Hold noch an einem periodischen Input-Timer.
- Der periodische Timer ist ein plausibler Crash-/Stabilitaetskandidat fuer den Runloop-Thread.
- Wenn dieser Thread stirbt, koennen Spielzustand und Eingaben haengen bleiben, obwohl Hauptfenster und Renderpfad weiterleben.
- Fuer Breakout ist das besonders kritisch, weil Input, Segment-Timer und Host-Callbacks ueber denselben Runloop-Pfad laufen.

## Schrittweises Vorgehen mit Gates

1. Baseline sichern
- Bestehende GPIO-, Timer-, Event-Loop- und Breakout-Tests ausfuehren.
- Den aktuellen digitalen Button-Polling-Pfad und die periodischen Timer-Abhaengigkeiten dokumentieren.
- Gate: Der Ist-Zustand ist reproduzierbar beschrieben.

2. Gemeinsame Button-State-Machine test-first
- Zuerst reine C-Tests fuer Debounce, Press, Release, Hold und optional Repeat schreiben.
- Tests muessen ohne GPIO, Threads oder Clojure lauffaehig sein.
- Gate: Gewuenschte Button-Semantik ist formalisiert und hostunabhaengig testbar.

3. Gemeinsamen Button-Kern minimal implementieren
- Plattformneutralen Kern ohne GPIO-/IRQ-Abhaengigkeit implementieren.
- Keine dynamischen Allokationen im Hot Path.
- Nur so viel implementieren, wie fuer die bereits vorhandenen Kern-Tests noetig ist.
- Gate: Der Kern besteht alle Kontrakt-Tests.

4. Host-Event-Quelle test-first anbinden
- Simulierte Host-Edges in dieselbe State-Machine einspeisen.
- Zuerst Regressionstests fuer Press/Release/Hold unter Host hinzufuegen.
- Erst danach den Host-Pfad umbauen.
- Gate: Host-Pfad bildet dieselbe Semantik ohne Sonderlogik ab.

5. ESP32-IRQ-Pfad test-first anbinden
- Tests fuer GPIO-Interrupt-Registration, Edge-Weitergabe und Runloop-Dispatch definieren.
- IRQ-Handler darf nur Minimalarbeit leisten und keine Clojure- oder komplexe Speicherlogik ausfuehren.
- Erst danach ESP32-spezifischen Glue-Code anbinden.
- Gate: ESP32-Button-Quelle ist klar vom gemeinsamen Kern getrennt.

6. Polling fuer digitale Buttons test-first entfernen
- Zuerst Schutztests schreiben, die belegen, dass digitale Buttons keinen periodischen Polling-Timer mehr benoetigen.
- Periodischen Input-Timer nur noch fuer analoge Sensor- bzw. Sampling-Pfade verwenden.
- Digitale Buttons duerfen keine globale periodische Refresh-Schleife mehr benoetigen.
- Gate: Kein produktiver Polling-Timer mehr im digitalen Button-Pfad.

7. Breakout- und Runloop-Regressionen test-first absichern
- Zuerst Breakout-Regressionsfaelle fuer Wandkontakt, weiterlaufenden Segment-Timer und Input-Reaktionsfaehigkeit ergaenzen.
- Danach Langzeittest oder wiederholte Input-/Collision-Zyklen gegen Runloop-Stabilitaet definieren.
- Erst danach verbleibende Glue- oder Lifecycle-Probleme beheben.
- Gate: Breakout bleibt spielbar, Eingaben reagieren weiter, Runloop bleibt stabil.

## Konkrete Reihenfolge

1. Bestehenden digitalen Button-/Timer-Pfad dokumentieren und Baseline-Tests ausfuehren.
2. Reine Kern-Tests fuer die Button-State-Machine schreiben.
3. Gemeinsamen Kern minimal implementieren, bis diese Tests gruen sind.
4. Host-spezifische Edge-Tests schreiben.
5. Host-Quelle auf den gemeinsamen Kern umstellen, bis Host-Tests gruen sind.
6. ESP32-IRQ- und Dispatch-Tests schreiben.
7. ESP32-Glue-Code anbinden, bis diese Tests gruen sind.
8. Schutztests gegen digitalen Polling-Timer schreiben.
9. Digitales Button-Polling entfernen, analoge Pfade erhalten.
10. Breakout-/Runloop-Regressionen schreiben.
11. Restliche Stabilitaetsprobleme beheben und den Vollsatz gruen ziehen.

## Teststrategie

- Reine Kern-Tests zuerst, damit Debounce/Hold-Logik deterministisch bleibt.
- Danach Adapter-Tests fuer Host und ESP32 getrennt.
- Breakout-Regressionen erst aufsetzen, wenn der gemeinsame Kern stabil ist.
- Nach jedem Implementierungsschritt den kompletten `unit-tests`-Satz laufen lassen.
- Eine Implementierungsphase ohne vorher passende Tests ist nicht zulaessig.

## Nicht-Ziele

- Keine Vermischung mit analogem Sensor-Sampling.
- Keine direkte Clojure-Eval aus Interrupt-Kontext.
- Kein neues Host-spezifisches Sonderverhalten fuer Buttons.
- Keine Rueckkehr zu globalem periodischem Button-Polling als Fallback.

## Abnahmekriterien

- Digitale GPIO-Buttons laufen auf ESP32 ohne periodisches Polling.
- Host und ESP32 nutzen dieselbe Button-State-Machine fuer Semantik und Regressionstests.
- Debounce/Hold/Release sind unter Host reproduzierbar testbar.
- Clojure-Callbacks laufen weiterhin ausserhalb des IRQ-Kontexts.
- Breakout bleibt bei laengerem Lauf eingabefaehig und der Runloop-Thread bleibt stabil.
