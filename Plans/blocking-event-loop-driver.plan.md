---
name: Blocking Event Loop Driver
overview: "`event_loop_run_next` bleibt non-blocking; zusätzlich kommt ein blockierender Driver, der auf Timer-Fälligkeit oder neue Queue-Einträge wartet. Damit kann der Interpreter-Thread ohne Polling schlafen und wird durch Ingress/Timer wieder aktiviert."
todos:
  - id: add-blocking-driver-api
    content: Neuen blockierenden Event-Loop-Driver API-seitig einfuehren, ohne `event_loop_run_next`-Semantik zu brechen
    status: pending
  - id: implement-wait-notify-core
    content: Wait/Notify-Kern im Event-Loop implementieren und an Ingress-/Task-/Timer-Producer koppeln
    status: pending
  - id: switch-interpreter-driver
    content: Interpreter-Loop auf blockierenden Driver umstellen und Polling minimieren
    status: pending
  - id: decommission-competing-concepts
    content: Konkurrierende Konzepte abrüsten – alte Polling-/Legacy-Pfade, doppelte Driver-/Wake-Varianten und temporäre Fallbacks entfernen, sobald der neue Driver stabil ist
    status: pending
  - id: extend-tests
    content: Bestehende run-next-Vertraege beibehalten und Wake/Timer-Blockierverhalten in vorhandenen Testgruppen absichern
    status: pending
  - id: update-docs
    content: Dokumentation fuer run-next vs. blockierenden Driver in C- und Clojure-API anpassen
    status: pending
  - id: cleanup
    content: Sourcecode aufräumen – Debug-Code, temporäre Workarounds, tote Codepfade, überflüssige Kommentare und nicht mehr benötigte Hilfsfunktionen entfernen
    status: pending
isProject: false
---

# Blocking Event Loop Driver

## Ziel

`event_loop_run_next` als Einzelschritt-Primitive beibehalten und einen neuen blockierenden Driver ergänzen, der solange wartet, bis

- ein Timer fällig ist oder
- ein Producer (z. B. Background-/Render-Thread) neue Arbeit in die Queue legt.

## Qualitäts-Gate pro Schritt (verpflichtend)

- Nach jedem Umsetzungsschritt wird der komplette Unit-Test-Lauf `./build/unit-tests` sofort ausgeführt.
- Der nächste Schritt startet nur, wenn der komplette Unit-Test-Lauf grün ist.
- Bei roten Tests: erst Fix, dann erneut `./build/unit-tests`, bis alles grün ist.
- Es gibt kein Teilmengen-Gate: maßgeblich ist immer der vollständige Unit-Test-Lauf.

## Relevante Stellen

- API-Vertrag von `run_next`: [`/Users/theisen/Projects/Work/tiny-clj-feature/src/event_loop.h`](/Users/theisen/Projects/Work/tiny-clj-feature/src/event_loop.h)
- Aktuelle Tick-Logik und Ingress-Drain: [`/Users/theisen/Projects/Work/tiny-clj-feature/src/event_loop.c`](/Users/theisen/Projects/Work/tiny-clj-feature/src/event_loop.c)
- Host-Interpreter-Loop (heute mit 1ms Polling): [`/Users/theisen/Projects/Work/tiny-clj-feature/src/fx_host_runloop.c`](/Users/theisen/Projects/Work/tiny-clj-feature/src/fx_host_runloop.c)
- Clojure-API-Vertrag `run-next-task`: [`/Users/theisen/Projects/Work/tiny-clj-feature/libs/clojure/core.clj`](/Users/theisen/Projects/Work/tiny-clj-feature/libs/clojure/core.clj)
- Regression/Vertrags-Tests fuer non-blocking `run-next-task`: [`/Users/theisen/Projects/Work/tiny-clj-feature/src/tests/test_go_blocks.c`](/Users/theisen/Projects/Work/tiny-clj-feature/src/tests/test_go_blocks.c)

## Umsetzungsschritte

1. **Neuen blockierenden Driver einführen**
   - In [`/Users/theisen/Projects/Work/tiny-clj-feature/src/event_loop.h`](/Users/theisen/Projects/Work/tiny-clj-feature/src/event_loop.h) neue API deklarieren (z. B. `event_loop_run` oder `event_loop_wait_and_run`) mit klarer Semantik: wartet bis Arbeit oder Timer faellig und fuehrt danach kontrollierte Drains aus.
   - `event_loop_run_next` unveraendert lassen.
   - Test-Gate: kompletter Unit-Test-Lauf `./build/unit-tests` muss grün sein.

2. **Wake-Mechanismus im Event-Loop implementieren**
   - In [`/Users/theisen/Projects/Work/tiny-clj-feature/src/event_loop.c`](/Users/theisen/Projects/Work/tiny-clj-feature/src/event_loop.c) eine interne Wait/Notify-Mechanik fuer den Driver aufbauen (plattformneutral via bestehende Abstraktion oder klar gekapselte Host/ESP32-Zweige).
   - Keine Busy-Wait-Schleife im Idle-Fall.
   - Test-Gate: kompletter Unit-Test-Lauf `./build/unit-tests` muss grün sein.

3. **Producer-Signalpunkte anschliessen**
   - Bei erfolgreichem Enqueue in Ingress-/Task-Pfaden den Waiter signalisieren (`event_loop_enqueue_ingress`, `event_loop_enqueue_ingress_call`, `event_loop_enqueue_ingress_native`, ggf. `event_loop_enqueue`).
   - Timer-Aenderungen (enqueue/upsert/cancel), die den naechsten Deadline-Zeitpunkt beeinflussen, ebenfalls signalisieren.
   - Test-Gate: kompletter Unit-Test-Lauf `./build/unit-tests` muss grün sein.

4. **Interpreter-Driver umstellen**
   - [`/Users/theisen/Projects/Work/tiny-clj-feature/src/fx_host_runloop.c`](/Users/theisen/Projects/Work/tiny-clj-feature/src/fx_host_runloop.c): von 1ms-Polling auf den neuen blockierenden Driver umstellen.
   - Falls zusaetzliche Plattformarbeit noetig bleibt (Host), das deterministisch und minimal halten, ohne den neuen Wait-Pfad zu unterlaufen.
   - Test-Gate: kompletter Unit-Test-Lauf `./build/unit-tests` muss grün sein.

5. **Konkurrierende Konzepte abrüsten**
   - Alte konkurrierende Polling-/Wake-Ansätze, doppelte Driver-Pfade und temporäre Legacy-/Fallback-Logik nachweisbar entfernen.
   - Zielzustand: ein klarer primärer Driver-Pfad mit eindeutigem Ownership-/Wake-Verhalten.
   - Test-Gate: kompletter Unit-Test-Lauf `./build/unit-tests` muss grün sein.

6. **Tests erweitern (bestehende Dateien)**
   - Bestehende Non-Blocking-Vertraege fuer `run-next-task` unveraendert absichern (insb. Tests in [`/Users/theisen/Projects/Work/tiny-clj-feature/src/tests/test_go_blocks.c`](/Users/theisen/Projects/Work/tiny-clj-feature/src/tests/test_go_blocks.c)).
   - Neue Tests in vorhandenen Gruppen (z. B. [`/Users/theisen/Projects/Work/tiny-clj-feature/src/tests/test_timer.c`](/Users/theisen/Projects/Work/tiny-clj-feature/src/tests/test_timer.c) oder passende Event-Loop-Tests):
     - blockierender Driver wacht bei Ingress aus Background-Thread auf,
     - blockierender Driver wacht bei Timer-Faelligkeit auf,
     - Idle-Fall erzeugt kein Polling-Busy-Loop-Verhalten.
   - Test-Gate: kompletter Unit-Test-Lauf `./build/unit-tests` muss grün sein.

7. **Dokumentation aktualisieren**
   - Kurze API-Doku in [`/Users/theisen/Projects/Work/tiny-clj-feature/src/event_loop.h`](/Users/theisen/Projects/Work/tiny-clj-feature/src/event_loop.h) und Clojure-seitige Klarstellung in [`/Users/theisen/Projects/Work/tiny-clj-feature/libs/clojure/core.clj`](/Users/theisen/Projects/Work/tiny-clj-feature/libs/clojure/core.clj): `run-next-task` bleibt Schrittfunktion; blockierendes Verhalten liegt im Driver.
   - Test-Gate: kompletter Unit-Test-Lauf `./build/unit-tests` muss grün sein.

8. **Aufräumen**
   - Debug-Reste, temporäre Workarounds und tote Codepfade entfernen.
   - Test-Gate: kompletter Unit-Test-Lauf `./build/unit-tests` ist grün.

## Risiken / Checks

- Keine Breaking-Change fuer `run-next-task`-Call-Sites.
- Kein Lost-Wakeup zwischen Queue-Update und Wait.
- Timer-Deadline-Berechnung bleibt korrekt bei parallelen Producer-Aktionen.

