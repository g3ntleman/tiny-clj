---
name: Blocking Event Loop Driver
overview: "`event_loop_run_next` bleibt non-blocking; zusätzlich kommt ein blockierender Driver, der auf Timer-Fälligkeit oder neue Queue-Einträge wartet. Damit kann der Interpreter-Thread ohne Polling schlafen und wird durch Ingress/Timer wieder aktiviert."
todos:
  - id: add-blocking-driver-api
    content: Neuen blockierenden Event-Loop-Driver API-seitig einfuehren, ohne `event_loop_run_next`-Semantik zu brechen
    status: completed
  - id: implement-wait-notify-core
    content: Wait/Notify-Kern im Event-Loop implementieren und an Ingress-/Task-/Timer-Producer koppeln
    status: completed
  - id: switch-interpreter-driver
    content: Interpreter-Loop auf blockierenden Driver umstellen und Polling minimieren
    status: completed
  - id: decommission-competing-concepts
    content: Konkurrierende Konzepte abrüsten – alte Polling-/Legacy-Pfade, doppelte Driver-/Wake-Varianten und temporäre Fallbacks entfernen, sobald der neue Driver stabil ist
    status: completed
  - id: extend-tests
    content: Bestehende run-next-Vertraege beibehalten und Wake/Timer-Blockierverhalten in vorhandenen Testgruppen absichern
    status: completed
  - id: update-docs
    content: Dokumentation fuer run-next vs. blockierenden Driver in C- und Clojure-API anpassen
    status: completed
  - id: breakout-practice-proof
    content: Praxis-Beweis erbringen, dass Breakout im neuen blockierenden Runloop läuft (Renderthread + Interpreterthread), inklusive reproduzierbarem Test-Nachweis
    status: completed
  - id: esp32-gpio-without-render-thread
    content: Sicherstellen und nachweisen, dass GPIO-Events auf ESP32 auch ohne Render-Thread und ohne zeitgesteuertes `run_next`-Polling in den Clojure-Event-Loop gelangen
    status: completed
  - id: esp32-strict-blocking-driver
    content: ESP32-Interpreterpfad auf einen strikt blockierenden Event-Loop-Driver umstellen (kein Polling-Fallback)
    status: completed
  - id: esp32-isr-wake-bridge
    content: ISR-zu-Runloop-Wakeup-Brücke ergänzen, damit GPIO-Drain den blockierenden Driver aktiv aufweckt
    status: completed
  - id: maximize-shared-host-esp32-code
    content: Gemeinsamen Runloop-/Wake-Code zwischen macOS-Host und ESP32 maximieren; Plattformcode auf dünne Adapter begrenzen
    status: completed
  - id: cleanup
    content: Sourcecode aufräumen – Debug-Code, temporäre Workarounds, tote Codepfade, überflüssige Kommentare und nicht mehr benötigte Hilfsfunktionen entfernen
    status: completed
isProject: false
---

# Blocking Event Loop Driver

## Ziel

`event_loop_run_next` als Einzelschritt-Primitive beibehalten und einen neuen blockierenden Driver ergänzen, der solange wartet, bis

- ein Timer fällig ist oder
- ein Producer (z. B. Background-/Render-Thread) neue Arbeit in die Queue legt.

## Fortschritt im Workspace

- Status: Umsetzung abgeschlossen; alle TODOs stehen auf `completed`.
- Implementierungs-Commit (Runloop + Wake): `0d83f2e8`
- Praxis-Beweis-Commit (Breakout auf blockierendem Runloop): `cf505cc8`
- Plan-/Gate-Commit (Planaufbau und Test-Gates): `ba022493`
- Vollständiger Testnachweis nach Umsetzung: `./build/unit-tests-prof` mit `1974 Tests, 0 Failures, 7 Ignored`.
- Praxis-Beweis-Testnachweis: `./build/unit-tests-prof --test "test_event_loop_latency/event_loop_run_blocking_thread_processes_breakout_input_ingress"` mit `1 Tests, 0 Failures`.
- ISR-Wakeup-Brücke umgesetzt: GPIO-ISR signalisiert jetzt zusätzlich `event_loop_wake()`; Event-Loop-Waitpfad verwendet auf ESP32 Task-Notifications (ISR-sicher), auf Host weiterhin pthread condvar.
- Nachweis nach ISR-Wakeup-Schritt:
  - `./build/unit-tests-prof --test "test_gpio_architecture_contract/*"` -> `13 Tests, 0 Failures`
  - `./build/unit-tests-prof --test "test_event_loop_latency/*"` -> `28 Tests, 0 Failures`
  - `./build/unit-tests-prof` -> `1974 Tests, 0 Failures, 7 Ignored`
- ESP32-REPL-Wartepfad auf blockierendes `event_loop_run(...)` umgestellt; Polling über `repl_process_event_loop` + `platform_sleep_ms(...)` entfernt.
- UART-RX-Wakeup-Brücke ergänzt (UART-Event-Queue + Wake-Task), damit eingehende Konsoleingaben einen blockierenden Runloop ohne Polling aufwecken können.
- Nachweis nach Schritt `esp32-strict-blocking-driver`:
  - `./build/unit-tests-prof --test "test_gpio_architecture_contract/*"` -> `15 Tests, 0 Failures`
  - `./build/unit-tests-prof --test "test_event_loop_latency/*"` -> `28 Tests, 0 Failures`
  - `./build/unit-tests-prof --test "test_timer/run_next_task_breakout_renderer_loop_regression"` -> `1 Tests, 0 Failures`
  - `./build/unit-tests-prof` -> `1976 Tests, 0 Failures, 7 Ignored`
- Nachweis nach Schritt `esp32-gpio-without-render-thread`:
  - Zusätzlicher Architekturtest: `gpio_architecture_esp32_gpio_ingress_path_is_renderer_independent`
  - `./build/unit-tests-prof --test "test_gpio_architecture_contract/*"` -> `16 Tests, 0 Failures`
  - `./build/unit-tests-prof --test "test_event_loop_latency/*"` -> `28 Tests, 0 Failures`
  - `./build/unit-tests-prof` -> `1977 Tests, 0 Failures, 7 Ignored`
- Nachweis nach Schritt `maximize-shared-host-esp32-code`:
  - Zusätzliche Architekturtests:
    - `gpio_architecture_host_and_esp32_share_blocking_runloop_driver_callsite`
    - `gpio_architecture_esp32_adapter_wakes_shared_runloop_without_private_driver_logic`
  - `./build/unit-tests-prof --test "test_gpio_architecture_contract/*"` -> `18 Tests, 0 Failures`
  - `./build/unit-tests-prof --test "test_event_loop_latency/*"` -> `28 Tests, 0 Failures`
  - `./build/unit-tests-prof` -> `1979 Tests, 0 Failures, 7 Ignored`
- Nachweis nach Cleanup:
  - Include-Cleanup im ESP32-Runloop-Pfad (nur benötigte Header, Tiny-FX-Header bedingt eingebunden).
  - `./build/unit-tests-prof` -> `1979 Tests, 0 Failures, 7 Ignored`

## Qualitäts-Gate pro Schritt (verpflichtend)

- Nach jedem Umsetzungsschritt wird der komplette Unit-Test-Lauf `./build/unit-tests` sofort ausgeführt.
- Der nächste Schritt startet nur, wenn der komplette Unit-Test-Lauf grün ist.
- Bei roten Tests: erst Fix, dann erneut `./build/unit-tests`, bis alles grün ist.
- Es gibt kein Teilmengen-Gate: maßgeblich ist immer der vollständige Unit-Test-Lauf.

## Leitprinzip: Shared Code Host/ESP32

- `src/event_loop.c` bleibt der zentrale gemeinsame Event-Loop-Kern (Single Source of Truth).
- Host und ESP32 sollen denselben blockierenden Driver (`event_loop_run`) und dieselbe Wake-Semantik verwenden.
- Plattformspezifika (z. B. ISR-Signalquelle, Konsole/Display-I/O) werden in kleine Adapter gekapselt, ohne zweite Driver- oder Wait-Implementierung.
- Neue ESP32-Änderungen müssen aktiv auf Code-Duplikation gegen Host geprüft werden.

## Relevante Stellen

- API-Vertrag von `run_next`: [`/Users/theisen/Projects/Work/tiny-clj-feature/src/event_loop.h`](/Users/theisen/Projects/Work/tiny-clj-feature/src/event_loop.h)
- Aktuelle Tick-Logik und Ingress-Drain: [`/Users/theisen/Projects/Work/tiny-clj-feature/src/event_loop.c`](/Users/theisen/Projects/Work/tiny-clj-feature/src/event_loop.c)
- Host-Interpreter-Loop (heute mit 1ms Polling): [`/Users/theisen/Projects/Work/tiny-clj-feature/src/fx_host_runloop.c`](/Users/theisen/Projects/Work/tiny-clj-feature/src/fx_host_runloop.c)
- ESP32-Einstieg/Loop: [`/Users/theisen/Projects/Work/tiny-clj-feature/esp32-idf/main/tinyclj_idf_run.c`](/Users/theisen/Projects/Work/tiny-clj-feature/esp32-idf/main/tinyclj_idf_run.c)
- ESP32 Yield-Runloop-Hook: [`/Users/theisen/Projects/Work/tiny-clj-feature/esp32-idf/main/tinyclj_idf_main.c`](/Users/theisen/Projects/Work/tiny-clj-feature/esp32-idf/main/tinyclj_idf_main.c)
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

8. **Praxis-Beweis mit Breakout (Pflicht-Abschluss)**
   - Breakout explizit im Host-Pfad mit Renderthread + Interpreterthread (blockierender Runloop) nachweisen.
   - Pflichtnachweis über den integrierten End-to-End-Test:
     - `./build/unit-tests-prof --test "test_event_loop_latency/event_loop_run_blocking_thread_processes_breakout_input_ingress"`
     - (gleichwertig ohne Profil-Suffix): `./build/unit-tests --test "test_event_loop_latency/event_loop_run_blocking_thread_processes_breakout_input_ingress"`
   - Test-Gate: kompletter Unit-Test-Lauf `./build/unit-tests` muss grün sein.

9. **Aufräumen**
   - Debug-Reste, temporäre Workarounds und tote Codepfade entfernen.
   - Test-Gate: kompletter Unit-Test-Lauf `./build/unit-tests` ist grün.

10. **ESP32 GPIO ohne Render-Thread und ohne Polling (neu, Pflicht)**
   - Verbindlich sicherstellen: GPIO-Watcher auf ESP32 funktionieren ohne aktiven Render-Thread und ohne zeitgesteuertes `run_next`-Polling.
   - Zielpfad: ISR/Ringbuffer -> `gpio_esp32_poll_drain` -> `event_loop_enqueue_ingress_call` -> Clojure-Callback im Event-Loop.
   - `esp32-idf/main/tinyclj_idf_run.c`: GPIO-Zustellung darf nicht davon abhängen, dass eine Schleife regelmäßig `repl_process_event_loop` + `platform_sleep_ms(...)` taktet.
   - Test-Gate: kompletter Unit-Test-Lauf `./build/unit-tests` muss grün sein.

11. **ESP32 strikt blockierenden Driver einführen (kein Polling-Fallback)**
   - ESP32-Interpreterpfad auf den bereits gemeinsamen blockierenden Driver `event_loop_run(...)` umstellen (gleicher Kern wie Host).
   - Keine zweite Event-Loop-Driver-Implementierung in `esp32-idf/` aufbauen.
   - Kein periodischer Fallback-Poller für `event_loop_run_next` im Idle.
   - Test-Gate: kompletter Unit-Test-Lauf `./build/unit-tests` muss grün sein.

12. **ISR-Wakeup-Brücke für den gemeinsamen blockierenden Driver**
   - ISR-seitig weiterhin keine direkte Clojure-Enqueue-Logik; nur lock-freies Signalisieren.
   - Zusätzlich muss die ISR-/Drain-Bridge den gemeinsamen blockierenden Event-Loop zuverlässig aufwecken, damit der Drain ohne Poller startet.
   - Wake-Logik nicht als ESP32-Sonder-Driver duplizieren, sondern über gemeinsame Event-Loop-API + dünnen Plattform-Adapter anbinden.
   - Test-Gate: kompletter Unit-Test-Lauf `./build/unit-tests` muss grün sein.

13. **Nachweise für „ohne Polling“**
   - Architektur-/Contract-Test: Kein direkter Enqueue aus ISR; keine implizite Abhängigkeit von zeitgesteuertem `run_next`-Polling für GPIO-Zustellung.
   - Architektur-/Contract-Test: ESP32-Pfad nutzt den gemeinsamen Driver (`event_loop_run`) statt eigener Poll-Driver-Logik.
   - Regressionstest: GPIO-Flanke wird unter blockierendem Driver zugestellt, auch wenn kein Poller aktiv ist.
   - Code-Duplikations-Check: keine neue parallele Runloop-/Wake-Implementierung zwischen Host und ESP32.
   - Zusätzlich (wenn Hardware verfügbar) kurzer ESP32-Smoke-Test ohne gestarteten Renderer und ohne Polling-Helfer.
   - Test-Gate: kompletter Unit-Test-Lauf `./build/unit-tests` muss grün sein.

## Risiken / Checks

- Keine Breaking-Change fuer `run-next-task`-Call-Sites.
- Kein Lost-Wakeup zwischen Queue-Update und Wait.
- Timer-Deadline-Berechnung bleibt korrekt bei parallelen Producer-Aktionen.
- Keine unkontrollierte Host/ESP32-Runloop-Code-Duplikation.

## Abschlusskriterium

- Der Plan gilt erst als erfüllt, wenn der Praxis-Beweis zeigt, dass Breakout mit aktivem Renderthread unter dem neuen blockierenden Runloop korrekt läuft und der vollständige Unit-Test-Lauf grün ist.
- Zusätzlich muss der ESP32-Nachweis erbracht sein, dass GPIO-Ereignisse auch ohne Render-Thread und ohne zeitgesteuertes `run_next`-Polling zuverlässig im Clojure-Event-Loop ankommen.
- Zusätzlich muss nachgewiesen sein, dass Host und ESP32 dafür denselben Runloop-/Wake-Kern verwenden und Plattformcode nur als Adapter ausgeführt ist.

